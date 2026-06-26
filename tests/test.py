#!/usr/bin/env python3

from __future__ import annotations

import argparse
import shlex
import subprocess
import sys
import time
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
UNIT_TEST_CMD = ["xmake", "run", "carven-unit-test", "--no-colors"]
E2E_TEST_CMD = [sys.executable, str(REPO_ROOT / "tests" / "e2e" / "test.py")]


def format_duration(seconds: float) -> str:
    if seconds >= 100:
        return f"{seconds:.1f}s"
    return f"{seconds:.2f}s"


def format_command(args: list[str]) -> str:
    return " ".join(shlex.quote(display_command_arg(arg)) for arg in args)


def display_command_arg(arg: str) -> str:
    if not arg.startswith(str(REPO_ROOT)):
        return arg

    return Path(arg).relative_to(REPO_ROOT).as_posix()


def print_run_header(run_unit: bool, run_e2e: bool, cases: list[str] | None) -> None:
    suites = []
    if run_unit:
        suites.append("unit")
    if run_e2e:
        suites.append("e2e")

    print("Carven Test Run", flush=True)
    print(f"  suites: {', '.join(suites)}", flush=True)
    if cases:
        print(f"  e2e filter: {', '.join(cases)}", flush=True)


def print_suite_header(name: str, args: list[str]) -> None:
    print(flush=True)
    print(name, flush=True)
    print(f"  command: {format_command(args)}", flush=True)
    print(flush=True)


def run_command(args: list[str], suite_name: str) -> None:
    print_suite_header(suite_name, args)
    start = time.perf_counter()
    completed = subprocess.run(args, cwd=REPO_ROOT, check=False)
    duration = time.perf_counter() - start
    status = "passed" if completed.returncode == 0 else "failed"
    print(flush=True)
    print(f"{suite_name} Summary", flush=True)
    print(f"  status:   {status}", flush=True)
    print(f"  duration: {format_duration(duration)}", flush=True)
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

    if not args.list_e2e:
        print_run_header(run_unit, run_e2e, args.case)

    if run_unit:
        run_command(UNIT_TEST_CMD, "Unit Tests")
    if run_e2e:
        e2e_cmd = E2E_TEST_CMD[:]
        if args.case:
            for case in args.case:
                e2e_cmd.extend(["--case", case])
        if args.list_e2e:
            e2e_cmd.append("--list")
        if args.trace_commands:
            e2e_cmd.append("--trace-commands")
        run_command(e2e_cmd, "E2E Tests")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
