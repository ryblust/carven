#!/bin/sh

set -eu

usage() {
    cat <<'EOF'
Usage: scripts/install.sh [--prefix <path>]

Build and install Carven with xmake.

Options:
  --prefix <path>  Install prefix.
  -h, --help       Show this help.

Without --prefix, the script installs to $HOME/.local and creates
$HOME/.local/bin if needed.

The script does not modify shell profiles, PATH, or other user environment
files. If the selected prefix's bin directory is not on PATH, it reports that
fact and leaves the decision to the user.
EOF
}

die() {
    printf 'install.sh: %s\n' "$1" >&2
    exit 1
}

prefix=

while [ "$#" -gt 0 ]; do
    case "$1" in
        --prefix)
            shift
            [ "$#" -gt 0 ] || die "--prefix requires a path"
            prefix="$1"
            ;;
        --prefix=*)
            prefix=${1#--prefix=}
            [ -n "$prefix" ] || die "--prefix requires a path"
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            die "unknown option: $1"
            ;;
    esac
    shift
done

if [ -z "$prefix" ]; then
    if [ -z "${HOME:-}" ]; then
        die "HOME is not set; pass an explicit --prefix"
    fi
    prefix=$HOME/.local
fi

case "$prefix" in
    "~")
        [ -n "${HOME:-}" ] || die "HOME is not set; pass an explicit --prefix"
        prefix=$HOME
        ;;
    "~/"*)
        [ -n "${HOME:-}" ] || die "HOME is not set; pass an explicit --prefix"
        prefix=$HOME/${prefix#"~/"}
        ;;
esac

command -v xmake >/dev/null 2>&1 || die "xmake was not found on PATH"

script_dir=$(CDPATH= cd "$(dirname "$0")" && pwd)
repo_root=$(CDPATH= cd "$script_dir/.." && pwd)

bin_dir=$prefix/bin
mkdir -p "$bin_dir"

printf 'Building Carven...\n'
(cd "$repo_root" && xmake build carven)

printf 'Installing Carven to %s...\n' "$prefix"
(cd "$repo_root" && xmake install -o "$prefix" carven)

carven_bin=$bin_dir/carven
rule_file=$prefix/share/carven/xmake/rules/carven.lua

printf '\nInstalled files:\n'
printf '  %s\n' "$carven_bin"
printf '  %s\n' "$rule_file"

printf '\nEnvironment:\n'
printf '  This script did not modify shell profiles, PATH, or other user files.\n'

case ":${PATH:-}:" in
    *:"$bin_dir":*)
        printf '  %s is already on PATH for this shell.\n' "$bin_dir"
        ;;
    *)
        printf '  %s is not on PATH for this shell.\n' "$bin_dir"
        printf '  Carven is installed, but the command name will not resolve until you choose how to expose that directory.\n'
        ;;
esac
