# 專業多軌錄音介面架構設計筆記（ESP32-P4 / XMOS XU316）

本文件整理自產品設計討論，涵蓋 **16I/16O（DB-25）機箱架構**、**主控晶片選型**、**Windows 驅動與 ASIO 實現**，以及**量產燒錄規劃**。

---

## 1. 硬體架構：16進16出 (16I/16O) 機架式設計
* **介面標準**：採用 **4 個 TASCAM (AES59) DB-25 接頭**
  * `DB-25 #1`：Input Ch 1–8
  * `DB-25 #2`：Input Ch 9–16
  * `DB-25 #3`：Output Ch 1–8
  * `DB-25 #4`：Output Ch 9–16
* **優點**：可塞進 **1U 標準機箱**，大幅縮小體積與散熱壓力，並相容錄音室現成 DB-25 蛇線。
* **核心元件（單台 16×16）**：
  * **ADC**：`2 × CS5368`（8ch ADC，串成 1 條 TDM16）
  * **DAC**：`1 × ADAU1966A`（16ch DAC，原生 TDM16）
  * **類比前端 / 輸出**：`16 × PGA2500`（SPI 控增益）+ `16 × DRV134`（平衡驅動）+ `8 × OPA1656`（雙運放濾波）
  * **+48V 幻象電源保護**：DB-25 進來第一級保留 `6.81kΩ (0.1%)` + `47µF/63V` 隔直 + `1N4148` 鉗位保護。
* **多櫃擴充**：需要 32×32 或 48×48 時，多台 16×16 櫃子透過 **Word Clock (BNC)** 同步時脈，並經由網路（VBAN / AES67）串接。

---

## 2. 主控晶片選型比較
* **ESP32-P4**：
  * 內建 **3 組 I2S**（`I2S0`、`I2S1`、`I2S2`），單組最高支援 **TDM16**，全晶片硬體上限為 **48進48出**（16+16+16）。
  * 單台 16×16 僅需使用 `I2S0 (TDM16)` 即可完成。
  * 注意：100M RMII 網口頻寬約在 32×32 (24-bit/48kHz, ~74Mbps) 左右，48×48 需改走 USB 2.0 HS (480Mbps) 或降為 16-bit。
* **XMOS XU316**（高階多軌 USB 方案）：
  * 無固定硬體 I2S 限制，透過 16 顆邏輯核心（雙 Tile）+ 1-bit Port 軟體定義 I2S/TDM（可開 8～16 條線）。
  * 受限於 USB 2.0 HS 頻寬，實務上限約 `64×64 @ 48kHz` 或 `32×32 @ 96kHz`。
  * 官方提供開源 **`sw_usb_audio` (`lib_xua`)** 參考韌體，可直接設定通道數與 TDM 模式。

---

## 3. Windows 驅動與 ASIO / WDM 實現方案
* **Windows 原生 `usbaudio2.sys`**：
  * 免寫驅動、免簽章，但因 USB Endpoint 硬體數量限制（通常 6～8 對），無法在 USB 層把 32 路拆成 16 個獨立立體聲 WDM 裝置。
* **商業授權方案（Thesycon `TUSBAudio`）**：
  * 技術上通用於所有標準 UAC2 裝置，商業上綁定公司 **USB `VID/PID`** 與 WHQL 簽章。
  * 一次提供 **ASIO + 多組拆分 WDM/MME**。
* **自研驅動路線（WDK + ASIO SDK + 控制 GUI）**：
  1. **核心層（`.sys`）**：參考微軟 `Windows-driver-samples/audio/sysvad`（PortCls + WaveRT），底層跑單一 32ch USB Isochronous Ring Buffer，在記憶體層透過 `PcRegisterSubdevice` 拆成 16 組獨立立體聲 WDM 裝置。
  2. **ASIO 層（`ASIO.dll`）**：下載 **Steinberg ASIO SDK**（改寫 `asiosample`），實作 `IASIO` 介面，透過 `DeviceIoControl` 與共享記憶體連接 `.sys` 驅動，最後用 `regsvr32` 註冊為 COM 元件。
  3. **控制 GUI**：使用 WPF / Qt / Electron 等開發：
     * **驅動參數（Buffer Size / Sample Rate）**：透過 `DeviceIoControl` 傳給 `.sys`。
     * **硬體參數（PGA2500 增益 / +48V 開關）**：透過 **USB HID** 或 **Vendor Control Transfer** 直接與晶片溝通（可跨平台至 macOS）。

---

## 4. 量產與大量燒錄（ESP32 系列）
1. **原廠 / 代理商預燒錄**：適合大批量，將 `merge_bin` 合併後的單一 `.bin` 交給原廠於 SMT 前燒錄。
2. **產線測試治具（探針）＋ 一拖多燒錄**：PCB 預留 `3V3 / GND / EN / IO0 / TX / RX` 測試點，搭配 `Flash Download Tool`（Factory 模式）或 `esptool.py` 多執行緒並行燒錄。
3. **量產細節**：合併單一 `.bin`、每台獨立寫入專屬 NVS 分區（序號/憑證），並於測試完成後啟用 **Flash Encryption** 與 **Secure Boot**。

---

## 5. 網路音訊擴充評估：AES67 與 AVB
* **AES67（推薦優先於 AVB）**：
  * 跑在 **Layer 3 (IP / UDP / RTP)**，不需專用交換機，可與 **Dante / Ravenna / Q-LAN** 互通。
  * **ESP32-P4 硬體條件**：原生 EMAC 支援 **IEEE 1588 (PTPv2) 硬體時間戳**，100M RMII 可承載 `16×16 @ 48kHz 24-bit`（~18.4 Mbps）或 `32×32`（~36.8 Mbps）。
  * **硬體預留建議**：PCB 上預留外部可程式化 PLL（如 `CS2100` 或 `Si5351`）微調 `24.576MHz` MCLK，以便未來透過韌體升級實現接收端時脈鎖定（Clock Recovery）。
  * **開發參考**：`ndac-todoroki/esp32_aes67_sender`（僅實作發送端 TX：RTP + SAP/SDP；接收端 RX 需自行補上 Jitter Buffer 與時脈鎖定）。
* **AVB (IEEE 802.1)**：
  * 跑在 **Layer 2**，強制需要支援 AVB 的專用交換機，且 Windows 原生不支援（主要為 macOS 與特定調音台）。
  * **開發參考**：`scrambletools/esp_avb`（可參考其 gPTP 時脈同步與封包排程實作邏輯）。

---

## 6. Luckfox Linux 平台評估與 RK3576 SAI 規格
* **Luckfox Lyra (RK3506G2)**：內建 5 組 SAI (`SAI0`~`SAI4`) + Matrix IO、雙 USB 2.0 OTG、雙 100M RMII；可跑 Linux `f_uac2` + `CamillaDSP` + `aes67-linux-daemon`，並可透過 `SAI2~4` 接 `CS8422` 實作最多 3 組 AES/EBU (XLR)。
* **Luckfox Core3566 (RK3566)**：內建 `I2S0`~`I2S3`（對外僅 `I2S1` 支援 8ch TDM，`I2S2/3` 僅 2ch），不適合直接原生接 32×32 以上類比；定位為 1000M Gigabit AES67 網路中樞，或透過 PCIe 接 FPGA 擴充。
* **Luckfox Core3576 (RK3576) — 大通道免 FPGA 旗艦方案**：
  * **音訊介面（共 5 組 SAI：`SAI0` ~ `SAI4`）**，全部支援 I2S / TDM / PCM（最高 32-bit / 192kHz）：
    * **`SAI0`**：4 條 TX / 4 條 RX 資料線（TDM8 = 32I/32O；TDM16 = 64I/64O）
    * **`SAI1`**：4 條 TX / 4 條 RX 資料線（TDM8 = 32I/32O；TDM16 = 64I/64O）
    * **`SAI2 ~ SAI4`**：各 1 條 TX / 1 條 RX 資料線（可接 AES/EBU XLR 或 S/PDIF；部分 SAI 內部可路由給 HDMI 音訊）
  * **優勢**：光靠 `SAI0 + SAI1`（合計 8 TX + 8 RX）跑 TDM8 即可直連 **64×64**、跑 TDM16 可直連 **128×128**，免去 FPGA；搭配 **USB 3.2 Gen 1 (5Gbps)** 與 **雙 1000M Gigabit 網口**，前後端皆無頻寬瓶頸。
  * **底板設計注意**：需對照官方 Pinout，確認 `SAI0` / `SAI1` 腳位未與 eMMC、雙網口或 MIPI 螢幕等介面復用衝突。


