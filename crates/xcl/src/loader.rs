//! The XCL PE loader: maps console executables/DLLs into the current process and wires
//! them to HLE modules, game DLLs and real Windows DLLs (design §5.1).

use std::collections::HashMap;
use std::ffi::c_void;
use std::path::{Path, PathBuf};
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::{Arc, Mutex, OnceLock};

use windows_sys::Win32::System::Memory::{
    VirtualAlloc, VirtualFree, VirtualProtect, MEM_COMMIT, MEM_RELEASE, MEM_RESERVE, PAGE_EXECUTE,
    PAGE_EXECUTE_READ, PAGE_EXECUTE_READWRITE, PAGE_NOACCESS, PAGE_READONLY, PAGE_READWRITE,
};

use crate::modules::{self, MappedModule};
use crate::pe::{self, ExportTarget, Layout, OwnedExports, Pe};
use crate::resolver;

static CACHE_DIR: OnceLock<PathBuf> = OnceLock::new();
/// Serialises loads so that two threads calling LoadLibrary on the same game DLL do not
/// map it twice. Re-entrant loads (import resolution mapping dependencies) happen on the
/// same thread, so a re-entrant flag is used instead of holding the mutex across them.
static LOAD_LOCK: Mutex<()> = Mutex::new(());
static FAILED_ATTACH: Mutex<Option<HashMap<String, String>>> = Mutex::new(None);
thread_local! {
    static IN_LOAD: std::cell::Cell<u32> = const { std::cell::Cell::new(0) };
}
/// Set once the main image is ready; thread notifications are ignored before that.
static READY: AtomicBool = AtomicBool::new(false);

pub fn set_cache_dir(p: PathBuf) {
    let _ = CACHE_DIR.set(p);
}

fn cache_dir() -> PathBuf {
    CACHE_DIR
        .get()
        .cloned()
        .unwrap_or_else(|| std::env::temp_dir().join("xcl_cache"))
}

fn section_protection(ch: u32) -> u32 {
    let x = ch & pe::SCN_MEM_EXECUTE != 0;
    let r = ch & pe::SCN_MEM_READ != 0;
    let w = ch & pe::SCN_MEM_WRITE != 0;
    match (x, r, w) {
        (true, _, true) => PAGE_EXECUTE_READWRITE,
        (true, true, false) => PAGE_EXECUTE_READ,
        (true, false, false) => PAGE_EXECUTE,
        (false, _, true) => PAGE_READWRITE,
        (false, true, false) => PAGE_READONLY,
        (false, false, false) => PAGE_NOACCESS,
    }
}

type TlsCallback = unsafe extern "system" fn(*mut c_void, u32, *mut c_void);
type DllMain = unsafe extern "system" fn(*mut c_void, u32, *mut c_void) -> i32;
type ExeEntry = unsafe extern "system" fn(*mut c_void) -> u32;

/// Map an image, apply relocations, resolve imports, set up TLS / exception data and
/// page protections. Does NOT run any code of the image.
fn map_image_ex(path: &Path, is_main: bool) -> Result<Arc<MappedModule>, String> {
    let file = std::fs::read(path).map_err(|e| format!("reading {}: {e}", path.display()))?;
    let pe = Pe::parse(&file, Layout::File).map_err(|e| format!("{}: {e}", path.display()))?;
    let name = crate::util::dll_key(&path.file_name().map(|s| s.to_string_lossy().into_owned()).unwrap_or_default());
    let mut all = pe.imports().map_err(|e| format!("{name}: import table: {e}"))?;
    match pe.delay_imports() {
        Ok(v) => all.extend(v),
        Err(e) => crate::xlog_warn!("{name}: delay-imports: {e}"),
    }
    let size = pe.size_of_image as usize;

    // 1. reserve + commit
    let mut base = unsafe {
        VirtualAlloc(pe.image_base as *const c_void, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE)
    } as usize;
    if base == 0 {
        if !pe.has_relocs() {
            return Err(format!(
                "{name}: preferred address {:#x} is taken and the image has no relocations",
                pe.image_base
            ));
        }
        base = unsafe { VirtualAlloc(std::ptr::null(), size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE) } as usize;
        if base == 0 {
            return Err(format!("{name}: VirtualAlloc({size:#x}) failed"));
        }
    }
    let delta = base as i64 - pe.image_base as i64;
    let fail = |msg: String| {
        unsafe { VirtualFree(base as *mut c_void, 0, MEM_RELEASE) };
        Err(msg)
    };

    // 2. headers + sections
    unsafe {
        let hdr = (pe.size_of_headers as usize).min(file.len()).min(size);
        std::ptr::copy_nonoverlapping(file.as_ptr(), base as *mut u8, hdr);
        for s in &pe.sections {
            let raw = (s.raw_size as usize).min(s.mapped_size() as usize);
            if raw == 0 {
                continue;
            }
            let src = s.raw_ptr as usize;
            if src + raw > file.len() || s.va as usize + raw > size {
                return fail(format!("{name}: section {} outside the file/image", s.name));
            }
            std::ptr::copy_nonoverlapping(file.as_ptr().add(src), (base + s.va as usize) as *mut u8, raw);
        }
    }

    // 3. relocations
    if delta != 0 {
        let relocs = match pe.relocations() {
            Ok(r) => r,
            Err(e) => return fail(format!("{name}: relocations: {e}")),
        };
        for r in relocs {
            let p = base + r.rva as usize;
            unsafe {
                match r.kind {
                    pe::IMAGE_REL_BASED_DIR64 => {
                        let v = (p as *mut u64).read_unaligned();
                        (p as *mut u64).write_unaligned((v as i64 + delta) as u64);
                    }
                    pe::IMAGE_REL_BASED_HIGHLOW => {
                        let v = (p as *mut u32).read_unaligned();
                        (p as *mut u32).write_unaligned((v as i64 + delta) as u32);
                    }
                    k => crate::xlog_warn!("{name}: unsupported relocation type {k} @ {:#x}", r.rva),
                }
            }
        }
    }

    let exports = OwnedExports::from_pe(&pe).unwrap_or_default();
    let module = Arc::new(MappedModule {
        name: name.clone(),
        path: path.to_path_buf(),
        base,
        size,
        entry_rva: pe.entry_rva,
        is_dll: pe.is_dll(),
        exports,
        tls_callbacks: Mutex::new(Vec::new()),
        no_thread_calls: AtomicBool::new(false),
        initialized: AtomicBool::new(false),
        stack_reserve: pe.stack_reserve,
    });
    // registered before import resolution so cyclic game-DLL imports terminate
    modules::register_mapped(module.clone());
    if is_main {
        // game DLLs initialised during import resolution may already ask for
        // GetModuleHandle(NULL) / GetModuleFileName(NULL)
        modules::set_main(module.clone());
    }
    crate::xlog_info!(
        "loaded {name} @ {base:#x} (size {size:#x}, preferred {:#x}{})",
        pe.image_base,
        if delta != 0 { ", relocated" } else { "" }
    );

    // 4. imports (normal + delay-load, resolved eagerly)
    for dll in &all {
        for e in &dll.entries {
            let addr = resolver::resolve_import(&dll.dll, &e.what, &name, dll.delay);
            unsafe { ((base + e.iat_rva as usize) as *mut u64).write_unaligned(addr as u64) };
        }
        if dll.delay && dll.module_handle_rva != 0 {
            let h = resolver::load_library(&dll.dll).unwrap_or(1);
            unsafe { ((base + dll.module_handle_rva as usize) as *mut u64).write_unaligned(h as u64) };
        }
    }

    // 5. static TLS
    if let Ok(Some(t)) = pe.tls() {
        let adj = |va: u64| (va as i64 + delta) as usize;
        let template = adj(t.start_raw);
        let template_len = t.template_len() as usize;
        let index_ptr = adj(t.address_of_index) as *mut u32;
        unsafe {
            if let Err(e) = tls_install(&module, template, template_len, t.total_len(), index_ptr) {
                reject_mapped(&module, &e);
                return Err(e);
            }
            if t.address_of_callbacks != 0 {
                let mut cb = adj(t.address_of_callbacks) as *const u64;
                let mut list = module.tls_callbacks.lock().unwrap();
                while *cb != 0 {
                    list.push(*cb as usize);
                    cb = cb.add(1);
                }
            }
        }
    }

    // 6. page protections
    unsafe {
        let mut old = 0u32;
        VirtualProtect(base as *const c_void, pe.size_of_headers as usize, PAGE_READONLY, &mut old);
        for s in &pe.sections {
            let len = ((s.mapped_size() as usize) + 0xFFF) & !0xFFF;
            if len == 0 {
                continue;
            }
            VirtualProtect((base + s.va as usize) as *const c_void, len, section_protection(s.characteristics), &mut old);
        }
        windows_sys::Win32::System::Diagnostics::Debug::FlushInstructionCache(
            windows_sys::Win32::System::Threading::GetCurrentProcess(),
            base as *const c_void,
            size,
        );
    }

    // 7. exception data
    let ex = pe.dir(pe::DIR_EXCEPTION);
    if ex.rva != 0 && ex.size != 0 {
        crate::eh::register_function_table(base, ex.rva, ex.size);
    }
    // 8. stack-cookie failures reported before the process ends
    for s in pe.sections.iter().filter(|s| s.characteristics & 0x2000_0000 != 0) {
        let len = (s.mapped_size() as usize).min(size.saturating_sub(s.va as usize));
        unsafe { crate::eh::hook_gs_failure(base + s.va as usize, len, &name) };
    }
    if is_main {
        bind_xmem_hooks(&module, &pe.sections);
    }
    Ok(module)
}

fn bind_xmem_hooks(module: &MappedModule, sections: &[pe::Section]) {
    let Some(export) = module.exports.by_name.get("__XMemAllocationHooks__") else { return };
    let in_section = |offset: usize, len: usize, flag: u32| {
        offset.checked_add(len).is_some_and(|end| {
            end <= module.size && sections.iter().any(|s| {
                s.characteristics & flag != 0 && offset >= s.va as usize && end - s.va as usize <= s.mapped_size() as usize
            })
        })
    };
    if let ExportTarget::Rva(rva) = *export {
        let offset = rva as usize;
        if in_section(offset, std::mem::size_of::<[usize; 2]>(), pe::SCN_MEM_READ) {
            let [alloc, free] = unsafe { ((module.base + offset) as *const [usize; 2]).read_unaligned() };
            let executable = |addr: usize| addr.checked_sub(module.base).is_some_and(|rva| in_section(rva, 1, pe::SCN_MEM_EXECUTE));
            if executable(alloc) && executable(free) {
                unsafe { crate::hle::kernelx::XMemSetAllocationHooks(alloc, free) };
                crate::xlog_info!("{}: bound exported XMem allocation hooks", module.name);
                return;
            }
        }
    }
    crate::xlog_warn!("{}: ignoring invalid XMem allocation hooks export", module.name);
}

unsafe fn tls_install(m: &MappedModule, template: usize, len: usize, total: u64, index_ptr: *mut u32) -> Result<(), String> {
    let k = crate::tls::install(&m.name, template, len, total, index_ptr, &cache_dir())?;
    // the title's executable: slot 0, as a process's executable has it
    if !m.is_dll {
        if let Err(e) = crate::tls::take_slot_zero(&m.name, index_ptr, k) {
            crate::xlog_warn!("TLS: {} keeps slot {k} ({e}) – code built with /GA reads slot 0", m.name);
        }
    }
    Ok(())
}

unsafe fn run_tls_callbacks(m: &MappedModule, reason: u32) {
    let cbs = m.tls_callbacks.lock().map(|v| v.clone()).unwrap_or_default();
    for cb in cbs {
        let f: TlsCallback = std::mem::transmute(cb);
        f(m.base as *mut c_void, reason, std::ptr::null_mut());
    }
}

/// Run TLS callbacks (PROCESS_ATTACH) and DllMain of a freshly mapped DLL.
pub fn init_dll(m: &Arc<MappedModule>) -> Result<(), String> {
    unsafe {
        run_tls_callbacks(m, 1);
        if m.entry_rva != 0 {
            let f: DllMain = std::mem::transmute(m.base + m.entry_rva as usize);
            crate::xlog_info!("DllMain({}, PROCESS_ATTACH)", m.name);
            if f(m.base as *mut c_void, 1, std::ptr::null_mut()) == 0 {
                return Err(format!("DllMain({}) returned FALSE", m.name));
            }
        }
    }
    m.initialized.store(true, Ordering::Release);
    Ok(())
}

struct LoadGuard;
pub(crate) fn on_loading_thread() -> bool {
    IN_LOAD.try_with(|c| c.get() != 0).unwrap_or(false)
}

impl LoadGuard {
    fn enter() -> (LoadGuard, Option<std::sync::MutexGuard<'static, ()>>) {
        let outer = IN_LOAD.with(|c| {
            let v = c.get();
            c.set(v + 1);
            v == 0
        });
        let g = if outer { Some(LOAD_LOCK.lock().unwrap_or_else(|e| e.into_inner())) } else { None };
        (LoadGuard, g)
    }
}
impl Drop for LoadGuard {
    fn drop(&mut self) {
        IN_LOAD.with(|c| c.set(c.get().saturating_sub(1)));
    }
}

/// Map + initialise a DLL that ships with the game (idempotent).
pub fn load_game_dll(path: &Path) -> Result<Arc<MappedModule>, String> {
    let key = crate::util::dll_key(&path.to_string_lossy());
    let (_g, _lock) = LoadGuard::enter();
    if let Some(m) = modules::find_mapped_by_name(&key) {
        return Ok(m);
    }
    if let Some(error) = FAILED_ATTACH.lock().unwrap().as_ref().and_then(|failed| failed.get(&key)).cloned() {
        return Err(error);
    }
    let m = map_image_ex(path, false)?;
    if !m.is_dll {
        crate::xlog_warn!("{key} is not a DLL but is being loaded as a library");
    }
    if let Err(e) = init_dll(&m) {
        reject_mapped(&m, &e);
        return Err(e);
    }
    Ok(m)
}

fn reject_mapped(m: &Arc<MappedModule>, error: &str) {
    modules::unregister_mapped(m);
    FAILED_ATTACH.lock().unwrap().get_or_insert_with(HashMap::new).insert(m.name.clone(), error.to_owned());
}

/// Map the title's main executable.
pub fn load_main(path: &Path) -> Result<Arc<MappedModule>, String> {
    let (_g, _lock) = LoadGuard::enter();
    map_image_ex(path, true)
}

#[inline(always)]
unsafe fn peb() -> *mut u8 {
    let p: *mut u8;
    core::arch::asm!("mov {}, gs:[0x60]", out(reg) p, options(nostack, readonly, preserves_flags));
    p
}

struct EntryArgs {
    entry: usize,
}

unsafe extern "system" fn main_thread(param: *mut c_void) -> u32 {
    let args = Box::from_raw(param as *mut EntryArgs);
    let f: ExeEntry = std::mem::transmute(args.entry);
    f(peb() as *mut c_void)
}

/// Make the game the "main image" of the process and run its entry point on a thread
/// with the stack size its PE header asks for. Usually never returns (the game calls
/// ExitProcess); if the entry returns, its value is returned.
pub fn run_main(m: &Arc<MappedModule>) -> u32 {
    use windows_sys::Win32::Foundation::{CloseHandle, WAIT_OBJECT_0};
    use windows_sys::Win32::System::Threading::{
        CreateThread, GetExitCodeThread, WaitForSingleObject, INFINITE, STACK_SIZE_PARAM_IS_A_RESERVATION,
    };
    unsafe {
        // PEB.ImageBaseAddress → GetModuleHandle(NULL), resources with NULL module, …
        use windows_sys::Win32::System::LibraryLoader::{GetModuleHandleA, GetProcAddress};
        let wine = GetProcAddress(GetModuleHandleA(c"ntdll.dll".as_ptr() as *const u8), c"wine_get_version".as_ptr() as *const u8).is_some();
        if wine {
            crate::xlog_info!("Wine: PEB.ImageBaseAddress left alone (its loader needs the real main image there)");
        } else {
            *(peb().add(0x10) as *mut usize) = m.base;
        }
        run_tls_callbacks(m, 1);
    }
    READY.store(true, Ordering::Release);
    let stack = (m.stack_reserve as usize).max(1 << 20);
    let args = Box::into_raw(Box::new(EntryArgs { entry: m.base + m.entry_rva as usize }));
    crate::xlog_info!(
        "start {}: entry point {} , stack {:#x}",
        m.name,
        modules::describe_address(m.base + m.entry_rva as usize),
        stack
    );
    crate::log::flush();
    unsafe {
        let h = CreateThread(
            std::ptr::null(),
            stack,
            Some(main_thread),
            args as *const c_void,
            STACK_SIZE_PARAM_IS_A_RESERVATION,
            std::ptr::null_mut(),
        );
        if h.is_null() {
            crate::xlog_error!("CreateThread for the game thread failed");
            return 0xC000_0001;
        }
        let mut code = 0u32;
        if WaitForSingleObject(h, INFINITE) == WAIT_OBJECT_0 {
            GetExitCodeThread(h, &mut code);
        }
        CloseHandle(h);
        code
    }
}

/// Called from xcl.exe's own TLS callback for every thread event in the process.
pub fn on_tls_event(reason: u32) {
    match reason {
        2 => unsafe {
            // DLL_THREAD_ATTACH: game TLS blocks first, then game callbacks/DllMains.
            crate::tls::on_thread_attach();
            if !READY.load(Ordering::Acquire) {
                return;
            }
            for m in modules::mapped_modules() {
                run_tls_callbacks(&m, 2);
                if m.is_dll && m.initialized.load(Ordering::Acquire) && !m.no_thread_calls.load(Ordering::Relaxed) && m.entry_rva != 0 {
                    let f: DllMain = std::mem::transmute(m.base + m.entry_rva as usize);
                    f(m.base as *mut c_void, 2, std::ptr::null_mut());
                }
            }
        },
        3 => unsafe {
            crate::tls::on_thread_detach();
            if !READY.load(Ordering::Acquire) {
                return;
            }
            for m in modules::mapped_modules().into_iter().rev() {
                if m.is_dll && m.initialized.load(Ordering::Acquire) && !m.no_thread_calls.load(Ordering::Relaxed) && m.entry_rva != 0 {
                    let f: DllMain = std::mem::transmute(m.base + m.entry_rva as usize);
                    f(m.base as *mut c_void, 3, std::ptr::null_mut());
                }
                run_tls_callbacks(&m, 3);
            }
        },
        0 => {
            // DLL_PROCESS_DETACH (ExitProcess): write the report while everything is alive.
            crate::report::write_final(None);
            crate::log::flush();
        }
        _ => {}
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn provisional_dll_is_visible_to_its_loader_until_rejected() {
        let m = Arc::new(MappedModule {
            name: "provisional_loader_test.dll".into(),
            path: PathBuf::from("provisional_loader_test.dll"),
            base: 0x7fff_ffff_0000,
            size: 4096,
            entry_rva: 0,
            is_dll: true,
            exports: OwnedExports::default(),
            tls_callbacks: Mutex::new(Vec::new()),
            no_thread_calls: AtomicBool::new(false),
            initialized: AtomicBool::new(false),
            stack_reserve: 0,
        });
        let (_g, _lock) = LoadGuard::enter();
        modules::register_mapped(m.clone());
        assert!(Arc::ptr_eq(&load_game_dll(&m.path).unwrap(), &m));
        assert!(std::thread::spawn(|| modules::find_mapped_by_name("provisional_loader_test.dll").is_none()).join().unwrap());
        reject_mapped(&m, "test attach failed");
        assert!(modules::find_mapped_by_name(&m.name).is_none());
        assert!(modules::image_base_for_addr(m.base).is_none());
        assert_eq!(load_game_dll(&m.path).err().as_deref(), Some("test attach failed"));
    }
}
