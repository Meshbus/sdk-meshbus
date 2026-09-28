// SPDX-License-Identifier: Apache-2.0
use super::{BuildArgs, Template};
use crate::{host, llext};
use anyhow::{Context, Result, ensure};
use serde_json::{Value, json};
use std::{
    fs,
    path::{Path, PathBuf},
};

pub fn metadata(input: &Value) -> Result<Value> {
    ensure!(input["project-schema"] == 1, "unsupported project-schema");
    let object = input
        .as_object()
        .context("project declaration must be a mapping")?;
    let allowed = [
        "project-schema",
        "id",
        "name",
        "version",
        "entry-point",
        "stack-size",
        "build",
        "dependencies",
        "requires",
    ];
    for key in object.keys() {
        ensure!(
            allowed.contains(&key.as_str()),
            "unknown project field: {key}"
        );
    }
    ensure!(
        matches!(
            input["build"]["template"].as_str(),
            Some("native" | "arduboy")
        ),
        "unsupported project template"
    );
    ensure!(
        input["build"].as_object().is_some_and(|m| m.len() == 1),
        "build supports only template; edit the generated CMake project for sources and flags"
    );
    let dependencies = input["dependencies"]
        .as_object()
        .context("dependencies must be a mapping")?;
    for (name, value) in dependencies {
        ensure!(
            name == "sdk-arduboy" && input["build"]["template"] == "arduboy",
            "unsupported dependency: {name}"
        );
        let sha = value
            .as_str()
            .context("sdk-arduboy dependency must be a SHA256 content identity")?;
        ensure!(
            sha.len() == 64 && sha.bytes().all(|b| b.is_ascii_hexdigit()),
            "invalid SDK SHA256"
        );
    }
    let requires = input["requires"]
        .as_array()
        .context("requires must be an array")?;
    let symbol_pattern = regex::Regex::new(r"^[A-Za-z_][A-Za-z0-9_]*$")?;
    for item in requires {
        let symbol = item
            .as_str()
            .and_then(|s| s.strip_prefix("symbol:"))
            .context("requires entries must use symbol:<exported-host-symbol>")?;
        ensure!(
            symbol_pattern.is_match(symbol),
            "invalid required host symbol"
        );
    }
    let mut result = serde_json::Map::new();
    for key in ["id", "name", "version", "entry-point", "stack-size"] {
        if let Some(value) = object.get(key) {
            result.insert(key.into(), value.clone());
        }
    }
    Ok(Value::Object(result))
}
pub fn read(root: &Path) -> Result<Value> {
    let value = serde_yaml_ng::from_slice(&host::read(&root.join("llext.yaml"), 65536)?)?;
    metadata(&value)?;
    Ok(value)
}
pub fn create(directory: &Path, template: Template) -> Result<()> {
    ensure!(
        !directory.exists(),
        "project directory already exists; no files changed"
    );
    let name = directory
        .file_name()
        .and_then(|s| s.to_str())
        .context("invalid project directory")?;
    ensure!(
        regex::Regex::new(r"^[a-z][a-z0-9_.-]{0,30}$")?.is_match(name),
        "project name must be a valid lowercase MBA id"
    );
    let parent = directory
        .parent()
        .filter(|p| !p.as_os_str().is_empty())
        .unwrap_or(Path::new("."));
    ensure!(parent.is_dir(), "project parent directory does not exist");
    let temp = tempfile::tempdir_in(parent)?;
    fs::create_dir(temp.path().join("src"))?;
    let kind = match template {
        Template::Native => "native",
        Template::Arduboy => "arduboy",
    };
    let declaration = json!({"project-schema":1,"id":name,"name":name,"version":"0.1.0","entry-point":"app_main","stack-size":4096,"build":{"template":kind},"dependencies":{},"requires":[]});
    fs::write(
        temp.path().join("llext.yaml"),
        serde_yaml_ng::to_string(&declaration)?,
    )?;
    fs::write(
        temp.path().join("toolchain.cmake"),
        "set(CMAKE_SYSTEM_NAME Generic)\nset(CMAKE_C_COMPILER arm-zephyr-eabi-gcc)\nset(CMAKE_CXX_COMPILER arm-zephyr-eabi-g++)\nset(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)\n",
    )?;
    let cmake = match template {
        Template::Native => include_str!("native.cmake"),
        Template::Arduboy => include_str!("arduboy.cmake"),
    };
    fs::write(
        temp.path().join("CMakeLists.txt"),
        cmake.replace("@APP_ID@", name),
    )?;
    match template {
        Template::Native => {
            fs::write(
                temp.path().join("src/main.c"),
                "#include <zephyr/llext/symbol.h>\n#include <zephyr/sys/printk.h>\nvoid app_main(void *args) { (void)args; printk(\"Hello from MBA\\n\"); }\nLL_EXTENSION_SYMBOL(app_main);\n",
            )?;
        }
        Template::Arduboy => {
            fs::create_dir(temp.path().join("src/sketch"))?;
            fs::write(temp.path().join("src/main.cpp"), "#include <zephyr/llext/symbol.h>\n#include <meshbus_arduboy/runtime.hpp>\nvoid setup(); void loop();\nextern \"C\" void app_main(void *args) { meshbus::arduboy::SketchConfig config{\"@APP_ID@\", setup, loop}; (void)meshbus::arduboy::run_sketch(args, config); }\nLL_EXTENSION_SYMBOL(app_main);\n".replace("@APP_ID@",name))?;
            fs::write(
                temp.path().join("src/sketch/game.ino"),
                "#include <Arduboy2.h>\nArduboy2 game;\nvoid setup() { game.begin(); }\nvoid loop() { game.clear(); game.setCursor(0, 0); game.print(\"Hello Arduboy\"); game.display(); }\n",
            )?;
        }
    }
    fs::write(
        temp.path().join(".gitignore"),
        "build/\n.meshbus-target.json\n.meshbus-device.json\n.meshbus-runs/\n.meshbus-last-run.json\n",
    )?;
    // Do not replace an existing empty directory created by another process.
    fs::create_dir(directory).context("project directory was created concurrently")?;
    for entry in fs::read_dir(temp.path())? {
        let entry = entry?;
        fs::rename(entry.path(), directory.join(entry.file_name()))?;
    }
    host::print(&json!({"project":directory,"template":kind}))
}
pub fn build(root: &Path, args: &BuildArgs) -> Result<PathBuf> {
    let input = read(root)?;
    let local = args.edk.is_some() || args.toolchain.is_some();
    ensure!(
        !local || !args.locked,
        "--locked cannot bypass the lock with explicit EDK/toolchain"
    );
    let lock = if local {
        None
    } else {
        let lock = super::inputs::read_lock(root)?;
        super::inputs::verify(&lock)?;
        Some(lock)
    };
    let edk = args
        .edk
        .clone()
        .or_else(|| lock.as_ref().map(|l| l.release.edk.path.clone()))
        .context("explicit build requires --edk")?;
    let toolchain = args
        .toolchain
        .clone()
        .or_else(|| lock.as_ref().map(|l| l.release.toolchain.path.clone()))
        .context("explicit build requires --toolchain")?;
    let mut definitions = Vec::new();
    let sdk = args.sdk.clone().or_else(|| {
        lock.as_ref()
            .and_then(|l| l.release.sdk.as_ref().map(|a| a.path.clone()))
    });
    let mut override_identity = Value::Null;
    check_requirements(&input, &edk, sdk.as_deref(), args.sdk.is_some())?;
    super::inputs::check_compiler(&edk, &toolchain)?;
    if input["build"]["template"] == "arduboy" {
        let sdk = sdk.unwrap().canonicalize()?;
        definitions.push(format!("-DMESHBUS_ARDUBOY_SDK_DIR={}", sdk.display()));
        override_identity = json!({"path":sdk,"sha256":super::inputs::digest(&sdk)?,"local_override":args.sdk.is_some()});
    }
    let tools = lock.as_ref().map(|l| llext::BuildTools {
        cmake: l.release.tools["cmake"].path.clone(),
        ninja: l.release.tools["ninja"].path.clone(),
        python: l.release.tools["python"].path.clone(),
    });
    let package = llext::build(&llext::BuildRequest {
        tools,
        build_dir: None,
        output_dir: Some(root.join("build")),
        llext_sdk: Some(edk),
        zephyr_sdk: Some(toolchain),
        force_edk: false,
        source_dir: root.into(),
        cmake_args: definitions,
    })?;
    let report_path = package.with_extension("build.json");
    let mut report: Value = super::inputs::load(&report_path)?;
    report["project_inputs"] =
        json!({"locked":lock.is_some(),"lock":lock,"sdk":override_identity,"network":"disabled"});
    super::inputs::write_json(&report_path, &report)?;
    Ok(package)
}

/// Shared preflight for doctor and actual builds.
pub fn check_requirements(
    input: &Value,
    edk: &Path,
    sdk: Option<&Path>,
    override_sdk: bool,
) -> Result<()> {
    stack_budget(input, edk)?;
    let manifest = super::inputs::edk_manifest(edk)?;
    let exports = crate::edk::named_exports(&manifest)?;
    for requirement in input["requires"].as_array().unwrap() {
        let symbol = requirement
            .as_str()
            .unwrap()
            .strip_prefix("symbol:")
            .unwrap();
        ensure!(
            exports.contains(symbol),
            "selected EDK does not export required host symbol: {symbol}"
        );
    }
    if input["build"]["template"] == "arduboy" {
        let sdk =
            sdk.context("Arduboy project requires an SDK; select a source containing sdk-arduboy")?;
        ensure!(
            sdk.join("meshbus_arduboy_llext.cmake").is_file(),
            "SDK build entry is missing"
        );
        if let Some(expected) = input["dependencies"]["sdk-arduboy"].as_str() {
            ensure!(
                override_sdk || super::inputs::digest(sdk)? == expected,
                "SDK content does not match project dependency; explicitly update or use --sdk for development"
            );
        }
    }
    Ok(())
}

pub fn stack_budget(input: &Value, edk: &Path) -> Result<Value> {
    let temp = tempfile::tempdir()?;
    let root = if edk.is_dir() {
        edk.to_path_buf()
    } else {
        crate::archive::extract_edk(edk, temp.path())?
    };
    let config_path = root.join("include/zephyr/include/generated/zephyr/autoconf.h");
    let requested = input["stack-size"].as_u64().context("invalid stack-size")?;
    if !config_path.exists() {
        return Ok(
            json!({"requested":requested,"maximum":"unknown: EDK lacks host configuration"}),
        );
    }
    let config = crate::edk::config(&config_path)?;
    let maximum = config.get("CONFIG_MBS_DESKTOP_APP_SHARED_STACK_SIZE");
    if let Some(maximum) = maximum {
        let maximum: u64 = maximum.parse()?;
        ensure!(
            requested > 0 && requested <= maximum,
            "stack-size {requested} exceeds host app stack budget {maximum}"
        );
        return Ok(json!({"requested":requested,"maximum":maximum}));
    }
    Ok(json!({"requested":requested,"maximum":"unknown: host stack configuration unavailable"}))
}
