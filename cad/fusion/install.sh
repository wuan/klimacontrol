#!/usr/bin/env bash
# Register the CAD script with Autodesk Fusion.
#
# Fusion discovers a script as a FOLDER under its API/Scripts directory that
# contains <folder>.py and <folder>.manifest. This links cad/fusion in as
# "klimacontrol", so Fusion sees klimacontrol.py, klimacontrol.manifest and
# the kc/ package without anything being copied — edits in the repo are live.
#
#   ./cad/fusion/install.sh            link it
#   ./cad/fusion/install.sh --remove   unlink it
#
# Restart Fusion, or reopen Utilities -> Add-Ins -> Scripts and Add-Ins,
# afterwards for the entry to appear under My Scripts.

set -euo pipefail

SRC="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
NAME="klimacontrol"

case "$(uname -s)" in
    Darwin)
        DEST="$HOME/Library/Application Support/Autodesk/Autodesk Fusion 360/API/Scripts"
        ;;
    *)
        # Windows via Git Bash / MSYS
        DEST="$APPDATA/Autodesk/Autodesk Fusion 360/API/Scripts"
        ;;
esac

if [ "${1:-}" = "--remove" ]; then
    if [ -L "$DEST/$NAME" ]; then
        rm "$DEST/$NAME"
        echo "unlinked $DEST/$NAME"
    else
        echo "nothing to remove at $DEST/$NAME"
    fi
    exit 0
fi

if [ ! -d "$DEST" ]; then
    echo "Fusion's script directory does not exist:" >&2
    echo "  $DEST" >&2
    echo "Is Fusion installed, and has it been run at least once?" >&2
    exit 1
fi

if [ -e "$DEST/$NAME" ] && [ ! -L "$DEST/$NAME" ]; then
    echo "$DEST/$NAME exists and is not a symlink — refusing to replace it." >&2
    exit 1
fi

ln -sfn "$SRC" "$DEST/$NAME"
echo "linked $DEST/$NAME -> $SRC"
echo
echo "In Fusion: Utilities -> Add-Ins -> Scripts and Add-Ins (Shift+S)"
echo "-> My Scripts -> $NAME -> Run"
