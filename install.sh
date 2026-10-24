#!/bin/sh
# Install a complete, versioned Carven toolchain. No root privileges or Node needed.
set -eu

prefix="${XDG_DATA_HOME:-$HOME/.local/share}/carven"
bin_dir="$HOME/.local/bin"
version=
origin=https://github.com/ryblust/carven/releases

fail() { printf '%s\n' "carven: $*" >&2; exit 1; }

while [ "$#" -gt 0 ]; do
    case "$1" in
        --prefix|--bin-dir|--version)
            [ "$#" -ge 2 ] || fail "missing argument for $1"
            case "$1" in
                --prefix) prefix=$2 ;;
                --bin-dir) bin_dir=$2 ;;
                --version) version=$2 ;;
            esac
            shift 2 ;;
        --help|-h)
            printf '%s\n' 'Usage: sh install.sh [--version X.Y.Z] [--prefix DIR] [--bin-dir DIR]' \
                'Defaults: latest release, $XDG_DATA_HOME/carven (or ~/.local/share/carven), with links in ~/.local/bin.'
            exit 0 ;;
        *) fail "unknown option: $1" ;;
    esac
done

while [ "${prefix%/}" != "$prefix" ] && [ "$prefix" != / ]; do prefix=${prefix%/}; done
while [ "${bin_dir%/}" != "$bin_dir" ] && [ "$bin_dir" != / ]; do bin_dir=${bin_dir%/}; done
case "$prefix" in /*) ;; *) fail '--prefix must be absolute' ;; esac
case "$bin_dir" in /*) ;; *) fail '--bin-dir must be absolute' ;; esac
[ "$prefix" != / ] || fail 'cannot install at /'
case "$bin_dir/" in "$prefix/"*) fail '--bin-dir must be outside the toolchain prefix' ;; esac
[ ! -e "$prefix" ] || [ -f "$prefix/.carven-toolchain" ] || fail "refusing to replace an unowned directory: $prefix"
mkdir -p "$(dirname "$prefix")" "$bin_dir"
work=$(mktemp -d "$(dirname "$prefix")/.carven-install.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM

case "$(uname -s)" in Darwin) platform=darwin ;; Linux) platform=linux ;; *) fail 'unsupported operating system' ;; esac
case "$(uname -m)" in arm64|aarch64) arch=arm64 ;; x86_64|amd64) arch=x64 ;; *) fail 'unsupported architecture' ;; esac
release="$origin/latest/download"
if [ -n "$version" ]; then
    case "$version" in *[!0-9.]*) fail 'expected a numeric release version' ;; esac
    release="$origin/download/v$version"
fi
name="carven-$platform-$arch.tar.gz"
archive="$work/$name"
curl -fsSL "$release/$name" -o "$archive"
curl -fsSL "$release/$name.sha256" -o "$archive.sha256"

expected=$(awk 'NR == 1 {print $1}' "$archive.sha256")
if command -v sha256sum >/dev/null 2>&1; then
    actual=$(sha256sum "$archive" | awk '{print $1}')
else
    actual=$(shasum -a 256 "$archive" | awk '{print $1}')
fi
[ "$expected" = "$actual" ] || fail 'archive checksum mismatch'

mkdir "$work/toolchain"
tar -tzf "$archive" | awk '/^\// || /(^|\/)\.\.(\/|$)/ {exit 1}' || fail 'unsafe archive path'
tar -xzf "$archive" -C "$work/toolchain"
[ -f "$work/toolchain/.carven-toolchain" ] || fail 'not a Carven toolchain archive'
for name in carven carven-format carven-lsp carven-analyzer; do
    if [ -e "$bin_dir/$name" ] || [ -L "$bin_dir/$name" ]; then
        [ -L "$bin_dir/$name" ] && [ "$(readlink "$bin_dir/$name")" = "$prefix/bin/$name" ] || fail "refusing to replace $bin_dir/$name"
    fi
done
"$work/toolchain/bin/carven" --version

if [ -e "$prefix" ]; then mv "$prefix" "$work/previous"; fi
if ! mv "$work/toolchain" "$prefix"; then
    if [ -e "$work/previous" ]; then mv "$work/previous" "$prefix"; fi
    fail 'could not publish toolchain'
fi
for name in carven carven-format carven-lsp carven-analyzer; do
    ln -sfn "$prefix/bin/$name" "$bin_dir/$name"
done
printf '%s\n' "Installed Carven in $prefix" "Add $bin_dir to PATH, then install the separate carven-lang.carven-vscode VS Code extension."
