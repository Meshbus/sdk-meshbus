# SPDX-FileCopyrightText: 2026 FoBE Studio
# SPDX-License-Identifier: Apache-2.0
"""Collect notices from selected build inputs without a repository notice index.

Selection is deliberately conservative: a selected component can carry extra
license texts. Collected materials are evidence, not a license compatibility
decision or a guarantee that a distribution's source obligations are fulfilled.
"""
import json
from pathlib import Path
import re

import artifacts as art


def cargo_packages(metadata, root_id):
    """Walk the target-filtered resolve graph; never treat packages as the graph.

    Normal dependencies can contribute target code. Proc macros can emit code:
    conservatively retain their notices and normal dependencies as generators.
    Standalone build tools are recorded separately for generated-input review.
    """
    packages = {p["id"]: p for p in metadata["packages"]}
    resolve = metadata.get("resolve") or {}
    nodes = {node["id"]: node for node in resolve.get("nodes", [])}
    art.require(root_id in nodes and root_id in packages, "Cargo resolve graph missing CLI root")
    pending, seen = [(root_id, "runtime")], set()
    while pending:
        identifier, scope = pending.pop()
        if (identifier, scope) in seen:
            continue
        art.require(identifier in nodes and identifier in packages, "incomplete Cargo resolve graph")
        seen.add((identifier, scope))
        for edge in nodes[identifier]["deps"]:
            dependency = edge["pkg"]
            art.require(dependency in packages, "unknown Cargo dependency")
            macro = any("proc-macro" in target["kind"] for target in packages[dependency]["targets"])
            for kind in edge["dep_kinds"]:
                if kind["kind"] == "dev":
                    continue
                art.require(kind["kind"] in (None, "build"), "unknown Cargo dependency kind")
                if scope == "build-tool" or kind["kind"] == "build":
                    child_scope = "build-tool"
                else:
                    child_scope = "code-generator" if macro or scope == "code-generator" else "runtime"
                pending.append((dependency, child_scope))
    selected = sorted({identifier for identifier, scope in seen if scope == "runtime"})
    generators = sorted({identifier for identifier, scope in seen if scope == "code-generator"} - set(selected))
    tools = sorted({identifier for identifier, scope in seen if scope == "build-tool"} - set(selected) - set(generators))
    return ([{**packages[identifier], "distribution_scope": "runtime"} for identifier in selected]
            + [{**packages[identifier], "distribution_scope": "code-generator"} for identifier in generators],
            [{key: packages[identifier].get(key) for key in ("name", "version", "license", "repository")}
             for identifier in tools])


def retained_file(root, name):
    path = root / art.relative(name)
    art.require(path.resolve().is_relative_to(root.resolve()), "license path escapes component")
    return path


def material_files(root):
    """Root declarations and standard license directories; no full source walk."""
    result = set()
    for path in root.iterdir():
        if path.is_file() and (path.name == "REUSE.toml" or
                              path.name.lower().startswith(("license", "licensing", "copying", "copyright", "notice"))):
            result.add(path)
        elif path.is_dir() and path.name.lower() in ("licenses", "license"):
            result.update(art.files(path))
    return result


def copy_component(root, output, name, source_files=(), standard_roots=()):
    """Keep component terms, nested source licenses and original source banners."""
    root = root.resolve()
    destination = output / "licenses" / art.relative(name)
    candidates = material_files(root)
    banners = set()
    identifiers = set()
    visited = {root}
    for filename in source_files:
        path = retained_file(root, filename.removeprefix("./"))
        art.require(path.is_file(), f"missing SPDX source in {name}: {filename}")
        parent = path.parent
        while parent not in visited:
            visited.add(parent)
            candidates.update(material_files(parent))
            parent = parent.parent
        # Preserve leading C/C++ copyright/permission comments verbatim. Source
        # filenames stay in private SPDX; the public bundle deduplicates banners.
        text = art.read(path, 64 * 1024 * 1024).decode("utf-8", errors="replace")
        sidecar = path.with_name(path.name + ".license")
        if sidecar.is_file():
            candidates.add(sidecar)
            text += "\n" + art.notice_text(sidecar)
        match = re.match(r"\s*((?:(?:/\*.*?\*/|//[^\n]*(?:\n|$))\s*)+)", text, re.S)
        if match and re.search(r"copyright|license|permission", match[1], re.I):
            banners.add(match[1].strip())
        # Split the marker so REUSE does not mistake this pattern for a file tag.
        for expression in re.findall("SPDX" + r"-License-Identifier:\s*([^\n]+)", text):
            identifiers.update(token for token in re.findall(r"[A-Za-z0-9][A-Za-z0-9.+-]*", expression)
                               if token not in {"AND", "OR", "WITH"})
    supplemental = {}
    for identifier in identifiers:
        for directory in (root / "LICENSES", *standard_roots):
            path = directory / f"{identifier}.txt"
            if path.is_file():
                supplemental[identifier] = path
                break
    full_banners = any(re.search(r"redistribution and use|permission is hereby granted|permission to use", b, re.I)
                       for b in banners)
    art.require(candidates or supplemental or full_banners, f"no license materials for selected component: {name}")
    destination.mkdir(parents=True, exist_ok=True)
    for path in sorted(candidates):
        relative = path.relative_to(root).as_posix()
        target = destination / art.relative(relative)
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(art.notice_text(retained_file(root, relative)), encoding="utf-8")
    for identifier, path in sorted(supplemental.items()):
        target = destination / "LICENSES" / f"{identifier}.txt"
        target.parent.mkdir(exist_ok=True)
        target.write_text(art.notice_text(path), encoding="utf-8")
    if banners:
        (destination / "SOURCE-NOTICES.txt").write_text(
            "\n\n".join(sorted(banners)) + "\n", encoding="utf-8")
    return {"component": name, "materials": [p.relative_to(output).as_posix() for p in art.files(destination)]}


def spdx_sources(raw_root, application_prefixes, zephyr_prefix):
    selected = {}
    for domain in sorted(raw_root.iterdir()):
        if not domain.is_dir():
            continue
        for filename in ("app.spdx", "zephyr.spdx"):
            text = art.read(domain / filename).decode()
            for block in re.split(r"(?=^##### Package:)", text, flags=re.M):
                name = re.search(r"^PackageName: (.+)$", block, re.M)
                paths = re.findall(r"^FileName: (.+)$", block, re.M)
                if not name or not paths:
                    continue
                component = name[1].removesuffix("-sources")
                if filename == "app.spdx":
                    component = "meshbus" if domain.name == "app" else "mcuboot"
                    paths = [(application_prefixes[domain.name] / art.relative(p.removeprefix("./"))).as_posix()
                             for p in paths]
                elif component == "zephyr":
                    # Zephyr's SPDX component uses west topdir, unlike module
                    # components, whose filenames are relative to module roots.
                    paths = [Path(p.removeprefix("./")).relative_to(zephyr_prefix).as_posix() for p in paths]
                selected.setdefault(component, set()).update(paths)
    return selected


def font_notices(module, symbols, output):
    """Collect selected family attribution plus retained supplemental full terms."""
    fonts = module / "fonts"
    catalog = json.loads(art.read(fonts / "catalog.json"))
    sources = json.loads(art.read(fonts / "sources.json"))
    records = {entry["id"]: entry for entry in sources["notices"]}
    art.require(len(records) == len(sources["notices"]), "duplicate font notice record")
    # These are generic full terms referenced by group notices. Keep the small
    # set together rather than infer license choices from free-form strings.
    wanted = {name for name in records if name.startswith("u8g2-texts-")}
    source = art.read(module / "src/u8g2_fonts.c", 32 * 1024 * 1024).decode()
    entries = []
    for symbol in sorted(symbols):
        matches = [name for name in catalog["families"] if symbol.startswith(f"u8g2_font_{name}_")]
        art.require(matches, f"unmapped selected font: {symbol}")
        family = max(matches, key=len)
        entry = catalog["families"][family]
        group = catalog["groups"][entry["group"]]
        art.require(group.get("notice"), f"missing family notice: {symbol}")
        for key in ("notice", "baseline_notice"):
            if group.get(key):
                wanted.add(group[key])
        declaration = re.search(r"const\s+uint8_t\s+" + re.escape(symbol) + r"\s*\[", source)
        art.require(declaration, f"missing selected font source: {symbol}")
        start = source.rfind("/*", 0, declaration.start())
        end = source.find("*/", start)
        banner = source[start:end + 2]
        art.require(start >= 0 and end < declaration.start() and "Copyright:" in banner,
                    f"missing font attribution: {symbol}")
        entries.append({"symbol": symbol, "family": family, "group": entry["group"],
                        "license": entry.get("license", group["license"]),
                        "status": entry.get("status", group["status"]), "attribution": banner})
    destination = output / "licenses/u8g2/fonts"
    destination.mkdir(parents=True, exist_ok=True)
    for name in sorted(wanted):
        art.require(name in records, f"missing font notice record: {name}")
        path = retained_file(fonts, f"notices/{name}.txt")
        data = art.read(path, 1024 * 1024)
        art.require(data.strip() and art.digest(data) == records[name]["sha256"], f"changed font notice: {name}")
        (destination / f"{name}.txt").write_bytes(data)
    art.write_json(destination / "selected-fonts.json", entries)
    art.write_json(destination / "sources.json", {"notices": [records[name] for name in sorted(wanted)]})
    return entries


def linked_fonts(elf):
    # pyelftools is already a Zephyr base requirement.
    from elftools.elf.elffile import ELFFile
    with elf.open("rb") as stream:
        table = ELFFile(stream).get_section_by_name(".symtab")
        art.require(table is not None, "font notice collection requires an unstripped ELF")
        return {symbol.name for symbol in table.iter_symbols()
                if symbol.name.startswith("u8g2_font_") and symbol["st_shndx"] != "SHN_UNDEF"
                and symbol["st_info"]["type"] == "STT_OBJECT"}


def firmware(source_root, domains, sbom, sysbuild, output, configs, cache_value):
    """Use SPDX source selection and build module roots, not all west projects."""
    roots = {"meshbus": source_root}
    fonts = set()
    for domain, build in domains.items():
        modules = build / "zephyr_modules.txt"
        if modules.is_file():
            for line in modules.read_text().splitlines():
                match = re.fullmatch(r'"([^"]+)":"([^"]+)":"[^"]*"', line)
                art.require(match, "invalid build module record")
                name, root = match[1], Path(match[2])
                art.require(name not in roots or roots[name] == root, f"conflicting module roots: {name}")
                roots[name] = root
        if sbom["status"] == "generated":
            roots["zephyr"] = Path(cache_value(build / "CMakeCache.txt", "ZEPHYR_BASE"))
        if configs[domain].get("CONFIG_U8G2") == "y":
            fonts.update(linked_fonts(build / "zephyr/zephyr.elf"))
    if sbom["status"] == "generated":
        from west.util import west_topdir
        prefixes = {domain: Path(cache_value(build / "CMakeCache.txt", "APPLICATION_SOURCE_DIR")).relative_to(
            roots["meshbus" if domain == "app" else "mcuboot"]) for domain, build in domains.items()}
        selected = spdx_sources(sysbuild / art.relative(sbom["private_retention"]["path"]), prefixes,
                                roots["zephyr"].relative_to(west_topdir(source_root)))
        # mcuboot is also a module in normal sysbuild module records.
    else:
        selected = {"meshbus": set()}
    if fonts:
        selected.setdefault("u8g2", set())
    if any(conf.get("CONFIG_ZUI") == "y" for conf in configs.values()):
        art.require("zui" in roots, "ZUI module missing from build records")
        selected.setdefault("zui", set())

    # Schema source is incorporated by Nanopb generation but is not a C source
    # entry in Zephyr's SPDX documents. Keep its independent attribution.
    if any(conf.get("CONFIG_MBS") == "y" for conf in configs.values()):
        art.require("meshbus-protobufs" in roots, "schema module missing from build records")
        schemas = list((roots["meshbus-protobufs"] / "meshbus").glob("*.proto"))
        art.require(schemas, "schema module contains no protobuf sources")
        selected.setdefault("meshbus-protobufs", set()).update(
            p.relative_to(roots["meshbus-protobufs"]).as_posix() for p in schemas)
    entries = []
    for name, paths in sorted(selected.items()):
        art.require(name in roots, f"selected component missing from build modules: {name}")
        standards = [source_root / "LICENSES"]
        if "zephyr" in roots:
            standards.append(roots["zephyr"] / "LICENSES")
        entries.append(copy_component(roots[name], output, name, paths, standards))
    font_entries = font_notices(roots["u8g2"], fonts, output) if fonts else []
    for domain, conf in configs.items():
        if conf.get("CONFIG_ZUI_TEXT_EDITOR_PREDICTIVE") != "y":
            continue
        custom = conf.get("CONFIG_ZUI_TEXT_EDITOR_PREDICTIVE_DICT_SOURCE")
        if custom:
            source = Path(custom)
            if not source.is_absolute():
                source = Path(cache_value(domains[domain] / "CMakeCache.txt", "APPLICATION_SOURCE_DIR")) / source
            source = source.resolve(strict=True)
            entries.append(copy_component(source.parent, output, f"dictionary-{domain}",
                                          [source.name], [source_root / "LICENSES"]))
            continue
        if conf.get("CONFIG_ZUI_TEXT_EDITOR_PREDICTIVE_BUILTIN_ENGLISH_DICT") == "y":
            art.require("zui" in roots, "ZUI module missing from build records")
            destination = output / "licenses/zui/SUBTLEX-LICENSE.txt"
            destination.parent.mkdir(parents=True, exist_ok=True)
            destination.write_text(art.notice_text(roots["zui"] / "src/dicts/LICENSE"), encoding="utf-8")
    for entry in entries:
        entry["materials"] = [p.relative_to(output).as_posix()
                              for p in art.files(output / "licenses" / entry["component"])]
    result = {"selection": "spdx-source-components" if sbom["status"] == "generated" else "partial-no-spdx",
              "components": entries, "fonts": [{k: v for k, v in entry.items() if k != "attribution"}
                                                for entry in font_entries],
              "scope": "Component materials and selected font notices; compatibility, source obligations, "
                       "toolchain runtimes and unrecorded generated inputs still require release review."}
    art.write_json(output / "license-materials.json", result)
    return {"selection": result["selection"], "manifest": "license-materials.json"}
