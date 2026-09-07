#!/usr/bin/env python3
"""Measure structured analysis workloads without timing assertions."""

from __future__ import annotations

import argparse
import statistics
import subprocess
import tempfile
import time
from pathlib import Path


def workloads() -> list[tuple[str, str]]:
    cases: list[tuple[str, str]] = []
    for count in (64, 128, 256):
        independent = [f"fn f{i}(x: i32) -> i32 {{ return x; }}" for i in range(count)]
        chain = [
            f"fn f{i}(x: i32) -> i32 {{ return "
            + (f"f{i + 1}(x);" if i + 1 < count else "x;")
            + " }"
            for i in range(count)
        ]
        for name, declarations in (
            ("independent", independent),
            ("caller_first", chain),
            ("callee_first", list(reversed(chain))),
        ):
            cases.append((f"{name}_{count}", "\n".join(declarations)
                          + "\nfn main() { let _ = f0(1); }\n"))
    for depth in (4, 8, 12, 16):
        body = "x += 1;"
        for _ in range(depth):
            body = "while flag { " + body + " break; }"
        cases.append((f"terminating_loops_{depth}",
                      "fn probe(flag: bool, &x: i32) { " + body
                      + " }\nfn main() { var x = 0; probe(false, &x); }\n"))
    return cases


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("compiler", type=Path, help="built Carven executable")
    parser.add_argument("--samples", type=int, default=3)
    parser.add_argument("--warmups", type=int, default=1)
    args = parser.parse_args()
    compiler = args.compiler.resolve()
    if not compiler.is_file() or args.samples < 1 or args.warmups < 0:
        parser.error("provide an existing compiler, positive samples and nonnegative warmups")
    print(f"Compiler: {compiler}")
    print(f"Sampling: {args.warmups} warmup + {args.samples} measured runs")
    print("End-to-end source-to-C++ timings; compare the same machine and build mode.")
    with tempfile.TemporaryDirectory(prefix="carven-analysis-") as directory:
        root = Path(directory)
        for name, source in workloads():
            path = root / f"{name}.cv"
            path.write_text(source, encoding="utf-8")
            samples = []
            for ordinal in range(args.warmups + args.samples):
                started = time.perf_counter()
                result = subprocess.run(
                    [str(compiler), "--stdout", path.name], cwd=root,
                    stdout=subprocess.DEVNULL, stderr=subprocess.PIPE,
                    text=True, timeout=60, check=False,
                )
                if result.returncode:
                    raise RuntimeError(f"{name}: {result.stderr}")
                if ordinal >= args.warmups:
                    samples.append(time.perf_counter() - started)
            print(f"{name:>24}: {statistics.median(samples) * 1000:9.2f} ms", flush=True)


if __name__ == "__main__":
    main()
