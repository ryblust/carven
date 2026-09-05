#!/usr/bin/env python3
"""Report the Carven build-workflow performance pulse.

The workload and measurement contract are defined in benchmarks/README.md.
"""

from __future__ import annotations

import argparse
import json
import re
import statistics
import subprocess
import tempfile
import time
from pathlib import Path


SMALL_BATCH = 16
LARGE_BATCH = 128
DEFAULT_SAMPLES = 5
DEFAULT_WARMUPS = 1

LIBRARY = """export struct Value { number: i32, }
private fn adjust(number: i32) -> i32 { return number + 1; }
export fn make_value(number: i32) -> Value {
    return Value { number: adjust(number) };
}
"""

FACADE = """import library using { Value, make_value, };
export fn create_value(number: i32) -> Value { return make_value(number); }
"""

APP = """import facade using create_value;
fn main() { let value = create_value(41); }
"""


def run(command: list[str], root: Path) -> str:
    try:
        completed = subprocess.run(
            command,
            cwd=root,
            capture_output=True,
            text=True,
            check=False,
        )
    except OSError as error:
        raise RuntimeError(f"cannot run {command[0]}: {error}") from error
    if completed.returncode != 0:
        output = completed.stdout + completed.stderr
        raise RuntimeError(f"command failed: {' '.join(command)}\n{output}")
    return completed.stdout


def write_batch(root: Path, count: int) -> list[str]:
    inputs: list[str] = []
    for index in range(count):
        relative = f"module_{index:03d}.cv"
        source = f"export struct Value{index:03d} {{ value: i32, }}\n"
        (root / relative).write_text(source, encoding="utf-8")
        inputs.append(relative)
    return inputs


def batch_sample(
    compiler: Path,
    root: Path,
    inputs: list[str],
    count: int,
    ordinal: int,
) -> float:
    command = [
        str(compiler),
        "--output-dir",
        f"out-{ordinal:03d}",
        f"--linkage-domain=benchmark:build-pulse:{count}",
        *inputs,
    ]
    started = time.perf_counter_ns()
    run(command, root)
    return (time.perf_counter_ns() - started) / 1_000_000_000


def measure_batch(
    compiler: Path,
    count: int,
    samples: int,
    warmups: int,
) -> list[float]:
    with tempfile.TemporaryDirectory(prefix=f"carven-batch-{count}-") as temporary:
        root = Path(temporary)
        inputs = write_batch(root, count)
        for ordinal in range(warmups):
            batch_sample(compiler, root, inputs, count, -(ordinal + 1))
        return [
            batch_sample(compiler, root, inputs, count, ordinal)
            for ordinal in range(samples)
        ]


def lua_string(path: Path) -> str:
    return json.dumps(str(path.resolve()))


def write_edit_project(
    root: Path,
    compiler: Path,
    rules_repo: Path,
    crafts: Path,
) -> None:
    (root / "library.cv").write_text(LIBRARY, encoding="utf-8")
    (root / "facade.cv").write_text(FACADE, encoding="utf-8")
    (root / "app.cv").write_text(APP, encoding="utf-8")
    repository_spec = json.dumps(f"carven-benchmark {rules_repo.resolve()}")
    project = f"""set_project(\"carven-benchmark\")
add_rules(\"mode.debug\")
add_repositories({repository_spec})
add_requires(\"carven-benchmark@carven\", {{alias = \"carven\", system = false, configs = {{rules_only = true}}}})

target(\"bench\")
    set_kind(\"binary\")
    set_languages(\"c++20\")
    add_packages(\"carven\")
    add_rules(\"@carven/carven\", {{linkage_domain = \"benchmark:build-pulse:edit\"}})
    set_values(\"carven.program\", {lua_string(compiler)})
    set_values(\"carven.includedir\", {lua_string(crafts)})
    add_files(\"*.cv\")
"""
    (root / "xmake.lua").write_text(project, encoding="utf-8")


def object_mtimes(root: Path) -> dict[str, int]:
    objects = list(root.rglob("*.o")) + list(root.rglob("*.obj"))
    return {str(path.relative_to(root)): path.stat().st_mtime_ns for path in objects}


def measure_private_edit(
    compiler: Path,
    rules_repo: Path,
    crafts: Path,
    xmake: str,
) -> tuple[list[str], int]:
    with tempfile.TemporaryDirectory(prefix="carven-private-edit-") as temporary:
        root = Path(temporary)
        write_edit_project(root, compiler, rules_repo, crafts)
        run([xmake, "f", "-m", "debug"], root)
        run([xmake, "build", "bench"], root)
        before = object_mtimes(root)
        if not before:
            raise RuntimeError("warm build produced no C++ object files")

        library = root / "library.cv"
        source = library.read_text(encoding="utf-8")
        library.write_text(
            source.replace("return number + 1", "return number + 2"),
            encoding="utf-8",
        )
        run([xmake, "build", "bench"], root)
        after = object_mtimes(root)

    changed = sorted(
        path for path in before.keys() | after.keys() if before.get(path) != after.get(path)
    )
    return changed, len(after)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--xmake", default="xmake")
    parser.add_argument("--samples", type=int, default=DEFAULT_SAMPLES)
    parser.add_argument("--warmups", type=int, default=DEFAULT_WARMUPS)
    parser.add_argument("--verbose", action="store_true")
    return parser.parse_args()


def milliseconds(seconds: float) -> str:
    return f"{seconds * 1_000:.2f} ms"


def resolve_compiler(repository: Path, xmake: str) -> Path:
    output = run([xmake, "show", "-t", "carven", "--format=json"], repository)
    try:
        target = json.loads(output)
        targetfile = target["targetfile"]
    except (json.JSONDecodeError, KeyError, TypeError) as error:
        raise RuntimeError("cannot read the configured Carven target from Xmake") from error
    compiler = Path(targetfile)
    if not compiler.is_absolute():
        compiler = repository / compiler
    compiler = compiler.resolve()

    if not compiler.is_file():
        raise SystemExit(f"Carven compiler does not exist: {compiler}")
    return compiler


def cache_string(text: str, key: str) -> str | None:
    pattern = rf'^\s*{re.escape(key)}\s*=\s*("(?:\\.|[^"\\])*")'
    match = re.search(pattern, text, re.MULTILINE)
    return json.loads(match.group(1)) if match else None


def resolve_rules_repo(repository: Path) -> Path:
    for cache in sorted((repository / ".xmake").glob("*/*/cache/package")):
        package_cache = cache.read_text(encoding="utf-8")
        require_string = cache_string(package_cache, "__requirestr")
        install_dir = cache_string(package_cache, "installdir")
        if not require_string or not require_string.endswith("@carven") or not install_dir:
            continue

        repository_name = require_string.rsplit("@", 1)[0]
        cached_repository = cache.parent.parent / "repositories" / repository_name
        installed_manifest = Path(install_dir) / "manifest.txt"
        candidates = [cached_repository]
        if installed_manifest.is_file():
            manifest = installed_manifest.read_text(encoding="utf-8")
            repository_url = cache_string(manifest, "url")
            if repository_url:
                candidates.append(Path(repository_url))

        for candidate in candidates:
            rules_repo = candidate.resolve()
            if (rules_repo / "packages/c/carven/rules/carven.lua").is_file():
                return rules_repo

    raise SystemExit(
        "configured Carven Xmake rules are unavailable; configure and build the project first"
    )


def main() -> int:
    args = parse_args()
    repository = Path(__file__).resolve().parents[1]
    crafts = repository / "crafts"

    if args.samples < 1 or args.warmups < 0:
        raise SystemExit("samples must be positive and warmups cannot be negative")
    compiler = resolve_compiler(repository, args.xmake)
    rules_repo = resolve_rules_repo(repository)

    small_samples = measure_batch(compiler, SMALL_BATCH, args.samples, args.warmups)
    large_samples = measure_batch(compiler, LARGE_BATCH, args.samples, args.warmups)
    small_median = statistics.median(small_samples)
    large_median = statistics.median(large_samples)
    scale_ratio = large_median / small_median
    changed_objects, object_count = measure_private_edit(
        compiler,
        rules_repo,
        crafts,
        args.xmake,
    )

    print(f"{'Compiler'.ljust(9)} {compiler}")
    print(
        f"{'Sampling'.ljust(9)} "
        f"{args.warmups} warmup + {args.samples} measured runs"
    )
    print(
        f"{'Mode'.ljust(9)} "
        "observational; compare on the same machine and build mode"
    )
    print()
    print("Fresh batch")
    print("  Modules      Median")
    print(f"  {SMALL_BATCH:>7}    {milliseconds(small_median):>10}")
    print(f"  {LARGE_BATCH:>7}    {milliseconds(large_median):>10}")
    print(
        f"  {'Growth':>7}    "
        f"{scale_ratio:.2f}x for {LARGE_BATCH // SMALL_BATCH}x input"
    )
    print()
    print("Private edit")
    print(f"  {'Rebuilt':>7}    {len(changed_objects)} / {object_count} objects")
    print()
    print("Use --verbose to show individual samples and object paths.")

    if args.verbose:
        print()
        print("Details")
        print(
            f"  {SMALL_BATCH:>3} modules    "
            + ", ".join(map(milliseconds, small_samples))
        )
        print(
            f"  {LARGE_BATCH:>3} modules    "
            + ", ".join(map(milliseconds, large_samples))
        )
        if changed_objects:
            print("  Recompiled objects")
            for path in changed_objects:
                print(f"    {path}")

    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except RuntimeError as error:
        raise SystemExit(str(error)) from error
