#!/usr/bin/env python3

from __future__ import annotations

import os
import shutil
import subprocess
import sys
from dataclasses import dataclass
from difflib import unified_diff
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
CASES_DIR = REPO_ROOT / "tests" / "cases"
E2E_ROOT = REPO_ROOT / "tests" / "e2e" / ".sandbox"
INSTALL_DIR = E2E_ROOT / "install"
PROJECTS_DIR = E2E_ROOT / "projects"
GENERATED_DIR = E2E_ROOT / "generated"


@dataclass(frozen=True)
class CommandResult:
    args: list[str]
    cwd: Path
    returncode: int
    stdout: str
    stderr: str


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


def run(args: list[str], cwd: Path = REPO_ROOT, *, check: bool = True, env: dict[str, str] | None = None) -> CommandResult:
    cwd.mkdir(parents=True, exist_ok=True)
    completed = subprocess.run(
        args,
        cwd=cwd,
        env=env,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )
    result = CommandResult(
        args=args,
        cwd=cwd,
        returncode=completed.returncode,
        stdout=completed.stdout,
        stderr=completed.stderr,
    )
    if check and result.returncode != 0:
        fail_command(result)
    return result


def fail(message: str) -> None:
    print(f"e2e: {message}", file=sys.stderr)
    print(f"e2e: workspace kept at {E2E_ROOT}", file=sys.stderr)
    raise SystemExit(1)


def fail_command(result: CommandResult) -> None:
    print("e2e: command failed", file=sys.stderr)
    print(f"  cwd: {result.cwd}", file=sys.stderr)
    print(f"  cmd: {' '.join(result.args)}", file=sys.stderr)
    print(f"  exit: {result.returncode}", file=sys.stderr)
    if result.stdout:
        print("\n--- stdout ---", file=sys.stderr)
        print(result.stdout, file=sys.stderr)
    if result.stderr:
        print("\n--- stderr ---", file=sys.stderr)
        print(result.stderr, file=sys.stderr)
    print(f"\ne2e: workspace kept at {E2E_ROOT}", file=sys.stderr)
    raise SystemExit(result.returncode if result.returncode != 0 else 1)


def require(condition: bool, message: str) -> None:
    if not condition:
        fail(message)


def write_file(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8", newline="\n")


def reset_workspace() -> None:
    shutil.rmtree(E2E_ROOT, ignore_errors=True)
    for path in (INSTALL_DIR, PROJECTS_DIR, GENERATED_DIR):
        path.mkdir(parents=True, exist_ok=True)


def install_carven() -> dict[str, str]:
    run(["xmake", "build", "carven"], cwd=REPO_ROOT)
    run(["xmake", "install", "-o", path_text(INSTALL_DIR), "carven"], cwd=REPO_ROOT)

    require(carven_exe().is_file(), f"missing installed carven executable: {carven_exe()}")
    require(not (INSTALL_DIR / "share" / "carven" / "xmake" / "rules" / "carven.lua").exists(), "carven.lua should not be installed under share")
    env = e2e_env()
    resolved = shutil.which("carven", path=env["PATH"])
    require(resolved is not None, "e2e PATH does not resolve carven")
    require(Path(resolved).resolve() == carven_exe().resolve(), f"e2e PATH resolves carven to {resolved}, expected {carven_exe()}")
    return env


def create_initialized_project(env: dict[str, str], parent_name: str, project_name: str) -> Path:
    parent = PROJECTS_DIR / parent_name
    run(["carven", "init", project_name], cwd=parent, env=env)
    return parent / project_name


def test_transpile(env: dict[str, str]) -> None:
    source = PROJECTS_DIR / "transpile" / "main.cv"
    output = PROJECTS_DIR / "transpile" / "out.cpp"
    write_file(source, "import std;\n\nfn main() {\n    std::println(\"transpile ok\");\n}\n")

    result = run(["carven", "transpile", "-o", path_text(output), path_text(source)], env=env)
    require(result.stdout == "", "transpile -o should not print generated source")
    generated = output.read_text(encoding="utf-8")
    require("std::println(\"transpile ok\");" in generated, "transpiled C++ does not contain expected println")


def test_dump(env: dict[str, str]) -> None:
    source = PROJECTS_DIR / "dump" / "main.cv"
    write_file(source, "import std;\n\nfn main() {\n    std::println(\"dump ok\");\n}\n")

    tokens = run(["carven", "dump", "--only-tokens", path_text(source)], env=env)
    require("StringLiteral" in tokens.stdout, "dump --only-tokens did not print token stream")
    require("dump ok" in tokens.stdout, "dump --only-tokens did not include source token text")

    ast = run(["carven", "dump", "--only-ast", path_text(source)], env=env)
    require("Import: std" in ast.stdout, "dump --only-ast did not print AST import")
    require("Call(Field(Ident(std)::println)" in ast.stdout, "dump --only-ast did not print expected call")


def test_single_file_run(env: dict[str, str]) -> None:
    project = PROJECTS_DIR / "single-file"
    source = project / "single.cv"
    write_file(source, "import std;\n\nfn main() {\n    std::println(\"single file ok\");\n}\n")

    result = run(["carven", "run", path_text(source)], cwd=project, env=env)
    require("single file ok\n" in result.stdout, "single-file run did not print expected output")

    cache_root = project / ".carven"
    require(cache_root.exists(), f"single-file cache root was not created: {cache_root}")
    require(any(cache_root.glob("scripts/*/xmake/rules/carven.lua")), "single-file cache project did not write embedded carven.lua")


def test_init_project(env: dict[str, str]) -> None:
    project = create_initialized_project(env, "init", "hello")
    require((project / "src" / "main.cv").is_file(), "carven init did not write src/main.cv")
    require((project / "xmake.lua").is_file(), "carven init did not write xmake.lua")
    rule = project / "xmake" / "rules" / "carven.lua"
    require(rule.is_file(), "carven init did not write xmake/rules/carven.lua")
    require("rule(\"carven\")" in rule.read_text(encoding="utf-8"), "generated carven.lua does not contain carven rule")

    run(["carven", "build"], cwd=project, env=env)
    result = run(["carven", "run", "app"], cwd=project, env=env)
    require("Hello from Carven\n" in result.stdout, "initialized project did not run expected program")


def test_xmake_project(env: dict[str, str]) -> None:
    project = PROJECTS_DIR / "xmake-project"
    rule = project / "xmake" / "rules" / "carven.lua"
    generated_rule_source = create_initialized_project(env, "xmake-project-rule", "seed") / "xmake" / "rules" / "carven.lua"
    rule.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(generated_rule_source, rule)

    write_file(
        project / "xmake.lua",
        """set_project("carven-e2e-batch")
add_rules("mode.debug", "mode.release")
set_languages("c++23")
set_defaultmode("debug")

includes("xmake/rules/carven.lua")

target("hello")
    set_kind("binary")
    add_rules("carven")
    set_values("carven.standard", "c++23")
    add_files("hello.cv")

target("args_probe")
    set_kind("binary")
    add_rules("carven")
    set_values("carven.standard", "c++23")
    add_files("args_probe.cv")
""",
    )
    write_file(project / "hello.cv", "import std;\n\nfn main() {\n    std::println(\"batch hello\");\n}\n")
    write_file(
        project / "args_probe.cv",
        """import std;

fn main(args) {
    std::println("{}", std::ranges::distance(args));
    let first = *std::ranges::begin(args);
    std::println("{} {}", first.first, first.second);
}
""",
    )

    run(["xmake", "build", "-F", "xmake.lua"], cwd=project, env=env)
    hello = run(["xmake", "run", "-F", "xmake.lua", "hello"], cwd=project, env=env)
    require("batch hello\n" in hello.stdout, "batch hello target did not print expected output")

    args = run(["xmake", "run", "-F", "xmake.lua", "args_probe", "--name", "Ada", "Lovelace"], cwd=project, env=env)
    require("3\n" in args.stdout, "args_probe did not receive all forwarded args")
    require("1 --name\n" in args.stdout, "args_probe did not receive first forwarded arg")


def test_project_mode_requires_current_xmake(env: dict[str, str]) -> None:
    project = create_initialized_project(env, "current-xmake", "hello")
    child = project / "src"
    result = run(["carven", "build"], cwd=child, env=env, check=False)
    require(result.returncode != 0, "carven build from child directory unexpectedly succeeded")
    require("cannot find xmake.lua in current directory" in result.stdout, "missing current-directory xmake error")


def test_case_outputs(env: dict[str, str]) -> None:
    sources = sorted(CASES_DIR.glob("*.cv"))
    require(bool(sources), f"missing case inputs under {CASES_DIR}")

    for source in sources:
        expected = source.with_suffix(".cpp")
        generated = GENERATED_DIR / expected.name
        require(expected.is_file(), f"missing expected C++ case: {expected}")

        result = run(["carven", "transpile", "-o", path_text(generated), path_text(source)], env=env)
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


def main() -> int:
    if len(sys.argv) != 1:
        fail("unexpected command-line arguments")

    reset_workspace()
    env = install_carven()

    test_case_outputs(env)
    test_transpile(env)
    test_dump(env)
    test_single_file_run(env)
    test_init_project(env)
    test_xmake_project(env)
    test_project_mode_requires_current_xmake(env)
    print(f"e2e: ok (workspace kept at {E2E_ROOT})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
