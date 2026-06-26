#!/usr/bin/env python3

from __future__ import annotations

import argparse
import os
import shlex
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
        sink = self.commands if self.trace_commands else None
        return run_command(args, cwd or self.root, env=self.env, check=check, trace=self.trace_commands, sink=sink)


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
        with OUTPUT_LOCK:
            print(f"    [cmd {status:<6} {format_duration(duration):>7}] {format_command(args)}", flush=True)
    if check and result.returncode != 0:
        fail_command(result)
    return result


def fail(message: str) -> None:
    raise E2EFailure(message)


def fail_command(result: CommandResult) -> None:
    lines = [
        "command failed",
        f"  cwd: {result.cwd}",
        f"  cmd: {format_command(result.args)}",
        f"  exit: {result.returncode}",
        f"  duration: {format_duration(result.duration)}",
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


def test_root_command(ctx: E2EContext) -> None:
    help_result = ctx.run(["carven"])
    require("Carven Language Toolchain" in help_result.stdout, "carven did not print root help")

    long_help = ctx.run(["carven", "--help"])
    require("Carven Language Toolchain" in long_help.stdout, "carven --help did not print root help")

    short_help = ctx.run(["carven", "-h"])
    require("Carven Language Toolchain" in short_help.stdout, "carven -h did not print root help")

    version = ctx.run(["carven", "--version"])
    require("carven 0.1.0" in version.stdout, "carven --version did not print version")

    short_version = ctx.run(["carven", "-V"])
    require("carven 0.1.0" in short_version.stdout, "carven -V did not print version")

    unknown = ctx.run(["carven", "nope"], check=False)
    require(unknown.returncode != 0, "unknown command unexpectedly succeeded")
    require("unknown command 'nope'" in unknown.stdout, "unknown command did not report command name")

    run_help = ctx.run(["carven", "run", "--help"])
    require("USAGE:" in run_help.stdout and "carven run" in run_help.stdout, "carven run --help did not print run help")

    missing_transpile_input = ctx.run(["carven", "transpile"], check=False)
    require(missing_transpile_input.returncode != 0, "transpile without input unexpectedly succeeded")
    require("carven transpile: error: no input file" in missing_transpile_input.stdout, "transpile missing input error changed")

    missing_init_path = ctx.run(["carven", "init"], check=False)
    require(missing_init_path.returncode != 0, "init without path unexpectedly succeeded")
    require("carven init: error: no project path" in missing_init_path.stdout, "init missing path error changed")

    run_forwarded_without_target = ctx.run(["carven", "run", "--", "--version"], check=False)
    require(run_forwarded_without_target.returncode != 0, "run forwarded args without target unexpectedly succeeded")
    require("explicit target" in run_forwarded_without_target.stdout, "run forwarded args error changed")


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

    args_source = project / "single_args.cv"
    write_file(
        args_source,
        """import std;

fn main(args) {
    std::println("single args");
    std::println("{}", std::ranges::distance(args));
    let first = *std::ranges::begin(args);
    std::println("{} {}", first.first, first.second);
}
""",
    )
    args_result = ctx.run(["carven", "run", path_text(args_source), "--", "--name", "Ada"], cwd=project)
    require("single args\n" in args_result.stdout, "single-file args target did not run")
    require("2\n" in args_result.stdout, "single-file run did not receive all forwarded args")
    require("1 --name\n" in args_result.stdout, "single-file run did not receive first forwarded arg")

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
    "root_command": test_root_command,
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
    ["root_command", "case_outputs", "transpile", "dump", "project_mode_requires_current_xmake", "init_project_structure"],
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


def validate_case_names(names: list[str]) -> bool:
    unknown = [name for name in names if name not in CASES]
    if not unknown:
        return True

    print("E2E Cases", file=sys.stderr)
    print(f"  status: failed", file=sys.stderr)
    print(f"  error:  unknown case{'s' if len(unknown) != 1 else ''}: {', '.join(unknown)}", file=sys.stderr)
    print(f"  cases:  {', '.join(CASES)}", file=sys.stderr)
    return False


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


def print_case_result(result: CaseResult, *, trace_commands: bool) -> None:
    command_count = len(result.commands)
    print_step_result(
        result.name,
        result.duration if trace_commands else None,
        failed=result.failure is not None,
        detail=f"{command_count} {'cmd' if command_count == 1 else 'cmds'}" if trace_commands else None,
        workspace=case_root(result.name) if result.failure else None,
    )
    if result.failure:
        print_failure_detail(result.failure)


def slowest_commands(results: list[CaseResult]) -> list[CommandResult]:
    commands = [command for result in results for command in result.commands]
    return sorted(commands, key=lambda command: command.duration, reverse=True)[:5]


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


def display_path(path: Path) -> str:
    try:
        return path.resolve().relative_to(REPO_ROOT.resolve()).as_posix()
    except ValueError:
        return path.as_posix()


def case_name_width() -> int:
    names = [*CASES, "setup.install_carven"]
    return max(len(name) for name in names)


def print_run_header(selected_cases: list[str], trace_commands: bool) -> None:
    mode = "selected" if selected_cases else "default"
    case_count = len(selected_cases) if selected_cases else len(CASES)
    case_text = f"{case_count} {mode} case"
    if case_count != 1:
        case_text += "s"

    print("E2E Cases")
    print(f"  cases:     {case_text}")
    if selected_cases:
        print(f"  filter:    {', '.join(selected_cases)}")
    print(f"  trace:     {'on' if trace_commands else 'off'}")
    print(f"  workspace: {display_path(E2E_ROOT)}")
    print()
    print("Steps")


def print_step_result(
    name: str,
    duration: float | None,
    *,
    failed: bool,
    detail: str | None = None,
    workspace: Path | None = None,
) -> None:
    status = "FAIL" if failed else "PASS"
    name_text = f"{name:<{case_name_width()}}" if duration is not None or detail else name
    duration_text = f"  {format_duration(duration):>7}" if duration is not None else ""
    detail_text = f"  {detail}" if detail else ""
    print(f"  [{status}] {name_text}{duration_text}{detail_text}", flush=True)
    if workspace is not None:
        print(f"         workspace: {display_path(workspace)}", flush=True)


def print_failure_detail(message: str, *, stream: object = sys.stdout) -> None:
    print("         error:", file=stream)
    for line in message.rstrip().splitlines():
        print(f"           {line}", file=stream)


def print_summary(results: list[CaseResult], duration: float, *, setup_failed: bool = False, trace_commands: bool = False) -> None:
    passed = [result for result in results if result.failure is None]
    failed = [result for result in results if result.failure is not None]
    status = "failed" if setup_failed or failed else "passed"

    print()
    print("Summary")
    print(f"  status:    {status}")
    print(f"  cases:     {len(results)} total, {len(passed)} passed, {len(failed)} failed")
    print(f"  setup:     {'failed' if setup_failed else 'passed'}")
    print(f"  duration:  {format_duration(duration)}")
    print(f"  workspace: {display_path(E2E_ROOT)}")

    if failed:
        print()
        print("Failed Cases")
        for result in failed:
            print(f"  {result.name}  workspace: {display_path(case_root(result.name))}")

    if trace_commands and results:
        print()
        print("Slowest Cases")
        for result in sorted(results, key=lambda item: item.duration, reverse=True)[:5]:
            print(f"  {format_duration(result.duration):>7}  {result.name}")

    commands = slowest_commands(results) if trace_commands else []
    if commands:
        print()
        print("Slowest Commands")
        for command in commands:
            print(f"  {format_duration(command.duration):>7}  {format_command(command.args)}")


def run_case_group(names: list[str], env: dict[str, str], trace_commands: bool) -> list[CaseResult]:
    results = []
    for name in names:
        result = run_case(name, env, trace_commands)
        with OUTPUT_LOCK:
            print_case_result(result, trace_commands=trace_commands)
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

    if args.case and not validate_case_names(args.case):
        return 1

    start = time.perf_counter()
    reset_workspace()
    print_run_header(args.case or [], args.trace_commands)
    setup_start = time.perf_counter()
    try:
        env = install_carven(args.trace_commands)
    except E2EFailure as failure:
        duration = time.perf_counter() - start
        print_step_result(
            "setup.install_carven",
            time.perf_counter() - setup_start if args.trace_commands else None,
            failed=True,
            workspace=E2E_ROOT,
        )
        print_failure_detail(str(failure))
        print_summary([], duration, setup_failed=True, trace_commands=args.trace_commands)
        return failure.returncode

    print_step_result("setup.install_carven", time.perf_counter() - setup_start if args.trace_commands else None, failed=False)
    try:
        results = run_case_group(args.case, env, args.trace_commands) if args.case else run_default_cases(env, args.trace_commands)
    except E2EFailure as failure:
        print(failure, file=sys.stderr)
        return failure.returncode

    print_summary(results, time.perf_counter() - start, trace_commands=args.trace_commands)

    for result in results:
        if result.failure is not None:
            return result.returncode
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
