#!/usr/bin/env bash
# Switch between the original McBopomofo and McBopomofo Plus.
#
#   packaging/switch.sh            toggle: Plus <-> original (same as "toggle")
#   packaging/switch.sh plus       install Plus (or upgrade to the newest built Plus)
#   packaging/switch.sh original   roll back to the exact original package
#   packaging/switch.sh status     show what is installed
#
# Your Plus settings are stashed when switching to the original and restored
# when switching back, so toggling never loses them.
#
# Both are real pacman packages that conflict with each other, so pacman
# swaps them atomically and its database always stays accurate.
set -euo pipefail
# Resolve symlinks (e.g. ~/.local/bin/mcb-switch) to find the packaging dir.
here=$(cd "$(dirname "$(readlink -f "$0")")" && pwd)
conf_dir=${XDG_CONFIG_HOME:-$HOME/.config}/fcitx5/conf
data_dir=${XDG_DATA_HOME:-$HOME/.local/share}/fcitx5/mcbopomofo
backup_root=$here/backup/user-config
plus_stash=$here/backup/plus-settings.conf
# Overridable for tests only.
pacman_cmd=${SWITCH_PACMAN:-sudo pacman}

# Newest package file in a directory.
pkg() { ls -t "$here/$1"/*.pkg.tar.zst 2>/dev/null | head -1; }

installed() { pacman -Qq fcitx5-mcbopomofo-plus fcitx5-mcbopomofo-git 2>/dev/null || true; }

restart_fcitx() {
  [[ -n ${SWITCH_NO_RESTART:-} ]] && return 0
  if pgrep -x fcitx5 >/dev/null; then
    echo "Restarting fcitx5..."
    fcitx5 -r -d >/dev/null 2>&1 &
    disown || true
  fi
}

backup_user_config() {
  local dest=$backup_root/$(date +%Y%m%d-%H%M%S)
  mkdir -p "$dest"
  [[ -f $conf_dir/mcbopomofo.conf ]] && cp -a "$conf_dir/mcbopomofo.conf" "$dest/"
  # User phrases are never modified by either package; copied only as insurance.
  [[ -d $data_dir ]] && cp -a "$data_dir" "$dest/mcbopomofo-userdata"
  echo "User config backed up to $dest"
}

stash_plus_settings() {
  local conf=$conf_dir/mcbopomofo.conf
  [[ -f $conf ]] || return 0
  if grep -q '^Plus[A-Za-z]*=' "$conf"; then
    grep '^Plus[A-Za-z]*=' "$conf" > "$plus_stash"
    # The original addon ignores Plus options; strip them so its config file
    # is exactly as before.
    sed -i '/^Plus[A-Za-z]*=/d' "$conf"
    echo "Plus settings saved to $plus_stash"
  fi
}

restore_plus_settings() {
  [[ -s $plus_stash ]] || return 0
  local conf=$conf_dir/mcbopomofo.conf
  mkdir -p "$conf_dir"
  touch "$conf"
  # Settings changed while on Plus win; only add the ones that are missing.
  while IFS= read -r line; do
    grep -q "^${line%%=*}=" "$conf" || printf '%s\n' "$line" >> "$conf"
  done < "$plus_stash"
  echo "Plus settings restored"
}

action=${1:-toggle}
if [[ $action == toggle ]]; then
  case $(installed) in
    *fcitx5-mcbopomofo-plus*) action=original ;;
    *) action=plus ;;
  esac
  echo "Toggle -> $action"
fi

case $action in
  plus)
    p=$(pkg plus) || true
    [[ -n ${p:-} ]] || { echo "Plus package not built. Run: (cd $here/plus && makepkg -f)"; exit 1; }
    [[ -n $(pkg backup) ]] || { echo "Refusing: original backup package missing in $here/backup"; exit 1; }
    backup_user_config
    if [[ $(installed) == *fcitx5-mcbopomofo-git* ]]; then
      echo "pacman will ask to remove fcitx5-mcbopomofo-git: answer y."
    fi
    $pacman_cmd -U "$p"
    restore_plus_settings
    restart_fcitx
    echo "Now using: McBopomofo Plus. Enable features in fcitx5 settings (options starting with 'Plus:')."
    ;;
  original)
    b=$(pkg backup) || true
    [[ -n ${b:-} ]] || { echo "Original backup package missing in $here/backup"; exit 1; }
    if [[ $(installed) == *fcitx5-mcbopomofo-git* ]]; then
      echo "Already on the original McBopomofo."; exit 0
    fi
    echo "pacman will ask to remove fcitx5-mcbopomofo-plus: answer y."
    $pacman_cmd -U "$b"
    stash_plus_settings
    restart_fcitx
    echo "Now using: original McBopomofo (3.1.1.r8.faa4972)."
    ;;
  status)
    echo "Installed: $(installed)"
    echo "Original backup: $(pkg backup || echo missing)"
    echo "Plus package:    $(pkg plus || echo not built)"
    ls -1 "$backup_root" 2>/dev/null | sed 's/^/Config backup: /' || true
    ;;
  *)
    echo "usage: $0 [toggle|plus|original|status]"; exit 2;;
esac
