# MADZINE MetaModule 移植指南

版本：2.6.0
更新日期：2026-03-31

---

## 問題解決原則

當遇到問題時優先搜尋使用相關 Agent/Skill 解決，目的是一面除錯一面強化 Agent/Skill 的能力。

---

## 編譯指令

```bash
cd /Users/madzine/Documents/OpenSource/MADZINE-MetaModule-repository
cmake --build build
```

### 測試模擬器

```bash
cd /Users/madzine/Documents/OpenSource/4ms-metamodule/simulator
timeout 8 ./build/simulator
```

### 強制重建面板資源（解決面板快取問題）

```bash
cd /Users/madzine/Documents/OpenSource/4ms-metamodule/simulator
rm -rf build/assets build/assets.uimg
cmake --build build
```

---

## 專案結構

```
MADZINE-MetaModule-repository/
├── src/
│   ├── plugin.hpp              # 主要標頭檔（extern Model* 宣告）
│   ├── plugin.cpp              # 模組註冊（p->addModel）
│   └── [ModuleName].cpp        # 各模組實作
├── assets/                     # PNG 面板（高度 240px，寬度依 HP 而定）
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
   - 從 VCV 版本的 `.img` 檔（data URI base64 PNG）轉換
   - 高度固定 240px，寬度依 HP：4HP=38, 8HP=76, 12HP=113, 16HP=152, 32HP=304, 40HP=380
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
| WeiiiDocumenta | ✅ | 10秒錄音限制，v2.6.0 加入 Load WAV（async_open_file） |
| Facehugger | ✅ | v2.5.0 新增 |
| Ovomorph | ✅ | v2.5.0 新增 |
| Runner | ✅ | v2.5.0 新增 |
| theKICK | ✅ | v2.5.0 新增，面板寬度已修正，v2.6.0 加入 Load Sample（async_open_file + dr_wav） |
| Drummmmmmer | ✅ | v2.5.0 新增 |
| ALEXANDERPLATZ | ✅ | v2.5.0 新增 |
| SHINJUKU | ✅ | v2.5.0 新增，需移除自訂 Widget |
| UniRhythm | ✅ | v2.5.0 新增，需移除 std::vector |

---

## 常見問題

### 面板讀不到或顯示錯誤

1. 確認 `assets/ModuleName.png` 存在且尺寸正確
2. 清除快取：`rm -rf build/assets build/assets.uimg`
3. 重新編譯

### UI 元件位置偏移

1. 移除所有自訂 Widget（TransparentWidget、Widget 子類）
2. 如果所有模組的旋鈕/port 都太小且偏右下，問題是 simulator 的 `build/assets/` 缺少標準元件圖片（`rack-lib/` 等）。CMake 建置順序競爭會導致 `firmware/assets/` 的複製被跳過。修復方式：
   ```bash
   cd /Users/madzine/Documents/OpenSource/4ms-metamodule/simulator
   cp -r ../firmware/assets/* build/assets/
   rm -f build/assets.uimg
   cmake --build build -- asset-image
   ```

### 編譯錯誤：vector/string 相關

將動態容器改為固定陣列，參考移植規則第 1 節

---

## 參考資料

- [4ms MetaModule Plugin SDK](https://github.com/4ms/metamodule-plugin-sdk)
- [VCV Rack Plugin 開發](https://vcvrack.com/manual/PluginDevelopmentTutorial)
