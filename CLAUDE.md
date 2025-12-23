# MADZINE MetaModule 移植指南

版本：1.0
更新日期：2025-12-23

---

## 編譯指令

```bash
cd /Users/madzine/Documents/MetaModule/MADZINE-MetaModule
cmake --build build
```

### 測試模擬器

```bash
cd /Users/madzine/Documents/MetaModule/4ms-metamodule/simulator
timeout 8 ./build/simulator
```

### 強制重建面板資源（解決面板快取問題）

```bash
cd /Users/madzine/Documents/MetaModule/4ms-metamodule/simulator
rm -rf build/assets build/assets.uimg
cmake --build build
```

---

## 專案結構

```
MADZINE-MetaModule/
├── src/
│   ├── plugin.hpp              # 主要標頭檔（extern Model* 宣告）
│   ├── plugin.cpp              # 模組註冊（p->addModel）
│   └── [ModuleName].cpp        # 各模組實作
├── assets/                     # PNG 面板（12HP = 183x380 像素）
├── CMakeLists.txt              # 編譯設定
└── CLAUDE.md                   # 本文件
```

---

## 移植規則

### 1. 記憶體管理（最重要）

MetaModule 不支援動態記憶體配置：

```cpp
// 禁止使用
std::vector<float> buffer;
std::string name;

// 改用固定陣列
static constexpr int MAX_BUFFER = 48000;
float buffer[MAX_BUFFER];
int bufferSize = 0;
char name[64];
```

### 2. 自訂 Widget 會造成 UI 偏移

**問題**：繼承 TransparentWidget、Widget 的自訂元件會造成 MetaModule 介面偏移。

**解法**：移除所有自訂 Widget，僅保留模組核心功能。

```cpp
// 移除這些
struct WaveformDisplay : TransparentWidget { ... };
struct CustomLight : GreenRedLight { ... };

// 使用標準元件
addChild(createLightCentered<MediumLight<GreenRedLight>>(...));
```

### 3. 不支援的功能

- `osdialog`（檔案對話框）
- 動態字串操作
- 複雜的 Widget 繼承

---

## 新增模組步驟

1. **複製並修改原始碼**
   ```bash
   cp /path/to/original/Module.cpp src/Module.cpp
   # 套用移植規則修改
   ```

2. **建立面板**
   - 從 VCV 版本截圖或轉換 SVG
   - 尺寸：12HP = 183x380, 6HP = 92x380
   - 格式：PNG
   - 放置於 `assets/ModuleName.png`

3. **註冊模組**
   - `src/plugin.hpp`: `extern Model* modelModuleName;`
   - `src/plugin.cpp`: `p->addModel(modelModuleName);`
   - `CMakeLists.txt`: 加入 `src/ModuleName.cpp`

4. **編譯測試**
   ```bash
   cmake --build build
   # 測試模擬器
   cd ../4ms-metamodule/simulator
   timeout 8 ./build/simulator
   ```

---

## 製作日誌

### 2025-12-23

#### WeiiiDocumenta 移植完成

- 將 10 秒錄音緩衝改為固定陣列：
  ```cpp
  static constexpr int MAX_BUFFER_SECONDS = 10;
  static constexpr int MAX_BUFFER_SIZE = 48000 * MAX_BUFFER_SECONDS;
  static constexpr int MAX_SLICES = 64;
  static constexpr int MAX_VOICES = 8;
  static constexpr int MAX_MORPHERS = 20;
  ```
- 移除自訂 Widget：WaveformDisplay、UnderlineWidget、SpeedPolyCVLine、GreenBlueLight
- 移除 osdialog 載入/儲存功能
- 將 GreenBlueLight 改為標準 GreenRedLight

#### WeiiiDocumenta 錯誤修正

- **Polyphonic Voice 功能修正**：移除 `|| false` 條件判斷錯誤（第 471, 843, 875 行）
- **切片序列化修正**：`dataToJson()` 改用索引迴圈只儲存有效的 numSlices 個切片
- **例外處理移除**：移除 try-catch（MetaModule 不支援例外處理）

#### TWNCLight UI 偏移修正

- 移除 WhiteBottomPanel 自訂元件

#### UniversalRhythm UI 偏移修正

- 移除 WhiteBottomPanel 自訂元件

#### UniversalRhythm std::vector 移除

- **PatternGenerator.hpp**：
  - 新增 `MAX_PATTERN_LENGTH = 32` 常數
  - `Pattern` struct 改用固定陣列：`float velocities[MAX_PATTERN_LENGTH]`、`bool accents[MAX_PATTERN_LENGTH]`
  - 所有 `std::vector<float>` 改為 `float weights[MAX_PATTERN_LENGTH]`
  - 所有 `std::vector<int>` 改為 `int skeleton[8]; int skeletonCount`

- **FillGenerator.hpp**：
  - 新增 `FillPattern` struct（取代 `std::vector<float>` 返回值）
  - 新增 `PitchedRollPattern`、`AngselPatternData` struct
  - 所有 `generate*` 函數改為返回 `FillPattern`

- **UniversalRhythm.cpp**：
  - `delayedTriggers` 改為固定陣列（MAX_DELAYED_TRIGGERS = 64）
  - 迭代器迴圈改為索引迴圈（swap-and-pop 刪除法）
  - `generateFillPattern` 呼叫改用 `FillPattern` 返回值

#### CMake 編譯腳本修正

- **metamodule-plugin-sdk/plugin.cmake**（第 47 行）：
  - 修正 `PLUGIN_FILE_FULL` 路徑問題
  - 改用 `cmake_path(APPEND ...)` 產生絕對路徑
  - 原問題：strip 命令找不到 debug.so 檔案

---

## 已移植模組清單

| 模組 | 狀態 | 備註 |
|------|------|------|
| SwingLFO | ✅ | |
| EuclideanRhythm | ✅ | |
| ADGenerator | ✅ | |
| Pinpple | ✅ | |
| MADDY | ✅ | |
| PPaTTTerning | ✅ | |
| TWNC | ✅ | |
| TWNCLight | ✅ | UI 偏移已修正 |
| QQ | ✅ | |
| Observer | ✅ | |
| TWNC2 | ✅ | |
| U8 | ✅ | |
| YAMANOTE | ✅ | |
| Obserfour | ✅ | |
| KIMO | ✅ | |
| Quantizer | ✅ | |
| EllenRipley | ✅ | |
| MADDYPlus | ✅ | |
| EnvVCA6 | ✅ | |
| NIGOQ | ✅ | |
| Runshow | ✅ | |
| DECAPyramid | ✅ | |
| KEN | ✅ | |
| Launchpad | ✅ | |
| Pyramid | ✅ | |
| SongMode | ✅ | |
| UniversalRhythm | ✅ | UI偏移+vector已修正 |
| WeiiiDocumenta | ✅ | 10秒錄音限制 |

---

## 常見問題

### 面板讀不到或顯示錯誤

1. 確認 `assets/ModuleName.png` 存在且尺寸正確
2. 清除快取：`rm -rf build/assets build/assets.uimg`
3. 重新編譯

### UI 元件位置偏移

移除所有自訂 Widget（TransparentWidget、Widget 子類）

### 編譯錯誤：vector/string 相關

將動態容器改為固定陣列，參考移植規則第 1 節

---

## 參考資料

- [4ms MetaModule Plugin SDK](https://github.com/4ms/metamodule-plugin-sdk)
- [VCV Rack Plugin 開發](https://vcvrack.com/manual/PluginDevelopmentTutorial)
