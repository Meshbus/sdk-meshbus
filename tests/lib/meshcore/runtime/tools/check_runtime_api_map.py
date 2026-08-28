#!/usr/bin/env python3
#
# SPDX-License-Identifier: Apache-2.0

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path
from typing import Any


ALLOWED_STATUSES = {"covered", "excluded", "deferred", "target_only", "unmapped"}
COMMENT_BLOCK_PATTERN = re.compile(r"/\*.*?\*/", re.DOTALL)
COMMENT_LINE_PATTERN = re.compile(r"//.*?$", re.MULTILINE)
PUBLIC_FUNCTION_PATTERN = re.compile(
    r"^\s*(?:int|void|bool|size_t|uint[0-9]+_t|int[0-9]+_t|unsigned\s+long)"
    r"\s+(meshcore_[A-Za-z0-9_]+)\s*\(",
    re.MULTILINE,
)
ZTEST_PATTERN = re.compile(
    r"ZTEST\s*\(\s*([A-Za-z0-9_]+)\s*,\s*([A-Za-z0-9_]+)\s*\)",
    re.MULTILINE,
)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Validate meshcore public runtime API coverage map."
    )
    parser.add_argument(
        "--repo-root",
        default=".",
        help="Repository root. Defaults to the current working directory.",
    )
    parser.add_argument(
        "--map",
        default="tests/lib/meshcore/runtime/runtime_api_map.json",
        help="Runtime API map JSON file.",
    )
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument(
        "--emit-template",
        action="store_true",
        help=(
            "Print a regenerated runtime API coverage template to stdout. "
            "Existing rows are preserved; new public APIs become unmapped."
        ),
    )
    mode.add_argument(
        "--refresh-map",
        action="store_true",
        help="Rewrite the map file in place while preserving existing rows.",
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


def rel_path(path: Path, repo_root: Path) -> str:
    try:
        return str(path.relative_to(repo_root))
    except ValueError:
        return str(path)


def extract_public_functions(header_text: str) -> list[str]:
    stripped = strip_comments(header_text)
    functions: list[str] = []
    seen: set[str] = set()

    for match in PUBLIC_FUNCTION_PATTERN.finditer(stripped):
        name = match.group(1)
        if name in seen:
            continue
        seen.add(name)
        functions.append(name)

    return functions


def collect_source_files(root: Path) -> list[Path]:
    files: list[Path] = []
    for pattern in ("*.c", "*.cpp"):
        files.extend(root.rglob(pattern))
    return sorted(files)


def extract_ztests(source_roots: list[Path]) -> set[str]:
    tests: set[str] = set()

    for root in source_roots:
        if not root.exists():
            continue
        for path in collect_source_files(root):
            for suite, test in ZTEST_PATTERN.findall(load_text(path)):
                tests.add(test)
                tests.add(f"{suite}:{test}")

    return tests


def evidence_paths(row: dict[str, Any]) -> list[str]:
    evidence = row.get("upstream_evidence", [])
    if evidence is None:
        return []
    return evidence


def validate_upstream_evidence(
    name: str, row: dict[str, Any], repo_root: Path, errors: list[str]
) -> int:
    evidence = evidence_paths(row)
    if not isinstance(evidence, list):
        errors.append(f"{name}: upstream_evidence must be a list")
        return 0

    checked = 0
    for item in evidence:
        if not isinstance(item, str):
            errors.append(f"{name}: upstream_evidence entries must be strings")
            continue
        checked += 1
        evidence_path = repo_root / item
        if not evidence_path.exists():
            errors.append(f"{name}: upstream evidence missing: {item}")
    return checked


def validate_map_shape(api_map: dict[str, Any]) -> list[str]:
    errors: list[str] = []
    if api_map.get("version") != 1:
        errors.append("map version must be 1")
    if not isinstance(api_map.get("public_header"), str):
        errors.append("map must contain public_header")
    if not isinstance(api_map.get("test_source_roots"), list):
        errors.append("map must contain test_source_roots list")
    if not isinstance(api_map.get("functions"), list):
        errors.append("map must contain functions list")
    return errors


def generated_row(function_name: str, existing: dict[str, Any] | None) -> dict[str, Any]:
    if existing is None:
        return {
            "name": function_name,
            "status": "unmapped",
            "reason": "TODO: classify runtime coverage and list test names.",
        }
    return dict(existing)


def stale_row(existing: dict[str, Any]) -> dict[str, Any]:
    row = dict(existing)
    row["status"] = "stale_public_api"
    row["reason"] = (
        "TODO: public function is no longer extracted from the runtime public header; "
        "remove or update this row."
    )
    return row


def generate_map_template(
    api_map: dict[str, Any], repo_root: Path
) -> tuple[dict[str, Any], list[str]]:
    errors: list[str] = []
    public_header = repo_root / api_map["public_header"]
    generated = {
        "version": api_map.get("version"),
        "public_header": api_map.get("public_header"),
        "test_source_roots": api_map.get("test_source_roots", []),
        "functions": [],
    }

    if not public_header.exists():
        errors.append(f"public header missing: {rel_path(public_header, repo_root)}")
        return generated, errors

    public_functions = extract_public_functions(load_text(public_header))
    existing_rows = {row["name"]: row for row in api_map.get("functions", [])}
    public_set = set(public_functions)

    for name in public_functions:
        generated["functions"].append(generated_row(name, existing_rows.get(name)))

    for name, row in existing_rows.items():
        if name not in public_set:
            generated["functions"].append(stale_row(row))

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

    if args.emit_template or args.refresh_map:
        generated, generate_errors = generate_map_template(api_map, repo_root)
        if generate_errors:
            print("runtime API map generation: FAIL", file=sys.stderr)
            for line in generate_errors:
                print(f"  - {line}", file=sys.stderr)
            return 1

        output = json_dump(generated)
        if args.refresh_map:
            map_path.write_text(output, encoding="utf-8")
            print(f"runtime API map refreshed: {rel_path(map_path, repo_root)}")
        else:
            print(output, end="")
        return 0

    public_header = repo_root / api_map["public_header"]
    if not public_header.exists():
        errors.append(f"public header missing: {rel_path(public_header, repo_root)}")
        public_functions: list[str] = []
    else:
        public_functions = extract_public_functions(load_text(public_header))

    test_roots = [repo_root / root for root in api_map["test_source_roots"]]
    for root in test_roots:
        if not root.exists():
            errors.append(f"test source root missing: {rel_path(root, repo_root)}")
    ztests = extract_ztests(test_roots)

    rows = api_map.get("functions", [])
    row_by_name: dict[str, dict[str, Any]] = {}
    for row in rows:
        name = row.get("name")
        if not isinstance(name, str):
            errors.append("function row missing string name")
            continue
        if name in row_by_name:
            errors.append(f"{name}: duplicate map row")
        row_by_name[name] = row

    public_set = set(public_functions)
    for name in public_functions:
        if name not in row_by_name:
            errors.append(f"{name}: missing map row")

    covered_count = 0
    deferred_count = 0
    excluded_count = 0
    target_only_count = 0
    checked_tests = 0
    checked_evidence = 0

    for name, row in sorted(row_by_name.items()):
        status = row.get("status")
        if name not in public_set:
            errors.append(f"{name}: map row does not match a public API")
            continue
        if status not in ALLOWED_STATUSES:
            errors.append(f"{name}: invalid status {status!r}")
            continue
        if status == "unmapped":
            errors.append(f"{name}: public API is unmapped")
            continue

        if status in {"excluded", "deferred"}:
            if not row.get("reason"):
                errors.append(f"{name}: {status} row requires reason")
            checked_evidence += validate_upstream_evidence(
                name, row, repo_root, errors
            )
            if status == "excluded":
                excluded_count += 1
            else:
                deferred_count += 1
            continue

        checked_evidence += validate_upstream_evidence(name, row, repo_root, errors)
        if status == "covered" and not evidence_paths(row):
            errors.append(f"{name}: covered row requires upstream_evidence")
        if status == "target_only" and not row.get("reason"):
            errors.append(f"{name}: target_only row requires reason")

        tests = row.get("tests", [])
        if not tests:
            errors.append(f"{name}: {status} row requires tests")
            continue
        for test in tests:
            checked_tests += 1
            if test not in ztests:
                errors.append(f"{name}: test not found: {test}")

        if status == "covered":
            covered_count += 1
        else:
            target_only_count += 1

    if errors:
        print("runtime API map check: FAIL", file=sys.stderr)
        for line in errors:
            print(f"  - {line}", file=sys.stderr)
        return 1

    print(
        "runtime API map check: PASS "
        f"({len(public_functions)} public API(s), {covered_count} covered, "
        f"{target_only_count} target-only, {excluded_count} excluded, "
        f"{deferred_count} deferred, {checked_tests} test reference(s), "
        f"{checked_evidence} upstream evidence reference(s))"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
