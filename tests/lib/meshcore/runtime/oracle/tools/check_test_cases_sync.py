#!/usr/bin/env python3
#
# SPDX-License-Identifier: Apache-2.0

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path


ZTEST_PATTERN = re.compile(
    r"ZTEST\s*\(\s*meshcore_runtime_oracle\s*,\s*([A-Za-z0-9_]+)\s*\)"
)
SUMMARY_COUNT_PATTERN = re.compile(
    r"^\|\s*Current `ZTEST` count\s*\|\s*`(\d+)`\s*\|$",
    re.MULTILINE,
)
TABLE_TEST_PATTERN = re.compile(r"^\|\s*`(test_[A-Za-z0-9_]+)`\s*\|", re.MULTILINE)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Validate oracle_parity ZTEST list and test_cases.md are in sync."
    )
    parser.add_argument(
        "--src-dir", required=True, help="Path to oracle_parity src directory"
    )
    parser.add_argument("--cases", required=True, help="Path to test_cases.md")
    return parser.parse_args()


def load_text(path: Path) -> str:
    try:
        return path.read_text(encoding="utf-8")
    except Exception as exc:  # pragma: no cover
        raise RuntimeError(f"failed to read {path}: {exc}") from exc


def collect_source_files(src_dir: Path) -> list[Path]:
    files = sorted(src_dir.glob("*.cpp"))
    if not files:
        raise RuntimeError(f"no *.cpp files found under {src_dir}")
    return files


def extract_ztests(source_texts: list[str]) -> list[str]:
    ztests: list[str] = []
    for text in source_texts:
        ztests.extend(ZTEST_PATTERN.findall(text))
    return ztests


def extract_implemented_section(cases_text: str) -> str:
    anchor = "## Current Implemented ZTEST Cases"
    start = cases_text.find(anchor)
    if start < 0:
        raise RuntimeError("missing section: '## Current Implemented ZTEST Cases'")

    remainder = cases_text[start + len(anchor) :]
    next_header = remainder.find("\n## ")
    if next_header < 0:
        return remainder
    return remainder[:next_header]


def extract_case_rows(section_text: str) -> list[str]:
    return TABLE_TEST_PATTERN.findall(section_text)


def extract_summary_count(cases_text: str) -> int:
    match = SUMMARY_COUNT_PATTERN.search(cases_text)
    if not match:
        raise RuntimeError("missing summary row: 'Current `ZTEST` count'")
    return int(match.group(1))


def main() -> int:
    args = parse_args()
    src_dir = Path(args.src_dir).resolve()
    cases_path = Path(args.cases).resolve()

    src_files = collect_source_files(src_dir)
    source_texts = [load_text(path) for path in src_files]
    cases_text = load_text(cases_path)

    ztests = extract_ztests(source_texts)
    implemented_section = extract_implemented_section(cases_text)
    case_rows = extract_case_rows(implemented_section)
    summary_count = extract_summary_count(cases_text)

    errors: list[str] = []

    if len(ztests) != len(set(ztests)):
        errors.append("duplicate ZTEST names found in src/*.cpp")
    if len(case_rows) != len(set(case_rows)):
        errors.append("duplicate test rows found in test_cases.md implemented table")

    ztests_set = set(ztests)
    rows_set = set(case_rows)
    missing_in_cases = sorted(ztests_set - rows_set)
    missing_in_main = sorted(rows_set - ztests_set)

    if missing_in_cases:
        errors.append("tests present in src/*.cpp but missing in test_cases.md:")
        errors.extend(f"  - {name}" for name in missing_in_cases)
    if missing_in_main:
        errors.append("tests present in test_cases.md but missing in src/*.cpp:")
        errors.extend(f"  - {name}" for name in missing_in_main)

    if len(ztests) != summary_count:
        errors.append(
            f"summary count mismatch: table says {summary_count}, "
            f"but src/*.cpp has {len(ztests)}"
        )

    if errors:
        print("oracle_parity test-case sync check: FAIL", file=sys.stderr)
        for line in errors:
            print(line, file=sys.stderr)
        return 1

    print(
        "oracle_parity test-case sync check: PASS "
        f"({len(ztests)} tests, summary={summary_count})"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
