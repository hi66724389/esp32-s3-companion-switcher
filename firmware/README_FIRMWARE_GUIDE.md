# ESP32-S3 導播切換台 (80鍵 v2) & 獨立 VBAN Intercom 主站 (36鍵) · 韌體分階段測試與燒錄總指南

本目錄包含兩台設備的完整 **PlatformIO (Arduino / ESP-IDF 雙層架構)** 韌體專案，並將「分階段單項硬體測試（Stage 1 ~ Stage 5）」與「正式完整版韌體（Full Production）」清楚分離，讓你在焊接完成每一小塊電路時都能立刻單獨驗證！

> 搭配視覺化與工程圖紙 HTML 使用：
> * 🌐 **互動式面板模擬器 & 內建 Web UI 設定後台原型**：[../docs/vban_system_overview_and_webui.html](../docs/vban_system_overview_and_webui.html)
> * 🖨️ **A4 橫式好列印版 5 頁工程接線圖紙（含黑白省墨模式）**：[../docs/esp32s3_v2_and_intercom_printable.html](../docs/esp32s3_v2_and_intercom_printable.html)
> * 🗺️ **80 鍵導播台 v2 單頁全接線大圖**：[../docs/esp32s3_all_wiring_big.html](../docs/esp32s3_all_wiring_big.html)
> * 🗺️ **36 鍵 VBAN Intercom 主站單頁全接線大圖**：[../docs/vban_intercom_wiring_big.html](../docs/vban_intercom_wiring_big.html)

---

## 一、共用核心驅動與介面引擎 (`shared_components/`)

兩台設備共用以下 4 個高可靠度核心標頭檔（修改一處雙機同步受益）：

| 檔案路徑 | 負責功能與防呆保護機制 |
| :--- | :--- |
| [wm8960_pro_audio.h](./shared_components/wm8960_pro_audio.h) | • **0.0 ms 零延遲硬體類比自聽 (`R45/R46 LI2LO/RI2RO`)**：本機 Mic 直接在晶片內部類比混入耳機，無軟體 Buffer 回音<br>• **🔊 內建 1W 小喇叭防嘯叫自動消音 (`onTalkStateChanged`)**：壓下發話撥桿時，**先切斷小喇叭 4ms ➔ 再開麥克風**，杜絕回授嘯叫<br>• **⚡ XLR (`OFF / +5V / +48V`) 與 `6.35mm (Mic / Line-In -20dB Pad)` 先斷後通互斥鎖**：切換前自動靜音並先斷開 `48V/5V` 繼電器，防 48V 倒灌與電容爆音<br>• **即時本機 Mic VU 音量表計算器 (`updateMicLevelMeter`)**：計算 `-60 ~ 0 dBFS` RMS、1.5 秒 Peak Hold 與 `CLIP` 破音警告 |
| [vban_engine.h](./shared_components/vban_engine.h) | • **VBAN UDP `6980` 收發與 8 聲道總線自動拆軌 (`processMultiChannelSplitter`)**<br>• **🟠 來電有聲音自動閃燈 (`VOX > -40 dBFS`)**：自動偵測哪個工位有人開口講話並觸發橘白快閃<br>• **🟣 單獨 Mute 對方 (`listen_muted`)**、**📢 群組群發 (`ALL / GRP1–3`)**、**🩵 可選部分同發 (`selected_for_talk`)**<br>• **🏷️ VBAN 裝置名稱自動傳輸上屏**：支援 Header 16B `streamname` 與 `VBAN-TEXT` UTF-8 中文工位名廣播（含 UTF-8 邊界安全截斷防亂碼） |
| [webui_server.h](./shared_components/webui_server.h) | • **🌐 內建 `HTTP Port 80` Web UI 設定網頁 + `NVS` 斷電永久儲存**<br>• 支援 **W5500 / Wi-Fi 固定 IP (`Static IP / 子網路遮罩 / 預設閘道 / DNS`) 或 `DHCP`**、裝置中文名、VBAN 串流名、四合一音訊模式切換（與本機 4.0" Config 螢幕 100% 雙向同步） |
| [lcd_ui_renderer.h](./shared_components/lcd_ui_renderer.h) | • **4.0" `ST7796S (480×320)` 常駐雙音錶（Mic VU 音量表 + PGA 增益表 + 0ms 自聽表）**<br>• **LVGL 風格 `ENC` 旋轉選字 + `▲▼◀▶ ⏎ ⎋` 6 鍵虛擬鍵盤狀態機** |

---

## 二、專案 A：36 鍵獨立 VBAN Intercom 主站 (`intercom_36key/`)

進入 `firmware/intercom_36key/` 目錄，可依焊接進度執行 **5 個階段單項測試**，全部通過後再燒錄正式完整版：

```powershell
cd c:\Users\hi667\Documents\esp32-s3-companion-switcher\firmware\intercom_36key

# 階段 1：測試 6 片 74HC165 (48 bits：36工位鍵 + 3旋鈕S2 + 6導覽鍵 + 🕹️三段式發話撥桿 LATCH/MUTE/PTT)
pio run -e stage1_hc165_lever -t upload -t monitor

# 階段 2：測試 3 顆直連硬體 PCNT 旋鈕 (E1 耳機/喇叭、E2 PGM/自聽、ENC3 選單/Mic增益)
pio run -e stage2_pcnt_enc -t upload -t monitor

# 階段 3：測試 36 顆 SK6812 燈珠 (展示 VOX 來電橘白快閃、同發青燈、單獨 Mute 紫燈、發話綠燈)
pio run -e stage3_sk6812_vox -t upload -t monitor

# 階段 4：測試 單顆 MCP23017 (0x20) + 獨立 SPI3 驅動 6 塊 2.25" 小屏 + 1 塊 4.0" Config 大屏
pio run -e stage4_spi3_lcds -t upload -t monitor

# 階段 5：測試 WM8960 0.0ms 零延遲類比自聽 + 1W 內建小喇叭防嘯叫自動切斷 + XLR 5V/48V & 6.35mm 切換
pio run -e stage5_wm8960_audio -t upload -t monitor

# ★ 全部硬體測試通過後：燒錄 36 鍵 VBAN Intercom 主站完整版韌體
pio run -e full_intercom -t upload -t monitor
```

* 測試與主程式原始碼連結：
  * [test_01_hc165_and_lever.cpp](./intercom_36key/tests/test_01_hc165_and_lever.cpp)
  * [test_02_pcnt_encoders.cpp](./intercom_36key/tests/test_02_pcnt_encoders.cpp)
  * [test_03_sk6812_vox_sim.cpp](./intercom_36key/tests/test_03_sk6812_vox_sim.cpp)
  * [test_04_dual_spi_lcds.cpp](./intercom_36key/tests/test_04_dual_spi_lcds.cpp)
  * [test_05_wm8960_sidetone.cpp](./intercom_36key/tests/test_05_wm8960_sidetone.cpp)
  * ★ 正式完整版：[main_intercom_full.cpp](./intercom_36key/src/main_intercom_full.cpp)

---

## 三、專案 B：80 鍵主導播切換台 v2 (`switcher_80key/`)

進入 `firmware/switcher_80key/` 目錄，同樣提供 **5 個階段單項測試** 與 **正式完整版**：

```powershell
cd c:\Users\hi667\Documents\esp32-s3-companion-switcher\firmware\switcher_80key

# 階段 1：測試 15 片 74HC165 (120 bits @ 2kHz：80鍵 + 11旋鈕S2 + E4–E10/ENC11 灰碼解碼 + 6導覽鍵)
pio run -e stage1_hc165_15chips -t upload -t monitor

# 階段 2：測試 T-Bar 推桿 (G1 16倍過取樣濾波) + Jog 手輪 (G4/5) + 硬體 PCNT 旋鈕 (E1–E3)
pio run -e stage2_tbar_jog_pcnt -t upload -t monitor

# 階段 3：測試 80 顆 SK6812 Tally 燈鏈分區顯色與三點補電 (#0/#35/#79)
pio run -e stage3_sk6812_80leds -t upload -t monitor

# 階段 4：測試 雙 MCP23017 (0x20 + 0x21) + 15 塊 2.25" 小屏 + 1 塊 4.0" Config 大屏片選
pio run -e stage4_dual_mcp_lcds -t upload -t monitor

# 階段 5：測試 Companion Satellite API (TCP 16622) 自動握手、註冊 80 鍵面板與接收燈色/文字
pio run -e stage5_net_companion -t upload -t monitor

# ★ 全部硬體測試通過後：燒錄 80 鍵主導播切換台 v2 完整版韌體
pio run -e full_switcher -t upload -t monitor
```

* 測試與主程式原始碼連結：
  * [test_01_hc165_15chips.cpp](./switcher_80key/tests/test_01_hc165_15chips.cpp)
  * [test_02_tbar_jog_pcnt.cpp](./switcher_80key/tests/test_02_tbar_jog_pcnt.cpp)
  * [test_03_sk6812_80leds.cpp](./switcher_80key/tests/test_03_sk6812_80leds.cpp)
  * [test_04_dual_mcp_16lcds.cpp](./switcher_80key/tests/test_04_dual_mcp_16lcds.cpp)
  * [test_05_net_companion.cpp](./switcher_80key/tests/test_05_net_companion.cpp)
  * ★ 正式完整版：[main_switcher_full.cpp](./switcher_80key/src/main_switcher_full.cpp)
