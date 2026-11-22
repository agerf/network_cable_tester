#!/usr/bin/env bash
# Baut/flasht eine Rolle (master|slave) mit eigenem Build- und sdkconfig-Verzeichnis.
#
# Beispiele:
#   scripts/idf_role.sh master build
#   scripts/idf_role.sh slave  flash -p /dev/ttyUSB1
#   scripts/idf_role.sh master monitor -p /dev/ttyUSB0
set -euo pipefail

if ! command -v idf.py >/dev/null 2>&1; then
    echo "idf.py nicht im PATH. Zuerst die ESP-IDF-Umgebung laden:" >&2
    echo "  . \$HOME/esp/esp-idf/export.sh" >&2
    exit 1
fi

role="${1:?Aufruf: $0 <master|slave> <idf.py-unterbefehl> [args...]}"
shift
case "$role" in
    master|slave) ;;
    *) echo "Unbekannte Rolle '$role' (erwartet: master|slave)" >&2; exit 2 ;;
esac

project_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$project_root"

idf.py \
    -B "build-$role" \
    -DSDKCONFIG="sdkconfig.$role" \
    -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.$role.defaults" \
    "$@"
