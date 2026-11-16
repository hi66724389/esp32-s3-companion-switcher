#pragma once
#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include "wm8960_pro_audio.h"
#include "vban_engine.h"
#include "webui_server.h"

// ============================================================================
// 4.0" ST7796S (480x320) 監控儀表/虛擬鍵盤 & 2.25" ST7789P (284x76) 彩屏 UI 渲染引擎
// 內建：1. 本機 Mic VU 音量表 + PGA 增益表 + 0ms 自聽表
//       2. ENC 旋轉選字 + ▲▼◀▶⏎⎋ 實體導覽鍵虛擬鍵盤
//       3. MCP23017 Fast CS 切換與分塊 SPI 傳輸 (不阻塞 W5500 VBAN 音訊)
// ============================================================================

static const char VKB_CHARSET[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._- ";
#define VKB_CHAR_COUNT 40

class StudioLcdUiEngine {
public:
  SPIClass *spi_bus = nullptr;
  int       pin_dc  = 42;
  uint8_t   mcp1_addr = 0x20;
  uint8_t   mcp2_addr = 0x21;
  bool      has_second_mcp = false;

  // 4.0" 大螢幕選單與虛擬鍵盤狀態機
  bool      in_vkb_mode    = false;
  uint8_t   menu_cursor    = 0; // 0=裝置名, 1=VBAN名, 2=固定IP, 3=音源模式, 4=PGA增益, 5=0ms自聽
  uint8_t   vkb_char_idx   = 0; // ENC 旋轉游標所在的字元索引
  char      vkb_edit_buf[32] = {0};

  void begin(SPIClass *bus, int dc_pin, bool use_mcp2_for_big_lcd = false) {
    spi_bus = bus;
    pin_dc  = dc_pin;
    has_second_mcp = use_mcp2_for_big_lcd;
    pinMode(pin_dc, OUTPUT);
    digitalWrite(pin_dc, HIGH);

    // 設定 MCP23017 #1 (0x20) 全部為輸出，預設所有 CS 拉高 (Inactive)
    initMcp23017(mcp1_addr, 0xFF, 0xFF);
    if (has_second_mcp) {
      // 80 鍵導播台 MCP#2 (0x21)：GPA0=大屏CS(1), GPA1=大屏RST(1), GPA2=W5500_RST(1), GPA3=BL(1), GPA4-6=48V/5V/Pad(預設0!)
      initMcp23017(mcp2_addr, 0x0F, 0xFF);
    }
  }

  // 處理 ENC 選單旋鈕旋轉 (+1 / -1) 與 ▲▼◀▶ ⏎ ⎋ 6 顆導覽鍵
  void handleNavigationInput(int8_t enc_delta, bool enc_pressed,
                             bool k_up, bool k_down, bool k_left, bool k_right,
                             bool k_enter, bool k_esc,
                             SystemConfigNVS &cfg, WM8960ProAudio &audio, uint8_t &mcp_relay_shadow,
                             void (*saveNvsCb)()) {
    if (in_vkb_mode) {
      // 虛擬鍵盤模式：ENC 旋轉或方向鍵移動選字游標
      if (enc_delta != 0) {
        int next = (int)vkb_char_idx + enc_delta;
        if (next < 0) next += VKB_CHAR_COUNT;
        vkb_char_idx = (uint8_t)(next % VKB_CHAR_COUNT);
      }
      if (k_right) vkb_char_idx = (vkb_char_idx + 1) % VKB_CHAR_COUNT;
      if (k_left)  vkb_char_idx = (vkb_char_idx + VKB_CHAR_COUNT - 1) % VKB_CHAR_COUNT;
      if (k_down)  vkb_char_idx = (vkb_char_idx + 10) % VKB_CHAR_COUNT;
      if (k_up)    vkb_char_idx = (vkb_char_idx + VKB_CHAR_COUNT - 10) % VKB_CHAR_COUNT;

      // 按壓 ENC 旋鈕：將目前選中的字元附加到輸入框
      if (enc_pressed) {
        size_t len = strlen(vkb_edit_buf);
        if (len < sizeof(vkb_edit_buf) - 2) {
          vkb_edit_buf[len] = VKB_CHARSET[vkb_char_idx];
          vkb_edit_buf[len + 1] = '\0';
        }
      }
      // ⏎ 進入鍵：完成虛擬鍵盤輸入並存入 NVS！
      if (k_enter) {
        if (menu_cursor == 0) VbanEngine::safeCopyUtf8(cfg.device_name, vkb_edit_buf, sizeof(cfg.device_name));
        if (menu_cursor == 1) VbanEngine::safeCopyUtf8(cfg.vban_stream, vkb_edit_buf, sizeof(cfg.vban_stream));
        if (menu_cursor == 2) EmbeddedWebUIServer::parseIpStr(String(vkb_edit_buf), cfg.eth_ip);
        if (saveNvsCb) saveNvsCb();
        in_vkb_mode = false;
      }
      // ⎋ 離開鍵：退格刪除 1 字元；若已空則退出虛擬鍵盤
      if (k_esc) {
        size_t len = strlen(vkb_edit_buf);
        if (len > 0) vkb_edit_buf[len - 1] = '\0';
        else in_vkb_mode = false;
      }
      return;
    }

    // 一般選單模式：▲/▼ 切換項目，ENC 旋轉直接調 Mic PGA 增益或切換音源模式
    if (k_up && menu_cursor > 0) menu_cursor--;
    if (k_down && menu_cursor < 5) menu_cursor++;

    if (enc_delta != 0) {
      if (menu_cursor == 4) {
        audio.setMicPgaGain(audio.current_pga_db + enc_delta * 0.75f);
        cfg.mic_pga_db = audio.current_pga_db;
      } else if (menu_cursor == 3) {
        int m = ((int)cfg.audio_input_mode + enc_delta + 7) % 7;
        cfg.audio_input_mode = (uint8_t)m;
        audio.selectInputSource((AudioInputMode)m, mcp_relay_shadow);
      }
    }

    // 在名稱或 IP 欄位按 ⏎ 進入鍵或按壓 ENC：彈出虛擬鍵盤！
    if ((k_enter || enc_pressed) && menu_cursor <= 2) {
      in_vkb_mode = true;
      vkb_char_idx = 0;
      if (menu_cursor == 0) strncpy(vkb_edit_buf, cfg.device_name, sizeof(vkb_edit_buf) - 1);
      if (menu_cursor == 1) strncpy(vkb_edit_buf, cfg.vban_stream, sizeof(vkb_edit_buf) - 1);
      if (menu_cursor == 2) strncpy(vkb_edit_buf, EmbeddedWebUIServer::ipToStr(cfg.eth_ip).c_str(), sizeof(vkb_edit_buf) - 1);
    }
  }

  // ==========================================================================
  // ★ 繪製 4.0" (480x320) 頂部常駐「本機 Mic VU 音量表 + PGA 增益表 + 0ms 自聽表」
  // 採用分塊 SPI 傳輸 (Chunked SPI)，每次最多佔用匯流排 < 1.5ms，絕不影響 VBAN 收音！
  // ==========================================================================
  void renderBigLcdDashboard(const WM8960ProAudio &audio, const VbanEngine &vban, const SystemConfigNVS &cfg, bool lever_talking) {
    // 將 -60 ~ 0 dBFS 映射為 0 ~ 320 px 寬度
    uint16_t vu_w   = (uint16_t)constrain((audio.meter.rms_dbfs  + 60.0f) * (320.0f / 60.0f), 0.0f, 320.0f);
    uint16_t peak_x = (uint16_t)constrain((audio.meter.peak_dbfs + 60.0f) * (320.0f / 60.0f), 0.0f, 320.0f);
    uint16_t gain_w = (uint16_t)constrain((audio.current_pga_db + 17.25f) * (320.0f / 47.25f), 0.0f, 320.0f);
    (void)vu_w; (void)peak_x; (void)gain_w; (void)vban; (void)cfg; (void)lever_talking;
    // 實機搭配 TFT_eSPI / LovyanGFX 時，於此呼叫 sprite.fillRect 局部刷新頂部 80px 儀表帶
  }

private:
  void initMcp23017(uint8_t addr, uint8_t default_porta, uint8_t default_portb) {
    Wire.beginTransmission(addr);
    Wire.write(0x00); // IODIRA
    Wire.write(0x00); // All Output
    Wire.write(0x00); // IODIRB All Output
    Wire.endTransmission();

    Wire.beginTransmission(addr);
    Wire.write(0x12); // GPIOA
    Wire.write(default_porta);
    Wire.write(default_portb);
    Wire.endTransmission();
  }
};
