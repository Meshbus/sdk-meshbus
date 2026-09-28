mod support;
// SPDX-License-Identifier: Apache-2.0
use std::{fs, process::Command};
fn cli(args: &[&str]) -> std::process::Output {
    Command::new(env!("CARGO_BIN_EXE_meshbus"))
        .args(args)
        .output()
        .unwrap()
}
#[test]
fn create_project_without_overwriting_user_files() {
    let root = tempfile::tempdir().unwrap();
    let project = root.path().join("hello");
    let args = [
        "app",
        "new",
        project.to_str().unwrap(),
        "--template",
        "native",
    ];
    let result = cli(&args);
    assert!(
        result.status.success(),
        "{}",
        String::from_utf8_lossy(&result.stderr)
    );
    assert!(project.join("CMakeLists.txt").is_file());
    assert!(project.join("src/main.c").is_file());
    let yaml = fs::read(project.join("llext.yaml")).unwrap();
    assert!(!cli(&args).status.success());
    assert_eq!(fs::read(project.join("llext.yaml")).unwrap(), yaml);
}

#[test]
fn rejects_unimplemented_project_settings_before_building() {
    for (field, extra) in [
        ("build", "build: {template: native, typo: yes}"),
        ("dependencies", "dependencies: {unknown: '1.0'}"),
        ("requires", "requires: [unknown-capability]"),
    ] {
        let root = tempfile::tempdir().unwrap();
        let baseline = "project-schema: 1\nid: hello\nname: hello\nversion: 1.0.0\nbuild: {template: native}\ndependencies: {}\nrequires: []\n";
        let mut lines: Vec<_> = baseline
            .lines()
            .filter(|line| !line.starts_with(&format!("{field}:")))
            .collect();
        lines.push(extra);
        fs::write(root.path().join("llext.yaml"), lines.join("\n")).unwrap();
        let result = cli(&["app", "--project", root.path().to_str().unwrap(), "build"]);
        assert!(!result.status.success());
        let error = String::from_utf8_lossy(&result.stderr);
        assert!(!error.contains("not synchronized"), "{error}");
        assert!(!root.path().join("build").exists());
    }
}

#[test]
fn local_lock_restore_doctor_and_integrity_boundaries() {
    use meshbus_cli::{edk, host};
    use serde_json::{Value, json};
    let temp = tempfile::tempdir().unwrap();
    let root = temp.path().join("hello");
    assert!(
        cli(&["app", "new", root.to_str().unwrap()])
            .status
            .success()
    );
    let edk_path = temp.path().join("edk");
    support::public_headers(&edk_path);
    fs::create_dir_all(edk_path.join("include/build/generated")).unwrap();
    fs::write(
        edk_path.join("include/build/generated/config.h"),
        "#define HOST 1\n",
    )
    .unwrap();
    fs::write(edk_path.join("cmake.cflags"), "set(LLEXT_CFLAGS)\n").unwrap();
    fs::write(edk_path.join("Makefile.cflags"), "LLEXT_CFLAGS =\n").unwrap();
    host::json(
        &edk_path.join("edk-release.json"),
        &json!({
            "schema":1,"publishable":false,"target":"board/cpu","metadata-version":1,
            "host":{"version":"1.0.0","application":"meshbus","build-revision":"123"},
            "toolchain":{"compiler":"arm-zephyr-eabi-gcc"},"exported-symbols":["printk"],
            "edk":{"sdk-sha256":edk::digest(&edk_path).unwrap()}
        }),
    )
    .unwrap();
    // No compilation here: this fixture supplies local byte identities only.
    let tools = temp.path().join("toolchain");
    fs::create_dir_all(tools.join("bin")).unwrap();
    let tool = tools.join("bin/arm-zephyr-eabi-gcc");
    fs::write(&tool, b"local tool fixture").unwrap();
    let index = temp.path().join("source.json");
    let out = cli(&[
        "app",
        "source",
        "--output",
        index.to_str().unwrap(),
        "--edk",
        edk_path.to_str().unwrap(),
        "--toolchain",
        tools.to_str().unwrap(),
        "--cmake",
        tool.to_str().unwrap(),
        "--ninja",
        tool.to_str().unwrap(),
        "--python",
        tool.to_str().unwrap(),
    ]);
    assert!(
        out.status.success(),
        "{}",
        String::from_utf8_lossy(&out.stderr)
    );
    let index_value: Value = serde_json::from_slice(&fs::read(&index).unwrap()).unwrap();
    assert_eq!(index_value["releases"][0]["edk"]["path"], "edk");
    assert_eq!(
        index_value["releases"][0]["tools"]["cmake"]["sha256"],
        host::hash(b"local tool fixture")
    );
    let run = |cwd: &std::path::Path, args: &[&str]| {
        Command::new(env!("CARGO_BIN_EXE_meshbus"))
            .current_dir(cwd)
            .args(["app", "--project"])
            .arg(&root)
            .args(args)
            .output()
            .unwrap()
    };
    assert!(
        !run(
            temp.path(),
            &[
                "target",
                "--source",
                index.to_str().unwrap(),
                "--target",
                "board/cpu",
                "--firmware",
                "wrong"
            ]
        )
        .status
        .success()
    );
    assert!(
        run(
            temp.path(),
            &[
                "target",
                "--source",
                index.to_str().unwrap(),
                "--target",
                "board/cpu",
                "--firmware",
                "123"
            ]
        )
        .status
        .success()
    );
    assert!(!run(temp.path(), &["sync", "--locked"]).status.success());
    let out = run(temp.path(), &["--cache-dir", "cache", "sync", "--offline"]);
    assert!(
        out.status.success(),
        "{}",
        String::from_utf8_lossy(&out.stderr)
    );
    let before = fs::read(root.join("meshbus.lock")).unwrap();
    let lock: Value = serde_json::from_slice(&before).unwrap();
    let cached = std::path::Path::new(lock["release"]["edk"]["path"].as_str().unwrap());
    assert!(cached.is_absolute());
    assert!(run(&root, &["doctor"]).status.success());
    fs::remove_dir_all(cached).unwrap();
    assert!(
        run(
            temp.path(),
            &["--cache-dir", "cache", "sync", "--locked", "--offline"]
        )
        .status
        .success()
    );
    assert_eq!(fs::read(root.join("meshbus.lock")).unwrap(), before);
    let mut declaration: Value =
        serde_yaml_ng::from_slice(&fs::read(root.join("llext.yaml")).unwrap()).unwrap();
    let original = fs::read(root.join("llext.yaml")).unwrap();
    for change in [
        json!({"requires":["symbol:missing_export"]}),
        json!({"build":{"template":"arduboy"}}),
    ] {
        let mut changed = declaration.clone();
        for (key, value) in change.as_object().unwrap() {
            changed[key] = value.clone();
        }
        fs::write(
            root.join("llext.yaml"),
            serde_yaml_ng::to_string(&changed).unwrap(),
        )
        .unwrap();
        assert!(
            run(temp.path(), &["--cache-dir", "cache", "update"])
                .status
                .success()
        );
        let prior = fs::read(root.join("meshbus.lock")).unwrap();
        let out = run(&root, &["doctor"]);
        assert!(!out.status.success());
        let diagnosis: Value = serde_json::from_slice(&out.stdout).unwrap();
        assert_eq!(diagnosis["ready"], false);
        assert!(
            diagnosis["checks"]
                .as_array()
                .unwrap()
                .iter()
                .any(|c| c["check"] == "project compatibility" && c["ok"] == false)
        );
        assert_eq!(fs::read(root.join("meshbus.lock")).unwrap(), prior);
    }
    fs::write(root.join("llext.yaml"), original).unwrap();
    assert!(
        run(temp.path(), &["--cache-dir", "cache", "update"])
            .status
            .success()
    );
    fs::remove_dir_all(edk_path).unwrap();
    fs::remove_file(index).unwrap();
    fs::remove_file(root.join(".meshbus-target.json")).unwrap();
    assert!(
        run(&root, &["sync", "--locked", "--offline"])
            .status
            .success()
    );
    let mut wrong = lock.clone();
    wrong["release"]["host_platform"] = json!("wrong-platform");
    host::json(&root.join("meshbus.lock"), &wrong).unwrap();
    let out = run(&root, &["doctor"]);
    assert!(!out.status.success());
    assert!(String::from_utf8_lossy(&out.stdout).contains("another host platform"));
    fs::write(root.join("meshbus.lock"), &before).unwrap();
    fs::write(cached.join("cmake.cflags"), "corrupt").unwrap();
    assert!(
        !run(&root, &["sync", "--locked", "--offline"])
            .status
            .success()
    );
    assert_eq!(fs::read(root.join("meshbus.lock")).unwrap(), before);
    declaration["name"] = json!("Changed");
    fs::write(
        root.join("llext.yaml"),
        serde_yaml_ng::to_string(&declaration).unwrap(),
    )
    .unwrap();
    let out = run(&root, &["sync", "--locked"]);
    assert!(!out.status.success());
    assert!(String::from_utf8_lossy(&out.stderr).contains("declaration changed"));
}
