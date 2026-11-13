#!/bin/sh
# Usage: check-mingw.sh <liblatte.dll>
# Fails unless the MinGW DLL imports only Windows system DLLs (no libgcc,
# libwinpthread, libstdc++, libcurl, libsodium) and exports only latte_* names.
set -eu

dll=${1:?usage: check-mingw.sh <liblatte.dll>}
dump=$(objdump -p "$dll")
rc=0

for name in $(echo "$dump" | sed -n 's/^[[:space:]]*DLL Name: //p'); do
    case $(echo "$name" | tr 'A-Z' 'a-z') in
        # The UCRT (api-ms-win-crt-*) is a Windows component, msvcrt.dll too.
        kernel32.dll|advapi32.dll|winhttp.dll|msvcrt.dll|api-ms-win-crt-*.dll) ;;
        *) echo "disallowed import: $name"; rc=1 ;;
    esac
done

exports=$(echo "$dump" | sed -n 's/^[[:space:]]*\[[[:space:]]*[0-9]*\][[:space:]]*//p')
[ -n "$exports" ] || { echo "no exports found"; rc=1; }
for sym in $exports; do
    case $sym in
        latte_*) ;;
        *) echo "disallowed export: $sym"; rc=1 ;;
    esac
done

[ "$rc" -eq 0 ] && echo "mingw dll check passed"
exit "$rc"
