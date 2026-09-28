# SPDX-License-Identifier: Apache-2.0
"""Deterministic product archives, integrity checks and Cargo notices."""
import gzip
import hashlib
import io
import json
from pathlib import Path, PurePosixPath
import re
import shutil
import stat
import tarfile
import zipfile


def require(condition, message):
    if not condition:
        raise ValueError(message)


def digest(data):
    return hashlib.sha256(data).hexdigest()


def read(path, limit=1024 * 1024 * 1024):
    require(stat.S_ISREG(path.lstat().st_mode), f"not a regular file: {path}")
    require(path.stat().st_size <= limit, f"file exceeds size limit: {path}")
    return path.read_bytes()


def write_json(path, value):
    path.write_text(json.dumps(value, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def files(root):
    result = []
    for entry in sorted(root.iterdir()):
        require(not entry.is_symlink(), f"symlink is not permitted: {entry}")
        if entry.is_dir():
            result.extend(files(entry))
        else:
            require(stat.S_ISREG(entry.stat().st_mode), f"special file: {entry}")
            result.append(entry)
    return sorted(result)


def relative(name):
    parts = name.split("/")
    require(name and not name.startswith("/") and "\\" not in name
            and not any(ord(c) < 32 or c in ':<>"|?*' for c in name)
            and all(p not in ("", ".", "..") and not p.endswith((" ", ".")) for p in parts)
            and not any(re.fullmatch(r"CON|PRN|AUX|NUL|COM\d|LPT\d", p.split(".")[0], re.I)
                        for p in parts), "unsafe artifact path")
    return PurePosixPath(name)


def clean_destination(path):
    require(not path.exists() or (path.is_dir() and not any(path.iterdir())),
            f"output directory is not empty: {path}")
    path.mkdir(parents=True, exist_ok=True)


def checksums(root):
    entries = [f"{digest(read(p))}  {p.relative_to(root).as_posix()}\n"
               for p in files(root) if p != root / "SHA256SUMS"]
    (root / "SHA256SUMS").write_text("".join(entries), encoding="utf-8", newline="\n")


def verify_checksums(root):
    actual = {p.relative_to(root).as_posix() for p in files(root) if p != root / "SHA256SUMS"}
    seen = set()
    for line in read(root / "SHA256SUMS", 4 * 1024 * 1024).decode().splitlines():
        sha, name = line.split("  ", 1)
        relative(name)
        require(re.fullmatch(r"[0-9a-f]{64}", sha), "invalid SHA256 digest")
        require(name not in seen and name != "SHA256SUMS", "duplicate or self checksum")
        require(digest(read(root / name)) == sha, f"checksum mismatch: {name}")
        seen.add(name)
    require(seen and seen == actual, "checksum inventory does not cover the complete part")


def sidecar(path):
    path.with_name(path.name + ".sha256").write_text(
        f"{digest(read(path))}  {path.name}\n", encoding="utf-8", newline="\n")


def verify_sidecar(path):
    require(read(path.with_name(path.name + ".sha256"), 4096).decode().strip()
            == f"{digest(read(path))}  {path.name}", f"archive checksum mismatch: {path}")


def pack(root, destination, prefix):
    paths = files(root)
    for path in paths:
        relative(path.relative_to(root).as_posix())
    if destination.suffix == ".zip":
        with zipfile.ZipFile(destination, "w", compression=zipfile.ZIP_DEFLATED) as archive:
            for path in paths:
                entry = zipfile.ZipInfo(f"{prefix}/{path.relative_to(root).as_posix()}")
                entry.create_system = 3
                entry.external_attr = (stat.S_IFREG | 0o644) << 16
                entry.compress_type = zipfile.ZIP_DEFLATED
                archive.writestr(entry, read(path))
    else:
        with destination.open("wb") as output:
            with gzip.GzipFile(filename="", mode="wb", fileobj=output, mtime=0) as compressed:
                with tarfile.open(fileobj=compressed, mode="w", format=tarfile.GNU_FORMAT) as archive:
                    for path in paths:
                        data = read(path)
                        entry = tarfile.TarInfo(f"{prefix}/{path.relative_to(root).as_posix()}")
                        entry.size = len(data)
                        entry.mode = 0o755 if path.name == "meshbus" else 0o644
                        archive.addfile(entry, io.BytesIO(data))
    sidecar(destination)


def notice_text(path):
    """Read a required, nonempty notice from its owning component."""
    require(path.is_file(), f"missing notice file: {path}")
    data = read(path, 1024 * 1024)
    require(data.strip(), f"empty notice: {path}")
    return data.decode("utf-8")


def collect_licenses(source_root, output, packages):
    inventory = []
    for package in packages:
        name, version = package["name"], package["version"]
        directory = output / "licenses" / relative(f"{name}-{version}")
        directory.mkdir(parents=True)
        root = Path(package["manifest_path"]).parent
        license_id = package.get("license") or ""
        for path in sorted(root.iterdir()):
            if path.is_file() and path.name.lower().startswith(("license", "copying", "copyright", "notice")):
                (directory / path.name).write_text(notice_text(path), encoding="utf-8")
        # Cargo permits a license-file below the package root.
        if package.get("license_file"):
            path = root / relative(package["license_file"])
            require(path.resolve().is_relative_to(root.resolve()), "Cargo license-file escapes package")
            destination = directory / relative(package["license_file"])
            destination.parent.mkdir(parents=True, exist_ok=True)
            destination.write_text(notice_text(path), encoding="utf-8")
        # NOTICE alone is not the CLI's Apache license text.
        if name == "meshbus-cli":
            require(license_id == "Apache-2.0", "unexpected Meshbus CLI license")
            shutil.copyfile(source_root / "LICENSE", directory / "Apache-2.0.txt")
        elif not any(directory.iterdir()):
            if "Apache-2.0" in license_id.split(" OR "):
                (directory / "Apache-2.0.txt").write_text(
                    notice_text(source_root / "LICENSES/Apache-2.0.txt"), encoding="utf-8")
            elif name == "clipboard-win" and license_id == "BSL-1.0":
                (directory / "BSL-1.0.txt").write_text(
                    notice_text(source_root / "LICENSES/BSL-1.0.txt"), encoding="utf-8")
            elif name.startswith("protoc-bin-vendored") and license_id == "MIT":
                (directory / "NOTICE").write_text(
                    notice_text(source_root / "scripts/meshbus/NOTICE"), encoding="utf-8")
        require(any(directory.iterdir()), f"dependency {name}-{version} has no license text")
        inventory.append({key: package.get(key) for key in
                          ("name", "version", "license", "authors", "repository", "distribution_scope")})
    write_json(output / "dependencies.json", sorted(inventory, key=lambda p: (p["name"], p["version"])))
