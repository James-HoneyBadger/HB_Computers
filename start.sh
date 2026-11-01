#!/usr/bin/env bash
#
# start.sh -- build what has not been built, then start a machine.
#
#   ./start.sh                     the window, on the Kestrel
#   ./start.sh kestrel16           the window, on the Kestrel 16
#   ./start.sh emu [arguments...]  the interactive emulator, either machine
#   ./start.sh cli [arguments...]  the command line, arguments passed through
#   ./start.sh test                every suite
#   ./start.sh build               build only
#
# The build is Release in ./build. The window needs Qt 6 Widgets; without it the
# machine, the tools and the tests still build, and `cli` and `emu` still run.

set -euo pipefail

root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
build="$root/build"

usage() {
    cat <<'TEXT'
start.sh -- build what is not built, then start a machine.

  ./start.sh                     the window, on the Kestrel
  ./start.sh kestrel16           the window, on the Kestrel 16
  ./start.sh emu [arguments...]  the interactive emulator; with no --model it
                                 asks which machine, and /model switches
  ./start.sh cli [arguments...]  the command line, arguments passed through
  ./start.sh test                every suite
  ./start.sh build               build only

The build is Release in ./build. The window needs Qt 6 Widgets; without it the
machine, the tools and the tests still build, and `cli` and `emu` still run.
TEXT
}

build_it() {
    if [ ! -f "$build/CMakeCache.txt" ]; then
        cmake -S "$root" -B "$build" -DCMAKE_BUILD_TYPE=Release
    fi
    local jobs
    jobs=$(nproc 2>/dev/null || echo 4)
    if [ "${1:-quiet}" = loud ]; then
        cmake --build "$build" -j "$jobs"
        return
    fi
    # Keep the machine's own output clean: a silent build, and the log only if it
    # went wrong.
    if ! cmake --build "$build" -j "$jobs" >"$build/start.log" 2>&1; then
        cat "$build/start.log" >&2
        echo "start.sh: the build failed; the log is $build/start.log" >&2
        exit 1
    fi
}

window() {
    build_it
    if [ ! -x "$build/kestrel_computers" ]; then
        echo "start.sh: the window was not built -- this build has no Qt 6 Widgets." >&2
        echo "start.sh: ./start.sh cli or ./start.sh emu starts a machine on a shell instead." >&2
        exit 1
    fi
    local model=$1
    shift
    exec "$build/kestrel_computers" --model "$model" "$@"
}

case "${1:-}" in
    -h|--help|help)
        usage
        ;;
    build)
        build_it loud
        ;;
    test)
        build_it loud
        ctest --test-dir "$build" --output-on-failure
        ;;
    cli)
        shift
        build_it
        exec "$build/kestrel" "$@"
        ;;
    emu)
        shift
        build_it
        exec "$build/kestrel_emu" "$@"
        ;;
    kestrel|kestrel16|kestrel-16)
        window "$@"
        ;;
    "")
        window kestrel
        ;;
    *)
        echo "start.sh: no such thing: $1" >&2
        usage >&2
        exit 2
        ;;
esac
