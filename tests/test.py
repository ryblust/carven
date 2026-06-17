#!/usr/bin/env python3

from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
UNIT_TEST_CMD = ["xmake", "run", "carven-unit-test", "--no-colors"]
E2E_TEST_CMD = [sys.executable, str(REPO_ROOT / "tests" / "e2e" / "test.py")]


def run_command(args: list[str]) -> None:
    print(f"test: {' '.join(args)}", file=sys.stderr)
    completed = subprocess.run(args, cwd=REPO_ROOT, check=False)
    if completed.returncode != 0:
        raise SystemExit(completed.returncode)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(prog="tests/test.py")
    parser.add_argument("--unit", action="store_true", help="run unit tests")
    parser.add_argument("--e2e", action="store_true", help="run e2e tests")
    parser.add_argument("--case", action="append", metavar="NAME", help="run one e2e case; can be repeated")
    parser.add_argument("--list-e2e", action="store_true", help="list e2e cases")
    parser.add_argument("--trace-commands", action="store_true", help="print e2e subprocess durations")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    e2e_requested = args.e2e or args.case is not None or args.list_e2e
    run_unit = args.unit or not e2e_requested
    run_e2e = e2e_requested or not args.unit

    if run_unit:
        run_command(UNIT_TEST_CMD)
    if run_e2e:
        e2e_cmd = E2E_TEST_CMD[:]
        if args.case:
            for case in args.case:
                e2e_cmd.extend(["--case", case])
        if args.list_e2e:
            e2e_cmd.append("--list")
        if args.trace_commands:
            e2e_cmd.append("--trace-commands")
        run_command(e2e_cmd)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
