// SPDX-License-Identifier: Apache-2.0
//! Sequential build/deploy and observation of a bound managed Session.
use super::{BuildArgs, device, inputs, install, project, session};
use crate::commands::connect::DeviceClient;
use anyhow::{Context, Result, ensure};
use serde_json::json;
use std::{
    path::{Path, PathBuf},
    sync::atomic::{AtomicBool, Ordering},
    time::{Duration, Instant},
};

static INTERRUPTED: AtomicBool = AtomicBool::new(false);
// The initial CLI host is macOS. The handler only stores a lock-free atomic;
// normal Rust code owns cleanup, reporting and serial I/O outside signal context.
#[cfg(unix)]
unsafe extern "C" {
    fn signal(number: i32, handler: usize) -> usize;
}
#[cfg(unix)]
extern "C" fn interrupt(_: i32) {
    INTERRUPTED.store(true, Ordering::Relaxed);
}
pub struct InterruptGuard {
    #[cfg(unix)]
    previous: usize,
}
impl InterruptGuard {
    pub fn install() -> Result<Self> {
        INTERRUPTED.store(false, Ordering::Relaxed);
        #[cfg(unix)]
        {
            // SAFETY: SIGINT is 2 on the supported Unix hosts; signature matches signal().
            let previous = unsafe { signal(2, interrupt as *const () as usize) };
            ensure!(previous != usize::MAX, "cannot install interrupt handler");
            Ok(Self { previous })
        }
        #[cfg(not(unix))]
        {
            anyhow::bail!("interactive app workflows currently require a Unix host")
        }
    }
    pub fn cancelled(&self) -> bool {
        INTERRUPTED.load(Ordering::Relaxed)
    }
}
impl Drop for InterruptGuard {
    fn drop(&mut self) {
        #[cfg(unix)]
        // SAFETY: restore the process handler returned by signal(), after UART cleanup.
        unsafe {
            signal(2, self.previous);
        }
    }
}
pub fn deploy(
    root: &Path,
    build: &BuildArgs,
    selector: Option<&str>,
    log: Option<PathBuf>,
    atomic: bool,
) -> Result<(DeviceClient, PathBuf, session::Status)> {
    eprintln!("app run: build");
    let mba = project::build(root, build).context("build failed; the device was not changed")?;
    ensure!(
        !INTERRUPTED.load(Ordering::Relaxed),
        "interrupted after build; device was not changed"
    );
    let manifest = mba.with_extension("install").join("package.json");
    let bundle = super::bundle::load(&manifest)?;
    let directory = root
        .join(".meshbus-runs")
        .join(uuid::Uuid::new_v4().to_string());
    std::fs::create_dir_all(&directory)?;
    // Keep the running build diagnosable even if a later build/deploy fails.
    let archived_mba = directory.join(mba.file_name().context("MBA filename missing")?);
    for extension in ["mba", "symbols.elf", "symbols.json"] {
        std::fs::copy(
            mba.with_extension(extension),
            archived_mba.with_extension(extension),
        )?;
    }
    let log = log.unwrap_or_else(|| directory.join("device.log"));
    if let Some(parent) = log.parent() {
        std::fs::create_dir_all(parent)?;
    }
    let (mut client, binding) = device::open(root, selector, Some(log.clone()))?;
    session::check_host(root, &binding.host)?;
    // Preserve a binding even when the project originally selected its EDK offline.
    inputs::write_json(&root.join(".meshbus-device.json"), &binding)?;
    client.follow_logs()?;
    let record = directory.join("session.json");
    let mut evidence = json!({"schema":1,"device":binding,"app_id":bundle.manifest.id,
        "bundle_sha256":bundle.sha256,"mba_sha256":bundle.manifest.mba.sha256,
        "mba":archived_mba.canonicalize()?,"log":log.canonicalize()?,"session":null,
        "log_scope":"shared firmware stream during this operation; deferred lines can predate the Session; state comes only from management responses"});
    inputs::write_json(&record, &evidence)?;
    eprintln!("app run: install");
    let installed = install::install(&mut client, &binding.host, &manifest, true, atomic)
        .context("install failed; inspect app installed and use app recover")?;
    ensure!(
        !INTERRUPTED.load(Ordering::Relaxed),
        "interrupted after install; package committed, app was not started"
    );
    eprintln!("app run: start");
    let started = session::start(&mut client, &bundle.manifest.id, &installed.mba_path)
        .context("start result unknown; query app status before retrying")?;
    evidence["session"] = serde_json::to_value(&started)?;
    inputs::write_json(&record, &evidence)?;
    inputs::write_json(&root.join(".meshbus-last-run.json"), &evidence)?;
    session::print_result(&started)?;
    eprintln!("Session evidence: {}", record.display());
    Ok((client, record, started))
}
pub fn follow(
    client: &mut DeviceClient,
    id: u64,
    seconds: Option<u64>,
    guard: &InterruptGuard,
) -> Result<()> {
    let started = Instant::now();
    while !guard.cancelled() && seconds.is_none_or(|s| started.elapsed() < Duration::from_secs(s)) {
        std::thread::sleep(Duration::from_millis(250));
        if started.elapsed().as_millis() % 1000 < 250 {
            let current = session::status(client, id).context(
                "device disconnected; Session state unknown; reconnect and query app status",
            )?;
            if current.resources_reclaimed {
                return session::print_result(&current);
            }
        }
    }
    let last = session::status(client, id)
        .context("final device state unknown; query app status after reconnecting")?;
    eprintln!("Observation ended; app retained on device.");
    session::print_result(&last)
}

#[derive(Default)]
struct Changes {
    observed: String,
    attempted: Option<String>,
    since_ms: u64,
}
impl Changes {
    fn observe(&mut self, fingerprint: String, now_ms: u64) {
        if self.observed != fingerprint {
            self.observed = fingerprint;
            self.since_ms = now_ms;
        }
    }
    fn take_ready(&mut self, now_ms: u64) -> bool {
        if now_ms.saturating_sub(self.since_ms) < 500
            || self.attempted.as_ref() == Some(&self.observed)
        {
            return false;
        }
        // Failure consumes this event too: no repeated failed builds or installs.
        self.attempted = Some(self.observed.clone());
        true
    }
}
fn source_fingerprint(root: &Path) -> Result<String> {
    fn visit(root: &Path, path: &Path, entries: &mut Vec<(PathBuf, String)>) -> Result<()> {
        for entry in std::fs::read_dir(path)? {
            let entry = entry?;
            let name = entry.file_name();
            let name = name.to_string_lossy();
            if name == "build" || name == ".git" || name.starts_with(".meshbus-") {
                continue;
            }
            let kind = entry.file_type()?;
            if kind.is_symlink() {
                continue;
            }
            if kind.is_dir() {
                visit(root, &entry.path(), entries)?;
            } else if kind.is_file() {
                entries.push((
                    entry.path().strip_prefix(root)?.into(),
                    crate::host::hash(&crate::host::read(&entry.path(), 64 * 1024 * 1024)?),
                ));
            }
        }
        Ok(())
    }
    let mut entries = Vec::new();
    visit(root, root, &mut entries)?;
    entries.sort();
    Ok(crate::host::hash(&serde_json::to_vec(&entries)?))
}
pub fn watch(root: &Path, build: &BuildArgs, selector: Option<&str>, atomic: bool) -> Result<()> {
    let guard = InterruptGuard::install()?;
    // Bind before the first build; every later reconnect checks this hardware ID.
    let (client, binding) = device::open(root, selector, None)?;
    inputs::write_json(&root.join(".meshbus-device.json"), &binding)?;
    drop(client);
    let origin = Instant::now();
    let mut changes = Changes::default();
    let mut last: Option<session::Status> = None;
    let mut connection: Option<DeviceClient> = None;
    let mut last_poll = Instant::now();
    while !guard.cancelled() {
        let now = origin.elapsed().as_millis() as u64;
        changes.observe(source_fingerprint(root)?, now);
        if changes.take_ready(now) {
            // Close observation before the sole deploy operation opens this UART.
            drop(connection.take());
            match deploy(root, build, None, None, atomic) {
                Ok((client, _, state)) => {
                    connection = Some(client);
                    last = Some(state);
                }
                Err(error) => eprintln!(
                    "watch paused for this revision: {error:#}; fix sources or reconnect the bound device and save a file to retry"
                ),
            }
        }
        if last_poll.elapsed() >= Duration::from_secs(1) {
            last_poll = Instant::now();
            if let (Some(client), Some(state)) = (connection.as_mut(), last.as_ref()) {
                if let Err(error) = session::status(client, state.session_id) {
                    eprintln!(
                        "watch connection lost; device state unknown: {error:#}; reconnect the bound device and save a file to retry"
                    );
                    drop(connection.take());
                }
            }
        }
        std::thread::sleep(Duration::from_millis(100));
    }
    drop(connection);
    let final_state = device::open(root, None, None).and_then(|(mut client, _)| {
        session::status(&mut client, last.as_ref().map_or(0, |s| s.session_id))
    });
    match final_state {
        Ok(state) => session::print_result(&state),
        Err(error) => {
            crate::host::print(
                &json!({"state":"unknown","last_confirmed":last,"error":format!("{error:#}")}),
            )?;
            Ok(())
        }
    }
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn watch_coalesces_events_and_consumes_failed_attempts() {
        let mut changes = Changes::default();
        changes.observe("one".into(), 0);
        assert!(!changes.take_ready(400));
        changes.observe("two".into(), 450);
        changes.observe("three".into(), 600);
        assert!(!changes.take_ready(1000));
        assert!(changes.take_ready(1100));
        // A compiler/transport failure does not trigger a busy retry loop.
        assert!(!changes.take_ready(5000));
        // A change arriving during the previous synchronous build gets one later turn.
        changes.observe("four".into(), 6000);
        assert!(changes.take_ready(6500));
        assert!(!changes.take_ready(7000));
    }
}
