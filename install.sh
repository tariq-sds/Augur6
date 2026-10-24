#!/bin/bash
# Installs every Toota Audio plugin in this folder (VST3 + Audio Unit) for the current user.
# Run from Terminal:  bash install.sh
set -e
cd "$(dirname "$0")"

VST3_DIR="$HOME/Library/Audio/Plug-Ins/VST3"
AU_DIR="$HOME/Library/Audio/Plug-Ins/Components"
mkdir -p "$VST3_DIR" "$AU_DIR"

echo ""
for p in *.vst3; do
  [ -e "$p" ] || continue
  rm -rf "$VST3_DIR/$p"; cp -R "$p" "$VST3_DIR/"
  xattr -dr com.apple.quarantine "$VST3_DIR/$p" 2>/dev/null || true
  echo "  installed $VST3_DIR/$p"
done
for p in *.component; do
  [ -e "$p" ] || continue
  rm -rf "$AU_DIR/$p"; cp -R "$p" "$AU_DIR/"
  xattr -dr com.apple.quarantine "$AU_DIR/$p" 2>/dev/null || true
  echo "  installed $AU_DIR/$p"
done

# Mirror Bridge (Max for Live): into the Live User Library so it shows up in Live's browser
BRIDGE_SRC="Mirror Bridge (Max for Live)"
if [ -d "$BRIDGE_SRC" ]; then
  M4L_DIR="$HOME/Music/Ableton/User Library/Presets/Audio Effects/Max Audio Effect/Mirror Bridge"
  mkdir -p "$M4L_DIR"
  cp "$BRIDGE_SRC"/* "$M4L_DIR/"
  xattr -dr com.apple.quarantine "$M4L_DIR" 2>/dev/null || true
  echo "  installed $M4L_DIR"
fi

# Toota Hits: drum one-shots, into the Live User Library so they show up under User Library > Samples
if [ -d "Toota Hits" ]; then
  HITS_DIR="$HOME/Music/Ableton/User Library/Samples/Toota Hits"
  mkdir -p "$HITS_DIR"
  cp -R "Toota Hits/"* "$HITS_DIR/"
  xattr -dr com.apple.quarantine "$HITS_DIR" 2>/dev/null || true
  echo "  installed $HITS_DIR"
fi

killall -9 AudioComponentRegistrar 2>/dev/null || true
echo ""
echo "Done. In Ableton Live open Settings > Plug-Ins and press Rescan."
