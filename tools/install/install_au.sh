#!/bin/sh
# Installs a built LABQ8.component for a manual smoke test in a host. Only on
# the user's explicit authorisation: nothing in the build installs plugins.
#
# Refuses (and changes nothing) while a host that may have LABQ8 loaded is
# running: Logic Pro, MainStage, GarageBand, AUHostingService, or any process
# with the installed binary open. Quit the host first; there is no override.
# The installed copy is moved to <backup> (never deleted or overwritten).
#
# Usage (from the repository root):
#   tools/install/install_au.sh <path/to/LABQ8.component> <backup path, must not exist>
#
# LABQ8_COMPONENTS_DIR replaces ~/Library/Audio/Plug-Ins/Components (tests).
set -eu

if [ $# -ne 2 ]; then
    echo "usage: $0 <path/to/LABQ8.component> <backup path>" >&2
    exit 2
fi
SRC=$1 BAK=$2
DIR=${LABQ8_COMPONENTS_DIR:-$HOME/Library/Audio/Plug-Ins/Components}
DST=$DIR/LABQ8.component

if [ ! -f "$SRC/Contents/MacOS/LABQ8" ]; then
    echo "error: $SRC is not a built LABQ8.component" >&2
    exit 2
fi
if [ -e "$BAK" ]; then
    echo "error: backup path $BAK already exists" >&2
    exit 2
fi

hosts_running() {
    found=1
    for name in "Logic Pro" "Logic Pro X" "MainStage" "GarageBand" "AUHostingService"; do
        if pgrep -x "$name" > /dev/null; then
            echo "  running: $name" >&2
            found=0
        fi
    done
    if [ -f "$DST/Contents/MacOS/LABQ8" ] \
        && lsof -t "$DST/Contents/MacOS/LABQ8" > /dev/null 2>&1; then
        echo "  the installed LABQ8 binary is open in another process" >&2
        found=0
    fi
    return $found
}

if hosts_running; then
    echo "error: quit the host first; nothing was installed" >&2
    exit 3
fi

if [ -e "$DST" ]; then
    mv "$DST" "$BAK"
    echo "previous copy moved to $BAK"
fi
mkdir -p "$DIR"
ditto "$SRC" "$DST"
diff -r "$SRC" "$DST" > /dev/null
shasum -a 256 "$DST/Contents/MacOS/LABQ8"
echo "installed $DST (identical to $SRC)"
