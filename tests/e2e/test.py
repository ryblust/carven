#!/usr/bin/env python3

from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
import threading
import time
from collections.abc import Callable
from concurrent.futures import ThreadPoolExecutor
from dataclasses import dataclass, field
from difflib import unified_diff
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
CASES_DIR = REPO_ROOT / "tests" / "cases"
E2E_ROOT = REPO_ROOT / "tests" / "e2e" / ".sandbox"
INSTALL_DIR = E2E_ROOT / "install"
CASES_ROOT = E2E_ROOT / "cases"


@dataclass(frozen=True)
class CommandResult:
    args: list[str]
    cwd: Path
    returncode: int
    stdout: str
    stderr: str
    duration: float


@dataclass
class E2EContext:
    root: Path
    env: dict[str, str]
    trace_commands: bool
    commands: list[CommandResult] = field(default_factory=list)

    def path(self, *parts: str) -> Path:
        return self.root.joinpath(*parts)

    def run(self, args: list[str], cwd: Path | None = None, *, check: bool = True) -> CommandResult:
        return run_command(args, cwd or self.root, env=self.env, check=check, trace=self.trace_commands, sink=self.commands)


@dataclass(frozen=True)
class CaseResult:
    name: str
    duration: float
    commands: list[CommandResult]
    failure: str | None = None
    returncode: int = 0


class E2EFailure(Exception):
    def __init__(self, message: str, returncode: int = 1) -> None:
        super().__init__(message)
        self.returncode = returncode


OUTPUT_LOCK = threading.Lock()


def path_text(path: Path) -> str:
    return path.as_posix()


def carven_exe() -> Path:
    suffix = ".exe" if os.name == "nt" else ""
    return INSTALL_DIR / "bin" / f"carven{suffix}"


def e2e_env() -> dict[str, str]:
    env = os.environ.copy()
    path_entries = [str(INSTALL_DIR / "bin")]
    if env.get("PATH"):
        path_entries.append(env["PATH"])

    env["PATH"] = os.pathsep.join(path_entries)
    return env


def run_command(
    args: list[str],
    cwd: Path,
    *,
    env: dict[str, str] | None = None,
    check: bool = True,
    trace: bool = False,
    sink: list[CommandResult] | None = None,
) -> CommandResult:
    cwd.mkdir(parents=True, exist_ok=True)
    start = time.perf_counter()
    completed = subprocess.run(
        args,
        cwd=cwd,
        env=env,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )
    duration = time.perf_counter() - start
    result = CommandResult(
        args=args,
        cwd=cwd,
        returncode=completed.returncode,
        stdout=completed.stdout,
        stderr=completed.stderr,
        duration=duration,
    )
    if sink is not None:
        sink.append(result)
    if trace:
        status = "ok" if result.returncode == 0 else f"exit {result.returncode}"
        print(f"e2e: command {status} ({duration:.2f}s): {' '.join(args)}", flush=True)
    if check and result.returncode != 0:
        fail_command(result)
    return result


def fail(message: str) -> None:
    raise E2EFailure(f"e2e: {message}")


def fail_command(result: CommandResult) -> None:
    lines = [
        "e2e: command failed",
        f"  cwd: {result.cwd}",
        f"  cmd: {' '.join(result.args)}",
        f"  exit: {result.returncode}",
        f"  duration: {result.duration:.2f}s",
    ]
    if result.stdout:
        lines.append("\n--- stdout ---")
        lines.append(result.stdout)
    if result.stderr:
        lines.append("\n--- stderr ---")
        lines.append(result.stderr)
    raise E2EFailure("\n".join(lines), result.returncode if result.returncode != 0 else 1)


def require(condition: bool, message: str) -> None:
    if not condition:
        fail(message)


def write_file(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8", newline="\n")


def reset_workspace() -> None:
    shutil.rmtree(E2E_ROOT, ignore_errors=True)
    INSTALL_DIR.mkdir(parents=True, exist_ok=True)
    CASES_ROOT.mkdir(parents=True, exist_ok=True)


def install_carven(trace_commands: bool) -> dict[str, str]:
    run_command(["xmake", "build", "carven"], REPO_ROOT, trace=trace_commands)
    run_command(["xmake", "install", "-o", path_text(INSTALL_DIR), "carven"], REPO_ROOT, trace=trace_commands)

    require(carven_exe().is_file(), f"missing installed carven executable: {carven_exe()}")
    require(not (INSTALL_DIR / "share" / "carven" / "xmake" / "rules" / "carven.lua").exists(), "carven.lua should not be installed under share")
    env = e2e_env()
    resolved = shutil.which("carven", path=env["PATH"])
    require(resolved is not None, "e2e PATH does not resolve carven")
    require(Path(resolved).resolve() == carven_exe().resolve(), f"e2e PATH resolves carven to {resolved}, expected {carven_exe()}")
    return env


def create_initialized_project(ctx: E2EContext, parent_name: str, project_name: str) -> Path:
    parent = ctx.path(parent_name)
    ctx.run(["carven", "init", project_name], cwd=parent)
    return parent / project_name


def test_transpile(ctx: E2EContext) -> None:
    source = ctx.path("transpile", "main.cv")
    write_file(source, "import std;\n\nfn main() {\n    std::println(\"transpile ok\");\n}\n")

    result = ctx.run(["carven", "transpile", path_text(source)])
    require(result.stdout.strip() != "", "transpile did not print generated source to stdout")


def test_dump(ctx: E2EContext) -> None:
    source = ctx.path("dump", "main.cv")
    write_file(source, "import std;\n\nfn main() {\n    std::println(\"dump ok\");\n}\n")

    tokens = ctx.run(["carven", "dump", "--only-tokens", path_text(source)])
    require(tokens.stdout.strip() != "", "dump --only-tokens did not print token stream")

    ast = ctx.run(["carven", "dump", "--only-ast", path_text(source)])
    require(ast.stdout.strip() != "", "dump --only-ast did not print AST")


def test_project_mode_requires_current_xmake(ctx: E2EContext) -> None:
    project = create_initialized_project(ctx, "current-xmake", "hello")
    child = project / "src"
    result = ctx.run(["carven", "build"], cwd=child, check=False)
    require(result.returncode != 0, "carven build from child directory unexpectedly succeeded")
    require("xmake.lua" in result.stdout, "missing xmake.lua error context")


def test_case_outputs(ctx: E2EContext) -> None:
    sources = sorted(CASES_DIR.glob("*.cv"))
    require(bool(sources), f"missing case inputs under {CASES_DIR}")

    generated_dir = ctx.path("generated")
    for source in sources:
        expected = source.with_suffix(".cpp")
        generated = generated_dir / expected.name
        require(expected.is_file(), f"missing expected C++ case: {expected}")

        result = ctx.run(["carven", "transpile", "-o", path_text(generated), path_text(source)])
        require(result.stdout == "", f"case transpile should not print generated source: {source.name}")

        expected_text = expected.read_text(encoding="utf-8")
        generated_text = generated.read_text(encoding="utf-8")
        if generated_text != expected_text:
            diff = "".join(
                unified_diff(
                    expected_text.splitlines(keepends=True),
                    generated_text.splitlines(keepends=True),
                    fromfile=path_text(expected),
                    tofile=path_text(generated),
                )
            )
            fail(f"generated C++ differs for {source.name}\n{diff}")


def test_init_project_structure(ctx: E2EContext) -> None:
    project = create_initialized_project(ctx, "init", "hello")
    require((project / "src" / "main.cv").is_file(), "carven init did not write src/main.cv")
    require((project / "xmake.lua").is_file(), "carven init did not write xmake.lua")
    require((project / "xmake" / "rules" / "carven.lua").is_file(), "carven init did not write xmake/rules/carven.lua")


def test_init_project_run(ctx: E2EContext) -> None:
    project = create_initialized_project(ctx, "init-run", "hello")
    ctx.run(["carven", "build"], cwd=project)
    result = ctx.run(["carven", "run", "hello"], cwd=project)
    require("Hello from Carven\n" in result.stdout, "initialized project did not run expected program")


def test_single_file_run(ctx: E2EContext) -> None:
    project = ctx.path("single-file")
    source = project / "single.cv"
    write_file(source, "import std;\n\nfn main() {\n    std::println(\"single file ok\");\n}\n")

    result = ctx.run(["carven", "run", path_text(source)], cwd=project)
    require("single file ok\n" in result.stdout, "single-file run did not print expected output")

    cache_root = project / ".carven"
    require(cache_root.exists(), f"single-file cache root was not created: {cache_root}")


def test_xmake_project(ctx: E2EContext) -> None:
    project = ctx.path("xmake-project")
    rule = project / "xmake" / "rules" / "carven.lua"
    generated_rule_source = create_initialized_project(ctx, "xmake-project-rule", "seed") / "xmake" / "rules" / "carven.lua"
    rule.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(generated_rule_source, rule)

    write_file(
        project / "xmake.lua",
        """set_project("carven-e2e-batch")
add_rules("mode.debug", "mode.release")
set_languages("c++23")
set_defaultmode("debug")

includes("xmake/rules/carven.lua")

target("args_probe")
    set_kind("binary")
    add_rules("carven")
    set_values("carven.standard", "c++23")
    add_files("args_probe.cv")
""",
    )
    write_file(
        project / "args_probe.cv",
        """import std;

fn main(args) {
    std::println("batch args");
    std::println("{}", std::ranges::distance(args));
    let first = *std::ranges::begin(args);
    std::println("{} {}", first.first, first.second);
}
""",
    )

    ctx.run(["xmake", "build", "-F", "xmake.lua"], cwd=project)
    args = ctx.run(["xmake", "run", "-F", "xmake.lua", "args_probe", "--name", "Ada", "Lovelace"], cwd=project)
    require("batch args\n" in args.stdout, "args_probe target did not print expected output")
    require("3\n" in args.stdout, "args_probe did not receive all forwarded args")
    require("1 --name\n" in args.stdout, "args_probe did not receive first forwarded arg")


CASES: dict[str, Callable[[E2EContext], None]] = {
    "case_outputs": test_case_outputs,
    "transpile": test_transpile,
    "dump": test_dump,
    "project_mode_requires_current_xmake": test_project_mode_requires_current_xmake,
    "init_project_structure": test_init_project_structure,
    "init_project_run": test_init_project_run,
    "single_file_run": test_single_file_run,
    "xmake_project": test_xmake_project,
}


DEFAULT_CASE_GROUPS = [
    ["xmake_project"],
    ["init_project_run"],
    ["single_file_run"],
    ["case_outputs", "transpile", "dump", "project_mode_requires_current_xmake", "init_project_structure"],
]


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(prog="tests/e2e/test.py")
    parser.add_argument("--case", action="append", metavar="NAME", help="run one e2e case; can be repeated")
    parser.add_argument("--list", action="store_true", help="list e2e cases")
    parser.add_argument("--trace-commands", action="store_true", help="print every subprocess and its duration")
    return parser.parse_args()


def print_case_list() -> None:
    for name in CASES:
        print(name)


def case_root(name: str) -> Path:
    return CASES_ROOT / name


def run_case(name: str, env: dict[str, str], trace_commands: bool) -> CaseResult:
    function = CASES.get(name)
    if function is None:
        available = ", ".join(CASES)
        fail(f"unknown e2e case: {name}\navailable cases: {available}")

    root = case_root(name)
    root.mkdir(parents=True, exist_ok=True)
    ctx = E2EContext(
        root=root,
        env=env,
        trace_commands=trace_commands,
    )

    start = time.perf_counter()
    try:
        function(ctx)
    except E2EFailure as failure:
        return CaseResult(
            name=name,
            duration=time.perf_counter() - start,
            commands=ctx.commands,
            failure=str(failure),
            returncode=failure.returncode,
        )

    return CaseResult(
        name=name,
        duration=time.perf_counter() - start,
        commands=ctx.commands,
    )


def print_case_result(result: CaseResult) -> None:
    status = "FAIL" if result.failure else "PASS"
    stream = sys.stderr if result.failure else sys.stdout
    print(f"e2e: {status} {result.name} ({result.duration:.2f}s)", file=stream, flush=True)
    if result.failure:
        print(result.failure, file=stream)
        print(f"e2e: workspace kept at {case_root(result.name)}", file=stream)


def slowest_commands(results: list[CaseResult]) -> list[CommandResult]:
    commands = [command for result in results for command in result.commands]
    return sorted(commands, key=lambda command: command.duration, reverse=True)[:5]


def print_summary(results: list[CaseResult], duration: float) -> None:
    passed = [result for result in results if result.failure is None]
    failed = [result for result in results if result.failure is not None]
    print("=" * 79)
    print(f"e2e test cases: {len(results):3} | {len(passed):3} passed | {len(failed)} failed")
    print(f"e2e duration:   {duration:.2f}s")
    print(f"e2e workspace:  {E2E_ROOT}")

    if results:
        print("e2e slowest cases:")
        for result in sorted(results, key=lambda item: item.duration, reverse=True)[:5]:
            print(f"  {result.duration:6.2f}s  {result.name}")

    commands = slowest_commands(results)
    if commands:
        print("e2e slowest commands:")
        for command in commands:
            print(f"  {command.duration:6.2f}s  {' '.join(command.args)}")


def run_case_group(names: list[str], env: dict[str, str], trace_commands: bool) -> list[CaseResult]:
    results = []
    for name in names:
        result = run_case(name, env, trace_commands)
        with OUTPUT_LOCK:
            print_case_result(result)
        results.append(result)
    return results


def run_default_cases(env: dict[str, str], trace_commands: bool) -> list[CaseResult]:
    results_by_name: dict[str, CaseResult] = {}
    with ThreadPoolExecutor(max_workers=len(DEFAULT_CASE_GROUPS)) as executor:
        futures = [
            executor.submit(run_case_group, group, env, trace_commands)
            for group in DEFAULT_CASE_GROUPS
        ]
        for future in futures:
            for result in future.result():
                results_by_name[result.name] = result

    return [results_by_name[name] for name in CASES]


def main() -> int:
    args = parse_args()
    if args.list:
        print_case_list()
        return 0

    start = time.perf_counter()
    reset_workspace()
    setup_start = time.perf_counter()
    try:
        env = install_carven(args.trace_commands)
    except E2EFailure as failure:
        duration = time.perf_counter() - start
        print(f"e2e: FAIL setup.install_carven ({time.perf_counter() - setup_start:.2f}s)", file=sys.stderr)
        print(failure, file=sys.stderr)
        print("=" * 79)
        print(f"e2e test cases:   0 |   0 passed | 1 failed")
        print(f"e2e duration:   {duration:.2f}s")
        print(f"e2e workspace:  {E2E_ROOT}")
        return failure.returncode

    print(f"e2e: PASS setup.install_carven ({time.perf_counter() - setup_start:.2f}s)")
    try:
        results = run_case_group(args.case, env, args.trace_commands) if args.case else run_default_cases(env, args.trace_commands)
    except E2EFailure as failure:
        print(failure, file=sys.stderr)
        return failure.returncode

    print_summary(results, time.perf_counter() - start)

    for result in results:
        if result.failure is not None:
            return result.returncode
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
