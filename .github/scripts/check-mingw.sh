#!/bin/sh
# Usage: check-mingw.sh <liblatte.dll>
# Fails unless the MinGW DLL imports only Windows system DLLs (so no libgcc,
# libwinpthread, libstdc++ or libcurl) and exports only latte_* names.
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

# The name is the last field of each row of the "[Ordinal/Name Pointer] Table".
exports=$(echo "$dump" | awk '/^\[Ordinal\/Name Pointer\] Table/ { t = 1; next }
                             t && /^[[:space:]]*$/ { t = 0 }
                             t && /\[/ { print $NF }')
[ -n "$exports" ] || { echo "no exports found"; rc=1; }
for sym in $exports; do
    case $sym in
        latte_*) ;;
        *) echo "disallowed export: $sym"; rc=1 ;;
    esac
done

[ "$rc" -eq 0 ] && echo "mingw dll check passed"
exit "$rc"
