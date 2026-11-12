#!/usr/bin/env bash
# No daemon restart, installation, real HOME, display, or session bus.
set -euo pipefail
source_dir=$(cd "$(dirname "$0")/.." && pwd)
build_dir=${1:-"$source_dir/build"}
root=$(mktemp -d)
trap 'rm -rf "$root"' EXIT
mkdir -p "$root/data/fcitx5/"{addon,inputmethod,data} "$root/config/fcitx5/conf"
cp "$build_dir/src/mcbopomofo-addon.conf" "$root/data/fcitx5/addon/mcbopomofo.conf"
cp "$build_dir/src/mcbopomofo.conf" "$root/data/fcitx5/inputmethod/"
cp "$source_dir/data/data.txt" "$root/data/fcitx5/data/mcbopomofo-data.txt"
cp "$source_dir/data/data-plain-bpmf.txt" "$root/data/fcitx5/data/mcbopomofo-data-plain-bpmf.txt"
cp "$source_dir/data/associated-phrases-v2.txt" "$root/data/fcitx5/data/mcbopomofo-associated-phrases-v2.txt"
cp "$source_dir/data/bpmfvs-pua.txt" "$root/data/fcitx5/data/mcbopomofo-bpmfvs-pua.txt"
cp "$source_dir/data/bpmfvs-variants.txt" "$root/data/fcitx5/data/mcbopomofo-bpmfvs-variants.txt"
cat > "$root/config/fcitx5/conf/mcbopomofo.conf" <<'INI'
PlusEmoji=True
PlusCalculator=True
PlusToolbar=True
PlusSpaceLetters=True
PlusSpaceDigits=True
INI
cat > "$root/config/fcitx5/profile" <<'INI'
[Groups/0]
Name=Default
Default Layout=us
DefaultIM=mcbopomofo

[Groups/0/Items/0]
Name=mcbopomofo
Layout=

[GroupOrder]
0=Default
INI
env -i PATH="$PATH" HOME="$root" XDG_DATA_HOME="$root/data" \
  XDG_CONFIG_HOME="$root/config" XDG_RUNTIME_DIR="$root" XDG_DATA_DIRS=/usr/share \
  FCITX_ADDON_DIRS="$build_dir/src:$(pkg-config --variable=libdir Fcitx5Core)/fcitx5" \
  "$build_dir/src/PlusIntegrationTest"
