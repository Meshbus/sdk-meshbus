# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 FoBE Studio

"""Deterministic filtering and packaging for public Meshbus LLEXT EDKs."""

from __future__ import annotations

from dataclasses import dataclass
import hashlib
import io
from pathlib import Path, PurePosixPath
import posixpath
import re
import shutil
import tarfile
import tempfile


EDK_ROOT_NAME = "llext-edk"
HEADER_POLICY = "meshbus-public-v1"
MAX_ARCHIVE_FILE_SIZE = 64 * 1024 * 1024
MAX_ARCHIVE_TOTAL_SIZE = 256 * 1024 * 1024

PUBLIC_SDK_PREFIXES = (
    PurePosixPath("include/meshbus/include/zephyr/display"),
    PurePosixPath("include/meshbus/include/zephyr/meshbus"),
    PurePosixPath("include/meshbus/include/zephyr/zui"),
)
SDK_SOURCE_PREFIX = PurePosixPath("include/meshbus")
CANONICAL_BUILD_PREFIX = PurePosixPath("include/build/host")
SDK_BUILD_MARKER = ("modules", "meshbus")
GENERATED_MESHBUS_MARKER = ("modules", "meshbus", "subsys", "meshbus", "meshbus")
PUBLIC_PB_INCLUDE_RE = re.compile(
    rb'^\s*#\s*include\s*"(meshbus/[A-Za-z0-9_.-]+\.pb\.h)"',
    re.MULTILINE,
)


class EdkReleaseError(ValueError):
    """Raised when an EDK cannot satisfy the public release contract."""


@dataclass(frozen=True)
class EdkFilterReport:
    """Evidence returned after filtering one staged EDK tree."""

    retained_public_headers: tuple[str, ...]
    retained_generated_headers: tuple[str, ...]
    removed_sdk_files: tuple[str, ...]
    canonicalized_build_prefix: str

    def as_dict(self) -> dict[str, object]:
        return {
            "header-policy": HEADER_POLICY,
            "retained-public-headers": list(self.retained_public_headers),
            "retained-generated-headers": list(self.retained_generated_headers),
            "removed-sdk-files": list(self.removed_sdk_files),
            "canonicalized-build-prefix": self.canonicalized_build_prefix,
        }


def sha256_file(path: Path) -> str:
    """Return the SHA-256 digest of a file."""

    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def safe_extract_edk(archive_path: Path, destination: Path) -> Path:
    """Extract a single-root EDK and materialize safe in-tree symlinks."""

    destination.mkdir(parents=True, exist_ok=True)
    seen: set[str] = set()
    total_size = 0

    try:
        archive = tarfile.open(archive_path, "r:*")
    except (OSError, tarfile.TarError) as exc:
        raise EdkReleaseError(f"unable to open EDK archive {archive_path}: {exc}") from exc

    with archive:
        members = archive.getmembers()
        regular_members: dict[str, tarfile.TarInfo] = {}
        symlink_members: list[tuple[tarfile.TarInfo, str]] = []
        for member in members:
            raw_name = member.name.rstrip("/")
            if not raw_name:
                continue
            path = PurePosixPath(raw_name)
            if path.is_absolute() or ".." in path.parts:
                raise EdkReleaseError(f"unsafe EDK archive path: {member.name}")
            if not path.parts or path.parts[0] != EDK_ROOT_NAME:
                raise EdkReleaseError(f"EDK archive member is outside {EDK_ROOT_NAME}/: {member.name}")
            normalized = path.as_posix()
            if normalized in seen:
                raise EdkReleaseError(f"duplicate EDK archive member: {normalized}")
            seen.add(normalized)
            if member.islnk():
                raise EdkReleaseError(f"unsupported EDK archive member type: {member.name}")
            if member.issym():
                if PurePosixPath(member.linkname).is_absolute():
                    raise EdkReleaseError(f"unsafe EDK symlink target: {member.name}")
                target = posixpath.normpath(
                    posixpath.join(posixpath.dirname(normalized), member.linkname)
                )
                target_path = PurePosixPath(target)
                if (
                    target_path.is_absolute()
                    or not target_path.parts
                    or target_path.parts[0] != EDK_ROOT_NAME
                ):
                    raise EdkReleaseError(f"unsafe EDK symlink target: {member.name}")
                symlink_members.append((member, target))
                continue
            if not (member.isdir() or member.isfile()):
                raise EdkReleaseError(f"unsupported EDK archive member type: {member.name}")
            if member.size < 0 or member.size > MAX_ARCHIVE_FILE_SIZE:
                raise EdkReleaseError(f"EDK archive member is too large: {member.name}")
            total_size += member.size
            if total_size > MAX_ARCHIVE_TOTAL_SIZE:
                raise EdkReleaseError("EDK archive expands beyond the configured size limit")

            if member.isfile():
                regular_members[normalized] = member

        for member in members:
            raw_name = member.name.rstrip("/")
            if not raw_name or member.issym():
                continue
            path = PurePosixPath(raw_name)
            output = destination.joinpath(*path.parts)
            if member.isdir():
                output.mkdir(parents=True, exist_ok=True)
                continue

            output.parent.mkdir(parents=True, exist_ok=True)
            source = archive.extractfile(member)
            if source is None:
                raise EdkReleaseError(f"unable to read EDK archive member: {member.name}")
            with source, output.open("wb") as stream:
                shutil.copyfileobj(source, stream)

        for member, target in symlink_members:
            target_member = regular_members.get(target)
            if target_member is None:
                raise EdkReleaseError(
                    f"EDK symlink does not target one regular archive file: {member.name}"
                )
            if target_member.size > MAX_ARCHIVE_FILE_SIZE:
                raise EdkReleaseError(f"EDK symlink target is too large: {member.name}")
            total_size += target_member.size
            if total_size > MAX_ARCHIVE_TOTAL_SIZE:
                raise EdkReleaseError("EDK archive expands beyond the configured size limit")
            output = destination.joinpath(*PurePosixPath(member.name).parts)
            output.parent.mkdir(parents=True, exist_ok=True)
            source = archive.extractfile(target_member)
            if source is None:
                raise EdkReleaseError(f"unable to read EDK symlink target: {member.name}")
            with source, output.open("wb") as stream:
                shutil.copyfileobj(source, stream)

    root = destination / EDK_ROOT_NAME
    if not root.is_dir():
        raise EdkReleaseError(f"EDK archive is missing the {EDK_ROOT_NAME}/ root")
    for required in ("cmake.cflags", "Makefile.cflags", "include"):
        if not (root / required).exists():
            raise EdkReleaseError(f"EDK archive is missing {EDK_ROOT_NAME}/{required}")
    return root


def filter_edk_tree(root: Path) -> EdkFilterReport:
    """Apply the Meshbus SDK public-header policy to an extracted EDK root."""

    if root.name != EDK_ROOT_NAME or not root.is_dir():
        raise EdkReleaseError(f"invalid extracted EDK root: {root}")

    public_headers = _public_header_paths(root)
    required_pb = _required_generated_pb_headers(root, public_headers)
    generated = _generated_meshbus_headers(root)
    selected_generated: dict[str, Path] = {}
    for include_name in sorted(required_pb):
        matches = generated.get(include_name, [])
        if len(matches) != 1:
            raise EdkReleaseError(
                f"generated dependency {include_name} has {len(matches)} matching EDK headers"
            )
        selected_generated[include_name] = matches[0]

    allowed_files = set(public_headers) | set(selected_generated.values())
    removed: list[str] = []
    for path in sorted((item for item in root.rglob("*") if item.is_file())):
        relative = PurePosixPath(path.relative_to(root).as_posix())
        if not _is_sdk_owned_path(relative):
            continue
        if path in allowed_files:
            continue
        removed.append(relative.as_posix())
        path.unlink()

    build_prefix = _generated_build_prefix(root, selected_generated.values())
    old_prefix = build_prefix.as_posix()
    if build_prefix != CANONICAL_BUILD_PREFIX:
        source = root.joinpath(*build_prefix.parts)
        target = root.joinpath(*CANONICAL_BUILD_PREFIX.parts)
        if target.exists():
            raise EdkReleaseError(f"canonical EDK build path already exists: {target}")
        target.parent.mkdir(parents=True, exist_ok=True)
        source.rename(target)

    _rewrite_cmake_flags(root / "cmake.cflags", old_prefix)
    _rewrite_makefile_flags(root / "Makefile.cflags", old_prefix)
    _remove_empty_directories(root, preserve=_flag_path_directories(root))
    _validate_flag_paths(root)
    _validate_sdk_files(root, required_pb)

    canonical_generated = tuple(
        sorted(
            (CANONICAL_BUILD_PREFIX / PurePosixPath("modules/meshbus/subsys/meshbus") / name)
            .as_posix()
            for name in required_pb
        )
    )
    return EdkFilterReport(
        retained_public_headers=tuple(
            sorted(PurePosixPath(path.relative_to(root).as_posix()).as_posix() for path in public_headers)
        ),
        retained_generated_headers=canonical_generated,
        removed_sdk_files=tuple(sorted(removed)),
        canonicalized_build_prefix=CANONICAL_BUILD_PREFIX.as_posix(),
    )


def write_deterministic_archive(root: Path, output_path: Path) -> None:
    """Write a normalized tar.xz containing one EDK root."""

    if root.name != EDK_ROOT_NAME or not root.is_dir():
        raise EdkReleaseError(f"invalid EDK archive root: {root}")
    output_path.parent.mkdir(parents=True, exist_ok=True)

    with tempfile.NamedTemporaryFile(
        dir=output_path.parent,
        prefix=f".{output_path.name}.",
        suffix=".tmp",
        delete=False,
    ) as temporary:
        temporary_path = Path(temporary.name)

    try:
        with tarfile.open(temporary_path, "w:xz", format=tarfile.GNU_FORMAT) as archive:
            paths = [root, *sorted(root.rglob("*"), key=lambda item: item.relative_to(root).as_posix())]
            for path in paths:
                relative = path.relative_to(root.parent).as_posix()
                info = tarfile.TarInfo(relative + ("/" if path.is_dir() else ""))
                info.uid = 0
                info.gid = 0
                info.uname = ""
                info.gname = ""
                info.mtime = 0
                if path.is_dir():
                    info.type = tarfile.DIRTYPE
                    info.mode = 0o755
                    info.size = 0
                    archive.addfile(info)
                    continue
                if not path.is_file():
                    raise EdkReleaseError(f"unsupported staged EDK file type: {path}")
                data = path.read_bytes()
                info.type = tarfile.REGTYPE
                info.mode = 0o644
                info.size = len(data)
                archive.addfile(info, io.BytesIO(data))
        temporary_path.replace(output_path)
    finally:
        temporary_path.unlink(missing_ok=True)


def create_pruned_edk(source_archive: Path, output_archive: Path) -> EdkFilterReport:
    """Create one deterministic pruned EDK archive and SHA-256 sidecar."""

    with tempfile.TemporaryDirectory(prefix="meshbus-llext-edk-") as temporary:
        root = safe_extract_edk(source_archive, Path(temporary))
        report = filter_edk_tree(root)
        write_deterministic_archive(root, output_archive)
    write_sha256_sidecar(output_archive)
    return report


def write_sha256_sidecar(archive_path: Path) -> Path:
    """Write an archive SHA-256 sidecar atomically."""

    sidecar = archive_path.with_suffix(archive_path.suffix + ".sha256")
    digest = sha256_file(archive_path)
    with tempfile.NamedTemporaryFile(
        mode="w",
        encoding="utf-8",
        dir=sidecar.parent,
        prefix=f".{sidecar.name}.",
        suffix=".tmp",
        delete=False,
    ) as temporary:
        temporary.write(f"{digest}  {archive_path.name}\n")
        temporary_path = Path(temporary.name)
    try:
        temporary_path.replace(sidecar)
    finally:
        temporary_path.unlink(missing_ok=True)
    return sidecar


def _path_starts_with(path: PurePosixPath, prefix: PurePosixPath) -> bool:
    return path.parts[: len(prefix.parts)] == prefix.parts


def _is_sdk_owned_path(path: PurePosixPath) -> bool:
    if _path_starts_with(path, SDK_SOURCE_PREFIX):
        return True

    marker = SDK_BUILD_MARKER
    return any(
        path.parts[index : index + len(marker)] == marker
        for index in range(0, len(path.parts) - len(marker) + 1)
    )


def _public_header_paths(root: Path) -> tuple[Path, ...]:
    headers: list[Path] = []
    for prefix in PUBLIC_SDK_PREFIXES:
        directory = root.joinpath(*prefix.parts)
        if not directory.is_dir():
            raise EdkReleaseError(f"EDK is missing public SDK header root: {prefix}")
        headers.extend(path for path in directory.rglob("*.h") if path.is_file())
    if not headers:
        raise EdkReleaseError("EDK contains no public SDK headers")
    return tuple(sorted(headers))


def _required_generated_pb_headers(root: Path, public_headers: tuple[Path, ...]) -> set[str]:
    required: set[str] = set()
    for path in public_headers:
        required.update(match.decode("ascii") for match in PUBLIC_PB_INCLUDE_RE.findall(path.read_bytes()))
    return required


def _generated_meshbus_headers(root: Path) -> dict[str, list[Path]]:
    result: dict[str, list[Path]] = {}
    marker = GENERATED_MESHBUS_MARKER
    for path in root.rglob("*.pb.h"):
        relative = PurePosixPath(path.relative_to(root).as_posix())
        parts = relative.parts
        for index in range(0, len(parts) - len(marker) + 1):
            if parts[index : index + len(marker)] != marker:
                continue
            suffix = parts[index + len(marker) :]
            if len(suffix) != 1:
                continue
            include_name = f"meshbus/{suffix[0]}"
            result.setdefault(include_name, []).append(path)
    return result


def _generated_build_prefix(root: Path, generated_paths) -> PurePosixPath:
    prefixes: set[PurePosixPath] = set()
    marker = GENERATED_MESHBUS_MARKER
    for path in generated_paths:
        relative = PurePosixPath(path.relative_to(root).as_posix())
        parts = relative.parts
        for index in range(0, len(parts) - len(marker) + 1):
            if parts[index : index + len(marker)] == marker:
                prefixes.add(PurePosixPath(*parts[:index]))
                break
    if len(prefixes) != 1:
        raise EdkReleaseError(f"unable to identify one generated EDK build prefix: {sorted(map(str, prefixes))}")
    prefix = next(iter(prefixes))
    if (
        len(prefix.parts) < 2
        or prefix.parts[0] != "include"
        or not prefix.parts[1].startswith("build")
    ):
        raise EdkReleaseError(f"generated SDK headers are outside include/build: {prefix}")
    return prefix


def _is_private_sdk_flag(token: str) -> bool:
    marker = "/include/meshbus/"
    if marker not in token:
        return False
    return "/include/meshbus/include" not in token


def _rewrite_cmake_flags(path: Path, old_prefix: str) -> None:
    lines: list[str] = []
    for line in path.read_text(encoding="utf-8").splitlines():
        if not line.startswith("set(LLEXT_") or not line.endswith('")'):
            lines.append(line.replace(old_prefix, CANONICAL_BUILD_PREFIX.as_posix()))
            continue
        first_quote = line.find('"')
        last_quote = line.rfind('"')
        if first_quote < 0 or last_quote <= first_quote:
            raise EdkReleaseError(f"malformed CMake EDK flag line: {line}")
        prefix = line[: first_quote + 1]
        suffix = line[last_quote:]
        value = line[first_quote + 1 : last_quote]
        tokens = [
            _normalize_flag_token(
                token.replace(old_prefix, CANONICAL_BUILD_PREFIX.as_posix())
            )
            for token in value.split(";")
            if token and not _is_private_sdk_flag(token)
        ]
        lines.append(prefix + ";".join(tokens) + suffix)
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


QUOTED_TOKEN_RE = re.compile(r'"((?:\\.|[^"\\])*)"')


def _rewrite_makefile_flags(path: Path, old_prefix: str) -> None:
    lines: list[str] = []
    for line in path.read_text(encoding="utf-8").splitlines():
        rewritten = line.replace(old_prefix, CANONICAL_BUILD_PREFIX.as_posix())
        if not rewritten.startswith("LLEXT_") or "=" not in rewritten:
            lines.append(rewritten)
            continue
        name, value = rewritten.split("=", 1)
        tokens = QUOTED_TOKEN_RE.findall(value)
        if not tokens and value.strip():
            raise EdkReleaseError(f"malformed Makefile EDK flag line: {line}")
        kept = [
            _normalize_flag_token(token)
            for token in tokens
            if not _is_private_sdk_flag(token)
        ]
        lines.append(f"{name.rstrip()} = " + " ".join(f'"{token}"' for token in kept))
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def _cmake_flag_tokens(path: Path) -> list[str]:
    tokens: list[str] = []
    for line in path.read_text(encoding="utf-8").splitlines():
        if not line.startswith("set(LLEXT_") or not line.endswith('")'):
            continue
        first_quote = line.find('"')
        last_quote = line.rfind('"')
        if first_quote >= 0 and last_quote > first_quote:
            tokens.extend(line[first_quote + 1 : last_quote].split(";"))
    return tokens


def _validate_flag_paths(root: Path) -> None:
    missing: set[str] = set()
    for relative in _flag_paths(root):
        candidate = _resolved_flag_path(root, relative)
        if not candidate.exists():
            missing.add(relative)
    if missing:
        raise EdkReleaseError(f"EDK flags reference missing paths: {sorted(missing)}")


def _validate_sdk_files(root: Path, required_pb: set[str]) -> None:
    retained_pb: set[str] = set()
    forbidden: list[str] = []
    for path in (item for item in root.rglob("*") if item.is_file()):
        relative = PurePosixPath(path.relative_to(root).as_posix())
        if not _is_sdk_owned_path(relative):
            continue
        if any(_path_starts_with(relative, prefix) for prefix in PUBLIC_SDK_PREFIXES):
            continue
        marker = GENERATED_MESHBUS_MARKER
        parts = relative.parts
        matched = False
        for index in range(0, len(parts) - len(marker) + 1):
            if parts[index : index + len(marker)] == marker and len(parts[index + len(marker) :]) == 1:
                retained_pb.add(f"meshbus/{parts[-1]}")
                matched = True
                break
        if not matched:
            forbidden.append(relative.as_posix())
    if forbidden:
        raise EdkReleaseError(f"forbidden SDK files remain in EDK: {sorted(forbidden)}")
    if retained_pb != required_pb:
        raise EdkReleaseError(
            f"generated Meshbus dependency closure mismatch: required={sorted(required_pb)}, "
            f"retained={sorted(retained_pb)}"
        )


def _flag_paths(root: Path) -> set[str]:
    marker = "${CMAKE_CURRENT_LIST_DIR}/"
    paths: set[str] = set()
    for token in _cmake_flag_tokens(root / "cmake.cflags"):
        if token.startswith("-I" + marker):
            paths.add(token[len("-I" + marker) :])
        elif token.startswith("-imacros" + marker):
            paths.add(token[len("-imacros" + marker) :])
    return paths


def _flag_path_directories(root: Path) -> set[Path]:
    directories: set[Path] = set()
    for relative in _flag_paths(root):
        candidate = _resolved_flag_path(root, relative)
        if candidate.is_dir():
            directories.add(candidate)
    return directories


def _remove_empty_directories(root: Path, *, preserve: set[Path]) -> None:
    for path in sorted((item for item in root.rglob("*") if item.is_dir()), reverse=True):
        if path in preserve:
            continue
        try:
            path.rmdir()
        except OSError:
            pass


def _normalize_flag_token(token: str) -> str:
    for marker in ("${CMAKE_CURRENT_LIST_DIR}/", "$(LLEXT_EDK_INSTALL_DIR)/"):
        for option in ("-I", "-imacros"):
            prefix = option + marker
            if not token.startswith(prefix):
                continue
            relative = posixpath.normpath(token[len(prefix) :])
            if relative in ("", ".", "..") or relative.startswith("../"):
                raise EdkReleaseError(f"EDK flag escapes the archive root: {token}")
            return prefix + relative
    return token


def _resolved_flag_path(root: Path, relative: str) -> Path:
    path = PurePosixPath(relative)
    if path.is_absolute() or ".." in path.parts:
        raise EdkReleaseError(f"EDK flag path escapes the archive root: {relative}")
    return root.joinpath(*path.parts)
