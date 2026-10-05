#!/usr/bin/env python3
"""Generate, build, and measure the frozen and current Crafts implementations."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import shutil
import subprocess
import time


def digest_tree(root):
    digest = hashlib.sha256()
    for path in sorted(root.rglob("*")):
        if path.is_file():
            digest.update(path.relative_to(root).as_posix().encode())
            digest.update(b"\0")
            digest.update(path.read_bytes())
    return digest.hexdigest()


def isolate_runtime(text, side):
    # Staged providers use qualified namespace declarations; reject an unhandled form.
    if re.search(r"\bnamespace\s+runtime\b", text):
        raise RuntimeError("unqualified runtime namespace declaration in benchmark staging")
    return re.sub(r"\bcarven\s*::\s*runtime\b", f"carven::benchmark_runtime_{side}", text)


def isolate_runtime_tree(root, side):
    for path in root.rglob("*"):
        if path.suffix in {".hpp", ".cpp"}:
            path.write_text(isolate_runtime(path.read_text(), side))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", type=Path, required=True)
    parser.add_argument("--baseline", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--cxx", default=shutil.which("clang++"))
    parser.add_argument("--cxx-flag", action="append", default=[])
    parser.add_argument("--samples", type=int, default=7)
    parser.add_argument("--batch-ms", type=float, default=10)
    parser.add_argument("--case-prefix", default="", help="measure only cases with this name prefix")
    args = parser.parse_args()
    if args.samples < 1 or args.samples % 2 == 0 or args.batch_ms <= 0:
        parser.error("samples must be positive and odd; batch-ms must be positive")
    source = Path(__file__).resolve().parent
    repository = source.parent.parent
    compiler = args.compiler.resolve(strict=True)
    baseline = args.baseline.resolve(strict=True)
    output = args.output.resolve()
    if output.exists():
        parser.error("output already exists; select a fresh directory")
    if not (baseline / "carven/runtime/runtime.hpp").is_file():
        parser.error("baseline must contain carven/runtime/runtime.hpp")
    if not args.cxx:
        parser.error("clang++ was not found; specify --cxx")
    cxx = shutil.which(args.cxx) or args.cxx
    output.mkdir(parents=True)
    flags = ["-std=c++20", "-O3", "-DNDEBUG", "-fno-exceptions", "-fno-rtti"] + args.cxx_flag
    metadata = {
        "host": {"platform": platform.platform(), "machine": platform.machine(),
                 "processor": platform.processor(), "logical_cpus": os.cpu_count()},
        "compiler": str(compiler), "compiler_sha256": hashlib.sha256(compiler.read_bytes()).hexdigest(),
        "cxx": cxx, "flags": flags, "lto": False,
        "harness_sha256": digest_tree(source),
        "runtime_namespace_isolation": {
            side: f"carven::benchmark_runtime_{side}" for side in ("baseline", "current")
        },
        "staged_harness_sha256": {},
        "sampling": {"samples": args.samples, "batch_ms": args.batch_ms, "warmups": 2,
                     "input": "repeatedly accessed, prepared and checked outside timing",
                     "thread_qos": "user_initiated" if platform.system() == "Darwin" else "default",
                     "case_prefix": args.case_prefix},
        "sources": {}, "commands": [], "build": {}, "measurements": {},
    }
    metadata_file = output / "metadata.json"

    def save():
        metadata_file.write_text(json.dumps(metadata, indent=2) + "\n")

    def command(argv, cwd, log):
        argv = [str(value) for value in argv]
        record = {"argv": argv, "cwd": str(cwd), "log": str(log.relative_to(output))}
        metadata["commands"].append(record)
        save()
        start = time.perf_counter()
        result = subprocess.run(argv, cwd=cwd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                text=True, check=False)
        record["seconds"] = time.perf_counter() - start
        record["returncode"] = result.returncode
        log.parent.mkdir(parents=True, exist_ok=True)
        log.write_text(result.stdout)
        save()
        if result.returncode:
            raise RuntimeError(f"command failed ({result.returncode}); see {log}\n{result.stdout[-5000:]}")
        return result.stdout

    metadata["cxx_version"] = command([cxx, "--version"], output, output / "cxx-version.log").strip()
    if platform.system() == "Darwin":
        metadata["host"]["cpu"] = command(["sysctl", "-n", "machdep.cpu.brand_string"], output,
                                           output / "cpu.log").strip()
    projects = {}
    for side, crafts in (("baseline", baseline), ("current", repository / "crafts")):
        root = output / side
        toolchain = root / "toolchain"
        (toolchain / "bin").mkdir(parents=True)
        shutil.copy2(compiler, toolchain / "bin/carven")
        staged_crafts = toolchain / "crafts"
        shutil.copytree(crafts, staged_crafts)
        original_crafts_sha256 = digest_tree(staged_crafts)
        project = root / "project"
        (project / side).mkdir(parents=True)
        shutil.copy2(source / "api.cv", project / side / "api.cv")
        generated = root / "generated"
        command([toolchain / "bin/carven", "compile", f"{side}/api.cv", "--output-dir", generated,
                 f"--linkage-domain=benchmark:crafts:{side}"], project, root / "generate.log")
        original_generated_sha256 = digest_tree(generated)
        isolate_runtime_tree(staged_crafts, side)
        isolate_runtime_tree(generated, side)
        projects[side] = (root, generated, staged_crafts)
        generated_sources = sorted(generated.rglob("*.cpp"))
        metadata["sources"][side] = {
            "crafts_sha256": original_crafts_sha256,
            "staged_crafts_sha256": digest_tree(staged_crafts),
            "generated_original_sha256": original_generated_sha256,
            "generated_sha256": digest_tree(generated),
            "generated_cpp_files": len(generated_sources),
            "generated_cpp_bytes": sum(path.stat().st_size for path in generated_sources),
        }
        save()
    for backend in ("native", "portable"):
        build = output / backend
        build.mkdir()
        backend_flags = ["-DCARVEN_SIMD_FORCE_SCALAR"] if backend == "portable" else []
        objects = []
        metadata["staged_harness_sha256"][backend] = {}
        for side, (root, generated, crafts) in projects.items():
            side_build = build / side
            side_build.mkdir()
            adapter = side_build / "adapter.cpp"
            adapter.write_text(isolate_runtime(
                (source / "adapter.cpp").read_text().replace("BENCH_SIDE", side), side))
            metadata["staged_harness_sha256"][backend][f"adapter_{side}"] = hashlib.sha256(
                adapter.read_bytes()).hexdigest()
            sources = sorted(generated.rglob("*.cpp")) + sorted(crafts.rglob("*.cpp")) + [adapter]
            start = time.perf_counter()
            for index, path in enumerate(sources):
                object_path = side_build / f"unit-{index}.o"
                command([cxx, *flags, *backend_flags, "-I", generated, "-I", crafts,
                         "-c", path, "-o", object_path], side_build,
                        side_build / f"unit-{index}.log")
                objects.append(object_path)
            metadata["build"][f"{backend}/{side}"] = {
                "seconds": time.perf_counter() - start, "translation_units": len(sources),
                "object_bytes": sum(path.stat().st_size for path in side_build.glob("*.o")),
            }
            save()
        current_crafts = projects["current"][2]
        staged_main = build / "main.cpp"
        staged_main.write_text(isolate_runtime((source / "main.cpp").read_text(), "current"))
        metadata["staged_harness_sha256"][backend]["main"] = hashlib.sha256(
            staged_main.read_bytes()).hexdigest()
        executable = build / "benchmark"
        command([cxx, *flags, *backend_flags, "-I", current_crafts, staged_main,
                 *objects, "-o", executable], build, build / "link.log")
        run_output = command([executable, build, args.samples, args.batch_ms, args.case_prefix], build, build / "run.log")
        metadata["measurements"][backend] = {
            "backend": "portable" if backend == "portable" else (
                "neon" if platform.machine().lower() in ("arm64", "aarch64") else
                "avx2" if "-mavx2" in flags else "portable"),
            "output": run_output.strip(), "executable_bytes": executable.stat().st_size,
        }
        save()
        print(f"{backend}: {run_output.strip()}")
    print(f"Results: {output}")


if __name__ == "__main__":
    main()
