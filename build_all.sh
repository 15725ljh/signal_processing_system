#!/usr/bin/env bash
set -e
cd "$(dirname "$0")"
ROOT="$(pwd)"

echo "========================================"
echo "  Building all 4 GUIs (macOS)"
echo "========================================"
echo

for gui in 01_GUI_waveform 02_GUI_jamming 03_GUI_detection 04_GUI_signal_processing; do
    echo "--- Building $gui ---"
    (cd "$ROOT/$gui" && bash scripts/build.sh all)
    echo "[DONE] $gui"
    echo
done

echo "========================================"
echo "  All builds complete"
echo "========================================"
