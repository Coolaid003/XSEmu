//! kernelx.dll – the console flavour of kernel32/kernelbase.
//!
//! Only functions that differ from Windows are implemented here; everything else falls
//! back to the real KERNELBASE/KERNEL32 export of the same name (see resolver).
//! Signatures and constants: public GDK docs + WinDurango (LGPL-3.0) as reference only (no code taken).

#![allow(non_snake_case)]

use std::ffi::c_void;
use std::sync::atomic::{AtomicU32, AtomicUsize, Ordering};

use windows_sys::Win32::Foundation as fnd;
use windows_sys::Win32::System::LibraryLoader as ll;
use windows_sys::Win32::System::Memory as mem;
use windows_sys::Win32::System::Threading as thr;

use crate::modules;
use crate::pe::ImportName;
use crate::resolver;
use crate::util::{dll_key, from_ansi_ptr, from_wide_ptr, write_ansi_out, write_wide_out};

type HANDLE = *mut c_void;
type BOOL = i32;

/// The title's own unhandled-exception filter goes behind XCL's (`eh`): XCL's crash report
/// first, then the title's handler (its crash reporter) as on the console.
pub unsafe extern "system" fn SetUnhandledExceptionFilter(f: usize) -> usize {
    crate::eh::set_title_filter(f)
}

pub fn exports() -> Vec<(&'static str, usize)> {
    crate::hle_exports![
        // registry: the console mode the GDK thunks read (XSystemGetDeviceType)
        RegOpenKeyW,
        RegOpenKeyExW,
        RegQueryValueExW,
        RegCloseKey,
        // memory
        VirtualAlloc,
        VirtualAllocEx,
        VirtualFree,
        VirtualFreeEx,
        VirtualProtect,
        VirtualProtectEx,
        XMemAlloc,
        XMemFree,
        XMemAllocDefault,
        XMemFreeDefault,
        XMemSetAllocationHooks,
        XMemVirtualQuery,
        XMemVirtualAlloc,
        XMemIsToolsMemAvailable,
        XMemAllocatePhysicalPages,
        XMemMapPhysicalPages,
        XMemMapPhysicalPagesScatter,
        XMemFreePhysicalPages,
        XMemTransferMemory,
        XMemGetWorkingSetStatistics,
        TitleMemoryStatus,
        JobTitleMemoryStatus,
        ToolingMemoryStatus,
        AllocateTitlePhysicalPages,
        FreeTitlePhysicalPages,
        MapTitlePhysicalPages,
        // app state (minappmodelx): a title is never suspended or constrained here
        RegisterAppStateChangeNotification,
        UnregisterAppStateChangeNotification,
        RegisterAppConstrainedChangeNotification,
        UnregisterAppConstrainedChangeNotification,
        // threads / system
        SetUnhandledExceptionFilter,
        SetThreadName,
        GetThreadName,
        SetThreadpoolAffinityMask,
        QueryProcessorSchedulingStatistics,
        GetConsoleType,
        OutputDebugStringA,
        OutputDebugStringW,
        GetSystemOSVersion,
        // modules
        LoadLibraryW,
        LoadLibraryA,
        LoadLibraryExW,
        LoadLibraryExA,
        LoadPackagedLibrary,
        GetProcAddress,
        GetModuleHandleW,
        GetModuleHandleA,
        GetModuleHandleExW,
        GetModuleHandleExA,
        GetModuleFileNameW,
        GetModuleFileNameA,
        FreeLibrary,
        FreeLibraryWhenCallbackReturns,
        DisableThreadLibraryCalls,
    ]
}

// ---------------------------------------------------------------------------------------
// Memory
// ---------------------------------------------------------------------------------------

pub const MEM_GRAPHICS: u32 = 0x1000_0000;
pub const MEM_TITLE: u32 = 0x4000_0000;
const STD_ALLOC_MASK: u32 = 0x1000 | 0x2000 | 0x8_0000 | 0x10_0000 | 0x20_0000 | 0x100_0000;
const STD_PROTECT_MASK: u32 = 0xFF | 0x100 | 0x200 | 0x400;

static WARNED_FLAGS: AtomicU32 = AtomicU32::new(0);

pub fn sanitize_alloc_type(t: u32) -> u32 {
    let s = t & STD_ALLOC_MASK;
    let dropped = t & !STD_ALLOC_MASK;
    if dropped != 0 && WARNED_FLAGS.fetch_or(dropped, Ordering::Relaxed) & dropped != dropped {
        crate::xlog_debug!("VirtualAlloc: dropping console allocation flags {dropped:#x}");
    }
    if s == 0 {
        0x1000 | 0x2000
    } else {
        s
    }
}

pub fn sanitize_protect(p: u32) -> u32 {
    let s = p & STD_PROTECT_MASK;
    if s & 0xFF == 0 {
        (s & !0xFF) | mem::PAGE_READWRITE
    } else {
        s
    }
}

/// Graphics address space the title reserved (no commit) that unified memory had no room for
/// – where it maps physical pages (a texture streaming pool): base → size. Write-watched, like
/// unified memory, so textures in it follow CPU writes (`d3d12x::title_textures`).
static GRAPHICS_RESERVATIONS: std::sync::Mutex<std::collections::BTreeMap<usize, usize>> = std::sync::Mutex::new(std::collections::BTreeMap::new());
static GRAPHICS_RESERVATIONS_GEN: std::sync::atomic::AtomicU64 = std::sync::atomic::AtomicU64::new(0);

/// Generation counter incremented whenever `GRAPHICS_RESERVATIONS` changes.
pub fn graphics_reservations_gen() -> u64 {
    GRAPHICS_RESERVATIONS_GEN.load(Ordering::Acquire)
}

/// The title's graphics reservations (base, size).
pub fn graphics_reservations() -> Vec<(usize, usize)> {
    GRAPHICS_RESERVATIONS.lock().unwrap().iter().map(|(&b, &s)| (b, s)).collect()
}

/// Whether `p` lies in one of the title's graphics reservations.
pub fn in_graphics_reservation(p: usize) -> bool {
    GRAPHICS_RESERVATIONS.lock().unwrap().range(..=p).next_back().is_some_and(|(&b, &s)| p < b + s)
}

const MEM_WRITE_WATCH: u32 = 0x20_0000;

/// Graphics memory (MEM_GRAPHICS, committed, no fixed address) comes from the unified
/// memory region the GPU uses directly (`gpu::uma`); a graphics reservation (no commit) is
/// write-watched address space of Windows; everything else from Windows.
/// The console's MEM_64K_PAGES (MEM_LARGE_PAGES | MEM_PHYSICAL): title memory of 64 KiB
/// pages – GPU-visible on the console like all title memory.
const MEM_64K_PAGES: u32 = 0x2040_0000;

/// Commit `[a, a + n)` of a sparse window (the title's reservations there) and give it video
/// memory (`d3d12x::sparse`).
unsafe fn commit_sparse(a: usize, n: usize, p: u32) -> *mut c_void {
    let q = mem::VirtualAlloc(a as *const c_void, n, mem::MEM_COMMIT, sanitize_protect(p));
    if !q.is_null() {
        crate::hle::d3d12x::sparse::committed(q as usize, n);
    }
    q
}

pub unsafe extern "system" fn VirtualAlloc(addr: *const c_void, size: usize, t: u32, p: u32) -> *mut c_void {
    let uma = crate::gpu::uma::get();
    // committing inside one of the title's reservations in a sparse window
    if !addr.is_null() && t & 0x1000 != 0 && uma.is_some_and(|u| u.is_sparse(addr as usize)) {
        let q = commit_sparse(addr as usize, size, p);
        trace_mem("VirtualAlloc (sparse commit)", addr as usize, size, t as u64, p, q as usize);
        return q;
    }
    // the title's own reservations of 64 KiB pages: in a sparse window, so the GPU sees them
    if addr.is_null() && t & 0x2000 != 0 && t & MEM_64K_PAGES == MEM_64K_PAGES && crate::hle::d3d12x::sparse::accepts_allocations() {
        if let Some(q) = uma.and_then(|u| u.sparse_reserve(size)) {
            if t & 0x1000 != 0 && commit_sparse(q, size, p).is_null() {
                uma.map(|u| u.sparse_release(q));
                return std::ptr::null_mut();
            }
            trace_mem("VirtualAlloc (64K pages, sparse)", 0, size, t as u64, p, q);
            return q as *mut c_void;
        }
    }
    if addr.is_null() && t & MEM_GRAPHICS != 0 && t & 0x1000 != 0 {
        let align = if t & 0x8000_0000 != 0 {
            4 << 20
        } else if t & 0x2000_0000 != 0 && t & 0x0040_0000 == 0 {
            2 << 20
        } else {
            crate::gpu::uma::GRAIN
        };
        let rounded = size.max(1).div_ceil(align) * align;
        if let Some((q, _)) = crate::hle::d3d12x::sparse::alloc(rounded, align) {
            trace_mem("VirtualAlloc (graphics, sparse VRAM)", 0, rounded, t as u64, p, q);
            return q as *mut c_void;
        }
        if let Some(q) = uma.and_then(|u| u.alloc(rounded, align)) {
            trace_mem("VirtualAlloc (graphics, UMA)", 0, rounded, t as u64, p, q as usize);
            return q as *mut c_void;
        }
    }
    if addr.is_null() && t & MEM_GRAPHICS != 0 && t & 0x2000 != 0 {
        // on the console all graphics memory is GPU-visible, reserved address space too (titles
        // reserve a range, then commit or map pages and place resources in it): prefer the
        // sparse VRAM windows first so reservations consume zero system RAM until committed
        let reserved = if !crate::hle::d3d12x::sparse::accepts_allocations() {
            None
        } else if t & 0x8000_0000 != 0 {
            let rounded = size.max(1).div_ceil(4 << 20) * (4 << 20);
            uma.and_then(|u| u.sparse_alloc(rounded, 4 << 20)).map(|(q, _)| q)
        } else {
            uma.and_then(|u| u.sparse_reserve(size))
        };
        if let Some(q) = reserved {
            trace_mem("VirtualAlloc (graphics reserve, sparse)", 0, size, t as u64, p, q);
            return q as *mut c_void;
        }
        if let Some(q) = uma.and_then(|u| u.reserve(size, 0x1_0000)) {
            trace_mem("VirtualAlloc (graphics reserve, UMA)", 0, size, t as u64, p, q as usize);
            return q as *mut c_void;
        }
        let q = mem::VirtualAlloc(addr, size, mem::MEM_RESERVE | MEM_WRITE_WATCH, sanitize_protect(p));
        if !q.is_null() {
            GRAPHICS_RESERVATIONS.lock().unwrap().insert(q as usize, size);
            GRAPHICS_RESERVATIONS_GEN.fetch_add(1, Ordering::Release);
            return q;
        }
    }
    let q = mem::VirtualAlloc(addr, size, sanitize_alloc_type(t), sanitize_protect(p));
    trace_mem("VirtualAlloc", addr as usize, size, t as u64, p, q as usize);
    q
}

/// `XCL_TRACE_MEM=1`: allocations of 64 KiB or more that are not unified memory (first 60)
fn trace_mem(what: &str, addr: usize, size: usize, flags: u64, protect: u32, got: usize) {
    static ON: std::sync::OnceLock<bool> = std::sync::OnceLock::new();
    static N: AtomicUsize = AtomicUsize::new(0);
    if size >= 0x1_0000 && *ON.get_or_init(|| std::env::var_os("XCL_TRACE_MEM").is_some()) && N.fetch_add(1, Ordering::Relaxed) < 60 {
        crate::xlog_info!("mem: {what}({addr:#x}, {size:#x}, flags {flags:#x}, protect {protect:#x}) → {got:#x}");
    }
}

/// Blocks of the unified memory region go back to it (MEM_RELEASE) or stay (MEM_DECOMMIT
/// zeroes them); others to Windows.
pub unsafe extern "system" fn VirtualFree(addr: *mut c_void, size: usize, t: u32) -> BOOL {
    let a = addr as usize;
    let uma = crate::gpu::uma::get();
    let mut g = AWAY.lock().unwrap();
    // the title's reservations in a sparse window: decommit (and on release give the address
    // space back); their video memory goes with it
    if let Some(u) = uma.filter(|u| u.is_sparse(a)) {
        let release = t & mem::MEM_RELEASE != 0;
        let decommit = t & mem::MEM_DECOMMIT != 0;
        if release == decommit || (release && size != 0) {
            fnd::SetLastError(fnd::ERROR_INVALID_PARAMETER);
            return 0;
        }
        let n = if release || size == 0 {
            match u.sparse_reservation(a) {
                Some((start, len)) if start == a => len,
                _ => {
                    fnd::SetLastError(fnd::ERROR_INVALID_PARAMETER);
                    return 0;
                }
            }
        } else {
            match u.sparse_reservation(a) {
                Some((start, len)) if a.checked_add(size).is_some_and(|end| end <= start + len) => size,
                _ => {
                    fnd::SetLastError(fnd::ERROR_INVALID_PARAMETER);
                    return 0;
                }
            }
        };
        let mappings = flush_away_range(g.as_ref(), a, a + n, None);
        if n > 0 {
            // sparse first: under its lock, so no copy of CPU writes to video memory reads the
            // pages while they go (the submit thread's copy faulted on them)
            crate::hle::d3d12x::sparse::decommitted(a, n);
            if mem::VirtualFree(addr, n, mem::MEM_DECOMMIT) == 0 {
                return 0;
            }
        }
        remove_away_range(g.as_mut(), mappings);
        if release {
            u.sparse_release(a);
        }
        return 1;
    }
    if let Some(u) = uma.filter(|u| u.contains(a)) {
        if t & 0x8000 != 0 {
            let mappings = u.committed_reservation(a).filter(|&(base, _)| a == base)
                .map_or_else(Vec::new, |(_, n)| flush_away_range(g.as_ref(), a, a + n, None));
            if !u.free(a) {
                return 0;
            }
            remove_away_range(g.as_mut(), mappings);
            return 1;
        }
        if t & 0x4000 != 0 && size > 0 {
            let mappings = flush_away_range(g.as_ref(), a, a.saturating_add(size), None);
            std::ptr::write_bytes(addr as *mut u8, 0, size);
            remove_away_range(g.as_mut(), mappings);
        }
        return 1;
    }
    let mappings = match (t, size) {
        (mem::MEM_RELEASE | mem::MEM_DECOMMIT, 0) => flush_away_range(g.as_ref(), a, usize::MAX, Some(a)),
        (mem::MEM_DECOMMIT, n) => flush_away_range(g.as_ref(), a, a.saturating_add(n), None),
        _ => Vec::new(),
    };
    let ok = mem::VirtualFree(addr, size, t);
    if ok != 0 {
        remove_away_range(g.as_mut(), mappings);
        if t & 0x8000 != 0 && GRAPHICS_RESERVATIONS.lock().unwrap().remove(&a).is_some() {
            GRAPHICS_RESERVATIONS_GEN.fetch_add(1, Ordering::Release);
        }
    }
    ok
}

pub unsafe extern "system" fn VirtualFreeEx(proc_: HANDLE, addr: *mut c_void, size: usize, t: u32) -> BOOL {
    if proc_ == thr::GetCurrentProcess() {
        let a = addr as usize;
        if crate::gpu::uma::get().is_some_and(|u| u.is_sparse(a) || u.contains(a))
            || (t & 0x8000 != 0 && in_graphics_reservation(a))
        {
            return VirtualFree(addr, size, t);
        }
    }
    mem::VirtualFreeEx(proc_, addr, size, t)
}

pub unsafe extern "system" fn VirtualAllocEx(proc_: HANDLE, addr: *const c_void, size: usize, t: u32, p: u32) -> *mut c_void {
    mem::VirtualAllocEx(proc_, addr, size, sanitize_alloc_type(t), sanitize_protect(p))
}

pub unsafe extern "system" fn VirtualProtect(addr: *const c_void, size: usize, p: u32, old: *mut u32) -> BOOL {
    mem::VirtualProtect(addr, size, sanitize_protect(p), old)
}

pub unsafe extern "system" fn VirtualProtectEx(proc_: HANDLE, addr: *const c_void, size: usize, p: u32, old: *mut u32) -> BOOL {
    mem::VirtualProtectEx(proc_, addr, size, sanitize_protect(p), old)
}

/// XALLOC_ATTRIBUTES bit fields.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct XAllocAttrs {
    pub object_type: u32,
    pub page_size: u32,
    pub allocator_id: u32,
    pub alignment: u32,
    pub memory_type: u32,
}

impl XAllocAttrs {
    pub fn decode(a: u64) -> Self {
        XAllocAttrs {
            object_type: (a & 0x3FFF) as u32,
            page_size: ((a >> 14) & 0x3) as u32,
            allocator_id: ((a >> 16) & 0xFF) as u32,
            alignment: ((a >> 24) & 0x1F) as u32,
            memory_type: ((a >> 29) & 0xF) as u32,
        }
    }
}

unsafe fn aligned_heap_alloc(size: usize, align: usize) -> *mut c_void {
    let Some(total) = size.checked_add(align).and_then(|n| n.checked_add(16)) else {
        fnd::SetLastError(fnd::ERROR_NOT_ENOUGH_MEMORY);
        return std::ptr::null_mut();
    };
    let raw = mem::HeapAlloc(mem::GetProcessHeap(), mem::HEAP_ZERO_MEMORY, total) as usize;
    if raw == 0 {
        fnd::SetLastError(fnd::ERROR_NOT_ENOUGH_MEMORY);
        return std::ptr::null_mut();
    }
    let aligned = (raw + 16 + align - 1) & !(align - 1);
    *((aligned - 8) as *mut usize) = raw;
    aligned as *mut c_void
}

unsafe fn aligned_heap_free(p: *mut c_void) {
    if p.is_null() {
        return;
    }
    let raw = *((p as usize - 8) as *const usize);
    mem::HeapFree(mem::GetProcessHeap(), 0, raw as *const c_void);
}

/// Rounded byte size and alignment for an XMem graphics allocation honoring `page_size`
/// (`2` = `XALLOC_PAGESIZE_4MB`, otherwise 64 KiB sparse tile grain) and `alignment`.
fn xmem_graphics_layout(size: usize, a: XAllocAttrs) -> Option<(usize, usize)> {
    let page_grain = match a.page_size {
        2 => 4 << 20, // XALLOC_PAGESIZE_4MB
        _ => crate::gpu::uma::GRAIN,
    };
    let align = (1usize << a.alignment.min(usize::BITS - 2)).max(page_grain);
    let rounded = size.max(1).checked_next_multiple_of(page_grain)?;
    Some((rounded, align))
}

pub unsafe extern "system" fn XMemAllocDefault(size: usize, attrs: u64) -> *mut c_void {
    let a = XAllocAttrs::decode(attrs);
    if a.memory_type == 0 {
        let q = aligned_heap_alloc(size, 1usize << a.alignment.clamp(4, usize::BITS - 2));
        trace_mem("XMemAlloc", 0, size, attrs, 0, q as usize);
        return q;
    }
    let Some((rounded, align)) = xmem_graphics_layout(size, a) else {
        fnd::SetLastError(fnd::ERROR_NOT_ENOUGH_MEMORY);
        return std::ptr::null_mut();
    };
    // Graphics memory types live in the sparse VRAM pool (fallback to coherent UMA, then Windows).
    if let Some((q, _)) = crate::hle::d3d12x::sparse::alloc(rounded, align) {
        trace_mem("XMemAlloc (graphics, sparse VRAM)", 0, rounded, attrs, 0, q);
        return q as *mut c_void;
    }
    if let Some(q) = crate::gpu::uma::get().and_then(|u| u.alloc(rounded, align)) {
        trace_mem("XMemAlloc (graphics, UMA)", 0, rounded, attrs, 0, q as usize);
        return q as *mut c_void;
    }
    mem::VirtualAlloc(std::ptr::null(), rounded, mem::MEM_COMMIT | mem::MEM_RESERVE, mem::PAGE_READWRITE)
}

pub unsafe extern "system" fn XMemFreeDefault(p: *mut c_void, attrs: u64) {
    if p.is_null() {
        return;
    }
    if XAllocAttrs::decode(attrs).memory_type == 0 {
        aligned_heap_free(p);
    } else if !crate::hle::d3d12x::sparse::free(p as usize) && !crate::gpu::uma::get().is_some_and(|u| u.free(p as usize)) {
        mem::VirtualFree(p, 0, mem::MEM_RELEASE);
    }
}

type XMemAllocFn = unsafe extern "system" fn(usize, u64) -> *mut c_void;
type XMemFreeFn = unsafe extern "system" fn(*mut c_void, u64);
static XMEM_ALLOC_HOOK: AtomicUsize = AtomicUsize::new(0);
static XMEM_FREE_HOOK: AtomicUsize = AtomicUsize::new(0);

pub unsafe extern "system" fn XMemAlloc(size: usize, attrs: u64) -> *mut c_void {
    match XMEM_ALLOC_HOOK.load(Ordering::Acquire) {
        0 => XMemAllocDefault(size, attrs),
        f => std::mem::transmute::<usize, XMemAllocFn>(f)(size, attrs),
    }
}

pub unsafe extern "system" fn XMemFree(p: *mut c_void, attrs: u64) {
    match XMEM_FREE_HOOK.load(Ordering::Acquire) {
        0 => XMemFreeDefault(p, attrs),
        f => std::mem::transmute::<usize, XMemFreeFn>(f)(p, attrs),
    }
}

/// `void* XMemVirtualAlloc(void* address, SIZE_T size, DWORD type, XALLOC_ATTRIBUTES attributes,
/// DWORD protect)`: VirtualAlloc with an allocator's attributes – a graphics memory type is
/// GPU-visible memory (as MEM_GRAPHICS: sparse VRAM pool).
pub unsafe extern "system" fn XMemVirtualAlloc(addr: *const c_void, size: usize, t: u32, attrs: u64, p: u32) -> *mut c_void {
    let placeholder = t & (mem::MEM_RESERVE_PLACEHOLDER | mem::MEM_REPLACE_PLACEHOLDER);
    if placeholder != 0 {
        return mem::VirtualAlloc2(
            thr::GetCurrentProcess(),
            addr,
            size,
            (t & STD_ALLOC_MASK) | placeholder,
            sanitize_protect(p),
            std::ptr::null_mut(),
            0,
        );
    }
    let a = XAllocAttrs::decode(attrs);
    let mut t = if a.memory_type != 0 { t | MEM_GRAPHICS } else { t };
    if a.memory_type != 0 && a.page_size == 2 {
        t |= 0x8000_0000;
    }
    VirtualAlloc(addr, size, t, p)
}

/// `BOOL XMemVirtualQuery(HANDLE process, const void* address, XMEM_BASIC_INFORMATION* info)`
/// (signature from XboxBvhBuilder.dll's call sites): MEMORY_BASIC_INFORMATION plus a field
/// at +48 whose bits 0-1 the caller requires for "CPU-readable memory" – set to 1 (CPU
/// cached) for committed readable memory. The caller zeroes the rest.
pub unsafe extern "system" fn XMemVirtualQuery(process: HANDLE, address: *const c_void, info: *mut c_void) -> BOOL {
    if info.is_null() {
        fnd::SetLastError(fnd::ERROR_INVALID_PARAMETER);
        return 0;
    }
    let mbi = info as *mut mem::MEMORY_BASIC_INFORMATION;
    if mem::VirtualQueryEx(process, address, mbi, std::mem::size_of::<mem::MEMORY_BASIC_INFORMATION>()) == 0 {
        return 0;
    }
    const READABLE: u32 = mem::PAGE_READONLY
        | mem::PAGE_READWRITE
        | mem::PAGE_WRITECOPY
        | mem::PAGE_EXECUTE_READ
        | mem::PAGE_EXECUTE_READWRITE
        | mem::PAGE_EXECUTE_WRITECOPY;
    let protect = (*mbi).Protect;
    let cpu = (*mbi).State == mem::MEM_COMMIT && (protect & mem::PAGE_GUARD) == 0 && (protect & READABLE) != 0;
    *(info as *mut u8).add(48).cast::<u32>() = cpu as u32;
    1
}

/// `BOOL XMemGetWorkingSetStatistics(UINT32 allocator, void* stats)` (as FSR's upscaler calls
/// it: allocator 0x20000001, 128-byte buffer, reads +8 only on success). No statistics:
/// FALSE, which the caller reports as 0 bytes.
/// `BOOL XMemGetWorkingSetStatistics(DWORD flags, XMEM_WORKING_SET_STATISTICS* stats)`: +0 the
/// title's memory limit, +8 what it uses – engines size their pools and texture streaming from
/// these (Fortnite: flags 0x10000001). Only those two are written: the structure's size
/// differs between GDK versions (Fortnite's exe passes 0x78 bytes, its EOSSDK – older – has
/// at most 0x50 bytes before its stack cookie, which zeroing 0x78 bytes destroyed: the DLL's
/// static initializer failed its cookie check, 0xC0000409).
pub unsafe extern "system" fn XMemGetWorkingSetStatistics(_flags: u32, stats: *mut u8) -> BOOL {
    if stats.is_null() {
        fnd::SetLastError(fnd::ERROR_INVALID_PARAMETER);
        return 0;
    }
    let limit = crate::state::config().title_memory_budget_mb() * 1024 * 1024;
    (stats as *mut u64).write_unaligned(limit);
    (stats.add(8) as *mut u64).write_unaligned(process_private_bytes().min(limit));
    1
}

/// `BOOL XMemTransferMemory(direction, size)`: moving budget between the title and the tools
/// pools – there is one budget here
pub unsafe extern "system" fn XMemTransferMemory(_direction: u32, _size: usize) -> BOOL {
    1
}

/// The title's debug output (console builds print a lot there) goes to XCL's log as well.
pub unsafe extern "system" fn OutputDebugStringA(s: *const u8) {
    if let Some(m) = from_ansi_ptr(s) {
        crate::xlog_info!("[debug] {}", m.trim_end());
    }
}

pub unsafe extern "system" fn OutputDebugStringW(s: *const u16) {
    if let Some(m) = from_wide_ptr(s) {
        crate::xlog_info!("[debug] {}", m.trim_end());
    }
}

pub unsafe extern "system" fn XMemSetAllocationHooks(alloc: usize, free: usize) {
    if alloc == 0 {
        XMEM_ALLOC_HOOK.store(0, Ordering::Release);
        XMEM_FREE_HOOK.store(0, Ordering::Release);
    } else {
        XMEM_ALLOC_HOOK.store(alloc, Ordering::Release);
        XMEM_FREE_HOOK.store(free, Ordering::Release);
    }
}

#[repr(C)]
pub struct TitleMemoryStatusT {
    pub dw_length: u32,
    pub dw_reserved: u32,
    pub total: u64,
    pub avail: u64,
    pub legacy_used: u64,
    pub legacy_peak: u64,
    pub legacy_avail: u64,
    pub title_used: u64,
    pub title_avail: u64,
    pub legacy_page_table_used: u64,
    pub title_page_table_used: u64,
}

fn process_private_bytes() -> u64 {
    use windows_sys::Win32::System::ProcessStatus::{K32GetProcessMemoryInfo, PROCESS_MEMORY_COUNTERS_EX};
    unsafe {
        let mut c: PROCESS_MEMORY_COUNTERS_EX = std::mem::zeroed();
        c.cb = std::mem::size_of::<PROCESS_MEMORY_COUNTERS_EX>() as u32;
        if K32GetProcessMemoryInfo(thr::GetCurrentProcess(), &mut c as *mut _ as *mut _, c.cb) != 0 {
            c.PrivateUsage as u64
        } else {
            0
        }
    }
}

pub unsafe extern "system" fn TitleMemoryStatus(buf: *mut TitleMemoryStatusT) -> BOOL {
    if buf.is_null() || (*buf).dw_length < std::mem::size_of::<TitleMemoryStatusT>() as u32 {
        fnd::SetLastError(fnd::ERROR_INVALID_PARAMETER);
        return 0;
    }
    let total = crate::state::config().title_memory_budget_mb() * 1024 * 1024;
    let used = process_private_bytes().min(total);
    let b = &mut *buf;
    b.total = total;
    b.avail = total - used;
    b.legacy_used = 0;
    b.legacy_peak = 0;
    b.legacy_avail = 0;
    b.title_used = used;
    b.title_avail = total - used;
    b.legacy_page_table_used = 0;
    b.title_page_table_used = 0;
    1
}

pub unsafe extern "system" fn JobTitleMemoryStatus(buf: *mut TitleMemoryStatusT) -> BOOL {
    TitleMemoryStatus(buf)
}

pub unsafe extern "system" fn ToolingMemoryStatus(buf: *mut u32) -> BOOL {
    if buf.is_null() || *buf < 40 {
        fnd::SetLastError(fnd::ERROR_INVALID_PARAMETER);
        return 0;
    }
    std::ptr::write_bytes((buf as *mut u8).add(8), 0, 32);
    1
}

pub extern "system" fn XMemIsToolsMemAvailable() -> BOOL {
    0
}

/// Title physical pages (64 KiB) emulated in the unified memory region: an allocation is one
/// contiguous UMA block – its pages' home; a page's number is its UMA page index + 1 (0 means
/// "none": unmap). Mapping consecutive pages at their home (or at no chosen address) returns
/// that UMA address (GPU-visible, no copy). Mapping pages at another address – address space
/// the title reserved, in any order – commits it and copies their data there; unmapping them
/// (or mapping them elsewhere) copies it back home: a page is in one place at a time and keeps
/// its contents.
const PHYS_PAGE: usize = 0x1_0000;

/// Size of one console physical page (64 KiB).
pub const PHYS_PAGE_SIZE: usize = PHYS_PAGE;

/// first page number → (pages in the block, pages still allocated)
static PHYS_BLOCKS: std::sync::Mutex<std::collections::BTreeMap<usize, (usize, usize)>> = std::sync::Mutex::new(std::collections::BTreeMap::new());

/// Pages mapped away from home: address → page, and page → address.
#[derive(Default)]
struct Away {
    at: std::collections::BTreeMap<usize, usize>,
    page: std::collections::HashMap<usize, usize>,
}
static AWAY: std::sync::Mutex<Option<Away>> = std::sync::Mutex::new(None);

/// The home address of page `page` if it belongs to a live allocation.
fn page_home(page: usize) -> Option<usize> {
    let u = crate::gpu::uma::get()?;
    let index = page.checked_sub(1)?;
    let blocks = PHYS_BLOCKS.lock().ok()?;
    let (&first, &(count, live)) = blocks.range(..=index).next_back()?;
    (live > 0 && index < first.checked_add(count)?)
        .then(|| u.base().checked_add(index.checked_mul(PHYS_PAGE)?))
        .flatten()
        .filter(|&p| u.window_of(p).is_some() && u.window_of(p + PHYS_PAGE - 1) == u.window_of(p))
}

/// Resolve the live virtual addresses of the physical pages covering `offset..offset+len`.
/// Caller must hold `AWAY`.
fn resolve_physical_pages(away: Option<&Away>, pages: &[usize], offset: usize, len: usize) -> Option<Vec<usize>> {
    if offset >= PHYS_PAGE {
        return None;
    }
    let span = offset.checked_add(len)?;
    let need = span.div_ceil(PHYS_PAGE);
    if pages.len() < need {
        return None;
    }
    let mut addrs = Vec::with_capacity(need);
    for &p in &pages[..need] {
        let home = page_home(p)?;
        let addr = away.and_then(|a| a.page.get(&p).copied()).unwrap_or(home);
        addrs.push(addr);
    }
    Some(addrs)
}

/// Copy `dst.len()` bytes starting at `offset` within `pages` (each a 64 KiB physical page
/// number from `AllocateTitlePhysicalPages`). Returns `false` if `offset >= PHYS_PAGE_SIZE`,
/// `pages` is too short, or any page is not a live allocation.
pub unsafe fn read_physical_pages(pages: &[usize], offset: usize, dst: &mut [u8]) -> bool {
    if dst.is_empty() {
        return offset < PHYS_PAGE;
    }
    let g = AWAY.lock().unwrap();
    let Some(addrs) = resolve_physical_pages(g.as_ref(), pages, offset, dst.len()) else { return false };
    let mut done = 0;
    let mut page_off = offset;
    for addr in addrs {
        let chunk = (PHYS_PAGE - page_off).min(dst.len() - done);
        std::ptr::copy_nonoverlapping((addr + page_off) as *const u8, dst.as_mut_ptr().add(done), chunk);
        done += chunk;
        page_off = 0;
    }
    true
}

/// Copy `src` into `pages` starting at `offset` within the first page. Validates every page
/// before writing so the destination gets all bytes or nothing.
pub unsafe fn write_physical_pages(pages: &[usize], offset: usize, src: &[u8]) -> bool {
    if src.is_empty() {
        return offset < PHYS_PAGE;
    }
    let g = AWAY.lock().unwrap();
    let Some(addrs) = resolve_physical_pages(g.as_ref(), pages, offset, src.len()) else { return false };
    let mut done = 0;
    let mut page_off = offset;
    for addr in addrs {
        let chunk = (PHYS_PAGE - page_off).min(src.len() - done);
        std::ptr::copy_nonoverlapping(src.as_ptr().add(done), (addr + page_off) as *mut u8, chunk);
        done += chunk;
        page_off = 0;
    }
    true
}

/// Copy what is mapped at `at` back home and unmap it (Windows memory is decommitted).
unsafe fn unmap_away(away: &mut Away, at: usize) {
    if let Some(page) = away.at.remove(&at) {
        away.page.remove(&page);
        if let Some(home) = page_home(page) {
            std::ptr::copy_nonoverlapping(at as *const u8, home as *mut u8, PHYS_PAGE);
        }
        if !crate::gpu::uma::get().is_some_and(|u| u.is_committed(at)) {
            if crate::gpu::uma::get().is_some_and(|u| u.is_sparse(at)) {
                crate::hle::d3d12x::sparse::decommitted(at, PHYS_PAGE);
            }
            mem::VirtualFree(at as *mut c_void, PHYS_PAGE, mem::MEM_DECOMMIT);
        }
    }
}

fn mapped_page_readwrite(protect: u32) -> bool {
    protect & !(mem::PAGE_NOCACHE | mem::PAGE_WRITECOMBINE) == mem::PAGE_READWRITE
}

unsafe fn flush_away_range(away: Option<&Away>, start: usize, end: usize, allocation: Option<usize>) -> Vec<usize> {
    let Some(away) = away.filter(|_| start < end) else { return Vec::new() };
    let first = start & !(PHYS_PAGE - 1);
    let mut mappings = Vec::new();
    for (&at, &page) in away.at.range(first..end) {
        let mut mbi: mem::MEMORY_BASIC_INFORMATION = std::mem::zeroed();
        let queried = mem::VirtualQuery(at as *const c_void, &mut mbi, std::mem::size_of_val(&mbi)) != 0;
        if allocation.is_some_and(|base| !queried || mbi.AllocationBase as usize != base) {
            break;
        }
        if queried && mbi.State == mem::MEM_COMMIT && mapped_page_readwrite(mbi.Protect)
            && at + PHYS_PAGE - mbi.BaseAddress as usize <= mbi.RegionSize
        {
            if let Some(home) = page_home(page) {
                std::ptr::copy_nonoverlapping(at as *const u8, home as *mut u8, PHYS_PAGE);
            }
        }
        mappings.push(at);
    }
    mappings
}

fn remove_away_range(away: Option<&mut Away>, mappings: Vec<usize>) {
    if let Some(away) = away {
        for at in mappings {
            if let Some(page) = away.at.remove(&at) {
                away.page.remove(&page);
            }
        }
    }
}

pub unsafe extern "system" fn AllocateTitlePhysicalPages(_p: HANDLE, _t: u32, num: *mut usize, arr: *mut usize) -> BOOL {
    if num.is_null() || arr.is_null() || *num == 0 {
        fnd::SetLastError(fnd::ERROR_INVALID_PARAMETER);
        return 0;
    }
    let n = *num;
    let Some(bytes) = n.checked_mul(PHYS_PAGE) else {
        fnd::SetLastError(fnd::ERROR_NOT_ENOUGH_MEMORY);
        return 0;
    };
    let Some(u) = crate::gpu::uma::get() else {
        fnd::SetLastError(fnd::ERROR_NOT_ENOUGH_MEMORY);
        return 0;
    };
    let block = crate::hle::d3d12x::sparse::alloc(bytes, PHYS_PAGE)
        .map(|(p, _)| p)
        .or_else(|| u.alloc(bytes, PHYS_PAGE).map(|p| p as usize));
    let Some(block) = block else {
        crate::xlog_warn!("AllocateTitlePhysicalPages({n}): no room in UMA");
        fnd::SetLastError(fnd::ERROR_NOT_ENOUGH_MEMORY);
        return 0;
    };
    let first = (block - u.base()) / PHYS_PAGE;
    for i in 0..n {
        *arr.add(i) = first + i + 1;
    }
    PHYS_BLOCKS.lock().unwrap().insert(first, (n, n));
    1
}

pub unsafe extern "system" fn FreeTitlePhysicalPages(_p: HANDLE, n: usize, arr: *mut usize) -> BOOL {
    let Some(u) = crate::gpu::uma::get() else { return 0 };
    if arr.is_null() {
        fnd::SetLastError(fnd::ERROR_INVALID_PARAMETER);
        return 0;
    }
    // freed pages mapped elsewhere: that address keeps whatever it holds, the mapping goes
    let mut mappings = AWAY.lock().unwrap();
    if let Some(away) = mappings.as_mut() {
        for i in 0..n {
            if let Some(at) = away.page.remove(&*arr.add(i)) {
                away.at.remove(&at);
            }
        }
    }
    let mut g = PHYS_BLOCKS.lock().unwrap();
    for i in 0..n {
        let Some(page) = (*arr.add(i)).checked_sub(1) else { continue };
        // the block holding this page
        let Some((&first, &(count, _))) = g.range(..=page).next_back() else { continue };
        if page >= first + count {
            continue;
        }
        let e = g.get_mut(&first).unwrap();
        e.1 = e.1.saturating_sub(1);
        if e.1 == 0 {
            g.remove(&first);
            let addr = u.base() + first * PHYS_PAGE;
            if u.is_sparse(addr) {
                crate::hle::d3d12x::sparse::free(addr);
            } else {
                u.free(addr);
            }
        }
    }
    1
}

unsafe fn map_title_page(away: &mut Away, at: usize, page: usize) -> Result<(), u32> {
    let home = if page == 0 { None } else { Some(page_home(page).ok_or(fnd::ERROR_INVALID_PARAMETER)?) };
    unmap_away(away, at);
    let Some(home) = home else { return Ok(()) };
    if let Some(&old) = away.page.get(&page) {
        unmap_away(away, old);
    }
    if home == at {
        return Ok(());
    }
    let uma = crate::gpu::uma::get();
    if !uma.is_some_and(|u| u.is_committed(at)) && mem::VirtualAlloc(at as *const c_void, PHYS_PAGE, mem::MEM_COMMIT, mem::PAGE_READWRITE).is_null() {
        return Err(fnd::ERROR_INVALID_ADDRESS);
    }
    std::ptr::copy_nonoverlapping(home as *const u8, at as *mut u8, PHYS_PAGE);
    if uma.is_some_and(|u| u.is_sparse(at)) {
        crate::hle::d3d12x::sparse::committed(at, PHYS_PAGE);
    }
    away.at.insert(at, page);
    away.page.insert(page, at);
    Ok(())
}

pub unsafe extern "system" fn MapTitlePhysicalPages(va: *mut c_void, n: usize, _t: u32, _p: u32, arr: *mut usize) -> *mut c_void {
    if crate::gpu::uma::get().is_none() || n == 0 || (va.is_null() && arr.is_null()) {
        fnd::SetLastError(fnd::ERROR_INVALID_PARAMETER);
        return std::ptr::null_mut();
    }
    let pages: Vec<usize> = if arr.is_null() { vec![0; n] } else { (0..n).map(|i| *arr.add(i)).collect() };
    let mut g = AWAY.lock().unwrap();
    if pages.iter().any(|&p| p != 0 && page_home(p).is_none()) {
        fnd::SetLastError(fnd::ERROR_INVALID_PARAMETER);
        return std::ptr::null_mut();
    }
    let away = g.get_or_insert_with(Away::default);
    // consecutive pages at their home (or no address chosen): that UMA memory itself
    let consecutive = pages[0] != 0 && (1..n).all(|i| pages[0].checked_add(i) == Some(pages[i]));
    let home = if consecutive { page_home(pages[0]).filter(|_| page_home(pages[n - 1]).is_some()) } else { None };
    if va.is_null() || home == Some(va as usize) {
        let Some(home) = home else {
            fnd::SetLastError(fnd::ERROR_NOT_SUPPORTED); // scattered pages need an address
            return std::ptr::null_mut();
        };
        for &p in &pages {
            if let Some(&at) = away.page.get(&p) {
                unmap_away(away, at);
            }
        }
        return home as *mut c_void;
    }
    let base = va as usize;
    if !base.is_multiple_of(PHYS_PAGE) || n.checked_mul(PHYS_PAGE).and_then(|len| base.checked_add(len)).is_none() {
        fnd::SetLastError(fnd::ERROR_INVALID_PARAMETER);
        return std::ptr::null_mut();
    }
    for (i, &p) in pages.iter().enumerate() {
        if let Err(error) = map_title_page(away, base + i * PHYS_PAGE, p) {
            fnd::SetLastError(error);
            return std::ptr::null_mut();
        }
    }
    va
}

/// `BOOL XMemAllocatePhysicalPages(XALLOC_ATTRIBUTES, SIZE_T* count, ULONG64* pages)`: the
/// title physical pages above (64 KiB)
pub unsafe extern "system" fn XMemAllocatePhysicalPages(_attrs: u64, num: *mut usize, arr: *mut u64) -> BOOL {
    AllocateTitlePhysicalPages(std::mem::zeroed(), 0, num, arr as *mut usize)
}

/// `void* XMemMapPhysicalPages(void* address, SIZE_T count, ULONG64* pages)`
pub unsafe extern "system" fn XMemMapPhysicalPages(va: *mut c_void, n: usize, arr: *mut u64) -> *mut c_void {
    MapTitlePhysicalPages(va, n, 0, 0, arr as *mut usize)
}

pub unsafe extern "system" fn XMemMapPhysicalPagesScatter(addresses: *const *mut c_void, n: usize, pages: *const usize) -> BOOL {
    use crate::gpu::uma::{self, MAX_WINDOWS, WINDOW};
    let fail = |error| {
        fnd::SetLastError(error);
        0
    };
    if addresses.is_null() || !addresses.is_aligned() || !pages.is_aligned() || n == 0 || n > MAX_WINDOWS * WINDOW / PHYS_PAGE {
        return fail(fnd::ERROR_INVALID_PARAMETER);
    }
    let Some(u) = uma::get() else { return fail(fnd::ERROR_NOT_ENOUGH_MEMORY) };
    let mut g = AWAY.lock().unwrap();
    let away = g.get_or_insert_with(Away::default);
    let mut entries = Vec::with_capacity(n);
    for i in 0..n {
        let at = *addresses.add(i) as usize;
        let page = if pages.is_null() { 0 } else { *pages.add(i) };
        let Some(end) = at.checked_add(PHYS_PAGE) else { return fail(fnd::ERROR_INVALID_ADDRESS) };
        if at == 0 || !at.is_multiple_of(PHYS_PAGE) || (page != 0 && page_home(page).is_none()) {
            return fail(fnd::ERROR_INVALID_PARAMETER);
        }
        let reserved = if u.is_committed(at) {
            u.committed_reservation(at).is_some_and(|(base, size)| end - base <= size)
        } else if u.is_sparse(at) && !u.sparse_reservation(at).is_some_and(|(base, size)| end - base <= size) {
            false
        } else {
            let mut mbi: mem::MEMORY_BASIC_INFORMATION = std::mem::zeroed();
            mem::VirtualQuery(at as *const c_void, &mut mbi, std::mem::size_of_val(&mbi)) != 0
                && (mbi.State == mem::MEM_RESERVE
                    || (mbi.State == mem::MEM_COMMIT && mapped_page_readwrite(mbi.Protect) && away.at.contains_key(&at)))
                && end.checked_sub(mbi.BaseAddress as usize).is_some_and(|len| len <= mbi.RegionSize)
        };
        if !reserved {
            return fail(fnd::ERROR_INVALID_ADDRESS);
        }
        entries.push((at, page));
    }
    for (at, page) in entries {
        if let Err(error) = map_title_page(away, at, page) {
            return fail(error);
        }
    }
    1
}

/// `BOOL XMemFreePhysicalPages(SIZE_T count, ULONG64* pages)`
pub unsafe extern "system" fn XMemFreePhysicalPages(n: usize, arr: *mut u64) -> BOOL {
    FreeTitlePhysicalPages(std::mem::zeroed(), n, arr as *mut usize)
}

// ---------------------------------------------------------------------------------------
// App state (minappmodelx.dll, appnotify.h)
// ---------------------------------------------------------------------------------------

/// The registration handed out: the callbacks are never called (XCL does not suspend titles
/// or constrain them for the system UI).
static APP_NOTIFICATION: u8 = 0;

/// `ULONG RegisterAppStateChangeNotification(routine, context, PAPPSTATE_REGISTRATION* out)`
pub unsafe extern "system" fn RegisterAppStateChangeNotification(_routine: usize, _ctx: *mut c_void, out: *mut *const u8) -> u32 {
    if out.is_null() {
        return fnd::ERROR_INVALID_PARAMETER;
    }
    *out = &APP_NOTIFICATION;
    0
}

pub unsafe extern "system" fn UnregisterAppStateChangeNotification(_registration: *const u8) {}

/// `ULONG RegisterAppConstrainedChangeNotification(routine, context, PAPPCONSTRAIN_REGISTRATION* out)`
pub unsafe extern "system" fn RegisterAppConstrainedChangeNotification(routine: usize, ctx: *mut c_void, out: *mut *const u8) -> u32 {
    RegisterAppStateChangeNotification(routine, ctx, out)
}

pub unsafe extern "system" fn UnregisterAppConstrainedChangeNotification(_registration: *const u8) {}

// ---------------------------------------------------------------------------------------
// Threads / system info
// ---------------------------------------------------------------------------------------

fn hresult_to_win32(hr: i32) -> u32 {
    if (hr as u32 & 0xFFFF_0000) == 0x8007_0000 {
        hr as u32 & 0xFFFF
    } else {
        fnd::ERROR_INVALID_PARAMETER
    }
}

pub unsafe extern "system" fn SetThreadName(h: HANDLE, name: *const u16) -> BOOL {
    if let Some(n) = from_wide_ptr(name) {
        crate::xlog_debug!("SetThreadName({n})");
    }
    let hr = thr::SetThreadDescription(h, name);
    if hr < 0 {
        fnd::SetLastError(hresult_to_win32(hr));
        return 0;
    }
    1
}

pub unsafe extern "system" fn GetThreadName(h: HANDLE, buf: *mut u16, len: usize, ret_len: *mut usize) -> BOOL {
    if buf.is_null() && len > 0 {
        fnd::SetLastError(fnd::ERROR_INVALID_PARAMETER);
        return 0;
    }
    let mut p: *mut u16 = std::ptr::null_mut();
    let hr = thr::GetThreadDescription(h, &mut p);
    if hr < 0 || p.is_null() {
        fnd::SetLastError(hresult_to_win32(hr));
        return 0;
    }
    let s = from_wide_ptr(p).unwrap_or_default();
    fnd::LocalFree(p as *mut c_void);
    let n = s.encode_utf16().count();
    if !ret_len.is_null() {
        *ret_len = n;
    }
    if buf.is_null() || len <= n {
        fnd::SetLastError(fnd::ERROR_INSUFFICIENT_BUFFER);
        return 0;
    }
    write_wide_out(&s, buf, len.min(u32::MAX as usize) as u32);
    1
}

pub unsafe extern "system" fn SetThreadpoolAffinityMask(_pool: *mut c_void, _mask: usize) -> BOOL {
    1
}

pub unsafe extern "system" fn QueryProcessorSchedulingStatistics(out: *mut [u64; 3]) {
    if out.is_null() {
        return;
    }
    let mut idle = std::mem::zeroed::<fnd::FILETIME>();
    let mut kern = std::mem::zeroed::<fnd::FILETIME>();
    let mut user = std::mem::zeroed::<fnd::FILETIME>();
    thr::GetSystemTimes(&mut idle, &mut kern, &mut user);
    let ft = |f: fnd::FILETIME| ((f.dwHighDateTime as u64) << 32) | f.dwLowDateTime as u64;
    let (i, k, u) = (ft(idle), ft(kern), ft(user));
    *out = [(k + u).saturating_sub(i), i, k + u];
}

pub unsafe extern "system" fn GetConsoleType() -> u32 {
    crate::state::config().console_type_raw
}

#[repr(C)]
pub struct SystemOsVersionInfo {
    pub major: u8,
    pub minor: u8,
    pub build: u16,
    pub revision: u16,
}

#[repr(C)]
struct OsVersionInfoW {
    size: u32,
    major: u32,
    minor: u32,
    build: u32,
    platform: u32,
    csd: [u16; 128],
}

#[link(name = "ntdll", kind = "raw-dylib")]
extern "system" {
    fn RtlGetVersion(info: *mut OsVersionInfoW) -> i32;
}

pub fn host_os_version() -> (u32, u32, u32) {
    unsafe {
        let mut v: OsVersionInfoW = std::mem::zeroed();
        v.size = std::mem::size_of::<OsVersionInfoW>() as u32;
        RtlGetVersion(&mut v);
        (v.major, v.minor, v.build)
    }
}

pub unsafe extern "system" fn GetSystemOSVersion(out: *mut SystemOsVersionInfo) {
    if out.is_null() {
        return;
    }
    let (major, minor, build) = host_os_version();
    *out = SystemOsVersionInfo { major: major as u8, minor: minor as u8, build: build as u16, revision: 0 };
}

// ---------------------------------------------------------------------------------------
// Registry: the console mode
// ---------------------------------------------------------------------------------------
//
// The GDK thunks a title links compute XSystemGetDeviceType from
// HKLM\Software\Microsoft\Windows NT\CurrentVersion\OEM, value ConsoleMode (REG_DWORD):
// 0x64 Series X (XboxScarlettAnaconda), 0x60 Series S (Lockhart), 0x68 devkit, 0x40 / 0x50 /
// 0x58 Xbox One S / X / devkit, no key = PC – read from the thunk in the game's XCurl.dll.
// Only that key is answered here; everything else goes to the real registry.

const OEM_KEY: &str = "software\\microsoft\\windows nt\\currentversion\\oem";
/// handle of the answered key (never reaches the real registry)
const OEM_HANDLE: usize = 0x5843_4C4F; // "XCLO"

fn console_mode() -> u32 {
    if crate::state::config().is_series_s() {
        0x60
    } else {
        0x64
    }
}

unsafe fn is_oem_key(root: usize, path: *const u16) -> bool {
    let hklm = windows_sys::Win32::System::Registry::HKEY_LOCAL_MACHINE as usize;
    root == hklm && from_wide_ptr(path).is_some_and(|p| p.trim_start_matches('\\').eq_ignore_ascii_case(OEM_KEY))
}

pub unsafe extern "system" fn RegOpenKeyExW(root: usize, path: *const u16, options: u32, sam: u32, out: *mut usize) -> u32 {
    if is_oem_key(root, path) && !out.is_null() {
        crate::xlog_debug!("registry: OEM key → console mode {:#x}", console_mode());
        *out = OEM_HANDLE;
        return 0;
    }
    windows_sys::Win32::System::Registry::RegOpenKeyExW(root as _, path, options, sam, out as _)
}

pub unsafe extern "system" fn RegOpenKeyW(root: usize, path: *const u16, out: *mut usize) -> u32 {
    if is_oem_key(root, path) && !out.is_null() {
        *out = OEM_HANDLE;
        return 0;
    }
    windows_sys::Win32::System::Registry::RegOpenKeyW(root as _, path, out as _)
}

pub unsafe extern "system" fn RegQueryValueExW(key: usize, name: *const u16, reserved: *mut u32, ty: *mut u32, data: *mut u8, size: *mut u32) -> u32 {
    if key != OEM_HANDLE {
        return windows_sys::Win32::System::Registry::RegQueryValueExW(key as _, name, reserved, ty, data, size);
    }
    const ERROR_FILE_NOT_FOUND: u32 = 2;
    const ERROR_MORE_DATA: u32 = 234;
    if !from_wide_ptr(name).is_some_and(|n| n.eq_ignore_ascii_case("ConsoleMode")) {
        return ERROR_FILE_NOT_FOUND;
    }
    if !ty.is_null() {
        *ty = 4; // REG_DWORD
    }
    if !data.is_null() {
        if size.is_null() || *size < 4 {
            if !size.is_null() {
                *size = 4;
            }
            return ERROR_MORE_DATA;
        }
        std::ptr::copy_nonoverlapping(console_mode().to_le_bytes().as_ptr(), data, 4);
    }
    if !size.is_null() {
        *size = 4;
    }
    0
}

pub unsafe extern "system" fn RegCloseKey(key: usize) -> u32 {
    if key == OEM_HANDLE {
        0
    } else {
        windows_sys::Win32::System::Registry::RegCloseKey(key as _)
    }
}

// ---------------------------------------------------------------------------------------
// Modules
// ---------------------------------------------------------------------------------------

const LOAD_AS_DATA: u32 = 0x2 | 0x20 | 0x40; // DATAFILE | IMAGE_RESOURCE | DATAFILE_EXCLUSIVE

fn load_library_impl(name: Option<String>) -> usize {
    let Some(name) = name else {
        unsafe { fnd::SetLastError(fnd::ERROR_INVALID_PARAMETER) };
        return 0;
    };
    match resolver::load_library(&name) {
        Some(h) => {
            crate::xlog_debug!("LoadLibrary({name}) → {h:#x}");
            h
        }
        None => {
            crate::xlog_warn!("LoadLibrary({name}) → NULL");
            unsafe { fnd::SetLastError(fnd::ERROR_MOD_NOT_FOUND) };
            0
        }
    }
}

pub unsafe extern "system" fn LoadLibraryW(name: *const u16) -> usize {
    load_library_impl(from_wide_ptr(name))
}

pub unsafe extern "system" fn LoadLibraryA(name: *const u8) -> usize {
    load_library_impl(from_ansi_ptr(name))
}

unsafe fn load_as_data(name: &str, file: HANDLE, flags: u32) -> usize {
    let real = crate::vfs::get().and_then(|v| v.real_path_for_read(name)).unwrap_or_else(|| name.to_string());
    ll::LoadLibraryExW(crate::util::wide(&real).as_ptr(), file, flags) as usize
}

pub unsafe extern "system" fn LoadLibraryExW(name: *const u16, file: HANDLE, flags: u32) -> usize {
    if flags & LOAD_AS_DATA != 0 {
        let Some(name) = from_wide_ptr(name) else {
            fnd::SetLastError(fnd::ERROR_INVALID_PARAMETER);
            return 0;
        };
        return load_as_data(&name, file, flags);
    }
    load_library_impl(from_wide_ptr(name))
}

pub unsafe extern "system" fn LoadLibraryExA(name: *const u8, file: HANDLE, flags: u32) -> usize {
    if flags & LOAD_AS_DATA != 0 {
        let Some(name) = from_ansi_ptr(name) else {
            fnd::SetLastError(fnd::ERROR_INVALID_PARAMETER);
            return 0;
        };
        return load_as_data(&name, file, flags);
    }
    load_library_impl(from_ansi_ptr(name))
}

pub unsafe extern "system" fn LoadPackagedLibrary(name: *const u16, _reserved: u32) -> usize {
    load_library_impl(from_wide_ptr(name))
}

pub unsafe extern "system" fn GetProcAddress(h: usize, name: *const u8) -> usize {
    if name.is_null() {
        fnd::SetLastError(fnd::ERROR_INVALID_PARAMETER);
        return 0;
    }
    let what = if (name as usize) >> 16 == 0 {
        ImportName::Ordinal(name as usize as u16)
    } else {
        ImportName::Name { hint: 0, name: crate::util::from_cstr_ptr(name).unwrap_or_default() }
    };
    let h = if h == 0 { modules::main_module().map(|m| m.base).unwrap_or(0) } else { h };
    match resolver::get_proc_address(h, &what) {
        Some(a) => a,
        None => {
            fnd::SetLastError(fnd::ERROR_PROC_NOT_FOUND);
            0
        }
    }
}

fn module_handle_impl(name: &str) -> usize {
    module_handle_in(crate::hle::registry(), name)
}

fn module_handle_in(reg: &crate::hle::Registry, name: &str) -> usize {
    let key = dll_key(name);
    match reg.route(&key) {
        Some(crate::hle::Route::Hle(i)) => {
            let m = &reg.modules[*i];
            if m.aliases_are_system() && key != m.name {
                // Real Windows DLLs (kernel32 / kernelbase / advapi32 / Windows API sets) keep
                // their genuine HMODULE; console-only aliases (advapi32legacy, kernel32legacy,
                // minappmodelx) fall back to the HLE pseudo handle, matching LoadLibrary.
                // SAFETY: `wide(name)` produces a valid NUL-terminated UTF-16 string.
                let real = unsafe { ll::GetModuleHandleW(crate::util::wide(name).as_ptr()) as usize };
                if real != 0 {
                    return real;
                }
            }
            return modules::hle_handle(m.name);
        }
        Some(crate::hle::Route::Stub) => {
            // Return the pseudo handle only if the @stub DLL has already been loaded or imported,
            // preserving GetModuleHandle's non-loading probe semantics for optional DLLs.
            return modules::find_hle_handle(&key).unwrap_or(0);
        }
        Some(crate::hle::Route::System) => {
            // SAFETY: `wide(name)` produces a valid NUL-terminated UTF-16 string.
            return unsafe { ll::GetModuleHandleW(crate::util::wide(name).as_ptr()) as usize };
        }
        None => {}
    }
    if let Some(m) = modules::find_mapped_by_name(&key) {
        return m.base;
    }
    // SAFETY: `wide(name)` produces a valid NUL-terminated UTF-16 string.
    unsafe { ll::GetModuleHandleW(crate::util::wide(name).as_ptr()) as usize }
}

fn main_or_real_base() -> usize {
    modules::main_module()
        .map(|m| m.base)
        .unwrap_or_else(|| unsafe { ll::GetModuleHandleW(std::ptr::null()) as usize })
}

pub unsafe extern "system" fn GetModuleHandleW(name: *const u16) -> usize {
    match from_wide_ptr(name) {
        None => main_or_real_base(),
        Some(n) => {
            let h = module_handle_impl(&n);
            if h == 0 {
                fnd::SetLastError(fnd::ERROR_MOD_NOT_FOUND);
            }
            h
        }
    }
}

pub unsafe extern "system" fn GetModuleHandleA(name: *const u8) -> usize {
    match from_ansi_ptr(name) {
        None => main_or_real_base(),
        Some(n) => {
            let h = module_handle_impl(&n);
            if h == 0 {
                fnd::SetLastError(fnd::ERROR_MOD_NOT_FOUND);
            }
            h
        }
    }
}

const FROM_ADDRESS: u32 = 0x4;

unsafe fn module_handle_ex(flags: u32, name: Option<String>, addr: usize, out: *mut usize) -> BOOL {
    if out.is_null() {
        fnd::SetLastError(fnd::ERROR_INVALID_PARAMETER);
        return 0;
    }
    let h = if flags & FROM_ADDRESS != 0 {
        match modules::image_base_for_addr(addr) {
            Some(b) => b,
            None => {
                let mut r = std::ptr::null_mut();
                if ll::GetModuleHandleExW(flags, addr as *const u16, &mut r) != 0 {
                    r as usize
                } else {
                    0
                }
            }
        }
    } else {
        match name {
            None => main_or_real_base(),
            Some(n) => module_handle_impl(&n),
        }
    };
    *out = h;
    if h == 0 {
        fnd::SetLastError(fnd::ERROR_MOD_NOT_FOUND);
        0
    } else {
        1
    }
}

pub unsafe extern "system" fn GetModuleHandleExW(flags: u32, name: *const u16, out: *mut usize) -> BOOL {
    let n = if flags & FROM_ADDRESS != 0 { None } else { from_wide_ptr(name) };
    module_handle_ex(flags, n, name as usize, out)
}

pub unsafe extern "system" fn GetModuleHandleExA(flags: u32, name: *const u8, out: *mut usize) -> BOOL {
    let n = if flags & FROM_ADDRESS != 0 { None } else { from_ansi_ptr(name) };
    module_handle_ex(flags, n, name as usize, out)
}

fn module_path_for(h: usize) -> Option<String> {
    let main = modules::main_module();
    if h == 0 || main.as_ref().map(|m| m.base) == Some(h) {
        return main.map(|m| m.path.to_string_lossy().into_owned());
    }
    if let Some(m) = modules::find_mapped_by_base(h) {
        return Some(m.path.to_string_lossy().into_owned());
    }
    modules::hle_name_by_handle(h).map(|n| format!("C:\\Windows\\System32\\{n}"))
}

pub unsafe extern "system" fn GetModuleFileNameW(h: usize, buf: *mut u16, size: u32) -> u32 {
    match module_path_for(h) {
        Some(p) => write_wide_out(&p, buf, size),
        None => ll::GetModuleFileNameW(h as _, buf, size),
    }
}

pub unsafe extern "system" fn GetModuleFileNameA(h: usize, buf: *mut u8, size: u32) -> u32 {
    match module_path_for(h) {
        Some(p) => write_ansi_out(&p, buf, size),
        None => ll::GetModuleFileNameA(h as _, buf, size),
    }
}

pub unsafe extern "system" fn FreeLibrary(h: usize) -> BOOL {
    if modules::find_mapped_by_base(h).is_some() || modules::hle_name_by_handle(h).is_some() {
        return 1;
    }
    fnd::FreeLibrary(h as _)
}

/// A thread-pool callback asks for its module to be freed when it returns (the title's
/// msvcp140 does, for the reference it took on itself). A module XCL mapped is not in Windows'
/// loader – the pool's FreeLibrary fails and raises 0xC000070E in the worker (Minecraft: 20
/// workers at once, then a fail-fast). XCL's modules are never unloaded: nothing to do for them.
pub unsafe extern "system" fn FreeLibraryWhenCallbackReturns(instance: *mut c_void, h: usize) {
    let main = modules::main_module().is_some_and(|m| m.base == h);
    if main || modules::find_mapped_by_base(h).is_some() || modules::hle_name_by_handle(h).is_some() {
        return;
    }
    thr::FreeLibraryWhenCallbackReturns(instance as _, h as _)
}

pub unsafe extern "system" fn DisableThreadLibraryCalls(h: usize) -> BOOL {
    if let Some(m) = modules::find_mapped_by_base(h) {
        m.no_thread_calls.store(true, Ordering::Relaxed);
        return 1;
    }
    ll::DisableThreadLibraryCalls(h as _)
}

#[cfg(test)]
mod tests {
    use super::*;

    /// The GDK thunks' device type lookup: RegOpenKeyW(HKLM\…\OEM) + RegQueryValueExW(ConsoleMode).
    #[test]
    fn console_mode_registry_value() {
        unsafe {
            let hklm = windows_sys::Win32::System::Registry::HKEY_LOCAL_MACHINE as usize;
            let path = crate::util::wide("Software\\Microsoft\\Windows NT\\CurrentVersion\\OEM");
            let mut k = 0usize;
            assert_eq!(RegOpenKeyW(hklm, path.as_ptr(), &mut k), 0);
            let (mut ty, mut v, mut n) = (0u32, 0u32, 4u32);
            let name = crate::util::wide("ConsoleMode");
            assert_eq!(RegQueryValueExW(k, name.as_ptr(), std::ptr::null_mut(), &mut ty, &mut v as *mut u32 as *mut u8, &mut n), 0);
            assert_eq!((ty, n), (4, 4));
            assert_eq!(v, console_mode());
            let other = crate::util::wide("Other");
            assert_eq!(RegQueryValueExW(k, other.as_ptr(), std::ptr::null_mut(), &mut ty, &mut v as *mut u32 as *mut u8, &mut n), 2);
            assert_eq!(RegCloseKey(k), 0);
            // any other key: the real registry
            let real = crate::util::wide("Software\\Microsoft\\Windows NT\\CurrentVersion");
            let mut r = 0usize;
            assert_eq!(RegOpenKeyExW(hklm, real.as_ptr(), 0, 0x20019, &mut r), 0);
            assert_ne!(r, OEM_HANDLE);
            assert_eq!(RegCloseKey(r), 0);
        }
    }

    #[test]
    fn console_alloc_flags_are_stripped() {
        assert_eq!(sanitize_alloc_type(MEM_GRAPHICS | 0x1000 | 0x2000), 0x3000);
        assert_eq!(sanitize_alloc_type(MEM_TITLE), 0x3000);
        assert_eq!(sanitize_protect(0x1000_0004), 0x04);
        assert_eq!(sanitize_protect(0x2000_0000), mem::PAGE_READWRITE);
        unsafe {
            let p = VirtualAlloc(std::ptr::null(), 0x10000, MEM_GRAPHICS | MEM_TITLE | 0x3000, 0x0400_0004);
            assert!(!p.is_null());
            *(p as *mut u64) = 0xABCD;
            // graphics memory: from the GPU-visible region (when it could be created)
            if let Some(u) = crate::gpu::uma::get() {
                assert!(u.contains(p as usize));
            }
            assert_eq!(VirtualFree(p, 0, mem::MEM_RELEASE), 1);
            let q = VirtualAlloc(std::ptr::null(), 0x10000, 0x3000, 0x04); // ordinary memory: Windows
            assert!(crate::gpu::uma::get().is_none_or(|u| !u.contains(q as usize)));
            assert_eq!(VirtualFree(q, 0, mem::MEM_RELEASE), 1);
        }
    }

    #[test]
    fn xmem_alignment_and_zeroing() {
        let attrs = |align_log2: u64, mtype: u64| (align_log2 << 24) | (mtype << 29);
        assert_eq!(XAllocAttrs::decode(attrs(8, 0)).alignment, 8);
        unsafe {
            let p = XMemAlloc(1000, attrs(8, 0));
            assert!(!p.is_null());
            assert_eq!(p as usize % 256, 0);
            assert!(std::slice::from_raw_parts(p as *const u8, 1000).iter().all(|&b| b == 0));
            XMemFree(p, attrs(8, 0));
            let g = XMemAlloc(0x20000, attrs(0, 1)); // graphics memory → VirtualAlloc
            assert!(!g.is_null());
            assert_eq!(g as usize % 0x10000, 0);
            XMemFree(g, attrs(0, 1));
        }
    }

    #[test]
    fn xmem_virtual_query_marks_cpu_readable_memory() {
        unsafe {
            let mut info = [0u8; 64];
            let x = 5u32;
            let proc_ = thr::GetCurrentProcess();
            assert_eq!(XMemVirtualQuery(proc_, &x as *const u32 as _, info.as_mut_ptr() as _), 1);
            let mbi = &*(info.as_ptr() as *const mem::MEMORY_BASIC_INFORMATION);
            assert_eq!(mbi.State, mem::MEM_COMMIT);
            assert_eq!(u32::from_le_bytes(info[48..52].try_into().unwrap()) & 3, 1);
            // reserved, not committed: not CPU-readable
            let r = mem::VirtualAlloc(std::ptr::null(), 0x10000, mem::MEM_RESERVE, mem::PAGE_NOACCESS);
            let mut info = [0u8; 64];
            assert_eq!(XMemVirtualQuery(proc_, r, info.as_mut_ptr() as _), 1);
            assert_eq!(info[48] & 3, 0);
            mem::VirtualFree(r, 0, mem::MEM_RELEASE);
        }
    }

    #[test]
    fn physical_pages_in_uma() {
        unsafe {
            let Some(u) = crate::gpu::uma::get() else { return };
            let mut n = 3usize;
            let mut pages = [0usize; 3];
            assert_eq!(AllocateTitlePhysicalPages(std::ptr::null_mut(), 0, &mut n, pages.as_mut_ptr()), 1);
            assert_eq!(pages[1], pages[0] + 1);
            let p = MapTitlePhysicalPages(std::ptr::null_mut(), 3, 0, 4, pages.as_mut_ptr());
            assert!(!p.is_null() && u.contains(p as usize) && (p as usize - u.base()) % PHYS_PAGE == 0);
            *(p as *mut u32).add(PHYS_PAGE / 4) = 0x55AA; // page 1
            // the same pages mapped again: the same memory
            let q = MapTitlePhysicalPages(p, 3, 0, 4, pages.as_mut_ptr());
            assert_eq!(q, p);
            // a single page of the block maps to its own place
            let r = MapTitlePhysicalPages(std::ptr::null_mut(), 1, 0, 4, pages.as_mut_ptr().add(1));
            assert_eq!(*(r as *const u32), 0x55AA);
            // scattered pages: refused
            let mut scattered = [pages[2], pages[0]];
            assert!(MapTitlePhysicalPages(std::ptr::null_mut(), 2, 0, 4, scattered.as_mut_ptr()).is_null());
            // Page numbers must refer to one registered allocation; arithmetic is checked before
            // constructing an address inside the reserved UMA region.
            let mut outside = [u.size() / PHYS_PAGE + 1];
            assert!(MapTitlePhysicalPages(std::ptr::null_mut(), 1, 0, 4, outside.as_mut_ptr()).is_null());
            let mut wrapping = [usize::MAX];
            assert!(MapTitlePhysicalPages(std::ptr::null_mut(), 2, 0, 4, wrapping.as_mut_ptr()).is_null());
            // freed: the block is gone (other tests use UMA at the same time, so not `u.used()`)
            assert_eq!(FreeTitlePhysicalPages(std::ptr::null_mut(), 3, pages.as_mut_ptr()), 1);
            assert!(!PHYS_BLOCKS.lock().unwrap().contains_key(&(pages[0] - 1)));
            assert!(MapTitlePhysicalPages(std::ptr::null_mut(), 3, 0, 4, pages.as_mut_ptr()).is_null());
        }
    }

    #[test]
    fn xmem_scatter_maps_and_unmaps_noncontiguous_pages() {
        unsafe {
            let mut pages = [0u64; 2];
            let mut count = pages.len();
            assert_eq!(XMemAllocatePhysicalPages(0, &mut count, pages.as_mut_ptr()), 1);
            let u = crate::gpu::uma::get().unwrap();
            for base in [
                u.reserve(3 * PHYS_PAGE, PHYS_PAGE).unwrap().cast::<c_void>(),
                u.sparse_reserve(3 * PHYS_PAGE).unwrap() as *mut c_void,
                mem::VirtualAlloc(std::ptr::null(), 3 * PHYS_PAGE, mem::MEM_RESERVE, mem::PAGE_READWRITE),
            ] {
                assert!(!base.is_null());
                let addresses = [base.byte_add(2 * PHYS_PAGE), base];
                let pfns = [pages[1] as usize, pages[0] as usize];
                assert!(write_physical_pages(&pfns[..1], 0, &[0xA1]));
                assert!(write_physical_pages(&pfns[1..], 0, &[0xB2]));
                assert_eq!(XMemMapPhysicalPagesScatter(addresses.as_ptr(), addresses.len(), pfns.as_ptr()), 1);
                assert_eq!(*addresses[0].cast::<u8>(), 0xA1);
                assert_eq!(*addresses[1].cast::<u8>(), 0xB2);

                let invalid = [addresses[0], addresses[1].byte_add(1)];
                assert_eq!(XMemMapPhysicalPagesScatter(invalid.as_ptr(), invalid.len(), std::ptr::null()), 0);
                assert_eq!(fnd::GetLastError(), fnd::ERROR_INVALID_PARAMETER);
                let allocated_not_reserved = [addresses[0], page_home(pfns[0]).unwrap() as *mut c_void];
                assert_eq!(XMemMapPhysicalPagesScatter(allocated_not_reserved.as_ptr(), allocated_not_reserved.len(), std::ptr::null()), 0);
                assert_eq!(fnd::GetLastError(), fnd::ERROR_INVALID_ADDRESS);
                let invalid_pfns = [0, usize::MAX];
                assert_eq!(XMemMapPhysicalPagesScatter(addresses.as_ptr(), addresses.len(), invalid_pfns.as_ptr()), 0);
                assert_eq!(fnd::GetLastError(), fnd::ERROR_INVALID_PARAMETER);
                assert_eq!(*addresses[0].cast::<u8>(), 0xA1);

                *addresses[0].cast::<u8>() = 0xC3;
                let mixed = [0, pfns[1]];
                assert_eq!(XMemMapPhysicalPagesScatter(addresses.as_ptr(), addresses.len(), mixed.as_ptr()), 1);
                let g = AWAY.lock().unwrap();
                assert!(!g.as_ref().unwrap().at.contains_key(&(addresses[0] as usize)));
                assert_eq!(g.as_ref().unwrap().at.get(&(addresses[1] as usize)), Some(&pfns[1]));
                drop(g);
                *addresses[1].cast::<u8>() = 0xD4;
                assert_eq!(XMemMapPhysicalPagesScatter(addresses.as_ptr(), addresses.len(), std::ptr::null()), 1);

                let swapped = [pfns[1], pfns[0]];
                assert_eq!(XMemMapPhysicalPagesScatter(addresses.as_ptr(), addresses.len(), swapped.as_ptr()), 1);
                assert_eq!(*addresses[0].cast::<u8>(), 0xD4);
                assert_eq!(*addresses[1].cast::<u8>(), 0xC3);
                *addresses[0].cast::<u8>() = 0xE5;
                assert_eq!(VirtualFree(base, 0, mem::MEM_RELEASE), 1);
                let mut saved = [0];
                assert!(read_physical_pages(&swapped[..1], 0, &mut saved));
                assert_eq!(saved, [0xE5]);
            }
            assert_eq!(XMemFreePhysicalPages(pages.len(), pages.as_mut_ptr()), 1);
        }
    }

    #[test]
    fn xmem_split_and_replace_physical_page_placeholders() {
        unsafe {
            let size = 2 * PHYS_PAGE;
            let base = XMemVirtualAlloc(std::ptr::null(), 2 * size, mem::MEM_RESERVE | mem::MEM_RESERVE_PLACEHOLDER, 7, mem::PAGE_NOACCESS);
            assert!(!base.is_null(), "reserve placeholder: {}", fnd::GetLastError());
            let split = VirtualFree(base, size, mem::MEM_RELEASE | mem::MEM_PRESERVE_PLACEHOLDER);
            if split == 0 {
                let error = fnd::GetLastError();
                VirtualFree(base, 0, mem::MEM_RELEASE);
                panic!("split placeholder: {error}");
            }
            let second = base.byte_add(size);
            for (at, protect) in [(base, 0x1004), (second, 0x1404)] {
                let flags = MEM_64K_PAGES | mem::MEM_REPLACE_PLACEHOLDER | mem::MEM_RESERVE;
                assert_eq!(XMemVirtualAlloc(at, size, flags, 0x27, protect), at, "replace placeholder: {}", fnd::GetLastError());
            }
            let mut pages = [0u64; 2];
            let mut count = pages.len();
            assert_eq!(XMemAllocatePhysicalPages(0, &mut count, pages.as_mut_ptr()), 1);
            assert_eq!(count, pages.len());
            assert_eq!(XMemMapPhysicalPages(second, count, pages.as_mut_ptr()), second);
            second.cast::<u8>().write(0x3a);
            assert_eq!(XMemMapPhysicalPagesScatter(&second, 1, std::ptr::null()), 1);
            assert_eq!(XMemMapPhysicalPages(second, count, pages.as_mut_ptr()), second);
            assert_eq!(second.cast::<u8>().read(), 0x3a);
            second.cast::<u8>().write(0x4b);
            assert_eq!(VirtualFree(second, 0, mem::MEM_RELEASE), 1);
            let mut bytes = [0];
            assert!(read_physical_pages(&[pages[0] as usize], 0, &mut bytes));
            assert_eq!(bytes, [0x4b]);
            assert_eq!(XMemFreePhysicalPages(count, pages.as_mut_ptr()), 1);
            assert_eq!(VirtualFree(base, 0, mem::MEM_RELEASE), 1);
        }
    }

    #[test]
    fn xmem_tools_memory_is_unavailable() {
        assert_eq!(XMemIsToolsMemAvailable(), 0);
    }

    #[test]
    fn title_memory_status_reports_budget() {
        let mut s: TitleMemoryStatusT = unsafe { std::mem::zeroed() };
        s.dw_length = std::mem::size_of::<TitleMemoryStatusT>() as u32;
        assert_eq!(std::mem::size_of::<TitleMemoryStatusT>(), 80);
        assert_eq!(unsafe { TitleMemoryStatus(&mut s) }, 1);
        assert_eq!(s.total, 13_824 * 1024 * 1024);
        assert!(s.avail <= s.total);
        s.dw_length = 8;
        assert_eq!(unsafe { TitleMemoryStatus(&mut s) }, 0);
    }

    #[test]
    fn get_proc_address_and_load_library_ex_reject_null_name() {
        unsafe {
            let k32 = ll::GetModuleHandleW(crate::util::wide("kernel32.dll").as_ptr()) as usize;
            assert_ne!(k32, 0);
            fnd::SetLastError(0);
            assert!(ll::GetProcAddress(k32 as _, std::ptr::null()).is_none());
            let win32_err = fnd::GetLastError();

            fnd::SetLastError(0);
            assert_eq!(GetProcAddress(k32, std::ptr::null()), 0);
            assert_eq!(fnd::GetLastError(), win32_err);

            // Non-zero ordinal lookup still resolves when valid (ordinal #1 on kernel32).
            assert_ne!(GetProcAddress(k32, 1usize as *const u8), 0);

            fnd::SetLastError(0);
            assert_eq!(LoadLibraryExW(std::ptr::null(), std::ptr::null_mut(), LOAD_AS_DATA), 0);
            assert_eq!(fnd::GetLastError(), fnd::ERROR_INVALID_PARAMETER);

            fnd::SetLastError(0);
            assert_eq!(LoadLibraryExA(std::ptr::null(), std::ptr::null_mut(), LOAD_AS_DATA), 0);
            assert_eq!(fnd::GetLastError(), fnd::ERROR_INVALID_PARAMETER);
        }
    }

    #[test]
    fn get_thread_name_sizing_truncation_and_errors() {
        std::thread::spawn(|| unsafe {
            let cur = thr::GetCurrentThread();

            // Empty thread name: capacity 0 fails with ERROR_INSUFFICIENT_BUFFER, capacity 1 writes NUL.
            let empty = crate::util::wide("");
            assert_eq!(SetThreadName(cur, empty.as_ptr()), 1);
            let mut ret = 99usize;
            fnd::SetLastError(0);
            assert_eq!(GetThreadName(cur, std::ptr::null_mut(), 0, &mut ret), 0);
            assert_eq!(fnd::GetLastError(), fnd::ERROR_INSUFFICIENT_BUFFER);
            assert_eq!(ret, 0);
            let mut nul_only = [0xBEEFu16; 1];
            assert_eq!(GetThreadName(cur, nul_only.as_mut_ptr(), 1, &mut ret), 1);
            assert_eq!((ret, nul_only[0]), (0, 0));

            let want = crate::util::wide("xcl-worker-1");
            assert_eq!(SetThreadName(cur, want.as_ptr()), 1);

            // Null buffer with non-zero length -> ERROR_INVALID_PARAMETER.
            fnd::SetLastError(0);
            assert_eq!(GetThreadName(cur, std::ptr::null_mut(), 4, &mut ret), 0);
            assert_eq!(fnd::GetLastError(), fnd::ERROR_INVALID_PARAMETER);

            // Size query (null buffer, len == 0) -> FALSE + ERROR_INSUFFICIENT_BUFFER with ret_len = 12.
            fnd::SetLastError(0);
            assert_eq!(GetThreadName(cur, std::ptr::null_mut(), 0, &mut ret), 0);
            assert_eq!(fnd::GetLastError(), fnd::ERROR_INSUFFICIENT_BUFFER);
            assert_eq!(ret, 12);

            // Buffer equal to char count (no room for NUL terminator) -> FALSE + ERROR_INSUFFICIENT_BUFFER.
            let mut exact = [0xBEEFu16; 12];
            fnd::SetLastError(0);
            assert_eq!(GetThreadName(cur, exact.as_mut_ptr(), exact.len(), &mut ret), 0);
            assert_eq!(fnd::GetLastError(), fnd::ERROR_INSUFFICIENT_BUFFER);
            assert_eq!(ret, 12);

            // Buffer with room for chars + NUL -> TRUE and exact match (both with and without ret_len).
            let mut full = [0u16; 13];
            assert_eq!(GetThreadName(cur, full.as_mut_ptr(), full.len(), &mut ret), 1);
            assert_eq!(ret, 12);
            assert_eq!(&full[..], &want[..]);
            full.fill(0);
            assert_eq!(GetThreadName(cur, full.as_mut_ptr(), full.len(), std::ptr::null_mut()), 1);
            assert_eq!(&full[..], &want[..]);

            // Invalid thread handle -> FALSE + Win32 error set.
            fnd::SetLastError(0);
            assert_eq!(GetThreadName(std::ptr::null_mut(), full.as_mut_ptr(), full.len(), &mut ret), 0);
            assert_ne!(fnd::GetLastError(), 0);
        })
        .join()
        .unwrap();
    }

    #[test]
    fn xmem_virtual_query_excludes_guard_pages() {
        unsafe {
            let proc_ = thr::GetCurrentProcess();
            let page = mem::VirtualAlloc(std::ptr::null(), 0x1000, mem::MEM_COMMIT | mem::MEM_RESERVE, mem::PAGE_READWRITE | mem::PAGE_GUARD);
            assert!(!page.is_null());
            let mut info = [0u64; 8];
            assert_eq!(XMemVirtualQuery(proc_, page, info.as_mut_ptr() as _), 1);
            assert_eq!((info[6] as u32) & 3, 0, "PAGE_GUARD page must not be marked CPU-readable");
            mem::VirtualFree(page, 0, mem::MEM_RELEASE);
        }
    }

    #[test]
    fn xmem_and_physical_page_overflows_fail_cleanly() {
        unsafe {
            // CPU heap allocation overflow -> null + ERROR_NOT_ENOUGH_MEMORY.
            fnd::SetLastError(0);
            assert!(XMemAllocDefault(usize::MAX, 0).is_null());
            assert_eq!(fnd::GetLastError(), fnd::ERROR_NOT_ENOUGH_MEMORY);

            // Graphics layout rounding overflow (usize::MAX) and largest 64 KiB multiple
            // (which previously overflowed `size + GRAIN - 1` inside Uma::sparse_alloc / alloc_where).
            let gfx_attrs = 1u64 << 29;
            assert!(xmem_graphics_layout(usize::MAX, XAllocAttrs::decode(gfx_attrs)).is_none());
            fnd::SetLastError(0);
            assert!(XMemAllocDefault(usize::MAX, gfx_attrs).is_null());
            assert_eq!(fnd::GetLastError(), fnd::ERROR_NOT_ENOUGH_MEMORY);

            let max_aligned = usize::MAX & !(crate::gpu::uma::GRAIN - 1);
            assert!(XMemAllocDefault(max_aligned, gfx_attrs).is_null());

            // Physical page byte-count overflow -> FALSE + ERROR_NOT_ENOUGH_MEMORY.
            let mut huge = usize::MAX;
            let mut page = 0usize;
            fnd::SetLastError(0);
            assert_eq!(AllocateTitlePhysicalPages(std::ptr::null_mut(), 0, &mut huge, &mut page), 0);
            assert_eq!(fnd::GetLastError(), fnd::ERROR_NOT_ENOUGH_MEMORY);
        }
    }

    #[test]
    fn virtual_free_ex_and_sparse_validation() {
        unsafe {
            let cur = thr::GetCurrentProcess();

            // Failed MEM_RELEASE (non-zero size) must keep a tracked graphics reservation registered,
            // and VirtualFreeEx(GetCurrentProcess(), ...) must release & untrack it.
            let host = mem::VirtualAlloc(std::ptr::null(), 0x10000, mem::MEM_RESERVE, mem::PAGE_NOACCESS);
            assert!(!host.is_null());
            GRAPHICS_RESERVATIONS.lock().unwrap().insert(host as usize, 0x10000);
            assert!(in_graphics_reservation(host as usize));
            assert_eq!(VirtualFree(host, 0x1000, mem::MEM_RELEASE), 0);
            assert!(in_graphics_reservation(host as usize));
            assert_eq!(VirtualFreeEx(cur, host, 0, mem::MEM_RELEASE), 1);
            assert!(!in_graphics_reservation(host as usize));

            // Sparse UMA reservation: invalid flag combos / non-zero MEM_RELEASE fail without freeing;
            // VirtualFreeEx(GetCurrentProcess(), ...) releases cleanly.
            let sp = VirtualAlloc(std::ptr::null(), 0x10000, mem::MEM_RESERVE | MEM_GRAPHICS, mem::PAGE_READWRITE);
            if !sp.is_null() && crate::gpu::uma::get().is_some_and(|u| u.is_sparse(sp as usize)) {
                fnd::SetLastError(0);
                assert_eq!(VirtualFree(sp, 0x1000, mem::MEM_RELEASE), 0);
                assert_eq!(fnd::GetLastError(), fnd::ERROR_INVALID_PARAMETER);

                fnd::SetLastError(0);
                assert_eq!(VirtualFree(sp, 0, mem::MEM_RELEASE | mem::MEM_DECOMMIT), 0);
                assert_eq!(fnd::GetLastError(), fnd::ERROR_INVALID_PARAMETER);

                fnd::SetLastError(0);
                assert_eq!(VirtualFree(sp, 0, 0), 0);
                assert_eq!(fnd::GetLastError(), fnd::ERROR_INVALID_PARAMETER);

                assert_eq!(VirtualFreeEx(cur, sp, 0, mem::MEM_RELEASE), 1);
                assert!(crate::gpu::uma::get().unwrap().sparse_reservation(sp as usize).is_none());
            } else if !sp.is_null() {
                assert_eq!(VirtualFreeEx(cur, sp, 0, mem::MEM_RELEASE), 1);
            }
        }
    }

    #[test]
    fn get_module_handle_matches_load_library_for_console_aliases_and_stubs() {
        unsafe {
            // Real Windows system DLL keeps its genuine Windows HMODULE.
            let real_k32 = ll::GetModuleHandleW(crate::util::wide("kernel32.dll").as_ptr()) as usize;
            assert_ne!(real_k32, 0);
            assert_eq!(GetModuleHandleW(crate::util::wide("kernel32.dll").as_ptr()), real_k32);
            assert_eq!(GetModuleHandleA(c"kernel32.dll".as_ptr() as *const u8), real_k32);

            // Console-only kernelx aliases return the kernelx HLE pseudo handle (matching LoadLibrary).
            let kx = modules::hle_handle(crate::hle::KERNELX);
            for dll in ["advapi32legacy.dll", "kernel32legacy.dll", "minappmodelx.dll"] {
                let w = crate::util::wide(dll);
                let h_load = LoadLibraryW(w.as_ptr());
                assert_eq!(h_load, kx, "LoadLibraryW({dll})");
                assert_eq!(GetModuleHandleW(w.as_ptr()), kx, "GetModuleHandleW({dll})");

                let c = std::ffi::CString::new(dll).unwrap();
                assert_eq!(GetModuleHandleA(c.as_ptr() as *const u8), kx, "GetModuleHandleA({dll})");

                let mut out = 0usize;
                assert_eq!(GetModuleHandleExW(0, w.as_ptr(), &mut out), 1, "GetModuleHandleExW({dll})");
                assert_eq!(out, kx);
            }

            // Unknown DLL still returns NULL + ERROR_MOD_NOT_FOUND.
            fnd::SetLastError(0);
            assert_eq!(GetModuleHandleW(crate::util::wide("no_such_optional_dll_xyz.dll").as_ptr()), 0);
            assert_eq!(fnd::GetLastError(), fnd::ERROR_MOD_NOT_FOUND);
        }

        // @stub DLLs: unloaded probe returns 0; once loaded (hle_handle created), returns that handle.
        let mut cfg = crate::config::Config::default();
        cfg.aliases.insert("unit_stub_probe.dll".into(), "@stub".into());
        let reg = crate::hle::build(&cfg);
        assert_eq!(module_handle_in(&reg, "unit_stub_probe.dll"), 0);
        let stub_h = modules::hle_handle("unit_stub_probe.dll");
        assert_ne!(stub_h, 0);
        assert_eq!(module_handle_in(&reg, "unit_stub_probe.dll"), stub_h);
    }
}
