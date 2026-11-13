# ime-switch

One CLI to switch between the input methods built in `~/Work`.

```bash
ime-switch            # status (never switches)
ime-switch mcb-plus   # 小麥注音 Plus
ime-switch mcb        # 原本的小麥注音
ime-switch chewing    # 新酷音 Plus (libchewing 0.14)
ime-switch original   # all original packages (小麥 git + 新酷音 0.13.1)
```

- No toggle: a mode is always explicit, so a forgotten argument cannot switch anything.
- Each mode installs only what is missing, in one pacman transaction (answer `y` to remove conflicting packages).
- Makes the chosen IM the active one in fcitx5 (adds 新酷音 to the input method list if needed) and restarts fcitx5 via `omarchy-fcitx5.service`.
- 小麥 Plus settings are stashed when switching to the original and put back (before the first `[Section]`) when switching to Plus.
- 新酷音 Plus features are switched on the first time only; after that your own settings are kept.
- Before each switch a snapshot of the fcitx5 profile, IM configs and user dictionaries goes to `~/.local/share/ime-switch/snapshots/`.

Packages it uses:
| | Plus build | Original (rollback) |
|---|---|---|
| 小麥 | `~/Work/fcitx5-mcbopomofo-plus/packaging/plus/` | `.../packaging/backup/` (+ spare in `~/.local/share/fcitx5-mcbopomofo-rollback/`) |
| 新酷音 | `~/Work/fcitx5-chewing/packaging/{libchewing,fcitx5-chewing}/` | `~/.local/share/chewing-rollback/` (signed Arch packages) |

`mcb-switch` and `chewing-switch` are replaced by stubs that only print the status.
