#!/bin/bash
# Installs Augur-6 (VST3 + Audio Unit) for the current user.
# Run from Terminal:  bash install.sh
set -e
cd "$(dirname "$0")"

VST3_DIR="$HOME/Library/Audio/Plug-Ins/VST3"
AU_DIR="$HOME/Library/Audio/Plug-Ins/Components"
mkdir -p "$VST3_DIR" "$AU_DIR"

rm -rf "$VST3_DIR/Augur-6.vst3" "$AU_DIR/Augur-6.component"
cp -R "Augur-6.vst3" "$VST3_DIR/"
cp -R "Augur-6.component" "$AU_DIR/"

# Downloaded files are quarantined by macOS; clear that so Live can load the plugin
xattr -dr com.apple.quarantine "$VST3_DIR/Augur-6.vst3" "$AU_DIR/Augur-6.component" 2>/dev/null || true

# Make macOS re-scan Audio Units
killall -9 AudioComponentRegistrar 2>/dev/null || true

echo ""
echo "Augur-6 installed:"
echo "  $VST3_DIR/Augur-6.vst3"
echo "  $AU_DIR/Augur-6.component"
echo ""
echo "Now open Ableton Live > Settings > Plug-Ins and press Rescan."
