# SPDX-FileCopyrightText: 2026 FoBE Studio
# SPDX-License-Identifier: Apache-2.0
"""Compare Trivy reports produced with one database; retain all findings."""

import argparse
import json
from pathlib import Path


def findings(report):
    result = {}
    for target in report.get("Results", []):
        for finding in target.get("Vulnerabilities") or []:
            key = (target["Target"], finding["PkgName"], finding["InstalledVersion"], finding["VulnerabilityID"])
            result[key] = finding
    return result


def compare(current, base):
    old = findings(base)
    new = findings(current)
    blockers = [list(key) for key, value in new.items() if key not in old
                and value.get("Severity") in ("HIGH", "CRITICAL")]
    review = [list(key) for key, value in new.items() if value.get("Severity") in (None, "UNKNOWN")]
    return {"introduced_high_or_critical": blockers, "severity_requires_review": review,
            "total": len(new), "preexisting": sum(key in old for key in new)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("current", type=Path)
    parser.add_argument("base", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    result = compare(json.loads(args.current.read_text()), json.loads(args.base.read_text()))
    args.output.write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps(result, indent=2))
    if result["introduced_high_or_critical"]:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
