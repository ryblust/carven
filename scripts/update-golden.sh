#!/usr/bin/env bash
set -euo pipefail

script_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
repo_root=$(cd "$script_dir/.." && pwd)

cd "$repo_root"
xmake build carven

for source in tests/golden/*.cv; do
    output=${source%.cv}.cpp
    xmake run carven transpile -o "$output" "$source"
done
