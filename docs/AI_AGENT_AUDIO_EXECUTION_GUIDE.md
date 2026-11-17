# AI Agent 專用：Luckfox Core3576 多軌專業音訊系統實作與避坑執行指南

> **用途**：本文件專為後續接手設定、編寫驅動/設備樹、配置 PipeWire/VBAN 與混音路由的 **AI Agent** 撰寫。執行任何系統設定或給出硬體建議前，請先完整閱讀本指南，避免落入常見的嵌入式音訊與網路協議誤區。

---

## 一、AI Agent 常見誤判點（必讀避坑清單）

| 誤區編號 | 常見錯誤判斷 | 真相與正確執行邏輯 |
| :--- | :--- | :--- |
| **#1 VBAN 通道上限** | 誤以為「單一 VBAN 串流上限為 8 軌」，128 軌必須拆成 16 個不同 Stream Name。 | **錯誤（那是舊版 Voicemeeter 限制）**。檢查 `vban.h` 原始碼：`VBAN_CHANNELS_MAX_NB = 256`，`uint8_t format_nbc` 為 8-bit 無號整數。**單一 `stream_name` 原生支援 1–256 軌**，Windows **VB-Audio Matrix Coconut** 與 Linux **PipeWire `module-vban`** 均支援單一串流直接收發 **128 軌**（靠減少每包取樣點 `format_nbs` 塞進 `1436 bytes` UDP 封包）。 |
| **#2 `.c` 驅動與設備樹混淆** | 誤將 `<chip>.c`（如 `cs42xx8.c`）當成設備樹（DTB），或以為 I2C 與 SPI 都要接線。 | `<chip>.c` 是 **ASoC Codec 核心邏輯**（DAI 格式、取樣率、ALSA 音量暫存器）；`<chip>-i2c.c` / `-spi.c` 是**控制匯流排掛載介面（硬體二選一）**；高速音訊資料永遠走 SoC 的 **SAI / I2S (TDM)** 腳位；**`.dts` → `.dtb/.dtbo`** 才是獨立的硬體接線說明書。 |
| **#3 多顆相同晶片自動偵測** | 誤以為接上 2 顆 `CS42448` 後 Linux 會自動辨識。 | **I2C/SPI 不會自動枚舉**。每顆晶片必須設定不同硬體位址（如 `0x48`、`0x49` 或不同 SPI CS），並在 `.dts` 建立各自節點，分別綁定 `&sai0` / `&sai1`（或指定 TDM slot）。 |
| **#4 每次改驅動/DTS 都要重燒整機** | 要求使用者每次修改都用 RKDevTool 重燒整顆 eMMC 映像檔。 | **不需要**。驅動編譯為 `.ko` 後可直接 `insmod` / `modprobe` 動態載入；修改 `.dts` 只需編譯成 `.dtb` / `.dtbo`，`scp` 覆蓋 `/boot/` 後重開機即可生效。 |
| **#5 x86 VST 插件與延遲** | 建議在 ARM64 Linux 上直接跑 Windows x86 VST 或隨意用 Wine。 | Luckfox Core3576 為 **ARM64 (Cortex-A72/A53)**。務必優先使用 **ARM64 原生 `LV2` / `CLAP` / `VST3`**（如 `LSP Plugins`、`x42`、`Dragonfly Reverb`、`Calf`），直接 `apt` 安裝、執行延遲極低；x86 VST 需靠 Wine/box64/Yabridge 轉譯，不適合低延遲現場混音。 |
| **#6 輸出級當成麥克風前級 / 48V 保護** | 混淆 Mic 前級與 DAC 輸出級，或把 PAD 放在隔直電容前。 | DAC 輸出級需 **LPF + 緩衝/平衡驅動**（`THAT1646` / `DRV134` / `OPA1632`），不需高電壓增益。XLR 輸入端切換 **Hi-Z 樂器模式時務必軟硬體互鎖關閉 +48V**；`-20 dB PAD` 須置於 `47µF/63V` 隔直電容**之後**。 |

---

## 二、目標硬體與系統架構規格

* **主控開發板**：**Luckfox Core3576**（Rockchip **RK3576**）
  * **CPU**：4× ARM Cortex-A72 (2.2–2.3 GHz) + 4× ARM Cortex-A53 (2.0–2.2 GHz)
  * **網路**：**雙 1000M (Gigabit) 有線乙太網路**（音訊網路傳輸**必須走有線**，禁走 Wi-Fi）
  * **音訊控制器**：內建多組 **SAI (Serial Audio Interface)**，支援 I2S 與多通道 TDM（`dsp_a` / `dsp_b`）
  * **OS**：官方 Debian / Ubuntu ARM64（確保 Kernel 與 Device Tree 支援 RK3576 SAI）
* **輔助控制 / 電動推桿端**：**ESP32-S3**（透過 USB MIDI / Mackie Control / OSC 或 I2C 同步 Ardour / CamillaDSP）
* **遠端桌面混音端**：Windows **VB-Audio Matrix Coconut**（支援單一 VBAN Stream 128 軌對接）

---

## 三、核心協議與技術實作邏輯

### 1. VBAN 128 軌單一串流封包計算與調優
* **協定參數**（實證來源：PipeWire `vban.h`、`module-vban/audio.c` 與 `quiniouben/vban`）：
  * `VBAN_PROTOCOL_MAX_SIZE = 1464`，`VBAN_DATA_MAX_SIZE = 1436 bytes`（避免超過 Ethernet MTU 1500 分片）
  * `VBAN_CHANNELS_MAX_NB = 256`（`header.format_nbc = channels - 1`）
* **Core3576 頻寬與封包率 (PPS) 估算**（以 **48 kHz / 128 軌** 計算）：
  * **24-bit（3 bytes/sample）**：1 幀 = `384 bytes` → 每 UDP 包塞 **3 samples**（`1152 bytes`，`format_nbs = 2`）→ **`16,000 pkt/s`**，總頻寬約 **185 Mbps**（僅佔 Gigabit 網口 ~18.5%）。
  * **16-bit（2 bytes/sample）**：1 幀 = `256 bytes` → 每 UDP 包塞 **5 samples** → **`9,600 pkt/s`**，大幅降低網卡中斷率。
  * **執行建議**：若 Core3576 同時執行大量 Ardour 插件或 CamillaDSP FIR 濾波而出現 xrun，優先採取「改為 **16-bit**」或「拆分為 **2 條 64 軌串流**」來降低 PPS 中斷負載。

### 2. PipeWire VBAN 收發配置重點
* 優先使用 PipeWire 原生 **`libpipewire-module-vban-send`** 與 **`libpipewire-module-vban-recv`**，不要另外開多個 `vban_emitter` CLI 進程。
* 關鍵屬性：`audio.channels = 128`（或 `64`）、`stream.name = "<與 Matrix Coconut 一致>"`、`sess.latency.msec` 依有線網路抖動調整。

### 2.5 USB 3.0 OTG 直通模式（`ConfigFS f_uac2`）備案規範
* Core3576 支援透過 Linux USB Gadget ConfigFS (`f_uac2`) 模擬為 USB 錄音介面（單一多軌 UAC2 或多組雙聲道複合端點）。
* **Windows**：內建 `usbaudio2.sys` 僅提供 WDM/WASAPI；Linux 無法直接吐出 ASIO。若需進 DAW 建議用 **VB-Audio Matrix**（使用者不偏好 FlexASIO），或用 ASIO4ALL（全聚合裝置會鎖定同取樣率，最高可跑 192 kHz）。
* **macOS**：CoreAudio 原生免驅動，多組雙聲道端點只需在「音訊 MIDI 設定」建 **聚集設備 (Aggregate Device)** 即可（若改為單一多軌 UAC2 則免建）。
* **首選方案**：只要接線允許，優先走 **有線 Gigabit VBAN 128 軌**，延遲與穩定度優於 USB WDM/ASIO 聚合。

### 3. Linux ASoC 多晶片設備樹（DTS）綁定邏輯
* **單顆 / 多顆 Codec（以雙 `CS42448` 為例）**：
  1. 在 `&i2c1` 下分別宣告 `cs42448@48` 與 `cs42448@49`（`compatible = "cirrus,cs42xx8"` 或 `"cirrus,cs42448"`，提供 `mclk` 與 `reset-gpios`）。
  2. 開啟 `&sai0`、`&sai1`（`#sound-dai-cells = <0>; status = "okay";`）。
  3. 使用 `simple-audio-card` 建立 `dai-link@0`（`&sai0 ↔ &cs42448_a`）與 `dai-link@1`（`&sai1 ↔ &cs42448_b`），設定 `format = "dsp_a"`、`dai-tdm-slot-num = <8>`、`dai-tdm-slot-width = <32>`。
  4. **無專用驅動的純 TDM ADC**（如 `CS5368`）：可使用 `dummy-codec` + `simple-audio-card` 直接收進 SAI TDM。

### 4. 類比前端與輸出級數位控制匯流排
* **輸入端（XLR / Combo Jack）**：
  * 透過 **`TI TCA9555`**（16-bit I2C IO 擴充）+ **`ULN2803A`** 驅動：
    * **+48V**：`Toshiba TLP241A` PhotoMOS + RC 緩啟動 + 雙 `6.81kΩ (0.1%)` 電阻
    * **Mic / Hi-Z / -20 dB PAD**：`Omron G6K-2F-Y` 訊號繼電器或 `ADI ADG1419`（±15V 類比開關）；Hi-Z 經 `OPA1656` 高阻緩衝
    * **Mic 前級增益**：`TI PGA2500`（SPI 16-bit 控制字，10–65 dB）或單電阻 `THAT1512` / `INA163`
* **輸出端（DAC 後級）**：
  * **固定專業平衡 Line Out**：`THAT1646`（首選，+6 dB，抗單端短路）或 `DRV134`
  * **外部電阻可調增益 / LPF**：`OPA1632`（全差動）+ `OPA1612` / `NE5532`
  * **數位可調輸出 ↔ 0 dB Line Out 切換**：`TI PGA4311` (4ch SPI) / `PGA2311` (2ch SPI) / `CS3318` (8ch I2C/SPI)
  * **監聽耳放**：`TI TPA6120A2`（雙電源 ±5V–±15V）

---

## 四、AI Agent 現場執行與除錯指令清單

當你在 Luckfox Core3576 上進行設定與驗證時，請依序檢查以下項目：

1. **驗證 I2C Codec 接線與位址**：
   ```bash
   sudo i2cdetect -y 1
   ```
   *(確認 `0x48` / `0x49` 等 Codec 或 `0x20` 等 `TCA9555` 有正確回應)*
2. **編譯並套用 DTS Overlay**：
   ```bash
   dtc -@ -I dts -O dtb -o custom-audio.dtbo custom-audio.dts
   ```
3. **驗證 ALSA 音效卡與多軌 PCM 裝置**：
   ```bash
   cat /proc/asound/cards
   aplay -l
   arecord -l
   ```
4. **檢查核心驅動與設備樹錯誤訊息**：
   ```bash
   dmesg | grep -iE "asoc|sai|cs42|simple-audio|i2s|tdm"
   ```
5. **驗證 PipeWire 即時排程與 xrun**：
   ```bash
   pw-top
   pw-link -l
   ```
6. **設定矩陣路由（Matrix Routing）**：
   * DAW 內建網格矩陣：**Ardour**（快捷鍵 `P`：`Window > Audio Connections`）、**REAPER**（`Alt+R`：`Routing Matrix`）。
   * 無桌面純 DSP 矩陣：**CamillaDSP**（`camillagui` Mixers 矩陣頁）或 PipeWire 圖形跳線 **`qpwgraph`**。

---

## 五、權威參考原始碼倉庫與文檔連結

* **PipeWire `module-vban`（官方收發與 256 軌封包邏輯）**：
  * [vban.h](https://gitlab.freedesktop.org/pipewire/pipewire/-/blob/master/src/modules/module-vban/vban.h) ｜ [audio.c](https://gitlab.freedesktop.org/pipewire/pipewire/-/blob/master/src/modules/module-vban/audio.c) ｜ [module-vban-send.c](https://gitlab.freedesktop.org/pipewire/pipewire/-/blob/master/src/modules/module-vban-send.c) ｜ [module-vban-recv.c](https://gitlab.freedesktop.org/pipewire/pipewire/-/blob/master/src/modules/module-vban-recv.c)
* **獨立 VBAN CLI 工具**：[quiniouben/vban (GitHub)](https://github.com/quiniouben/vban)
* **Linux 主線驅動與 DTS 規範**：
  * [cs42xx8.c](https://github.com/torvalds/linux/blob/master/sound/soc/codecs/cs42xx8.c) ｜ [cirrus,cs42xx8.yaml](https://github.com/torvalds/linux/blob/master/Documentation/devicetree/bindings/sound/cirrus,cs42xx8.yaml) ｜ [simple-card.yaml](https://github.com/torvalds/linux/blob/master/Documentation/devicetree/bindings/sound/simple-card.yaml)
* **同架構（Linux + CS42448 + I2C + TDM）實戰專案**：[Audio-Injector/Octo (GitHub)](https://github.com/Audio-Injector/Octo)
