#!/usr/bin/env bash
#
# start.sh -- build what is out of date, then start a machine.
#
#   ./start.sh                     the window, on the Kestrel
#   ./start.sh kestrel16           the window, on the Kestrel 16
#   ./start.sh emu [arguments...]  the interactive emulator, either machine
#   ./start.sh cli [arguments...]  the command line, arguments passed through
#   ./start.sh test                every suite
#   ./start.sh build               build only
#
# The programs are built in release, because a machine paced at eight megahertz
# needs the speed; the tests are built in debug, because they are run far more
# often than the window is opened.
#
# The ROMs are not built by this script at all: the programs assemble them from
# roms/*.s when they start.

set -euo pipefail

root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
export PATH="$HOME/.cargo/bin:$PATH"

usage() {
    cat <<'TEXT'
start.sh -- build what is out of date, then start a machine.

  ./start.sh                     the window, on the Kestrel
  ./start.sh kestrel16           the window, on the Kestrel 16
  ./start.sh emu [arguments...]  the interactive emulator; with no --model it
                                 asks which machine, and /model switches
  ./start.sh cli [arguments...]  the command line, arguments passed through
  ./start.sh test                every suite
  ./start.sh build               build only

Everything is in rust/, and cargo builds it. The ROMs need no build: the
programs assemble them from roms/*.s when they start.
TEXT
}

build_it() {
    cd "$root/rust"
    local profile=${1:-release}
    cargo build --offline --profile "$profile" --quiet
}

case "${1:-}" in
    -h|--help|help)
        usage
        ;;
    build)
        build_it release
        echo "start.sh: built rust/target/release"
        ;;
    test)
        cd "$root/rust"
        cargo test --offline
        ;;
    cli)
        shift
        build_it release
        exec "$root/rust/target/release/kestrel" "$@"
        ;;
    emu)
        shift
        build_it release
        exec "$root/rust/target/release/kestrel_emu" "$@"
        ;;
    kestrel|kestrel16|kestrel-16)
        build_it release
        exec "$root/rust/target/release/kestrel_computers" --model "$1"
        ;;
    "")
        build_it release
        exec "$root/rust/target/release/kestrel_computers" --model kestrel
        ;;
    *)
        echo "start.sh: no such thing: $1" >&2
        usage >&2
        exit 2
        ;;
esac
