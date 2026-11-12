# 小麥注音 Plus（M1 + M2）

所有功能預設關閉，請在 fcitx5 設定 → 小麥注音 中開啟（`Plus:` 開頭的選項）。
密碼／敏感欄位中所有 Plus 功能會自動停用。

| 功能 | 用法 |
|---|---|
| Emoji 候選 | 打「開心」「謝謝」「台灣」等，選字時第 3 個候選是「開心😊」 |
| 計算器 | 空白組字區按 `=`，輸入 `1+2*3`，Enter 上屏 `7`（支援 `+ - * / % ( )`） |
| 常用語／剪貼簿／符號 | `Ctrl+\` → 「常用語／剪貼簿／符號」。常用語放在 `~/.local/share/fcitx5/mcbopomofo/plus-quick-phrases.txt`，一行一則。剪貼簿只在開啟選單時讀取，不留紀錄 |
| 中英自動空格 | `你好Linux第5章` → `你好 Linux 第 5 章`（字母、數字分開設定） |
| 首注音 | `Ctrl+\` → 「首注音」，每個音節打一個聲母鍵：`ㄊㄅ` → 台北（依目前鍵盤排列） |
| 部分讀音 | 打到一半（例 `ㄊㄞˊ` + `ㄅ`）按 `↓`，列出符合的詞 |
| 按錯鍵修正 | 只在讀音「組不成字」時，列出相鄰鍵／對調／多按／漏按一鍵後可用的字；正確輸入不受影響 |

候選選取：`Shift+1…9`、`↑↓` + Enter，或滑鼠點選；`Esc` 返回原組字內容。

## 建置與測試
```bash
cmake -B build -G Ninja -DCMAKE_INSTALL_PREFIX=/usr -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build           # 含私有 fcitx5 實例的按鍵整合測試
sudo cmake --install build && fcitx5 -r -d
```
