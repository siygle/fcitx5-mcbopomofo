# 中英混打評估程式

一次性量測用，不進產品、不在 CMake 裡。英文詞表（Google 10k）是 LDC 授權，不隨專案散布，要自己下載。

```bash
S=src; E=$S/Engine
c++ -std=c++20 -O2 -I$S -I$E -I$E/Mandarin tools/eval/fp.cpp $E/Mandarin/Mandarin.cpp $S/PlusEnglish.cpp -o /tmp/fp
/tmp/fp data/data.txt data/plus-english.txt
```

| 檔案 | 量什麼 |
|---|---|
| `collide.cpp` | 英文字是否全由注音鍵組成、會不會中途組不成字 |
| `amb.cpp` | 英文字剛好是合法一聲音節的比例 |
| `detect.cpp` | 「成分被覆蓋」訊號的偵測率 |
| `hint.cpp` | 提示門檻（≥N 鍵、≥M 次覆蓋）的召回率 |
| `fp.cpp` | 真正的注音（含打錯重打）誤觸提示的次數 |
