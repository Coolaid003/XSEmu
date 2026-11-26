//! The launcher's control pipe: `XSEmu run --control-pipe <name>` (docs/launcher-protocol.md).
//! XSEmu connects as a client to `\\.\pipe\<name>` (the launcher's QLocalServer) and speaks JSON
//! lines: `hello`, `state`, `stats` once a second, `key` and `exit` from here; `pause`,
//! `resume`, `stop`, `overlay` and `focus` from the launcher. The pipe handle is overlapped,
//! so the reader waiting for a line never holds up a write, and lines go out through one writer
//! thread, so no caller (the window thread with a key) waits for a launcher that does not read.
//! Without the option nothing here runs.

use std::collections::VecDeque;
use std::sync::atomic::{AtomicBool, AtomicU32, AtomicU8, Ordering};
use std::sync::mpsc::{self, Receiver, Sender, SyncSender};
use std::sync::{Condvar, Mutex, OnceLock};
use std::time::{Duration, Instant};

use serde::Serialize;

/// The pipe's name, handed from `XSEmu run` to the host process it starts.
pub const CONTROL_PIPE_ENV: &str = "XSEMU_CONTROL_PIPE";

/// A launcher → emulator message.
#[derive(Debug, PartialEq, Eq)]
pub enum Command {
    Pause,
    Resume,
    Stop,
    /// on, off, or (no `on`) the other way round
    Overlay(Option<bool>),
    Focus,
}

/// One line from the launcher (None: not a command; unknown ones are ignored).
pub fn parse_command(line: &str) -> Option<Command> {
    let v: serde_json::Value = serde_json::from_str(line.trim()).ok()?;
    match v.get("cmd")?.as_str()? {
        "pause" => Some(Command::Pause),
        "resume" => Some(Command::Resume),
        "stop" => Some(Command::Stop),
        "overlay" => Some(Command::Overlay(v.get("on").and_then(serde_json::Value::as_bool))),
        "focus" => Some(Command::Focus),
        _ => None,
    }
}

fn line(v: &impl Serialize) -> String {
    let mut s = serde_json::to_string(v).unwrap_or_default();
    s.push('\n');
    s
}

#[derive(Serialize)]
struct Hello<'a> {
    ev: &'static str,
    proto: u32,
    pid: u32,
    version: &'static str,
    #[serde(skip_serializing_if = "Option::is_none")]
    title: Option<&'a str>,
    #[serde(skip_serializing_if = "Option::is_none")]
    device_type: Option<&'a str>,
    #[serde(skip_serializing_if = "Option::is_none")]
    hwnd: Option<String>,
}

fn hello_line(pid: u32, title: Option<&str>, device_type: Option<&str>, hwnd: Option<isize>) -> String {
    line(&Hello { ev: "hello", proto: 1, pid, version: env!("CARGO_PKG_VERSION"), title, device_type, hwnd: hwnd.map(|h| format!("{h:x}")) })
}

fn state_line(state: &str) -> String {
    line(&serde_json::json!({ "ev": "state", "state": state }))
}

fn key_line(key: &str) -> String {
    line(&serde_json::json!({ "ev": "key", "key": key }))
}

fn exit_line(code: u32, reason: &str, message: &str) -> String {
    line(&serde_json::json!({ "ev": "exit", "code": code, "reason": reason, "message": message }))
}

/// What `stats` says; a field left None was not measured and is left out.
#[derive(Serialize, Debug, Default, PartialEq)]
pub struct Stats {
    pub fps: f64,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub frame_ms: Option<f64>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub ram_free_mb: Option<u64>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub vram_used_mb: Option<u64>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub vram_budget_mb: Option<u64>,
    pub stalls: u32,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub worst_ms: Option<f64>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub pipelines: Option<usize>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub compiling: Option<usize>,
}

fn stats_line(s: &Stats) -> String {
    #[derive(Serialize)]
    struct Line<'a> {
        ev: &'static str,
        #[serde(flatten)]
        stats: &'a Stats,
    }
    line(&Line { ev: "stats", stats: s })
}

/// a frame shown this long after the one before is a stall (ms)
const STALL_MS: f32 = 100.0;

fn tenths(v: f64) -> f64 {
    (v * 10.0).round() / 10.0
}

/// The frame rate, mean and worst frame time and the stalls of `shown` frames in `elapsed_s`
/// seconds, the times between them `intervals_ms`.
fn frame_stats(shown: u64, elapsed_s: f64, intervals_ms: &[f32]) -> (f64, Option<f64>, Option<f64>, u32) {
    let fps = if elapsed_s > 0.0 { tenths(shown as f64 / elapsed_s) } else { 0.0 };
    if intervals_ms.is_empty() {
        return (fps, None, None, 0);
    }
    let mean = intervals_ms.iter().map(|&m| m as f64).sum::<f64>() / intervals_ms.len() as f64;
    let worst = intervals_ms.iter().copied().fold(0.0f32, f32::max) as f64;
    let stalls = intervals_ms.iter().filter(|&&m| m >= STALL_MS).count() as u32;
    (fps, Some(tenths(mean)), Some(tenths(worst)), stalls)
}

/// frames of the overlay's graph
const GRAPH_FRAMES: usize = 120;
/// frame time at the graph's top (ms)
const GRAPH_MS: f32 = 50.0;

/// The performance overlay: the last frames' times as bars in the top-left corner of a
/// `w`×`h` frame, newest on the right, green to 60 fps, amber to 30, red slower, with a line at
/// 16.7 ms; drawn by `present` like the progress bar (rectangles with their colour).
pub fn graph_rects(w: u32, h: u32, frame_ms: &[f32]) -> Vec<(crate::progress::Rect, [f32; 4])> {
    let (w, h) = (w as i32, h as i32);
    let margin = (h / 60).max(2);
    let bar = (w / 4 / GRAPH_FRAMES as i32).max(1);
    let (gw, gh) = (bar * GRAPH_FRAMES as i32, (h / 8).max(16));
    if gw + 2 * margin > w || gh + 2 * margin > h {
        return Vec::new();
    }
    let (left, bottom) = (margin, margin + gh);
    let mut rects = vec![([left, margin, left + gw, bottom], [0.02, 0.02, 0.02, 1.0])];
    for (k, &ms) in frame_ms.iter().rev().take(GRAPH_FRAMES).enumerate() {
        let x = left + gw - (k as i32 + 1) * bar;
        let height = ((ms / GRAPH_MS).min(1.0) * gh as f32).round().max(1.0) as i32;
        let colour = if ms <= 17.5 {
            [0.2, 0.8, 0.3, 1.0]
        } else if ms <= 34.0 {
            [1.0, 0.72, 0.18, 1.0]
        } else {
            [0.9, 0.2, 0.2, 1.0]
        };
        rects.push(([x, bottom - height, x + bar, bottom], colour));
    }
    let y = bottom - (16.7 / GRAPH_MS * gh as f32).round() as i32;
    rects.push(([left, y, left + gw, y + 1], [0.6, 0.6, 0.6, 1.0]));
    rects
}

/// `\\.\pipe\<name>` for a QLocalServer name (a full pipe path stays as it is).
fn pipe_path(name: &str) -> String {
    if name.starts_with(r"\\") {
        name.to_string()
    } else {
        format!(r"\\.\pipe\{name}")
    }
}

// ---------------------------------------------------------------------------------------
// the state the launcher sees and steers
// ---------------------------------------------------------------------------------------

const BOOTING: u8 = 0;
const RUNNING: u8 = 1;
const PAUSED: u8 = 2;
const STOPPING: u8 = 3;

fn state_name(s: u8) -> &'static str {
    match s {
        BOOTING => "booting",
        RUNNING => "running",
        PAUSED => "paused",
        _ => "stopping",
    }
}

static STATE: AtomicU8 = AtomicU8::new(BOOTING);
static PAUSE: Mutex<bool> = Mutex::new(false);
static RESUMED: Condvar = Condvar::new();
static IS_PAUSED: AtomicBool = AtomicBool::new(false);
static OVERLAY: AtomicBool = AtomicBool::new(false);
static EXITED: AtomicBool = AtomicBool::new(false);
/// the status the process ends with (`eh`'s NtTerminateProcess hook), for an `exit` at the end
static EXIT_STATUS: AtomicU32 = AtomicU32::new(0);

/// The launcher paused the title: its presentation is held (`wait_while_paused`) and no
/// keyboard, mouse or gamepad input reaches it.
pub fn paused() -> bool {
    IS_PAUSED.load(Ordering::Acquire)
}

/// Called where the title presents a frame: while paused, it waits here (no lock of XCL's is
/// held at the call sites, so the window thread and its watchdog go on).
pub fn wait_while_paused() {
    if !paused() {
        return;
    }
    let mut p = PAUSE.lock().unwrap_or_else(|e| e.into_inner());
    while *p {
        p = RESUMED.wait(p).unwrap_or_else(|e| e.into_inner());
    }
}

fn set_paused(on: bool) {
    let mut p = PAUSE.lock().unwrap_or_else(|e| e.into_inner());
    *p = on;
    IS_PAUSED.store(on, Ordering::Release);
    RESUMED.notify_all();
}

struct Frames {
    last: Option<Instant>,
    shown: u64,
    intervals: Vec<f32>,
    since: Instant,
    history: VecDeque<f32>,
}

static FRAMES: Mutex<Option<Frames>> = Mutex::new(None);

/// A frame was presented (`present`): its time for `stats` and the overlay, and the first one
/// moves the state from `booting` to `running`.
pub fn note_present() {
    if !active() {
        return;
    }
    let now = Instant::now();
    {
        let mut g = FRAMES.lock().unwrap_or_else(|e| e.into_inner());
        let f = g.get_or_insert_with(|| Frames { last: None, shown: 0, intervals: Vec::new(), since: now, history: VecDeque::new() });
        if let Some(last) = f.last {
            let ms = now.duration_since(last).as_secs_f32() * 1000.0;
            f.intervals.push(ms);
            if f.history.len() == GRAPH_FRAMES {
                f.history.pop_front();
            }
            f.history.push_back(ms);
        }
        f.last = Some(now);
        f.shown += 1;
    }
    if STATE.compare_exchange(BOOTING, RUNNING, Ordering::AcqRel, Ordering::Acquire).is_ok() {
        send(state_line("running"));
    }
}

/// The overlay's rectangles for a `w`×`h` frame (none while it is off).
pub fn overlay_rects(w: u32, h: u32) -> Vec<(crate::progress::Rect, [f32; 4])> {
    if !OVERLAY.load(Ordering::Acquire) {
        return Vec::new();
    }
    let history: Vec<f32> = FRAMES.lock().unwrap_or_else(|e| e.into_inner()).as_ref().map_or(Vec::new(), |f| f.history.iter().copied().collect());
    graph_rects(w, h, &history)
}

fn take_stats() -> Stats {
    let now = Instant::now();
    let (shown, elapsed, intervals) = {
        let mut g = FRAMES.lock().unwrap_or_else(|e| e.into_inner());
        match g.as_mut() {
            Some(f) => {
                let taken = (f.shown, now.duration_since(f.since).as_secs_f64(), std::mem::take(&mut f.intervals));
                f.shown = 0;
                f.since = now;
                taken
            }
            None => (0, 1.0, Vec::new()),
        }
    };
    let (fps, frame_ms, worst_ms, stalls) = frame_stats(shown, elapsed, &intervals);
    let vram = crate::hle::d3d12x::real::video_memory_info().map(|[local, _]| (local.CurrentUsage >> 20, local.Budget >> 20));
    let (pipelines, _, compiling) = crate::hle::d3d12x::pso_async::counts();
    Stats {
        fps,
        frame_ms,
        ram_free_mb: Some(crate::hle::d3d12x::real::available_physical() >> 20).filter(|&mb| mb > 0),
        vram_used_mb: vram.map(|v| v.0),
        vram_budget_mb: vram.map(|v| v.1).filter(|&mb| mb > 0),
        stalls,
        worst_ms,
        pipelines: Some(pipelines),
        compiling: Some(compiling),
    }
}

// ---------------------------------------------------------------------------------------
// the pipe
// ---------------------------------------------------------------------------------------

use windows_sys::Win32::Foundation::{GetLastError, ERROR_FILE_NOT_FOUND, ERROR_IO_PENDING, ERROR_PIPE_BUSY, GENERIC_READ, GENERIC_WRITE, HANDLE, INVALID_HANDLE_VALUE};
use windows_sys::Win32::Storage::FileSystem::{CreateFileW, ReadFile, WriteFile, FILE_FLAG_OVERLAPPED, OPEN_EXISTING};
use windows_sys::Win32::System::Threading::CreateEventW;
use windows_sys::Win32::System::IO::{GetOverlappedResult, OVERLAPPED};

#[link(name = "kernel32")]
extern "system" {
    fn WaitNamedPipeW(name: *const u16, timeout_ms: u32) -> i32;
}

enum Out {
    Line(String),
    Flushed(SyncSender<()>),
}

struct Link {
    /// the pipe (a HANDLE)
    pipe: isize,
    lines: Sender<Out>,
}

static LINK: OnceLock<Option<Link>> = OnceLock::new();

/// The pipe, connected on first use (None: no `--control-pipe`, or no launcher answered).
fn link() -> Option<&'static Link> {
    LINK.get_or_init(connect).as_ref()
}

fn active() -> bool {
    LINK.get().is_some_and(Option::is_some)
}

fn send(line: String) {
    if let Some(l) = LINK.get().and_then(Option::as_ref) {
        let _ = l.lines.send(Out::Line(line));
    }
}

/// One overlapped read or write on `pipe`, waited for: the bytes moved (None: it failed).
unsafe fn io(pipe: HANDLE, event: HANDLE, op: impl FnOnce(*mut OVERLAPPED) -> i32) -> Option<u32> {
    let mut ov: OVERLAPPED = std::mem::zeroed();
    ov.hEvent = event;
    if op(&mut ov) == 0 && GetLastError() != ERROR_IO_PENDING {
        return None;
    }
    let mut n = 0u32;
    (GetOverlappedResult(pipe, &ov, &mut n, 1) != 0).then_some(n)
}

unsafe fn write_all(pipe: HANDLE, event: HANDLE, mut bytes: &[u8]) -> bool {
    while !bytes.is_empty() {
        match io(pipe, event, |ov| WriteFile(pipe, bytes.as_ptr(), bytes.len() as u32, std::ptr::null_mut(), ov)) {
            Some(n) if n > 0 => bytes = &bytes[n as usize..],
            _ => return false,
        }
    }
    true
}

fn connect() -> Option<Link> {
    let name = std::env::var(CONTROL_PIPE_ENV).ok().filter(|n| !n.trim().is_empty())?;
    let path: Vec<u16> = pipe_path(name.trim()).encode_utf16().chain(std::iter::once(0)).collect();
    let deadline = Instant::now() + Duration::from_secs(5);
    let pipe = loop {
        let h = unsafe { CreateFileW(path.as_ptr(), GENERIC_READ | GENERIC_WRITE, 0, std::ptr::null(), OPEN_EXISTING, FILE_FLAG_OVERLAPPED, std::ptr::null_mut()) };
        if h != INVALID_HANDLE_VALUE {
            break h as isize;
        }
        let e = unsafe { GetLastError() };
        if Instant::now() > deadline || (e != ERROR_PIPE_BUSY && e != ERROR_FILE_NOT_FOUND) {
            crate::xlog_warn!("control pipe {name}: cannot connect (error {e}) – no launcher control");
            return None;
        }
        if e == ERROR_PIPE_BUSY {
            unsafe { WaitNamedPipeW(path.as_ptr(), 500) };
        } else {
            std::thread::sleep(Duration::from_millis(100));
        }
    };
    let (lines, rx) = mpsc::channel();
    let boot = crate::state::boot();
    // (only the host process, which has the title's boot, has the game window)
    let hwnd = boot.and(crate::hosting::host_hwnd()).and_then(|_| crate::hle::d3d12x::present::window());
    let _ = lines.send(Out::Line(hello_line(std::process::id(), boot.map(|b| b.title.as_str()), boot.map(|b| b.config.device_type.as_str()), hwnd)));
    std::thread::Builder::new().name("xsemu-control-out".into()).spawn(move || writer(pipe, rx)).ok()?;
    crate::xlog_info!("control pipe {name}: connected");
    Some(Link { pipe, lines })
}

fn writer(pipe: isize, rx: Receiver<Out>) {
    let event = unsafe { CreateEventW(std::ptr::null(), 1, 0, std::ptr::null()) };
    for out in rx {
        match out {
            Out::Line(l) => {
                if !unsafe { write_all(pipe as HANDLE, event, l.as_bytes()) } {
                    crate::xlog_warn!("control pipe: a write failed (the launcher is gone?)");
                }
            }
            Out::Flushed(done) => {
                let _ = done.send(());
            }
        }
    }
}

fn reader(pipe: isize) {
    let event = unsafe { CreateEventW(std::ptr::null(), 1, 0, std::ptr::null()) };
    let mut buf = [0u8; 4096];
    let mut pending = Vec::new();
    loop {
        let read = unsafe { io(pipe as HANDLE, event, |ov| ReadFile(pipe as HANDLE, buf.as_mut_ptr(), buf.len() as u32, std::ptr::null_mut(), ov)) };
        let Some(n) = read else { break };
        pending.extend_from_slice(&buf[..n as usize]);
        while let Some(end) = pending.iter().position(|&b| b == b'\n') {
            let l: Vec<u8> = pending.drain(..=end).collect();
            if let Some(cmd) = parse_command(&String::from_utf8_lossy(&l)) {
                obey(cmd);
            }
        }
    }
    crate::hosting::stop_title("the launcher closed the control pipe");
}

fn obey(cmd: Command) {
    crate::xlog_info!("control pipe: {cmd:?}");
    match cmd {
        Command::Pause => {
            set_paused(true);
            crate::hle::kbm::release();
            STATE.store(PAUSED, Ordering::Release);
            send(state_line("paused"));
        }
        Command::Resume => {
            // the paused time is no frame time
            let ran = FRAMES.lock().unwrap_or_else(|e| e.into_inner()).as_mut().map(|f| f.last = None).is_some();
            let state = if ran { RUNNING } else { BOOTING };
            STATE.store(state, Ordering::Release);
            set_paused(false);
            send(state_line(state_name(state)));
        }
        Command::Stop => crate::hosting::stop_title("the launcher asked to stop"),
        Command::Overlay(on) => {
            let on = on.unwrap_or(!OVERLAY.load(Ordering::Acquire));
            OVERLAY.store(on, Ordering::Release);
        }
        Command::Focus => crate::hosting::focus(),
    }
}

/// The host process starts: connected to the launcher's pipe (`--control-pipe`), `hello` and
/// `state booting` sent, commands read and `stats` sent once a second from now on.
pub fn start() {
    let Some(l) = link() else { return };
    send(state_line(state_name(BOOTING)));
    let pipe = l.pipe;
    let _ = std::thread::Builder::new().name("xsemu-control-in".into()).spawn(move || reader(pipe));
    let _ = std::thread::Builder::new().name("xsemu-control-stats".into()).spawn(|| loop {
        std::thread::sleep(Duration::from_secs(1));
        match STATE.load(Ordering::Acquire) {
            STOPPING => return,
            PAUSED => continue,
            _ => send(stats_line(&take_stats())),
        }
    });
}

/// A launcher shortcut (F1, F3, F11, Escape) pressed in the game window.
pub fn key(name: &str) {
    send(key_line(name));
}

/// The last lines before a deliberate end: `state stopping` and `exit` with `reason`, written
/// before this returns (up to a second); only the first call of the process says anything.
pub fn exit(code: u32, reason: &str, message: &str) {
    if EXITED.swap(true, Ordering::AcqRel) {
        return;
    }
    let Some(l) = link() else { return };
    STATE.store(STOPPING, Ordering::Release);
    let _ = l.lines.send(Out::Line(state_line(state_name(STOPPING))));
    let _ = l.lines.send(Out::Line(exit_line(code, reason, message)));
    let (done, flushed) = mpsc::sync_channel(1);
    let _ = l.lines.send(Out::Flushed(done));
    let _ = flushed.recv_timeout(Duration::from_secs(1));
}

/// `exit` written by this thread itself, for where the writer thread may be gone or stuck: the
/// crash handler and the process ending (ExitProcess has ended every other thread). The line
/// starts with a line break, so a line the writer had under way ends there.
pub fn exit_now(code: u32, reason: &str, message: &str) {
    if EXITED.swap(true, Ordering::AcqRel) {
        return;
    }
    let Some(l) = LINK.get().and_then(Option::as_ref) else { return };
    let text = format!("\n{}", exit_line(code, reason, message));
    unsafe {
        let event = CreateEventW(std::ptr::null(), 1, 0, std::ptr::null());
        write_all(l.pipe as HANDLE, event, text.as_bytes());
        windows_sys::Win32::Foundation::CloseHandle(event);
    }
}

/// The process is ending with `status` (`eh`'s NtTerminateProcess hook).
pub fn note_exit_status(status: u32) {
    EXIT_STATUS.store(status, Ordering::Release);
}

/// The title ended the process (ExitProcess, its DLL_PROCESS_DETACH): `exit` `title_exit`.
pub fn title_exited() {
    exit_now(EXIT_STATUS.load(Ordering::Acquire), "title_exit", "");
}

#[cfg(test)]
mod tests {
    use super::*;

    fn parse(l: &str) -> serde_json::Value {
        assert!(l.ends_with('\n') && l.matches('\n').count() == 1, "{l:?}");
        serde_json::from_str(l).unwrap()
    }

    #[test]
    fn commands_are_parsed_and_unknown_ones_ignored() {
        assert_eq!(parse_command("{\"cmd\":\"pause\"}\n"), Some(Command::Pause));
        assert_eq!(parse_command("{\"cmd\":\"resume\",\"extra\":1}"), Some(Command::Resume));
        assert_eq!(parse_command("{\"cmd\":\"stop\"}\r\n"), Some(Command::Stop));
        assert_eq!(parse_command("{\"cmd\":\"overlay\",\"on\":true}"), Some(Command::Overlay(Some(true))));
        assert_eq!(parse_command("{\"cmd\":\"overlay\",\"on\":false}"), Some(Command::Overlay(Some(false))));
        assert_eq!(parse_command("{\"cmd\":\"overlay\"}"), Some(Command::Overlay(None)));
        assert_eq!(parse_command("{\"cmd\":\"focus\"}"), Some(Command::Focus));
        assert_eq!(parse_command("{\"cmd\":\"dance\"}"), None);
        assert_eq!(parse_command("{\"ev\":\"pause\"}"), None);
        assert_eq!(parse_command("not json"), None);
        assert_eq!(parse_command(""), None);
    }

    #[test]
    fn emulator_messages_are_single_json_lines() {
        let h = parse(&hello_line(4242, Some("Some.Title"), Some("lockhart"), Some(0x1a2b3c)));
        assert_eq!(h["ev"], "hello");
        assert_eq!(h["proto"], 1);
        assert_eq!(h["pid"], 4242);
        assert_eq!(h["version"], env!("CARGO_PKG_VERSION"));
        assert_eq!(h["title"], "Some.Title");
        assert_eq!(h["device_type"], "lockhart");
        assert_eq!(h["hwnd"], "1a2b3c");
        let h = parse(&hello_line(1, None, None, None));
        assert!(h.get("hwnd").is_none() && h.get("title").is_none() && h.get("device_type").is_none());
        let s = parse(&state_line("booting"));
        assert_eq!((s["ev"].as_str(), s["state"].as_str()), (Some("state"), Some("booting")));
        let k = parse(&key_line("F11"));
        assert_eq!((k["ev"].as_str(), k["key"].as_str()), (Some("key"), Some("F11")));
        let e = parse(&exit_line(0xC000_0005, "crash", "exception \"quoted\"\nline two"));
        assert_eq!(e["ev"], "exit");
        assert_eq!(e["code"], 0xC000_0005u32);
        assert_eq!(e["reason"], "crash");
        assert_eq!(e["message"], "exception \"quoted\"\nline two");
    }

    #[test]
    fn stats_leave_out_what_was_not_measured() {
        let s = parse(&stats_line(&Stats { fps: 58.2, frame_ms: Some(17.2), ram_free_mb: Some(9830), stalls: 0, ..Default::default() }));
        assert_eq!(s["ev"], "stats");
        assert_eq!(s["fps"], 58.2);
        assert_eq!(s["frame_ms"], 17.2);
        assert_eq!(s["ram_free_mb"], 9830);
        assert_eq!(s["stalls"], 0);
        for absent in ["vram_used_mb", "vram_budget_mb", "worst_ms", "pipelines", "compiling"] {
            assert!(s.get(absent).is_none(), "{absent}");
        }
    }

    #[test]
    fn frame_stats_from_present_intervals() {
        assert_eq!(frame_stats(0, 1.0, &[]), (0.0, None, None, 0));
        let (fps, mean, worst, stalls) = frame_stats(60, 1.0, &[16.0, 17.0, 18.0]);
        assert_eq!(fps, 60.0);
        assert_eq!(mean, Some(17.0));
        assert_eq!(worst, Some(18.0));
        assert_eq!(stalls, 0);
        let (fps, _, worst, stalls) = frame_stats(3, 2.0, &[16.0, 150.0, 400.0]);
        assert_eq!(fps, 1.5);
        assert_eq!(worst, Some(400.0));
        assert_eq!(stalls, 2);
    }

    #[test]
    fn overlay_graph_fits_the_frame() {
        let history: Vec<f32> = (0..200).map(|k| (k % 60) as f32).collect();
        let rects = graph_rects(1920, 1080, &history);
        // panel, 120 bars, the 60 fps line
        assert_eq!(rects.len(), 1 + GRAPH_FRAMES + 1);
        let panel = rects[0].0;
        for (r, _) in &rects {
            assert!(r[0] >= panel[0] && r[2] <= panel[2] && r[1] >= panel[1] && r[3] <= panel[3] && r[0] < r[2] && r[1] < r[3], "{r:?} outside {panel:?}");
        }
        // the newest frame is the rightmost bar
        assert_eq!(rects[1].0[2], panel[2]);
        assert!(graph_rects(1920, 1080, &[]).len() == 2);
        assert!(graph_rects(100, 20, &history).is_empty());
    }

    #[test]
    fn pipe_names_become_pipe_paths() {
        assert_eq!(pipe_path("xsemu-4242-1"), r"\\.\pipe\xsemu-4242-1");
        assert_eq!(pipe_path(r"\\.\pipe\x"), r"\\.\pipe\x");
    }
}
