#!/usr/bin/env python3
#
# SPDX-License-Identifier: Apache-2.0

from __future__ import annotations

import argparse
import json
import re
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Any


ALLOWED_STATUSES = {"mapped", "covered_by_parent", "excluded", "deferred"}
ACCESS_LABEL_PATTERN = re.compile(r"\b(public|protected|private)\s*:")
COMMENT_BLOCK_PATTERN = re.compile(r"/\*.*?\*/", re.DOTALL)
COMMENT_LINE_PATTERN = re.compile(r"//.*?$", re.MULTILINE)
CLASS_PATTERN_TEMPLATE = r"\b(class|struct)\s+{name}\b[^{{;}}]*\{{"
IDENTIFIER_PATTERN = re.compile(r"([A-Za-z_~][A-Za-z0-9_]*)\s*$")


@dataclass(frozen=True)
class MethodInfo:
    name: str
    access: str
    signature: str


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Check upstream MeshCore C++ method inventory against the "
            "tracked meshcore-C protocol API map."
        )
    )
    parser.add_argument(
        "--repo-root",
        default=".",
        help="Repository root. Defaults to the current working directory.",
    )
    parser.add_argument(
        "--map",
        default="tests/lib/meshcore/protocol/protocol_api_map.json",
        help="Protocol API map JSON file.",
    )
    parser.add_argument(
        "--dump-upstream",
        action="store_true",
        help="Print extracted upstream method inventory before checking.",
    )
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument(
        "--emit-upstream-template",
        action="store_true",
        help=(
            "Print a regenerated map template to stdout. Existing mapping rows "
            "are preserved; newly discovered upstream methods are marked "
            "unmapped."
        ),
    )
    mode.add_argument(
        "--refresh-map",
        action="store_true",
        help=(
            "Rewrite the map file in place from the extracted upstream method "
            "inventory while preserving existing mapping decisions."
        ),
    )
    return parser.parse_args()


def load_text(path: Path) -> str:
    try:
        return path.read_text(encoding="utf-8")
    except Exception as exc:  # pragma: no cover
        raise RuntimeError(f"failed to read {path}: {exc}") from exc


def load_json(path: Path) -> dict[str, Any]:
    try:
        return json.loads(load_text(path))
    except json.JSONDecodeError as exc:
        raise RuntimeError(f"failed to parse JSON {path}: {exc}") from exc


def strip_comments(text: str) -> str:
    text = COMMENT_BLOCK_PATTERN.sub("", text)
    return COMMENT_LINE_PATTERN.sub("", text)


def find_matching_brace(text: str, open_index: int) -> int:
    depth = 0
    for index in range(open_index, len(text)):
        char = text[index]
        if char == "{":
            depth += 1
        elif char == "}":
            depth -= 1
            if depth == 0:
                return index
    raise RuntimeError("unmatched class body brace")


def extract_class_body(header_text: str, class_name: str) -> tuple[str, str]:
    pattern = re.compile(
        CLASS_PATTERN_TEMPLATE.format(name=re.escape(class_name)),
        re.MULTILINE,
    )
    match = pattern.search(header_text)
    if not match:
        raise RuntimeError(f"class not found: {class_name}")

    kind = match.group(1)
    open_index = header_text.find("{", match.start())
    close_index = find_matching_brace(header_text, open_index)
    return kind, header_text[open_index + 1 : close_index]


def normalize_decl(decl: str) -> str:
    lines = []
    for line in decl.splitlines():
        stripped = line.strip()
        if stripped.startswith("#"):
            continue
        lines.append(stripped)
    decl = " ".join(lines)
    decl = re.sub(r"\s+", " ", decl).strip()
    return decl


def method_name_from_decl(decl: str) -> str | None:
    if "(" not in decl:
        return None

    before_paren = decl.split("(", 1)[0].strip()
    if not before_paren:
        return None

    match = IDENTIFIER_PATTERN.search(before_paren)
    if not match:
        return None

    name = match.group(1)
    if name in {"if", "for", "while", "switch", "return"}:
        return None
    return name.lstrip("~")


def extract_methods_from_body(
    body: str, default_access: str, allowed_access: set[str]
) -> list[MethodInfo]:
    body = ACCESS_LABEL_PATTERN.sub(r";__ACCESS_\1__;", body)

    access = default_access
    current: list[str] = []
    methods: list[MethodInfo] = []
    brace_depth = 0

    def process_current() -> None:
        nonlocal access, current
        decl = normalize_decl("".join(current))
        current = []
        if not decl:
            return
        access_match = re.fullmatch(r"__ACCESS_(public|protected|private)__", decl)
        if access_match:
            access = access_match.group(1)
            return
        if access not in allowed_access:
            return
        name = method_name_from_decl(decl)
        if name is None:
            return
        methods.append(MethodInfo(name=name, access=access, signature=decl))

    for char in body:
        if brace_depth > 0:
            if char == "{":
                brace_depth += 1
            elif char == "}":
                brace_depth -= 1
            continue

        if char == "{":
            process_current()
            brace_depth = 1
        elif char == ";":
            process_current()
        else:
            current.append(char)

    process_current()
    return methods


def extract_methods(header_text: str, class_name: str, access: set[str]) -> list[MethodInfo]:
    stripped = strip_comments(header_text)
    kind, body = extract_class_body(stripped, class_name)
    default_access = "public" if kind == "struct" else "private"
    return extract_methods_from_body(body, default_access, access)


def count_methods(methods: list[MethodInfo]) -> dict[str, int]:
    counts: dict[str, int] = {}
    for method in methods:
        counts[method.name] = counts.get(method.name, 0) + 1
    return counts


def symbol_exists(symbol: str, target_text: str) -> bool:
    return re.search(rf"\b{re.escape(symbol)}\s*\(", target_text) is not None


def rel_path(path: Path, repo_root: Path) -> str:
    try:
        return str(path.relative_to(repo_root))
    except ValueError:
        return str(path)


def validate_map_shape(api_map: dict[str, Any]) -> list[str]:
    errors: list[str] = []
    if api_map.get("version") != 1:
        errors.append("map version must be 1")
    if not isinstance(api_map.get("target_files"), list):
        errors.append("map must contain target_files list")
    if not isinstance(api_map.get("modules"), list):
        errors.append("map must contain modules list")
    return errors


def method_counts_in_order(methods: list[MethodInfo]) -> list[tuple[str, int]]:
    counts = count_methods(methods)
    ordered: list[tuple[str, int]] = []
    seen: set[str] = set()

    for method in methods:
        if method.name in seen:
            continue
        seen.add(method.name)
        ordered.append((method.name, counts[method.name]))

    return ordered


def generated_row(
    method_name: str, method_count: int, existing: dict[str, Any] | None
) -> dict[str, Any]:
    row: dict[str, Any] = {"name": method_name}

    if method_count != 1:
        row["count"] = method_count

    if existing is None:
        row["status"] = "unmapped"
        row["reason"] = "TODO: map to C target symbol or mark excluded/deferred."
        return row

    for key, value in existing.items():
        if key in {"name", "count"}:
            continue
        row[key] = value

    return row


def stale_row(existing: dict[str, Any]) -> dict[str, Any]:
    row = dict(existing)
    row["status"] = "stale_upstream"
    row["reason"] = (
        "TODO: upstream method is no longer extracted; remove or update this row."
    )
    return row


def generate_map_template(
    api_map: dict[str, Any], repo_root: Path
) -> tuple[dict[str, Any], list[str]]:
    errors: list[str] = []
    generated: dict[str, Any] = {
        "version": api_map.get("version"),
        "default_access": api_map.get("default_access", []),
        "target_files": api_map.get("target_files", []),
        "modules": [],
    }

    for module in api_map.get("modules", []):
        module_name = module.get("name", "<unnamed>")
        header_path = repo_root / module["upstream_header"]
        new_module = {key: value for key, value in module.items() if key != "classes"}
        new_module["classes"] = []
        generated["modules"].append(new_module)

        if not header_path.exists():
            errors.append(
                f"{module_name}: upstream header missing: "
                f"{rel_path(header_path, repo_root)}"
            )
            continue

        header_text = load_text(header_path)
        for class_spec in module.get("classes", []):
            class_name = class_spec["name"]
            access = set(class_spec.get("access", api_map.get("default_access", [])))
            if not access:
                access = {"public", "protected"}

            new_class = {
                key: value for key, value in class_spec.items() if key != "methods"
            }
            new_class["methods"] = []
            new_module["classes"].append(new_class)

            try:
                methods = extract_methods(header_text, class_name, access)
            except RuntimeError as exc:
                errors.append(f"{module_name}:{class_name}: {exc}")
                continue

            existing_rows = {
                row["name"]: row for row in class_spec.get("methods", [])
            }
            extracted_names: set[str] = set()
            for method_name, method_count in method_counts_in_order(methods):
                extracted_names.add(method_name)
                new_class["methods"].append(
                    generated_row(
                        method_name, method_count, existing_rows.get(method_name)
                    )
                )

            for method_name, row in existing_rows.items():
                if method_name not in extracted_names:
                    new_class["methods"].append(stale_row(row))

    return generated, errors


def json_dump(data: dict[str, Any]) -> str:
    return json.dumps(data, indent=2, ensure_ascii=False) + "\n"


def main() -> int:
    args = parse_args()
    repo_root = Path(args.repo_root).resolve()
    map_path = (repo_root / args.map).resolve()
    api_map = load_json(map_path)

    errors = validate_map_shape(api_map)
    if errors:
        for line in errors:
            print(line, file=sys.stderr)
        return 1

    if args.emit_upstream_template or args.refresh_map:
        generated, generate_errors = generate_map_template(api_map, repo_root)
        if generate_errors:
            print("protocol API map generation: FAIL", file=sys.stderr)
            for line in generate_errors:
                print(f"  - {line}", file=sys.stderr)
            return 1

        output = json_dump(generated)
        if args.refresh_map:
            map_path.write_text(output, encoding="utf-8")
            print(f"protocol API map refreshed: {rel_path(map_path, repo_root)}")
        else:
            print(output, end="")
        return 0

    target_files = [repo_root / path for path in api_map["target_files"]]
    missing_target_files = [path for path in target_files if not path.exists()]
    if missing_target_files:
        for path in missing_target_files:
            errors.append(f"target file missing: {rel_path(path, repo_root)}")
    target_text = "\n".join(load_text(path) for path in target_files if path.exists())

    extracted_total = 0
    mapped_total = 0
    excluded_total = 0
    deferred_total = 0
    checked_symbols = 0

    for module in api_map["modules"]:
        module_name = module.get("name", "<unnamed>")
        header_path = repo_root / module["upstream_header"]
        if not header_path.exists():
            errors.append(
                f"{module_name}: upstream header missing: "
                f"{rel_path(header_path, repo_root)}"
            )
            continue

        header_text = load_text(header_path)
        for class_spec in module.get("classes", []):
            class_name = class_spec["name"]
            access = set(class_spec.get("access", api_map.get("default_access", [])))
            if not access:
                access = {"public", "protected"}

            try:
                methods = extract_methods(header_text, class_name, access)
            except RuntimeError as exc:
                errors.append(f"{module_name}:{class_name}: {exc}")
                continue

            counts = count_methods(methods)
            extracted_total += sum(counts.values())

            if args.dump_upstream:
                for method in methods:
                    print(
                        f"{module_name}:{class_name}:{method.access}:"
                        f"{method.name}: {method.signature}"
                    )

            rows = class_spec.get("methods", [])
            row_by_method: dict[str, dict[str, Any]] = {}
            for row in rows:
                method_name = row["name"]
                if method_name in row_by_method:
                    errors.append(
                        f"{module_name}:{class_name}:{method_name}: duplicate map row"
                    )
                row_by_method[method_name] = row

            for method_name, actual_count in sorted(counts.items()):
                row = row_by_method.get(method_name)
                if row is None:
                    errors.append(
                        f"{module_name}:{class_name}:{method_name}: "
                        f"missing map row ({actual_count} upstream occurrence(s))"
                    )
                    continue

                expected_count = row.get("count")
                if expected_count is None and actual_count != 1:
                    errors.append(
                        f"{module_name}:{class_name}:{method_name}: "
                        f"{actual_count} overloads require explicit count"
                    )
                elif expected_count is not None and expected_count != actual_count:
                    errors.append(
                        f"{module_name}:{class_name}:{method_name}: count mismatch "
                        f"(map={expected_count}, upstream={actual_count})"
                    )

            for method_name, row in sorted(row_by_method.items()):
                status = row.get("status")
                if status not in ALLOWED_STATUSES:
                    errors.append(
                        f"{module_name}:{class_name}:{method_name}: "
                        f"invalid status {status!r}"
                    )
                    continue
                if method_name not in counts:
                    errors.append(
                        f"{module_name}:{class_name}:{method_name}: "
                        "map row does not match an upstream method"
                    )
                    continue

                if status in {"excluded", "deferred"}:
                    if not row.get("reason"):
                        errors.append(
                            f"{module_name}:{class_name}:{method_name}: "
                            f"{status} row requires reason"
                        )
                    if status == "excluded":
                        excluded_total += counts[method_name]
                    else:
                        deferred_total += counts[method_name]
                    continue

                targets = row.get("targets", [])
                if not targets:
                    errors.append(
                        f"{module_name}:{class_name}:{method_name}: "
                        f"{status} row requires at least one target symbol"
                    )
                    continue
                for symbol in targets:
                    checked_symbols += 1
                    if not symbol_exists(symbol, target_text):
                        errors.append(
                            f"{module_name}:{class_name}:{method_name}: "
                            f"target symbol missing: {symbol}"
                        )
                mapped_total += counts[method_name]

    if errors:
        print("protocol API map check: FAIL", file=sys.stderr)
        for line in errors:
            print(f"  - {line}", file=sys.stderr)
        return 1

    print(
        "protocol API map check: PASS "
        f"({extracted_total} upstream method occurrence(s), "
        f"{mapped_total} mapped/covered, {excluded_total} excluded, "
        f"{deferred_total} deferred, {checked_symbols} target symbol check(s))"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
