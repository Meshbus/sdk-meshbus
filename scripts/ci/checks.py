# SPDX-FileCopyrightText: 2026 FoBE Studio
# SPDX-License-Identifier: Apache-2.0
"""Small gates around existing tools; failures remain failures in the job summary."""

import argparse
import json
import os
import re
import subprocess
import sys
from pathlib import Path


def aggregate(needs, expected):
    missing = set(expected) - set(needs)
    failed = [name for name, job in needs.items() if job["result"] not in ("success", "skipped")]
    # Skips are legitimate only when the successful plan explicitly deselects
    # that class of work. A skipped planner must never green-light an empty run.
    if needs.get("plan", {}).get("result") != "success":
        failed.append("plan")
    for name in expected:
        if needs.get(name, {}).get("result") != "success":
            failed.append(name)
    if missing or failed:
        raise ValueError(f"required CI jobs did not succeed: {sorted(set(failed) | missing)}")


def expected_jobs(plan):
    expected = ['plan', 'lightweight']
    for flag, jobs in {'workspace': ['prepare'], 'host': ['host'], 'sdk': ['zephyr'],
                       'product_builds': ['products'], 'cli': ['cli-linux', 'cli-mac', 'native']}.items():
        enabled = plan.get('product_builds', plan['products']) if flag == 'product_builds' else plan[flag]
        if enabled:
            expected += jobs
    if any(plan[flag] for flag in ('fonts', 'checkpatch', 'audit')):
        expected += ['quality']
    return expected


def commits(base):
    hashes = subprocess.check_output(["git", "rev-list", "--no-merges", f"{base}..HEAD"], text=True).splitlines()
    for commit in hashes:
        message = subprocess.check_output(["git", "show", "-s", "--format=%B", commit], text=True)
        title, _, body = message.partition("\n")
        if len(title) >= 72 or not re.match(r"[^:\s][^:]*: \S", title) or not body.strip():
            raise ValueError(f"{commit[:12]} requires an area-prefixed title under 72 characters and a body")


def report_twister(path, runnable=False):
    report = json.loads(path.read_text())
    suites = report.get("testsuites", [])
    active = [s for s in suites if s.get("status") not in ("filtered", "skipped")]
    if not active:
        raise ValueError("Twister selected no executable/buildable scenarios")
    bad = [s.get("name", "unknown") for s in active if s.get("status") not in (("passed",) if runnable else ("passed", "not run"))]
    if bad:
        raise ValueError(f"Twister scenarios did not pass: {bad}")
    # Twister rounds durations to hundredths: a fast native test can report
    # 0.00 even after its harness records a passed case. Build-only reports
    # have no passed harness cases and must still fail the runtime gate.
    executed = any(float(s.get("execution_time") or 0) > 0 or
                   (s.get("runnable") is True and
                    any(case.get("status") == "passed" for case in s.get("testcases", [])))
                   for s in active)
    if runnable and not executed:
        raise ValueError("runtime job has no recorded execution evidence")
    print(f"Twister: {len(active)} accepted ({'runtime' if runnable else 'compile'}); "
          f"{len(suites) - len(active)} filtered/skipped (not passes)")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)
    aggregate_parser = sub.add_parser("aggregate")
    aggregate_parser.add_argument("--plan", type=Path, required=True)
    commit = sub.add_parser("commits")
    commit.add_argument("base")
    twister = sub.add_parser("twister")
    twister.add_argument("report", type=Path)
    twister.add_argument("--runnable", action="store_true")
    args = parser.parse_args()
    if args.command == "aggregate":
        aggregate(json.loads(os.environ["NEEDS_JSON"]), expected_jobs(json.loads(args.plan.read_text())))
    elif args.command == "commits":
        commits(args.base)
    else:
        report_twister(args.report, args.runnable)


if __name__ == "__main__":
    try:
        main()
    except (ValueError, KeyError) as error:
        print(error, file=sys.stderr)
        raise SystemExit(1) from error
