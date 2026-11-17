# 多通道音訊模組選型與採購搜尋指南（8 路以上 TDM 模組化原型專用）

本文件整理 **「免自行畫 PCB、買現成模組板跳線即可對接 Luckfox (Core3576 / Lyra) 或 ESP32-P4」** 的 **8 路以上輸入 (ADC)／輸出 (DAC)／整合 Codec** 晶片選型、採購搜尋關鍵字與硬體接線避坑清單。

---

## 一、快速決策：三種「8 路以上」模組化拼裝路線

| 方案路線 | 輸入晶片 (ADC) | 輸出晶片 (DAC) | 實際通道規模 | 優勢與採購難度 |
| :--- | :--- | :--- | :---: | :--- |
| **路線 1：現成平價模組最快上手（最推薦）** | **`2× AD1938`** 或 **`2× CS42448`** | 同左（Codec 內建 DAC） | **`8進 16出`**（`2× AD1938`）<br>**`12進 16出`**（`2× CS42448`） | 淘寶／AliExpress 現成小板最多、價格最低；`48kHz / 24-bit` 下單線 TDM16 即可跑滿 |
| **路線 2：輸入／輸出完全分離（高彈性）** | **`CS5368`** (8ch) 或 **`2× PCM1865`** (4ch×2) | **`ADAU1966A`** (16ch) 或 **`ES9080` / `AK4458`** (8ch) | **`8進 8出 ～ 8進 16出`** | 輸入與輸出各自走獨立 SAI/TDM 匯流排，徹底避開 Codec 通道與模式限制 |
| **路線 3：頂級錄音室旗艦規格** | **`AK5578`** (8ch, 32-bit, 121dB) | **`ADAU1966A`** (16ch) 或 **`ES9080`** (8ch) | **`8進 8出 ～ 16進 16出`** | 動態範圍最強，但現成平價小板較少，多需買官方評估板或找開源子板交 SMT 代貼 |

---

## 二、8 路以上純輸入（ADC）晶片與搜尋關鍵字

| 晶片型號 | 單片輸入數 | 位元 / 取樣率 | 核心規格亮點 | 搜尋關鍵字（淘寶 / AliExpress / 英文） |
| :--- | :---: | :--- | :--- | :--- |
| **`Cirrus Logic CS5368`** | **8 路** | 24-bit / 192kHz | `114 dB DR`；經典廣電 8ch TDM8/16 ADC；原廠板 **`CDB5368`** | • `CS5368 TDM ADC board`<br>• `CDB5368`<br>• `CS5368 8路 ADC 模組` |
| **`AKM AK5578`** | **8 路** | 32-bit / 768kHz | `121 dB DR`；支援 `TDM128/256/512` 串接 | • `AK5578 8 channel ADC board`<br>• `AK5578EN TDM module` |
| **`TI PCM1865`** *(買 2 片串接)* | **4 路**<br>*(2片=8路)* | 24-bit / 192kHz | `110 dB SNR`；**內建可程式前級 PGA**（免外接複雜類比放大即可測麥克風/Line）、支援 TDM & I2C/SPI；原廠板 **`PCM1865EVM`** | • `PCM1865 4 channel ADC module`<br>• `PCM1865 TDM board`<br>• `PCM1865 四路 音頻採集` |
| **`ESS ES9842 PRO`** *(買 2 片串接)* | **4 路**<br>*(2片=8路)* | 32-bit / 768kHz | `122 dB DR`；超高解析、TDM 主/從可調 | • `ES9842 ADC board TDM` |

---

## 三、8 路以上純輸出（DAC）晶片與搜尋關鍵字

| 晶片型號 | 單片輸出數 | 位元 / 取樣率 | 核心規格亮點 | 搜尋關鍵字（淘寶 / AliExpress / 英文） |
| :--- | :---: | :--- | :--- | :--- |
| **`ADI ADAU1966A`** | **16 路** | 24-bit / 192kHz | `118 dB SNR`；TDM8 / TDM16；原廠板 **`EVAL-ADAU1966AZ`**（自帶 8× 3.5mm TRS） | • `EVAL-ADAU1966AZ`<br>• `ADAU1966 16 channel DAC board`<br>• `ADAU1966A TDM 模組` |
| **`ESS ES9080`** | **8 路** | 32-bit / 768kHz | 高動態範圍 8 聲道純 DAC，支援 TDM/I2S | • `ES9080 8 channel DAC board`<br>• `ES9080Q TDM module` |
| **`AKM AK4458`** | **8 路** | 32-bit / 768kHz | 8 聲道 DAC，支援 TDM8/16 | • `AK4458 8 channel DAC board` |

---

## 四、高性價比「多合一 Codec（輸入＋輸出）」現成模組

*(注意：以下三顆單片皆為非對稱通道，需對稱 `8進8出` 時買 2 片並接或 Daisy-Chain)*

| 晶片型號 | 輸入 / 輸出 | 實務特性與避坑提醒 | 搜尋關鍵字 |
| :--- | :---: | :--- | :--- |
| **`ADI AD1938`** | **4 進 8 出**<br>*(2片串接 = 8進16出)* | • 淘寶平價成品板最多，常內建 STM32 透過跳線帽自動配置 SPI<br>• **重要模式差異**：固定 **24-bit**，差異在單線最高取樣率（TDM16 限 `48kHz`、TDM8 可到 `96kHz`、TDM4/Dual-Line 可到 `192kHz`）<br>• 若遇輸入錯位雜音，主控端反轉 `BCLK` 極性即可 | • `AD1938 4輸入8輸出`<br>• `AD1938 TDM SPI 模組`<br>• `AD1938 codec board` |
| **`Cirrus Logic CS42448`** | **6 進 8 出**<br>*(2片 = 12進16出)* | • 單片綜合通道數最高，I2C 控制 + TDM8/16<br>• Teensy / 開源音響社群有大量驗證過的 6I/8O 子板 | • `CS42448 audio board`<br>• `CS42448 TDM 模組` |
| **`TI PCM3168A`** | **6 進 8 出**<br>*(2片 = 12進16出)* | • 24-bit / 192kHz，部分 DIY 工作室有做帶 RCA/TRS 的子板 | • `PCM3168A TDM module`<br>• `PCM3168A 音頻模組` |

---

## 四之一、業界主流前級放大架構與四合一輸入切換設計（PAD / +48V / +5V / Line-In）

商業數位舞台箱（如 Midas DL16）與專業錄音介面最常採用的前級晶片與前端切換設計：

1. **業界主流專業前級晶片選型**：
   * **純類比低噪聲儀表放大器（電位器／步進電阻控增益）**：**`THAT1510` / `THAT1512`**、**`TI INA217` / `INA163`**（高 CMRR 共模抑制比、底噪極低、外圍元件精簡，模組化小板最易取得）。
   * **數位 SPI 程控前級（支援軟體記憶與遙控）**：**`TI PGA2500`**、**`THAT1580 + THAT5171`**（商業數位控台與遠端 Stage Box 標準方案）。
2. **輸入前端 4 大切換功能（由 MCU / MCP23017 驅動訊號繼電器實現「先斷後通互斥鎖」安全切換）**：
   * **`PAD` 衰減（通常 `-20 dB`）**：透過精密電阻橋式衰減網路（U-Pad）切換，防止高電平樂器或高聲壓訊號塞爆前級與 ADC。
   * **`+48V` 幻象電源**：低紋波升壓電源經 **`6.81kΩ (0.1%)`** 配對電阻送入 XLR Pin 2/3，並搭配 **`47µF / 63V`** 隔直電容與 **`1N4148`** 鉗位二極體保護前級。
   * **`+5V` 偏壓供電**：專供低壓電容鵝頸麥或領夾麥克風（與 `+48V` 嚴格互斥，切換前自動靜音並先斷開高壓繼電器）。
   * **純線路輸入（`Line Input`）**：可由獨立 6.35mm TRS（Combo 複合座中心孔，物理不帶 48V）或繼電器繞過話放高增益級，直接以低增益／`-20dB PAD` 送入平衡緩衝級進 ADC。

---

## 五、採購前必查的 5 個檢查點（避免買回來不能接）

1. **接頭擴充關鍵字**：若不想自己焊端子座，搜尋時加上 `with XLR Connectors`、`with TRS Connectors` 或 `RCA`。
2. **必須確認「I2C / SPI 控制腳」有引出**：多數 TDM 晶片預設上電不會自動進 TDM16 模式，必須讓板載 MCU 可透過跳線切換，或將 `SDA/SCL`（或 `SPI CS/MOSI/SCLK`）引出給 Luckfox 控制。
3. **確認時脈方向（Master / Slave & MCLK）**：檢查模組是否自帶 `24.576 MHz` 晶振，或需要由 Luckfox `SAI MCLK` 腳位提供；兩片串接時整機只能有一個時脈 Master。
4. **注意類比前端是否齊全**：部分便宜子板只有裸晶片輸入腳（僅容許 1–2Vrms Line-In），接麥克風需另配前級（或優先選內建 PGA 的 `PCM1865`）。
5. **高速跳線鐵則（`24.576 MHz` TDM16）**：杜邦線務必控制在 **5–8 cm 以內**，每條時脈／資料線旁並走 `GND` 迴流線，並在 `BCLK / FSYNC / SDO` 串接 `22Ω–33Ω` 阻尼電阻防振鈴錯位。

---

## 六、實機對標參考：MOTU 828mk3 Hybrid 內部晶片拆解與常見故障排查

| 功能級別 | MOTU 828mk3 Hybrid 實機晶片 | 備註與對標意義 |
| :--- | :--- | :--- |
| **ADC（類比輸入）** | **`Cirrus Logic CS5368`** (8路) + **`CS5364`** | 與本專案選型 `CS5368` 完全相同 |
| **Mic 前級（Preamp）** | **`TI PGA2500`** | SPI 數位控增益前級 |
| **DAC（類比輸出）** | **`AKM AK4358`** (8路 Line Out) + **`AK4396`** (Main) + **`AK4382A`** (耳機) | 高性價比多通道 AKM 架構（未使用昂貴 `ES9026PRO`；`ES9026PRO` 為後期 `828es` 世代晶片） |
| **類比運放 / 濾波** | **`JRC NJM4580`** (大量雙運放) | 經典低噪耐操雙運放 |
| **DSP 與路由核心** | **`Xilinx Spartan-3E (XC3S500E)`** FPGA + **`TI TMS320C6722`** DSP + **`AT91SAM7S256`** MCU | 對應本專案 Luckfox Core3576 (`Linux + CamillaDSP`) 核心 |

### 828mk3 Hybrid 偶發丟包 / 滋滋聲排查順序
1. **確認 Clock Source**：在 CueMix / 驅動設定確認為 `Internal`（避免誤鎖到未接線的外部 ADAT/Word Clock 造成爆音）。
2. **區分電腦緩衝 vs. 硬體電源／時鐘老化**：拔掉 USB/FireWire 單機測試 CueMix 直通監聽——若單機正常、接電腦才滋滋聲，為電腦 Buffer Size 過小或 USB DPC 延遲；若單機也出現偶發爆音，多為早期機型**電源板濾波電容老化**（導致類比與 PLL 供電紋波上升）或時脈晶振漂移所致。

---

## 七、開源 Linux 核心驅動範本、設備樹 (DTS) 與動態掛載（附 GitHub 連結）

修改驅動與設備樹時 **不需每次重燒整台系統**：驅動可編譯為 `.ko` 動態模組透過 `sudo insmod` / `modprobe` 載入，DTS Overlay (`.dtbo`) 放入 `/boot/` 重開機即可生效。

| 晶片型號 | Linux 主線開源驅動 / 實作範本（GitHub 直接連結） | 說明 |
| :--- | :--- | :--- |
| **`CS42448` / `CS42888`** | • [cs42xx8.c](https://github.com/torvalds/linux/blob/master/sound/soc/codecs/cs42xx8.c)<br>• [cs42xx8-i2c.c](https://github.com/torvalds/linux/blob/master/sound/soc/codecs/cs42xx8-i2c.c) / [cs42xx8-spi.c](https://github.com/torvalds/linux/blob/master/sound/soc/codecs/cs42xx8-spi.c) | Linux 主線內建 ASoC 多通道 TDM 驅動 |
| **`AD1938` / `AD193x`** | • [ad193x.c](https://github.com/torvalds/linux/blob/master/sound/soc/codecs/ad193x.c)<br>• [ad193x-i2c.c](https://github.com/torvalds/linux/blob/master/sound/soc/codecs/ad193x-i2c.c) / [ad193x-spi.c](https://github.com/torvalds/linux/blob/master/sound/soc/codecs/ad193x-spi.c) | Linux 主線內建 AD1936/1937/1938 系列驅動 |
| **`PCM1865` / `PCM186x`** | • [pcm186x.c](https://github.com/torvalds/linux/blob/master/sound/soc/codecs/pcm186x.c)<br>• [pcm186x-i2c.c](https://github.com/torvalds/linux/blob/master/sound/soc/codecs/pcm186x-i2c.c) / [pcm186x-spi.c](https://github.com/torvalds/linux/blob/master/sound/soc/codecs/pcm186x-spi.c) | Linux 主線內建（含 PGA 增益控制） |
| **`CS5368`** *(無需專用驅動)* | • 採萬用 **`simple-audio-card`** / **`audio-graph-card2`** 綁定 `SAI TDM`<br>• 參考同廠 I2C ADC：[cs53l30.c](https://github.com/torvalds/linux/blob/master/sound/soc/codecs/cs53l30.c) | DTS 設定 `format = "dsp_a"`、`dai-tdm-slot-num = <8>`、`dai-tdm-slot-width = <32>`、`dai-tdm-slot-rx-mask = <0xff>` |
| **`PGA2500`** *(SPI 數控前級)* | • 官方規格書：[TI PGA2500 Datasheet](https://www.ti.com/lit/ds/symlink/pga2500.pdf) | 無須寫核心驅動；直接由 `spidev` / Python / MCU 送出 16-bit SPI 控制字調增益（10–65 dB） |

---

## 八、Linux ASoC 驅動架構、SAI 介面與多晶片設備樹 (DTS) 實作詳解

### 1. `<chip>.c` vs `-i2c.c` / `-spi.c` vs 設備樹 (`.dts`/`.dtb`) 的分工
* **`<chip>.c`**（如 `cs42xx8.c`）：**音訊與 TDM 核心驅動**（不是設備樹）。定義支援通道數、取樣率、I2S/TDM 格式與 ALSA 混音器暫存器。
* **`<chip>-i2c.c` / `<chip>-spi.c`**：**控制匯流排掛載介面**（與硬體二選一即可）。僅負責傳送設定指令（音量、模式切換）；真正的高速音訊資料永遠走 SoC 的 **SAI / I2S (TDM)** 腳位。
* **SAI（Serial Audio Interface，串列音訊介面）**：SoC 晶片內部負責處理 I2S / TDM 高速數位音訊的硬體控制器，負責發送與接收 `MCLK`、`BCLK`、`FSYNC` 時脈與多通道串列音訊資料。
* **設備樹（`.dts` 編譯為 `.dtb` / `.dtbo`）**：硬體接線說明書。I2C / SPI 不會像 USB 自動偵測，若要掛載多顆相同晶片（例如 2 顆 `CS42448`），必須在硬體設定不同 I2C 位址（如 `0x48` 與 `0x49`），並在設備樹分別宣告、綁定到各自的 `&sai0` / `&sai1`。

### 2. 多顆 `CS42448` 設備樹 (`.dts`) 與 `simple-audio-card` 寫法範例

```dts
/ {
    sound {
        compatible = "simple-audio-card";
        simple-audio-card,name = "luckfox-multi-cs42448";

        /* 第一顆 CS42448 (0x48) <-> SAI0 */
        simple-audio-card,dai-link@0 {
            format = "dsp_a";
            bitclock-master = <&link0_cpu>;
            frame-master = <&link0_cpu>;
            mclk-fs = <256>;
            link0_cpu: cpu {
                sound-dai = <&sai0>;
                dai-tdm-slot-num = <8>;
                dai-tdm-slot-width = <32>;
            };
            codec { sound-dai = <&cs42448_a>; };
        };

        /* 第二顆 CS42448 (0x49) <-> SAI1 */
        simple-audio-card,dai-link@1 {
            format = "dsp_a";
            bitclock-master = <&link1_cpu>;
            frame-master = <&link1_cpu>;
            mclk-fs = <256>;
            link1_cpu: cpu {
                sound-dai = <&sai1>;
                dai-tdm-slot-num = <8>;
                dai-tdm-slot-width = <32>;
            };
            codec { sound-dai = <&cs42448_b>; };
        };
    };
};

&i2c1 {
    status = "okay";
    cs42448_a: cs42448@48 {
        compatible = "cirrus,cs42448";
        reg = <0x48>;
        #sound-dai-cells = <0>;
        clocks = <&audio_mclk>;
        clock-names = "mclk";
        status = "okay";
    };
    cs42448_b: cs42448@49 {
        compatible = "cirrus,cs42448";
        reg = <0x49>;
        #sound-dai-cells = <0>;
        clocks = <&audio_mclk>;
        clock-names = "mclk";
        status = "okay";
    };
};

&sai0 { #sound-dai-cells = <0>; status = "okay"; };
&sai1 { #sound-dai-cells = <0>; status = "okay"; };
```

載入後執行 `aplay -l` / `arecord -l` 即可看到 `device 0` 與 `device 1` 兩組獨立多軌裝置，並可直接於 `alsamixer` 與 PipeWire / JACK / CamillaDSP 路由。

### 3. 官方參考規範與樹莓派同架構實作專案（`CS42448` + I2C + TDM）
* **Linux 主線 CS42xx8 設備樹規範**：[cirrus,cs42xx8.yaml](https://github.com/torvalds/linux/blob/master/Documentation/devicetree/bindings/sound/cirrus,cs42xx8.yaml)
* **Linux 主線萬用音效卡規範**：[simple-card.yaml](https://github.com/torvalds/linux/blob/master/Documentation/devicetree/bindings/sound/simple-card.yaml)
* **完全同架構開源專案（樹莓派 + `CS42448` + I2C + TDM + CamillaDSP）**：[Audio-Injector/Octo (GitHub)](https://github.com/Audio-Injector/Octo)（包含完整的 DTS Overlay、安裝腳本與 6 進 8 出實作討論）
* **YouTube 實作影片搜尋**：[Audio Injector Octo Raspberry Pi](https://www.youtube.com/results?search_query=Audio+Injector+Octo+Raspberry+Pi) ｜ [Raspberry Pi I2S TDM Audio Codec Device Tree](https://www.youtube.com/results?search_query=Raspberry+Pi+I2S+TDM+Audio+Codec+Device+Tree)

---

## 九、DAC 輸出級放大器、平衡線路驅動、耳放與數位控制音量晶片／模組推薦

與麥克風前級（`PGA2500` 高倍電壓放大微弱訊號）不同，**DAC 輸出級**的任務是：**低通濾波（LPF）→ 類比緩衝／電流驅動 → 單端轉全平衡（+4 dBu 專業線性輸出）→（選配）數位類比音量衰減與耳機放大**。

### 1. 固定增益平衡輸出 / 濾波 / 耳放晶片與現成模組
| 用途定位 | 推薦晶片型號 | 核心規格與優勢 | 淘寶 / 蝦皮成品模組搜尋關鍵字 |
| :--- | :--- | :--- | :--- |
| **專業全平衡線路驅動 (首選)** | **`THAT1646`** *(THAT Corp)* | 專利 OutSmarts® 技術、0.0007% THD、固定 +6 dB 差動增益、+28 dBu @ 600Ω 餘裕；不怕誤接單端負載過熱 | `THAT1646 平衡輸出板` / `THAT1646 單端轉平衡模組` |
| **經典平衡線路驅動** | **`TI DRV134`** | 0.0005% THD、15 V/µs 轉換率、固定 +6 dB 差動增益，與 THAT1646 腳位兼容 | `DRV134 單端轉平衡板` / `DRV134 平衡輸出模組` |
| **DAC I/V 與差動濾波運放** | **`TI OPA1632`** + **`OPA1612`** *(平價版 `NE5532`)* | OPA1632 全差動放大（1.3 nV/√Hz）；OPA1612 超低噪聲雙運放（1.1 nV/√Hz），常見於 DAC 輸出 LPF 與緩衝級 | `OPA1632 平衡輸出板` / `OPA1612 雙運放前級板` / `DAC LPF 低通濾波輸出板` |
| **專業監聽耳機放大器** | **`TI TPA6120A2`** | 電流回授架構、128 dB 動態範圍、1300 V/µs、0.00014% THD+N，可推高阻抗監聽耳機（300–600Ω） | `TPA6120A2 耳放板` / `TPA6120 耳機放大器模組` |

> **供電提醒**：上述專業線性輸出／耳放模組多數需要**雙電源（±5V 至 ±15V）**，採購模組時請確認板載是否自帶升降壓或需外接雙電源模組。

### 2. 可數位控制音量 / 增益的輸出晶片（SPI / I2C，配合 Luckfox / MCU）
若希望像專業錄音介面一樣**由旋鈕編碼器或 Luckfox / ESP32-S3 透過程式直接控制輸出電平**（同時保留滿刻度 DAC 動態範圍，不在數位端砍 bit）：

| 晶片型號 | 通道數 | 控制介面 | 規格與特色 | 淘寶 / 現成模組搜尋關鍵字 |
| :--- | :--- | :--- | :--- | :--- |
| **`TI PGA2311`** *(立體聲)* | 2 聲道 | **3-wire SPI** | 120 dB 動態範圍、過零偵測無爆音切換、±5V 類比供電（另有 **±15V 高擺幅版 `PGA2310`**） | `PGA2311 數位音量控制板` / `PGA2310 SPI 音量模組` |
| **`TI PGA4311`** *(四聲道)* | 4 聲道 | **3-wire SPI** | 與 PGA2311 同架構，單顆即可控制 4 軌輸出（2 顆控 8 軌），接線最省腳位 | `PGA4311 4通道音量控制板` / `PGA4311 模組` |
| **`Cirrus Logic CS3318`** | **8 聲道** | **I2C / SPI** | 專為 8 通道多軌混音器／環繞設計，I2C 兩線即可掛載在與 `CS42448` 同一條匯流排上 | `CS3318 8通道音量板` / `CS3318 模組` |
| **`NJR MUSES72320`** | 2 聲道 | **2-wire 串列 (類 I2C)** | 旗艦級電阻陣列數位音量控制 IC，極致低噪聲，常搭配獨立高階運放（OPA1612）使用 | `MUSES72320 音量控制板` / `MUSES72320 前級模組` |

### 3. 可直接用外部電阻（或微調電位器）設定增益的晶片
> 注意：`THAT1646` / `DRV134` 為**固定 +6 dB** 差動增益；若想自行透過**更換電阻或旋鈕電位器**調整放大倍率，請改選下列晶片：

| 晶片型號 | 類型 | 增益設定方式 | 適用場景與模組關鍵字 |
| :--- | :--- | :--- | :--- |
| **`TI OPA1632`** *(全差動運放)* | 平衡輸出／LPF | 外部成對回授電阻比值：$G = R_f / R_{in}$ | 兼顧 DAC 低通濾波與可調平衡輸出；`OPA1632 平衡輸出板` |
| **`TI OPA1612`** / **`OPA2134`** / **`NE5532`** | 通用雙運放 | 傳統反相／非反相回授電阻：$G = 1 + R_f / R_g$ | 最彈性、套件最多，直接換電阻或接可變電阻即可調增益；`OPA1612 前級放大板` / `NE5532 前級模組` |
| **`TI TPA6120A2`** | 耳機放大器 | 外部回授電阻設定電流回授增益 | 依耳機靈敏度微調增益（通常設 +0 dB 至 +12 dB）；`TPA6120A2 耳放板` |
| **`THAT1512`** / **`TI INA163`** *(單電阻調增益)* | 儀表級音訊前級 | **只需一顆外部電阻 $R_G$** 即可線性設定 0–60 dB 增益 | 適合麥克風／樂器輸入級，不需動用 SPI；`THAT1512 前級板` / `INA163 麥克風放大板` |
