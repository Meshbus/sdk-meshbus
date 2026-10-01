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
import shutil

import artifacts as art


def font_term_ids(font):
    identifiers = re.findall(r"[A-Za-z0-9][A-Za-z0-9.-]*", font["license"])
    terms = {"u8g2-texts-" + identifier.lower().replace(".", "-") for identifier in identifiers}
    terms |= {name + "-txt" for name in terms}
    family = (font.get("family", "") + " " + font.get("group", "")).lower()
    if "iconic" in family:
        terms |= {"u8g2-texts-open-iconic-font-license-txt", "u8g2-texts-open-iconic-icon-license-txt"}
    if "siji" in family:
        terms.add("u8g2-texts-siji-license-txt")
    return terms


def combined_notice(root):
    """Deduplicate exact text, keeping associations and selected attribution."""
    groups = {}
    fonts = root / "licenses/u8g2/fonts/selected-fonts.json"
    selected_fonts = json.loads(art.read(fonts)) if fonts.exists() else []
    font_terms = set().union(*(font_term_ids(font) for font in selected_fonts))

    def retain(name, text):
        art.require(text.strip(), f"empty notice: {name}")
        key = art.digest(text.encode())
        groups.setdefault(key, {"text": text, "sources": []})["sources"].append(name)

    for path in art.files(root):
        name = path.relative_to(root).as_posix()
        if path.suffix == ".json" or path.name in ("REUSE.toml", "LICENSING.md"):
            continue
        if name.startswith("licenses/u8g2/fonts/u8g2-texts-") and path.stem not in font_terms:
            continue
        if name.startswith("licenses/") or path.name in ("LICENSE.txt", "NOTICE.txt", "THIRD-PARTY-NOTICES.txt"):
            retain(name, art.notice_text(path))
    for font in selected_fonts:
        retain("font " + font["symbol"], font["attribution"])
    dependencies = root / "dependencies.json"
    if dependencies.exists():
        for row in json.loads(art.read(dependencies)):
            if "MPL-2.0" in (row.get("license") or ""):
                name, version = row["name"], row["version"]
                art.relative(f"{name}/{version}")
                retain(f"source availability: {name}-{version}",
                       f"The MPL-2.0 covered source for {name} {version} is available at "
                       f"https://crates.io/api/v1/crates/{name}/{version}/download\n")
    art.require(groups, "missing notice materials")
    return ("Meshbus distribution notices. Original terms apply to their associated components.\n"
            + "".join("\n=== " + ", ".join(sorted(row["sources"])) + " ===\n"
                      + row["text"] + "\n" for _, row in sorted(groups.items()))).encode()


def separate_evidence(root, part):
    """Move collected materials out of the download and bind its one notice."""
    evidence = part / "material-evidence"
    evidence.mkdir()
    for path in list(root.iterdir()):
        if path.name in {"licenses", "license-materials.json", "dependencies.json", "build-tools.json",
                         "generated-materials.json", "LICENSE.txt", "NOTICE.txt", "THIRD-PARTY-NOTICES.txt"}:
            shutil.move(str(path), evidence / path.name)
    notice = combined_notice(evidence)
    (root / "NOTICE.txt").write_bytes(notice)
    return {"notice": "NOTICE.txt", "sha256": art.digest(notice)}


EDK_NOTICE = ("Meshbus public EDK. Original copyright and permission declarations remain in the exported headers.\n"
              "The terms below supplement those declarations. Apache/GPL dual-licensed headers use Apache-2.0;\n"
              "BSD-2-Clause/CC0 dual-licensed headers use BSD-2-Clause. Font arrays are not exported.\n")


def edk_license_ids(root):
    identifiers = {"Apache-2.0"}
    include = root / "include"
    marker = "SPDX" + r"-License-Identifier:\s*([^\n]+)"
    for path in art.files(include) if include.exists() else []:
        text = art.read(path).decode("utf-8", errors="replace")
        for expression in re.findall(marker, text):
            expression = expression.split(r"\n")[0].strip().removesuffix("*/").strip()
            art.require(re.fullmatch(r"[A-Za-z0-9.+-]+(?:\s+(?:AND|OR|WITH)\s+[A-Za-z0-9.+-]+)*", expression)
                        and not {"AND", "OR"} <= set(expression.split()),
                        f"unsupported exported-header license expression: {expression}")
            names = set(expression.split()) - {"AND", "OR", "WITH"}
            if "OR" in expression.split() and "Apache-2.0" in names:
                names = {"Apache-2.0"}
            elif "OR" in expression.split() and "BSD-2-Clause" in names:
                names = {"BSD-2-Clause"}
            identifiers.update(names)
    if (root / "include/modules/lib/u8g2/include/display").exists():
        identifiers.add("BSD-2-Clause")
    return identifiers


def edk_terms(root, apache):
    sdk = Path(__file__).resolve().parents[2]
    result = {}
    for identifier in sorted(edk_license_ids(root)):
        if identifier == "Apache-2.0":
            result[identifier] = apache
        else:
            paths = [directory / f"{identifier}.txt" for directory in
                     (sdk / "LICENSES", sdk.parent / "zephyr/LICENSES")]
            path = next((p for p in paths if p.is_file()), None)
            art.require(path is not None, f"missing exported-header license text: {identifier}")
            result[identifier] = art.notice_text(path)
    return result


def edk_notice(root, apache, terms=None):
    """Source headers retain attribution; include each applicable full text once."""
    terms = terms if terms is not None else edk_terms(root, apache)
    return (EDK_NOTICE + "".join(f"\n=== {name} ===\n{text}\n"
                                for name, text in sorted(terms.items()))).encode()


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
        if path.is_file() and path.name not in ("REUSE.toml", "LICENSING.md") and \
                path.name.lower().startswith(("license", "copying", "copyright", "notice")):
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
    wanted = set()
    source = art.read(module / "src/u8g2_fonts.c", 32 * 1024 * 1024).decode()
    entries = []
    for symbol in sorted(symbols):
        matches = [name for name in catalog["families"] if symbol.startswith(f"u8g2_font_{name}_")]
        art.require(matches, f"unmapped selected font: {symbol}")
        family = max(matches, key=len)
        entry = catalog["families"][family]
        group = catalog["groups"][entry["group"]]
        license_id = entry.get("license", group["license"])
        # Supplemental Creative Commons terms have stable SPDX-derived names.
        # Family notices carry the other grants (MIT, Adobe/DEC and BSD).
        for identifier in re.findall(r"[A-Za-z0-9][A-Za-z0-9.-]*", license_id):
            term = "u8g2-texts-" + identifier.lower().replace(".", "-")
            selected_terms = {name for name in records if name in (term, term + "-txt")}
            art.require(not identifier.startswith(("CC-", "CC0-")) or selected_terms,
                        f"missing supplemental font terms: {identifier}")
        wanted.update(records.keys() & font_term_ids({"license": license_id, "family": family, "group": entry["group"]}))
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


def toolchain_runtimes(domains, configs, cache_value, output):
    """Retain installed SDK terms for the archives passed to the final linker."""
    groups = {"libgcc.a": "gcc-runtime", "libstdc++.a": "gcc-runtime", "libsupc++.a": "gcc-runtime",
              "libc.a": "picolibc", "libm.a": "picolibc"}
    notices = {"gcc-runtime": ("gcc", ("COPYING3", "COPYING.RUNTIME")),
               "picolibc": ("picolibc", ("COPYING.picolibc", "COPYING.NEWLIB", "COPYING.GPL2"))}
    entries, inputs = {}, []
    for domain, build in sorted(domains.items()):
        conf = configs[domain]
        if not any(conf.get(key) == "y" for key in ("CONFIG_LIBGCC_RTLIB", "CONFIG_PICOLIBC_USE_TOOLCHAIN")):
            continue
        compiler = Path(cache_value(build / "CMakeCache.txt", "CMAKE_C_COMPILER")).resolve(strict=True)
        prefix = compiler.parent.parent
        sdk_version = art.read(prefix.parent.parent / "sdk_version").decode().strip()
        art.require(sdk_version, "missing toolchain SDK version")
        text = art.read(build / "zephyr/zephyr.map", 64 * 1024 * 1024).decode()
        archives = set()
        for name in re.findall(r"^LOAD (\S+\.a)\s*$", text, re.M):
            path = Path(name)
            if path.is_absolute() and path.resolve().is_relative_to(prefix):
                archives.add(path.resolve(strict=True))
        names = {p.name for p in archives}
        art.require(conf.get("CONFIG_LIBGCC_RTLIB") != "y" or "libgcc.a" in names,
                    "missing configured libgcc linker input")
        art.require(conf.get("CONFIG_PICOLIBC_USE_TOOLCHAIN") != "y" or "libc.a" in names,
                    "missing configured Picolibc linker input")
        art.require(names <= set(groups), "unreviewed toolchain runtime archive")
        components = sorted({groups[name] for name in names})
        for component in components:
            owner, filenames = notices[component]
            destination = output / "licenses" / ("toolchain-" + component)
            destination.mkdir(parents=True, exist_ok=True)
            for filename in filenames:
                data = art.read(prefix / "share/licenses" / owner / filename)
                art.require(data.strip(), "empty toolchain runtime notice")
                target = destination / filename
                art.require(not target.exists() or art.read(target) == data, "conflicting toolchain runtime terms")
                target.write_bytes(data)
            entries[component] = {"component": "toolchain-" + component,
                "materials": [p.relative_to(output).as_posix() for p in art.files(destination)]}
        inputs.append({"domain": domain, "sdk_version": sdk_version,
                       "components": ["toolchain-" + name for name in components],
                       "libraries": [{"path": p.relative_to(prefix).as_posix(),
                                      "sha256": art.digest(art.read(p, 64 * 1024 * 1024))} for p in sorted(archives)]})
    return [entries[name] for name in sorted(entries)], inputs


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
    runtime_entries, runtime_inputs = toolchain_runtimes(domains, configs, cache_value, output)
    entries.extend(runtime_entries)
    for entry in entries:
        entry["materials"] = [p.relative_to(output).as_posix()
                              for p in art.files(output / "licenses" / entry["component"])]
    result = {"selection": "spdx-source-components" if sbom["status"] == "generated" else "partial-no-spdx",
              "components": entries, "toolchain_runtimes": runtime_inputs,
              "fonts": [{k: v for k, v in entry.items() if k != "attribution"}
                                                for entry in font_entries],
              "scope": "Component materials and selected font notices; compatibility, source obligations, "
                       "toolchain runtimes and unrecorded generated inputs still require release review."}
    art.write_json(output / "license-materials.json", result)
    return {"selection": result["selection"], "manifest": "license-materials.json"}
