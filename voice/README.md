# voxkit

本機語音輸入的概念驗證，給這個 fcitx5-mcbopomofo fork 的 `plus` 分支用。輸入法行程裡只有一個 Unix socket 客戶端；錄音、VAD、模型都在獨立的 daemon，避免把 ggml 鏈進 fcitx5。

作法參考（不是把原始碼搬過來）：

- [anomalyco/hex](https://github.com/anomalyco/hex) `a7548271c1e40e1ddf5e1505a690b4c5f45a85db`：Silero 門檻（0.4、最短語音 160 ms）、Whisper／Qwen 的 context prompt（`", "` 串詞，Whisper 500 字、Qwen 1000 字）、模型檢查碼、推論放在輸入法行程外面。
- [Gloridust/WowType](https://github.com/Gloridust/WowType) `df43477`：按住說話要處理 key release；部分結果進預編輯，最終稿才 `commitString`；客戶端不阻塞。沒有使用它的微信離線模型。
- 推論函式庫是 crates.io 上的 [transcribe-cpp](https://github.com/handy-computer/transcribe.cpp) **0.4.1**（MIT）。Hex 另有一份 git patch，主要補 Parakeet 詞彙 rescoring；這個 PoC 的中文路徑用 prompt，不需要那份 patch。0.4.1 已能載入 Whisper 的 `.bin`、Qwen3-ASR，以及 Silero GGUF。

授權與第三方見 [NOTICE](NOTICE)。權重不會進 git。

## 什麼已經接上，什麼還沒有

已接上：

- `cpal` 開麥，轉成單聲道，線性重採樣到 16 kHz。
- Silero VAD v6.2（GGUF）。沒裝好時退回 −50 dBFS 能量門檻，不會把缺模型當成致命錯誤。
- 放開按鍵後做一次離線轉錄。這是權威結果。
- 詞彙 prompt：`data.txt` 的詞欄、`plus-quick-phrases.txt`、`plus-english.txt`。注音讀音不會當成 `sounds_like`。
- 可選的 OpenCC `s2twp`（系統上有 `opencc` 才跑；沒有就原樣輸出並在 stderr 警告）。
- 一行一個 JSON 的 Unix socket。
- fcitx5 客戶端，CMake 選項預設關閉，Plus 開關也預設關閉。

還沒有（下一步）：

- 串流 partial。現在的 partial 只是「聆聽中／辨識中」，不是模型的逐字稿。CPU 上對重疊視窗反覆編碼很貴，Hex 也是預設只在 GPU 串流。
- 按下之前的 pre-roll。麥克風在 daemon 收到 `start` 才開。
- 更好的重採樣（rubato）與下載續傳。
- systemd socket activation。現在是 daemon 自己 bind。
- 繁中辨識品質。語言碼是 `zh`，沒有 `zh-TW`。預設模型是 Whisper tiny，中文很弱。

## 建置 daemon

需要 CMake、C++ 編譯器、ALSA 標頭（`libasound2-dev`）。第一次會編譯 transcribe.cpp / ggml，請留幾分鐘。

```sh
cd voice
cargo build --release
```

二進位是 `voice/target/release/voxkit`。

## 模型

不會自動下載。快取目錄是 `$VOXKIT_MODELS_DIR`，否則 `$XDG_DATA_HOME/voxkit/models`（一般是 `~/.local/share/voxkit/models`）。

```sh
# 預設：Whisper tiny（多語言，約 74 MB）+ Silero VAD（約 1.2 MB）
./target/release/voxkit model install

# Hex 建議的中文模型，Q8 約 850 MB。檢查碼對得上 hex a7548271。
./target/release/voxkit model install qwen3-asr-0.6b

./target/release/voxkit model list
```

| id | 角色 | 說明 |
| --- | --- | --- |
| `whisper-tiny` | 預設 ASR | `ggerganov/whisper.cpp` 的 `ggml-tiny.bin`。SHA-256 `be07e048e1e599ad46341c8d2a135645097a538221678b7acdd1b1919c6e1b21`，77 691 713 bytes。OpenAI Whisper 權重，MIT。 |
| `qwen3-asr-0.6b` | 較好的 zh | `handy-computer/Qwen3-ASR-0.6B-gguf` @ `e4e16599`。SHA-256 `f081b2d5e23bd669d92cc331d722a8a0681943b8e6f34b48996fd5c319b5acd8`。授權見模型卡，不要跟著這個 repo 再散布。 |
| `silero-vad` | VAD | `silero-vad-v6.2-F32.gguf`。SHA-256 `0d871a800c420fdd5d1b17a3aea4296d1c39608f0ff4a9295eb76457eb1c88e6`。MIT。 |

`model install` 會印出授權一行。雜湊或大小不符就刪掉半成品並失敗。

為什麼預設不是 Qwen：這個 PoC 要在一般 CPU 上實際跑完 `voxkit transcribe`。Whisper tiny 走 transcribe-cpp 已支援的 whisper.cpp `.bin`，下載和一次推論都小得多。Qwen 在目錄裡，裝好後用 `--model qwen3-asr-0.6b`。tiny 的中文（尤其台灣用語）不要當真；繁體請打開 `--convert s2twp`，而且模型仍可能先吐簡體。

## 不開輸入法先測

```sh
# 16-bit 或 float WAV 都可以，會轉成 16 kHz 單聲道
./target/release/voxkit transcribe hello.wav --language zh
./target/release/voxkit transcribe hello.wav --language zh --convert s2twp \
  --vocab-dir ~/.local/share/fcitx5/mcbopomofo
```

詞彙目錄會讀：

- `data.txt`：第一欄是詞，後面的注音丟掉
- `plus-quick-phrases.txt`：整行
- `plus-english.txt`：整行

`#` 開頭是註解。每個詞最多 64 個字，最多 200 個詞，再依模型截斷 prompt。退出碼：0 有文字、2 判定沒有語音、1 錯誤。

OpenCC：

```sh
# Debian/Ubuntu
sudo apt install opencc
```

設定檔用 `/usr/share/opencc/s2twp.json`。這和 fcitx5 的 chttrans 是兩層；兩個都開可能轉換兩次。

## Daemon

```sh
./target/release/voxkit serve
# 或
VOXKIT_MODEL=qwen3-asr-0.6b ./target/release/voxkit serve --socket "$XDG_RUNTIME_DIR/voxkit.sock"
```

socket 權限是 `0600`。已有一個在聽就會拒絕再綁。上次崩潰留下的 socket，連不上就會刪掉再綁。

systemd user unit 範例：[`systemd/voxkit.service`](systemd/voxkit.service)。它假設 `~/.cargo/bin/voxkit`。這不是 socket activation。

```sh
cp systemd/voxkit.service ~/.config/systemd/user/voxkit.service
systemctl --user daemon-reload
systemctl --user enable --now voxkit.service
```

## 協定

一行一個 JSON，UTF-8，以 `\n` 結尾。`v` 必須是 1 或缺省。

輸入法 → daemon：

```json
{"v":1,"type":"start","req":1,"language":"zh","vocabulary":["小麥注音"],"convert":"s2twp","model":"whisper-tiny"}
{"v":1,"type":"stop","req":1}
{"v":1,"type":"cancel","req":1}
{"v":1,"type":"set_vocabulary","req":1,"vocabulary":["小麥注音"]}
{"v":1,"type":"ping","req":1}
```

`model` 可省略，用 serve 的預設。`start` 的 `vocabulary` 非空就整份換掉；空的話沿用 `set_vocabulary` 設過的清單。`convert` 是 `none`（預設）或 `s2twp`。

daemon → 輸入法：

```json
{"v":1,"type":"ready","req":1}
{"v":1,"type":"partial","req":1,"stable":"","tentative":"聆聽中"}
{"v":1,"type":"level","req":1,"rms_dbfs":-22.0}
{"v":1,"type":"final","req":1,"text":"你好"}
{"v":1,"type":"discarded","req":1,"reason":"no_speech"}
{"v":1,"type":"error","req":1,"code":"model-not-ready","message":"run: voxkit model install"}
{"v":1,"type":"pong","req":1}
```

`error.code`：`model-not-ready`、`no-input`、`bad-model`、`bad-request`、`not-recording`、`transcribe-failed`、`disconnected`。

生命週期：`start` 開麥並立刻回 `ready`。`stop` 再收 200 ms 尾巴，能量門檻不過就丟棄；過了才跑 Silero，有語音段才轉錄整段裁切後的音訊。`cancel` 會丟掉這輪，進行中的 `run` 透過 transcribe-cpp 的 `CancelToken` 中止。單輪最長約 60 秒，多出來的樣本直接丟掉。daemon 負責開麥，輸入法不送 PCM。

## 輸入法客戶端

預設不編譯、不改變現有行為。

```sh
cmake -S . -B build -DENABLE_VOICE_POC=ON
cmake --build build
```

`ENABLE_VOICE_POC=OFF`（預設）時，既有測試應和以前一樣。打開之後多三個設定，全部要使用者自己開：

| 鍵 | 預設 | 意思 |
| --- | --- | --- |
| `PlusVoice` | false | 按住說話 |
| `PlusVoiceHotkey` | `Alt_R` | 只允許修飾鍵 |
| `PlusVoiceTraditional` | false | commit 前請 daemon 跑 s2twp |

按鍵在 `keyEvent` 的 `isRelease()` 提早 return **之前**處理，否則收不到放開。注音組字、英文模式、選字中不搶鍵。密碼／敏感欄位不連線、不開麥。daemon 沒在聽：預編輯顯示「語音服務未啟動（voxkit serve）」，約 1.6 秒後清掉。部分結果寫預編輯，最終稿在組字區仍是空的時候才 `commitString`，不進注音緩衝。音量大約 5 Hz 寫在輔助列。

客戶端用 fcitx 的 `addIOEvent`，socket 設成 non-blocking。連線對本機 socket 最多等 200 ms。

## 已知限制

- 右 Alt 在不少鍵盤上是 AltGr。會搶鍵時改 `PlusVoiceHotkey`。
- 按住期間再按別的鍵會取消這輪，避免和注音組字打架。
- 沒有 pre-roll，開頭一個音可能被吃掉。
- Whisper tiny 對中文不可靠，幻覺也常見；VAD 只拿來丟靜音，擋不住幻覺。
- Qwen3-ASR 0.6B 這個環境沒有當預設跑完。目錄、URL、檢查碼在，`model install qwen3-asr-0.6b` 會抓那一檔。
- 簡繁轉換依賴系統 OpenCC，不是內嵌詞典。
- 推論在 CPU。第一次 `stop` 才載入模型，會卡一下。
- 一個連線裡同時只處理一輪。取消若發生在 `run` 裡，要等 native 呼叫看到 abort 才結束。
