# 小麥注音 Plus

所有功能預設關閉，請在 fcitx5 設定 → 小麥注音 中開啟（`Plus:` 開頭的選項）。
密碼／敏感欄位中所有 Plus 功能會自動停用。

| 功能 | 用法 |
|---|---|
| Emoji 候選 | 打「開心」「謝謝」「台灣」等，選字時第 3 個候選是「開心😊」 |
| 計算器 | 空白組字區按 `=`，輸入 `1+2*3`，Enter 上屏 `7`（支援 `+ - * / % ( )`） |
| Emoji 短碼 | 預設前綴 `;;`（快速連按兩下）：`;;smile` → 😄、`;;tw` 等。空白鍵上屏首選，`Shift+1…9` 選其他，`Backspace` 刪到底或 `Esc` 取消 |
| 英文模式 | `Ctrl+Shift+Space` 進入 `[EN]`，打 `github`，空白上屏 `GitHub `（自動修正大小寫）並回到注音。`Enter` 原樣上屏，`Esc` 只丟掉英文、保留中文 |
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
mcb-switch            # 一鍵切換：Plus ⇄ 原版（~/.local/bin/mcb-switch → packaging/switch.sh）
mcb-switch status     # 目前安裝哪個
mcb-switch plus       # 換成 Plus，或升級到最新打包的 Plus
mcb-switch original   # 還原成原本的小麥注音
```
切到原版時，Plus 的設定會先存到 `packaging/backup/plus-settings.conf` 並從設定檔移除；切回 Plus 時自動補回，來回切換不會遺失設定。
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

## 英文模式（M4 中英混打）

不做自動判斷（原因見 `docs/M4-mixed-input-assessment.md`），改用快捷鍵切換的英文暫存區，零誤判。

設定：
- `Plus: English buffer mode`：開關（預設關）。
- `Plus: English mode shortcut`：切換鍵，預設 `Ctrl+Shift+Space`，可改成任何組合鍵，也可以設多個。提示文字會跟著顯示你設的鍵。

  **改法一（建議）**：fcitx5 設定 → 輸入法 → 小麥注音 → 設定（齒輪）→ `Plus: English mode shortcut`，點按鈕後直接按想要的組合鍵。

  **改法二**：編輯 `~/.config/fcitx5/conf/mcbopomofo.conf`，在檔案**最後**加上（清單區段必須放在其他設定之後）：
  ```ini
  [PlusEnglishToggleKeys]
  0=Control+Alt+E
  1=Super+space
  ```
  修飾鍵要寫 `Control` `Alt` `Shift` `Super`（大小寫要對，`Ctrl`、`control` 不會被 fcitx 認得）。改完執行 `fcitx5 -r -d` 或切換一次輸入法。

- `Plus: English mode double-tap key`：**連按兩下**的切換鍵（預設關）。只能設修飾鍵，例如 `右 Shift`、`右 Ctrl`、`右 Alt`；在設定介面點按鈕後單按一下該鍵即可。
  - 300 ms 內放開再按一次才算（`Plus: English mode double-tap window` 可調 100–1000 ms），按住太久不算。
  - 拿來當修飾鍵用（`右 Shift + A` 打大寫）**不會**誤觸。
  - 和組合鍵可以同時使用，作用完全一樣：進入 `[EN]`、離開並上屏、或把提示的那串按鍵轉成英文；提示文字會同時顯示兩種（`Ctrl+Shift+Space / Right Shift ×2 → GitHub`）。
  - 設定檔寫法（放在檔案最後）：
    ```ini
    [PlusEnglishDoubleTapKeys]
    0=Shift_R
    ```
  - 不建議用 `左 Shift`：fcitx5 的 `AltTriggerKeys` 預設是左 Shift，會搶按鍵。

  避開已被佔用的鍵：`Ctrl+Shift+E`（Ghostty 分割視窗）、`Ctrl+Space`（fcitx5 預設切換輸入法）、`Super+Space`（若有設 fcitx5 群組切換）。

| 按鍵 | 在 `[EN]` 模式中 |
|---|---|
| 字母、數字、符號 | 原樣進暫存區（不經注音），可打 `C++`、`node.js` |
| `空白` | 上屏**修正大小寫**的字（`github`→`GitHub`）＋一個空格，回到注音 |
| `Enter` | 上屏**原樣**的字，回到注音（用 `↑↓` 選過候選時則上屏選的那個） |
| 切換鍵 | 上屏修正後的字（不加空格），回到注音 |
| `↑↓` + `Enter`／滑鼠 | 選其他候選（原樣、其他大小寫、補全）。英文模式下 `Shift+數字` 是打符號（`@` `#` `!`），不是選字 |
| `Esc` | 丟掉英文，保留前面的中文 |
| `Backspace` | 刪字；刪到底再按一次離開 |

- **只換掉英文的部分**：前後已經打好的中文一定保留，不管用空白、Enter、切換鍵或滑鼠上屏都一樣；游標在中間時英文插在游標位置。
- 會接在正在打的中文後面：`你好` → `[EN] linux` → 空白 → `你好 Linux `（加上「中英自動空格」）。
- 大小寫只在你打**全小寫**、且詞表裡只有一種寫法時才修正；你自己打大寫（`GITHUB`）或有多種寫法（`apple`/`Apple`）就照你打的。
- 候選視窗開著時，切換鍵不會作用。
- 密碼欄位中停用。

### 英文提示（選用，`Plus: Hint when typed keys look like an English word`，預設關）

不切換也可以：直接打 `github`（畫面上照常是注音），如果看起來像英文，下方會出現提示
`Ctrl+Shift+Space → GitHub`。**只是提示，不會改你的輸入**：
- 按切換鍵 → 剛才那串按鍵轉成 `[EN] github`，空白上屏 `GitHub `。
- 不理它、繼續打 → 照原本的注音走，跟沒有這個功能一樣。

觸發條件：同一個音節裡打了 ≥4 個字母鍵、其中 ≥2 個鍵蓋掉了前面的注音成分（正常打注音每個鍵都會加一個新成分，不會這樣），而且這串字母在英文詞表裡。

實測（`tools/eval/`）：
| | 標準 | 倚天 | IBM | 許氏 | 倚天26 |
|---|---|---|---|---|---|
| 常用英文字（≥4 字母）會出提示 | 98% | 96% | 77% | 42% | 68% |
| 真正的注音誤出提示（1,417 個音節 + 4,582 種打錯重打） | 0 | 0 | 0 | 0 | 0 |

許氏、倚天26 的聲調鍵在字母上（例如許氏 `d f j s`），英文字打到一半常被當成聲調直接組字，所以提示率較低；這時候請直接用切換鍵。

另外，即使沒有提示，**注音打到一半時按切換鍵**也會把剛才的字母轉成英文（例如打了 `lin` 想起來要打 Linux）。

詞表：
- 內建：SCOWL/ESDB 7 萬字（MIT 類授權），含 `GitHub`、`iPhone`、`JavaScript`、`PostgreSQL` 等常見品牌寫法。
- 自訂：`~/.local/share/fcitx5/mcbopomofo/plus-english.txt`，一行一個字，優先於內建（也可以蓋掉內建的大小寫）；切換一次輸入法後生效。

```
OpenVanilla
McBopomofo
k8s
```
