# 小麥注音 Plus

所有功能預設關閉，請在 fcitx5 設定 → 小麥注音 中開啟（`Plus:` 開頭的選項）。
密碼／敏感欄位中所有 Plus 功能會自動停用。

| 功能 | 用法 |
|---|---|
| Emoji 候選 | 打「開心」「謝謝」「台灣」等，選字時第 3 個候選是「開心😊」 |
| 計算器 | 空白組字區按 `=`，輸入 `1+2*3`，Enter 上屏 `7`（支援 `+ - * / % ( )`） |
| Emoji 短碼 | 預設前綴 `;;`（快速連按兩下）：`;;smile` → 😄、`;;tw` 等。空白鍵上屏首選，`Shift+1…9` 選其他，`Backspace` 刪到底或 `Esc` 取消 |
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

## Emoji 短碼

設定（fcitx5 設定 → 小麥注音）：
- `Plus: Emoji shortcodes`：開關（預設關）。
- `Plus: Emoji shortcode prefix`：前綴。**一個標點鍵**（例 `` ` ``）＝單鍵；**同一鍵兩次**（例 `;;`）＝連按兩下。預設 `;;`。
- `Plus: Double-tap window (ms)`：連按兩下的時間間隔，預設 300 ms。

行為細節：
- **單鍵前綴**只在組字區是空的時候觸發，句子打到一半時這個鍵照常是標點。注音鍵（字母、數字，以及標準排列的 `;` `,` `.` `/` `-`）不能當單鍵前綴，會被忽略。
- **連按兩下**：第一下照常輸出（標點或注音），第二下在時間內按下才會把第一下撤掉、打開短碼面板，所以單按一下完全不受影響。即使 `;` 是 ㄤ（標準排列），`;;` 也能用，因為「連打兩個 ㄤ」不是有效注音。
- Emoji 會插在游標位置，前面已經打的中文會保留，最後整段一起上屏。

自訂短碼：`~/.local/share/fcitx5/mcbopomofo/plus-emoji-shortcodes.txt`，一行一個 `短碼 emoji [標籤…]`，`#` 開頭是註解；切換一次輸入法後生效。自訂的會蓋掉同名的內建短碼，並排在最前面；只打前綴、不打字時，面板會列出你的自訂短碼。

```
tw 🇹🇼 taiwan
ok 👌
smile 🙂
```

內建 1,913 個短碼來自 GitHub gemoji（MIT），與 GitHub/Slack 的 `:shortcode:` 相同。

## M4 中英混打

評估見 `docs/M4-mixed-input-assessment.md`（結論：不做自動判斷，改做快捷鍵切換的英文暫存模式）。
