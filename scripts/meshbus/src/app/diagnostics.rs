// SPDX-License-Identifier: Apache-2.0
use super::{device, inputs, session};
use crate::{elf::Elf, host};
use anyhow::{Context, Result, ensure};
use serde_json::{Value, json};
use std::{fs, path::Path, process::Command};

fn last_run(root: &Path) -> Result<Value> {
    inputs::load(&root.join(".meshbus-last-run.json")).context("no recorded run; use app run first")
}
pub fn capture(root: &Path, selector: Option<&str>, output: &Path) -> Result<()> {
    ensure!(
        output.extension().is_some_and(|e| e == "png"),
        "capture output must use .png extension"
    );
    ensure!(
        !output.exists() && !output.with_extension("json").exists(),
        "capture output already exists"
    );
    let run = last_run(root)?;
    let id = run["session"]["session_id"]
        .as_u64()
        .context("run has no Session identity")?;
    let (mut client, binding) = device::open(root, selector, None)?;
    ensure!(
        serde_json::to_value(&binding.host)? == run["device"]["host"],
        "device/firmware differs from recorded run"
    );
    let state = session::status(&mut client, id)?;
    ensure!(
        state.state == "desktop_mba_state_running" && state.app_id == run["app_id"],
        "recorded Session is not running"
    );
    let installed = super::install::command(&mut client, "status", &state.app_id, "")?;
    ensure!(
        installed.current_bundle == run["bundle_sha256"] && installed.pending_bundle.is_empty(),
        "installed build differs from recorded run"
    );
    let mut data = Vec::new();
    let mut snapshot = 0;
    let mut first = Value::Null;
    loop {
        let part = client.command(&format!("display dump {snapshot} {} 256", data.len()))?;
        if snapshot == 0 {
            snapshot = part["snapshot_id"]
                .as_u64()
                .context("missing frame identity")?;
            first = part.clone();
        }
        ensure!(
            snapshot != 0 && part["snapshot_id"] == snapshot && part["offset"] == data.len(),
            "frame changed during capture"
        );
        for key in [
            "width",
            "height",
            "total_size",
            "format",
            "inverted",
            "orientation",
        ] {
            ensure!(part[key] == first[key], "frame metadata changed");
        }
        let bytes = host::unhex(part["data"].as_str().context("missing frame bytes")?)?;
        ensure!(
            !bytes.is_empty() && data.len() + bytes.len() <= 65536,
            "invalid frame size/progress"
        );
        data.extend(bytes);
        let total = part["total_size"]
            .as_u64()
            .context("missing frame length")? as usize;
        ensure!(data.len() <= total, "frame exceeds declared length");
        if data.len() == total {
            break;
        }
    }
    let after = session::status(&mut client, id)?;
    ensure!(
        after.state == "desktop_mba_state_running" && after.session_id == state.session_id,
        "Session ended during capture"
    );
    let width = first["width"].as_u64().context("missing width")? as usize;
    let height = first["height"].as_u64().context("missing height")? as usize;
    ensure!(
        width > 0
            && height > 0
            && width <= 1024
            && height <= 1024
            && data.len() == width * height.div_ceil(8)
            && first["format"] == "display_dump_format_ssd1306_page",
        "unsupported frame format/dimensions"
    );
    let inverted = first["inverted"].as_bool().context("missing polarity")?;
    let pixels: Vec<_> = (0..height)
        .flat_map(|y| (0..width).map(move |x| (x, y)))
        .map(|(x, y)| {
            if ((data[y / 8 * width + x] >> (y % 8)) & 1 != 0) ^ inverted {
                255
            } else {
                0
            }
        })
        .collect();
    let mut encoder = png::Encoder::new(fs::File::create(output)?, width as u32, height as u32);
    encoder.set_color(png::ColorType::Grayscale);
    encoder.set_depth(png::BitDepth::Eight);
    encoder.write_header()?.write_image_data(&pixels)?;
    inputs::write_json(
        &output.with_extension("json"),
        &json!({"schema":1,"run":run,"session":after,
        "frame":first,"png_sha256":host::hash(&fs::read(output)?),
        "evidence":"last complete software frame; not physical panel readback"}),
    )?;
    host::print(&json!({"capture":output,"metadata":output.with_extension("json")}))
}
pub fn diagnose(root: &Path, section: Option<&str>, offset: Option<u64>) -> Result<()> {
    let run = last_run(root)?;
    let result = || -> Result<Value> {
        let mba = std::path::PathBuf::from(run["mba"].as_str().context("missing MBA path")?);
        let symbols: Value = inputs::load(&mba.with_extension("symbols.json"))?;
        let elf_path = mba.with_extension("symbols.elf");
        let bytes = host::read(&elf_path, 64 * 1024 * 1024)?;
        ensure!(
            symbols["mba_sha256"] == run["mba_sha256"]
                && host::hash(&host::read(&mba, 64 * 1024 * 1024)?) == run["mba_sha256"],
            "MBA digest differs from recorded run"
        );
        ensure!(
            symbols["symbols_sha256"] == host::hash(&bytes),
            "symbol digest mismatch"
        );
        let elf = Elf::parse(&bytes)?;
        ensure!(
            elf.sections.iter().any(|s| s.name == ".debug_info"),
            "matching ELF has no source debug information"
        );
        let section = section.context("no trusted runtime address map; provide --section and a section-relative --offset from matching symbols")?;
        let offset = offset.context("missing section-relative offset")?;
        let info = elf
            .sections
            .iter()
            .find(|s| s.name == section)
            .context("unknown ELF section")?;
        ensure!(
            info.flags & 4 != 0 && offset < info.size,
            "offset is outside executable section"
        );
        let executable = symbols["addr2line"]
            .as_str()
            .context("missing local addr2line path")?;
        let source = host::output(Command::new(executable).args(["-e"]).arg(&elf_path).args([
            "-j",
            section,
            "-f",
            "-C",
            &format!("0x{offset:x}"),
        ]))?;
        ensure!(
            !source.contains("??"),
            "matching symbols cannot map this offset to source"
        );
        Ok(
            json!({"source":source,"section":section,"offset":offset,"address_basis":"explicit section-relative address; no inferred runtime load address"}),
        )
    }();
    match result {
        Ok(mapping) => host::print(&json!({"run":run,"mapping":mapping})),
        Err(error) => {
            host::print(&json!({"run":run,"mapping":"unknown","reason":format!("{error:#}")}))?;
            Err(error)
        }
    }
}
