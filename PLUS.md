# 小麥注音 Plus（M1）

所有功能預設關閉，請在 fcitx5 設定 → 小麥注音 中開啟（`Plus:` 開頭的選項）。
密碼／敏感欄位中所有 Plus 功能會自動停用。

| 功能 | 用法 |
|---|---|
| Emoji 候選 | 打「開心」「謝謝」「台灣」等，選字時第 3 個候選是「開心😊」 |
| 計算器 | 空白組字區按 `=`，輸入 `1+2*3`，Enter 上屏 `7`（支援 `+ - * / % ( )`） |
| 常用語／剪貼簿／符號 | `Ctrl+\` → 「常用語／剪貼簿／符號」。常用語放在 `~/.local/share/fcitx5/mcbopomofo/plus-quick-phrases.txt`，一行一則。剪貼簿只在開啟選單時讀取，不留紀錄 |
| 中英自動空格 | `你好Linux第5章` → `你好 Linux 第 5 章`（字母、數字分開設定） |

候選選取：`Shift+1…9`、`↑↓` + Enter，或滑鼠點選；`Esc` 返回原組字內容。

## 建置與測試
```bash
cmake -B build -G Ninja -DCMAKE_INSTALL_PREFIX=/usr -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build           # 含私有 fcitx5 實例的按鍵整合測試
sudo cmake --install build && fcitx5 -r -d
```

## 切換／還原（Rollback）
原本安裝的 `fcitx5-mcbopomofo-git` 已原樣備份成 pacman 套件（`packaging/backup/`，25 個檔案逐位元驗證相同）。
兩個套件互相衝突，pacman 會原子性地互換，資料庫保持正確。

```bash
packaging/switch.sh status     # 目前安裝哪個
packaging/switch.sh plus       # 換成 Plus（會先備份 ~/.config/fcitx5/conf/mcbopomofo.conf 與使用者詞庫）
packaging/switch.sh original   # 還原成原本的小麥注音
```
使用者詞庫（`~/.local/share/fcitx5/mcbopomofo/`）兩者共用，切換時不會被更動。
