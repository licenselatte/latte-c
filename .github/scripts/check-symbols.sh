#!/bin/sh
# Usage: check-symbols.sh <build-dir>
# Fails when liblatte.a defines a global symbol, or the shared library exports a
# dynamic symbol, that does not start with "latte_" (the renamed internals
# "latte__*" count). Works with GNU and BSD/LLVM nm via the POSIX format (-P).
set -eu

dir=${1:?usage: check-symbols.sh <build-dir>}
rc=0

# Prints "<file>: <symbol>" for every offending symbol. $1 is the library,
# the remaining arguments are the nm options selecting the symbols to look at.
# POSIX format lines: "<name> <type> <value> <size>"; "U"/"u" is undefined and
# lower-case w/v are weak undefined, so only upper-case non-U types count.
offenders() {
    lib=$1; shift
    nm "$@" -P "$lib" | awk -v lib="$lib" -v os="$(uname -s)" '
        NF < 2 || $1 ~ /:$/ { next }            # archive member headers
        $2 !~ /^[A-TV-Z]$/ { next }              # keep defined global types only
        {
            name = $1
            if (os == "Darwin") sub(/^_/, "", name)   # Mach-O prefixes every C symbol
            # ld-provided ELF symbols that GNU-linked shared objects carry.
            if (name ~ /^(_init|_fini|_edata|_end|__bss_start)$/) next
            if (name !~ /^latte_/) print lib ": " $1
        }'
}

check() {
    out=$(offenders "$@")
    if [ -n "$out" ]; then
        echo "$out"
        rc=1
    fi
}

static=$(find "$dir" -name liblatte.a | head -n 1)
[ -n "$static" ] || { echo "liblatte.a not found under $dir" >&2; exit 1; }
check "$static" -g

shared=$(find "$dir" \( -name liblatte.so -o -name liblatte.dylib \) | head -n 1)
[ -n "$shared" ] || { echo "liblatte.so/.dylib not found under $dir" >&2; exit 1; }
if [ "$(uname -s)" = Darwin ]; then
    check "$shared" -gU
else
    check "$shared" -D --defined-only
fi

[ "$rc" -eq 0 ] && echo "symbol check passed"
exit "$rc"
