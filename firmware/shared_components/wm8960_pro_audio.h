#pragma once
#include <Arduino.h>
#include <Wire.h>
#include <driver/i2s.h>
#include <math.h>

// ============================================================================
// WM8960 專業音訊前端驅動 + 0.0ms 硬體類比自聽 + 1W 小喇叭防嘯叫 + XLR 5V/48V 安全互斥鎖
// 同時支援：80 鍵主導播台 v2 & 36 鍵獨立 VBAN Intercom 主站
// ============================================================================

#define WM8960_I2C_ADDR 0x1A

enum AudioInputMode : uint8_t {
  IN_MIC1_35MM_HEADSET = 0, // LINPUT1 + 內部 MICBIAS (2.5V)
  IN_MIC2_XLR_DYNAMIC  = 1, // LINPUT2 + XLR 供電 OFF (動圈麥)
  IN_MIC2_XLR_5V       = 2, // LINPUT2 + XLR +5V 偏壓 (會議鵝頸電容麥)
  IN_MIC2_XLR_48V      = 3, // LINPUT2 + XLR +48V 幻象電源 (專業電容麥)
  IN_IN3_635_MIC       = 4, // LINPUT2 + 6.35mm MIC 高增益模式 (+25dB, Pad OFF)
  IN_IN3_635_LINE      = 5, // LINPUT2/RINPUT2 + 6.35mm LINE-IN 模式 (投入 -20dB Pad, PGA 0dB)
  IN_MIC4_BOARD_MEMS   = 6  // LINPUT3 + 面板內建類比 MEMS 麥克風
};

struct MicMeterState {
  float rms_dbfs  = -60.0f; // -60.0 ~ 0.0 dBFS
  float peak_dbfs = -60.0f; // 峰值停留 (Peak Hold)
  bool  clipping  = false;  // > -0.5 dBFS 破音警告
  uint32_t peak_hold_ms = 0;
};

class WM8960ProAudio {
public:
  uint8_t mcp_addr = 0x20;
  uint8_t bit_xlr_48v = 3; // 36鍵台預設 GPB3；80鍵台為 MCP#2(0x21) GPA4
  uint8_t bit_xlr_5v  = 4; // 36鍵台預設 GPB4；80鍵台為 MCP#2(0x21) GPA5
  uint8_t bit_635_pad = 5; // 36鍵台預設 GPB5；80鍵台為 MCP#2(0x21) GPA6
  bool    use_port_b  = true;

  float   current_pga_db      = 22.5f; // 0.0 ~ +30.0 dB
  int8_t  sidetone_db         = -6;    // -21 ~ +6 dB (3dB step)
  bool    sidetone_enabled    = true;
  uint8_t hp_volume           = 115;   // 47~127 (-73dB ~ +6dB, 121=0dB)
  uint8_t spk_volume          = 118;   // 內建 1W 小喇叭音量
  bool    speaker_user_on     = true;  // 使用者是否開啟內建小喇叭
  bool    speaker_talking_cut = false; // 發話時自動消音防嘯叫旗標
  AudioInputMode current_mode = IN_MIC2_XLR_48V;
  MicMeterState  meter;

  // 寫入 WM8960 9-bit 暫存器 (7-bit reg addr + 9-bit data)
  bool writeReg(uint8_t reg, uint16_t val) {
    Wire.beginTransmission(WM8960_I2C_ADDR);
    Wire.write((reg << 1) | ((val >> 8) & 0x01));
    Wire.write(val & 0xFF);
    return (Wire.endTransmission() == 0);
  }

  // 初始化 WM8960 + I2S0 全雙工 (48kHz, 16-bit, PLL 由 BCLK 自動倍頻免 MCLK)
  bool begin(int pin_bclk, int pin_lrck, int pin_dout, int pin_din,
             uint8_t relay_mcp_addr, bool relay_port_b,
             uint8_t b_48v, uint8_t b_5v, uint8_t b_pad) {
    mcp_addr    = relay_mcp_addr;
    use_port_b  = relay_port_b;
    bit_xlr_48v = b_48v;
    bit_xlr_5v  = b_5v;
    bit_635_pad = b_pad;

    // 1. 軟復位 WM8960
    writeReg(0x0F, 0x000);
    delay(20);

    // 2. 電源管理：開啟 VREF, VMID(50k), AINL/AINR, ADCL/ADCR, MICB
    writeReg(0x19, 0x1FC); // R25 Power Mgmt 1
    writeReg(0x1A, 0x1F8); // R26 Power Mgmt 2: DACL/R, LOUT1/ROUT1(HP), SPKL/SPKR, PLL
    writeReg(0x2F, 0x03C); // R47 Power Mgmt 3: LOMIX, ROMIX, LMIC, RMIC

    // 3. 時脈與インターフェース：I2S 16-bit, 48kHz (使用內部 PLL 鎖定 BCLK)
    writeReg(0x04, 0x001); // Clocking 1: SYSCLK from PLL
    writeReg(0x07, 0x002); // Audio Interface: 16-bit I2S format

    // 4. DAC 數位音量 0dB & 解除軟靜音
    writeReg(0x05, 0x000); // ADC/DAC Ctrl 1: Unmute DAC
    writeReg(0x0A, 0x1FF); // Left DAC Vol = 0dB (with VU update bit 0x100)
    writeReg(0x0B, 0x1FF); // Right DAC Vol = 0dB

    // 5. 開啟 1W Class-D 內建小喇叭功放 (R49 Class D Control 1)
    writeReg(0x31, 0x0F7); // Enable Class-D SPK_LP/LN & SPK_RP/RN

    // 6. 設定預設 PGA 增益、耳機音量、0ms 類比自聽與小喇叭音量
    setMicPgaGain(current_pga_db);
    setHeadphoneVolume(hp_volume);
    updateZeroLatencySidetone(false); // 待機 MUTE 時先關閉自聽，發話時秒開
    updateSpeakerOutput(false);

    // 7. 設定 ESP32-S3 I2S0 全雙工 DMA (48kHz, 16-bit Stereo)
    i2s_config_t i2s_cfg = {
      .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX | I2S_MODE_RX),
      .sample_rate = 48000,
      .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
      .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
      .communication_format = I2S_COMM_FORMAT_STAND_I2S,
      .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
      .dma_buf_count = 6,
      .dma_buf_len = 128, // 低延遲 2.66ms DMA frame
      .use_apll = false,
      .tx_desc_auto_clear = true
    };
    i2s_pin_config_t pin_cfg = {
      .mck_io_num = I2S_PIN_NO_CHANGE,
      .bck_io_num = pin_bclk,
      .ws_io_num = pin_lrck,
      .data_out_num = pin_dout,
      .data_in_num = pin_din
    };
    i2s_driver_install(I2S_NUM_0, &i2s_cfg, 0, NULL);
    i2s_set_pin(I2S_NUM_0, &pin_cfg);
    return true;
  }

  // ==========================================================================
  // ★ 核心功能 1：0.0 ms 零延遲硬體類比自聽 (Hardware Analog Sidetone)
  // 透過 R47 (Left Output Mixer) & R48 (Right Output Mixer) 的 LI2LO / RI2RO
  // 將麥克風前級訊號直接在晶片內類比混入耳機 HP_L/HP_R，完全不經過數位 Buffer！
  // ==========================================================================
  void updateZeroLatencySidetone(bool mic_is_live) {
    // R47/R48 Bit 8 = DAC2LO (遠端聲音進耳機，永遠開啟)
    // Bit 7 = LI2LO (本機 Mic 類比旁路進耳機，僅在發話且啟用自聽時開啟！)
    // Bits [6:4] = LI2LOVOL (000 = 0dB, 001 = -3dB ... 111 = -21dB)
    uint8_t atten_bits = 2; // 預設 -6dB
    if (sidetone_db >= 0) atten_bits = 0;
    else if (sidetone_db <= -21) atten_bits = 7;
    else atten_bits = (uint8_t)((-sidetone_db) / 3);

    bool enable_bypass = sidetone_enabled && mic_is_live;
    uint16_t mix_val = (1 << 8) | (enable_bypass ? (1 << 7) : 0) | ((atten_bits & 0x07) << 4);
    writeReg(0x2D, mix_val); // R45 Left Output Mixer
    writeReg(0x2E, mix_val); // R46 Right Output Mixer
  }

  // ==========================================================================
  // ★ 核心功能 2：面板內建 1W 小喇叭控制 + 發話自動消音防嘯叫 (Auto Speaker Cut)
  // 當壓下三段式發話撥桿 (mic_is_live == true) 時，第 1 時間先切斷小喇叭，防止嘯叫！
  // ==========================================================================
  void updateSpeakerOutput(bool mic_is_live) {
    speaker_talking_cut = mic_is_live;
    uint8_t out_vol = 0; // 0 = Mute
    if (speaker_user_on && !speaker_talking_cut) {
      out_vol = spk_volume; // 47~127
    }
    // R40 (0x28) SPK_L Volume, R41 (0x29) SPK_R Volume, Bit 8 = Update, Bit 7 = Zero-Cross
    writeReg(0x28, 0x180 | (out_vol & 0x7F));
    writeReg(0x29, 0x180 | (out_vol & 0x7F));
  }

  // 當發話狀態改變時（例如三段式撥桿切換 LATCH / MUTE / PTT），呼叫此函式：
  // 確保「先關喇叭 8ms ➔ 再開麥克風與 0ms 耳機自聽」的防嘯叫鐵律！
  void onTalkStateChanged(bool talking) {
    if (talking) {
      updateSpeakerOutput(true);       // 1. 立即切斷內建小喇叭 (Auto-Cut)
      delayMicroseconds(4000);         // 2. 等待喇叭振膜餘震消失
      updateZeroLatencySidetone(true); // 3. 開啟耳機 0.0ms 類比自聽
    } else {
      updateZeroLatencySidetone(false);// 1. 切斷耳機自聽 (MUTE)
      updateSpeakerOutput(false);      // 2. 恢復內建小喇叭監聽
    }
  }

  void setHeadphoneVolume(uint8_t vol) {
    hp_volume = constrain(vol, 47, 127);
    writeReg(0x02, 0x180 | hp_volume); // LOUT1 (HP_L) with Zero-Cross
    writeReg(0x03, 0x180 | hp_volume); // ROUT1 (HP_R) with Zero-Cross
  }

  void setMicPgaGain(float db) {
    current_pga_db = constrain(db, -17.25f, 30.0f);
    // WM8960 R0/R1 LINVOL: 000000 = -17.25dB, step 0.75dB, 0x3F = +30dB
    uint8_t code = (uint8_t)((current_pga_db + 17.25f) / 0.75f);
    code = constrain(code, 0, 63);
    writeReg(0x00, 0x140 | code); // Bit 8=IPVU, Bit 6=LINMUTE(0), [5:0]=Vol
    writeReg(0x01, 0x140 | code);
  }

  // ==========================================================================
  // ★ 核心功能 3：四合一音訊前端切換 + XLR (+5V / +48V) 先斷後通防爆音互斥鎖
  // ==========================================================================
  void selectInputSource(AudioInputMode mode, uint8_t &mcp_port_shadow) {
    current_mode = mode;

    // 步驟 1：切換期間先將 ADC 輸入靜音，防止 +48V 電容充放電產生巨響 (Pop Noise)
    writeReg(0x00, 0x180); // Mute LINPG
    delay(20);

    // 步驟 2：Break-Before-Make 先強制關閉 +48V 與 +5V 繼電器，避免 48V 倒灌 5V！
    mcp_port_shadow &= ~((1 << bit_xlr_48v) | (1 << bit_xlr_5v) | (1 << bit_635_pad));
    writeMcpPort(mcp_port_shadow);
    delay(40);

    // 步驟 3：根據目標模式設定繼電器與 WM8960 內部路由 (R32 ADCL Signal Path)
    switch (mode) {
      case IN_MIC1_35MM_HEADSET:
        // LINPUT1 (3.5mm 耳麥) + 開啟內部 MICBIAS
        writeReg(0x20, 0x138); // Connect LMN1 (LINPUT1) to PGA + MicBoost +20dB
        break;

      case IN_MIC2_XLR_DYNAMIC:
        // XLR 動圈麥 (48V=OFF, 5V=OFF, Pad=OFF) -> LINPUT2
        writeReg(0x20, 0x158); // Connect LMP2 (LINPUT2) to PGA
        break;

      case IN_MIC2_XLR_5V:
        // XLR +5V 偏壓鵝頸電容麥
        mcp_port_shadow |= (1 << bit_xlr_5v);
        writeMcpPort(mcp_port_shadow);
        writeReg(0x20, 0x158);
        delay(120);
        break;

      case IN_MIC2_XLR_48V:
        // XLR +48V 幻象電源專業電容麥
        mcp_port_shadow |= (1 << bit_xlr_48v);
        writeMcpPort(mcp_port_shadow);
        writeReg(0x20, 0x158);
        delay(350); // 等待 22uF/63V 隔直電容充電穩定後再解開靜音
        break;

      case IN_IN3_635_MIC:
        // 6.35mm TRS MIC 高增益模式 (Pad=OFF, PGA=+25dB)
        writeReg(0x20, 0x158);
        setMicPgaGain(25.0f);
        break;

      case IN_IN3_635_LINE:
        // 6.35mm TRS LINE-IN 線路輸入模式 (投入 -20dB Pad 衰減器, PGA=0dB)
        mcp_port_shadow |= (1 << bit_635_pad);
        writeMcpPort(mcp_port_shadow);
        writeReg(0x20, 0x148); // 0dB Boost, Line level
        setMicPgaGain(0.0f);
        break;

      case IN_MIC4_BOARD_MEMS:
        // 面板機載類比 MEMS 麥克風 -> LINPUT3
        writeReg(0x20, 0x198); // Connect LMP3 (LINPUT3) to PGA
        break;
    }

    // 步驟 4：恢復設定的 PGA 增益（解除靜音）
    if (mode != IN_IN3_635_LINE && mode != IN_IN3_635_MIC) {
      setMicPgaGain(current_pga_db);
    }
  }

  // ==========================================================================
  // ★ 核心功能 4：計算本機麥克風即時 VU 音量表 (RMS / Peak Hold / Clip 破音偵測)
  // 供 4.0" Config 大螢幕頂部 30fps 常駐儀表帶繪製
  // ==========================================================================
  void updateMicLevelMeter(const int16_t *pcm_stereo_or_mono, size_t frames, bool is_stereo) {
    if (frames == 0) return;
    int32_t max_abs = 0;
    uint64_t sum_sq = 0;
    for (size_t i = 0; i < frames; i++) {
      int32_t s = pcm_stereo_or_mono[is_stereo ? (i * 2) : i];
      int32_t a = abs(s);
      if (a > max_abs) max_abs = a;
      sum_sq += (uint64_t)(s * s);
    }
    float rms = sqrtf((float)sum_sq / (float)frames) / 32768.0f;
    float peak = (float)max_abs / 32768.0f;

    float cur_rms_db  = (rms  > 0.0001f) ? (20.0f * log10f(rms))  : -60.0f;
    float cur_peak_db = (peak > 0.0001f) ? (20.0f * log10f(peak)) : -60.0f;
    cur_rms_db  = constrain(cur_rms_db,  -60.0f, 0.0f);
    cur_peak_db = constrain(cur_peak_db, -60.0f, 0.0f);

    // 平滑衰減 (Ballistics) 與 1.5 秒峰值停留
    meter.rms_dbfs = (cur_rms_db > meter.rms_dbfs) ? cur_rms_db : (meter.rms_dbfs * 0.82f + cur_rms_db * 0.18f);
    uint32_t now = millis();
    if (cur_peak_db >= meter.peak_dbfs || (now - meter.peak_hold_ms > 1500)) {
      meter.peak_dbfs = cur_peak_db;
      meter.peak_hold_ms = now;
    }
    meter.clipping = (max_abs >= 31800); // > -0.25 dBFS 判定為 CLIP 破音
  }

private:
  void writeMcpPort(uint8_t val) {
    Wire.beginTransmission(mcp_addr);
    Wire.write(use_port_b ? 0x13 : 0x12); // GPIOA = 0x12, GPIOB = 0x13
    Wire.write(val);
    Wire.endTransmission();
  }
};
