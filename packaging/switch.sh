#!/usr/bin/env bash
# Switch between the original McBopomofo and McBopomofo Plus.
#
#   packaging/switch.sh plus       install Plus (backs up your config first)
#   packaging/switch.sh original   roll back to the exact original package
#   packaging/switch.sh status     show what is installed
#
# Both are real pacman packages that conflict with each other, so pacman
# swaps them atomically and its database always stays accurate.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
conf_dir=${XDG_CONFIG_HOME:-$HOME/.config}/fcitx5/conf
data_dir=${XDG_DATA_HOME:-$HOME/.local/share}/fcitx5/mcbopomofo
backup_root=$here/backup/user-config

pkg() { ls "$here/$1"/*.pkg.tar.zst 2>/dev/null | head -1; }

installed() { pacman -Qq fcitx5-mcbopomofo-plus fcitx5-mcbopomofo-git 2>/dev/null || true; }

restart_fcitx() {
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

case ${1:-status} in
  plus)
    p=$(pkg plus) || true
    [[ -n ${p:-} ]] || { echo "Plus package not built. Run: (cd $here/plus && makepkg -f)"; exit 1; }
    [[ -n $(pkg backup) ]] || { echo "Refusing: original backup package missing in $here/backup"; exit 1; }
    backup_user_config
    sudo pacman -U --noconfirm "$p"
    restart_fcitx
    echo "Now using: McBopomofo Plus. Enable features in fcitx5 settings (options starting with 'Plus:')."
    ;;
  original)
    b=$(pkg backup) || true
    [[ -n ${b:-} ]] || { echo "Original backup package missing in $here/backup"; exit 1; }
    sudo pacman -U --noconfirm "$b"
    # Plus options are unknown to the original addon and are simply ignored,
    # but strip them so the original config file is exactly as before.
    if [[ -f $conf_dir/mcbopomofo.conf ]]; then
      sed -i '/^Plus[A-Za-z]*=/d' "$conf_dir/mcbopomofo.conf"
    fi
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
    echo "usage: $0 {plus|original|status}"; exit 2;;
esac
