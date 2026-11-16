#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include <WiFi.h>
#include <Adafruit_NeoPixel.h>
#include <driver/pcnt.h>
#include "wm8960_pro_audio.h"
#include "vban_engine.h"
#include "webui_server.h"
#include "lcd_ui_renderer.h"

// ============================================================================
// ★ 正式完整版主程式：ESP32-S3 獨立 36 鍵 (6x[2x3]) VBAN 路由拆分 & Intercom 主站
// 架構亮點：
//   - SPI2 (G10-14, G47) 100% 獨佔 W5500 有線網口 (支援固定 IP Static IP / DHCP)
//   - SPI3 (G1/G2/G42) 專供 6x 2.25" 小彩屏 + 1x 4.0" Config 大螢幕 (刷屏絕不卡音訊)
//   - 6x 74HC165 (48 bits)：36 工位鍵 + 3 旋鈕 S2 + 6 導覽鍵 + 三段式發話撥桿 + 腳踏 PTT
//   - 按鍵手勢 FSM：單擊=勾選「部分同發」 / 雙擊=「單獨 Mute 對方 🔇」 / 長按=「直通 PTT」
//   - 0.0ms WM8960 硬體類比自聽 + 1W 內建小喇叭發話自動消音防嘯叫 (Auto Speaker Cut)
//   - 內建 HTTP Port 80 Web UI 設定後台 (NVS 斷電保存 + VBAN 裝置名稱自動傳輸上屏)
// ============================================================================

#define PIN_HC165_CLK   40
#define PIN_HC165_LOAD  41
#define PIN_HC165_QH    21
#define PIN_SK6812      48
#define NUM_STATION_KEYS 36

Adafruit_NeoPixel leds(NUM_STATION_KEYS, PIN_SK6812, NEO_GRB + NEO_KHZ800);
SPIClass          spi3_lcd(HSPI);
WM8960ProAudio    audio;
VbanEngine        vban;
EmbeddedWebUIServer webui;
StudioLcdUiEngine lcd_ui;

uint8_t  hc_raw[6]  = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
uint8_t  hc_prev[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
uint8_t  mcp_portb_shadow = 0x01; // GPB0(背光)=1, GPB1(OnAir)=0, GPB3/4/5(48V/5V/Pad)=0
bool     global_lever_talking = false;

// 36 顆工位鍵的「單擊同發 / 雙擊 Mute 對方 / 長按 PTT」手勢狀態機
struct KeyGestureFSM {
  bool     is_down = false;
  bool     long_triggered = false;
  uint8_t  click_count = 0;
  uint32_t t_press_ms = 0;
  uint32_t t_release_ms = 0;
} key_fsm[NUM_STATION_KEYS];

// 當網頁後台或 4.0" 螢幕虛擬鍵盤按下「儲存」時，立即套用新設定
void applySystemConfig() {
  VbanEngine::safeCopyUtf8(vban.self_device_name, webui.cfg.device_name, sizeof(vban.self_device_name));
  VbanEngine::safeCopyUtf8(vban.self_stream_name, webui.cfg.vban_stream, sizeof(vban.self_stream_name));
  audio.speaker_user_on  = webui.cfg.speaker_on;
  audio.sidetone_db      = webui.cfg.sidetone_db;
  audio.sidetone_enabled = webui.cfg.sidetone_en;
  audio.setMicPgaGain(webui.cfg.mic_pga_db);
  audio.selectInputSource((AudioInputMode)webui.cfg.audio_input_mode, mcp_portb_shadow);
  leds.setBrightness(webui.cfg.led_brightness);
}

void scan6xHc165() {
  memset(hc_raw, 0, sizeof(hc_raw));
  digitalWrite(PIN_HC165_LOAD, LOW);
  delayMicroseconds(2);
  digitalWrite(PIN_HC165_LOAD, HIGH);
  for (int b = 47; b >= 0; b--) {
    if (digitalRead(PIN_HC165_QH)) hc_raw[b >> 3] |= (1 << (b & 7));
    digitalWrite(PIN_HC165_CLK, HIGH);
    digitalWrite(PIN_HC165_CLK, LOW);
  }
}

// 處理 36 顆工位按鍵的單擊（部分同發）、雙擊（Mute 對方）、長按（直通 PTT）
void processKeyGestures() {
  uint32_t now = millis();
  for (int k = 0; k < NUM_STATION_KEYS; k++) {
    bool down = !(hc_raw[k >> 3] & (1 << (k & 7)));
    KeyGestureFSM &f = key_fsm[k];

    if (down && !f.is_down) {
      f.is_down = true;
      f.t_press_ms = now;
      f.long_triggered = false;
    } else if (down && f.is_down) {
      // 按住超過 250ms -> 啟動單鍵直通 PTT 發話！
      if (!f.long_triggered && (now - f.t_press_ms >= 250)) {
        f.long_triggered = true;
        f.click_count = 0;
        if (k < 28) vban.stations[k].momentary_ptt = true;
      }
    } else if (!down && f.is_down) {
      f.is_down = false;
      if (f.long_triggered) {
        if (k < 28) vban.stations[k].momentary_ptt = false;
      } else {
        f.click_count++;
        f.t_release_ms = now;
      }
    }

    // 判斷單擊 vs 雙擊 (180ms 觀察窗)
    if (!f.is_down && f.click_count > 0 && (now - f.t_release_ms > 180)) {
      if (f.click_count >= 2) {
        // ★ 雙擊：單獨 Mute / 解開該工位的來話聲音 (Listen Mute 🔇)
        if (k < 24) vban.stations[k].listen_muted = !vban.stations[k].listen_muted;
      } else {
        // ★ 單擊：功能鍵或勾選加入「🩵 可選部分同發 (Multi-Select)」
        if (k == 32) { // K32 = CLR 一鍵清空所有同發選取
          for (int i = 0; i < 28; i++) vban.stations[i].selected_for_talk = false;
        } else if (k == 33 || k == 34) { // K33/K34 = 啟停 VBAN 8ch 拆分規則 1/2
          vban.split_routes[k - 33].enabled = !vban.split_routes[k - 33].enabled;
        } else if (k == 35) { // K35 = DUCK 自動閃避開關
          vban.auto_ducking_enabled = !vban.auto_ducking_enabled;
        } else if (k < 28) {
          vban.stations[k].selected_for_talk = !vban.stations[k].selected_for_talk;
        }
      }
      f.click_count = 0;
    }
  }

  // 檢查「🕹️ 三段式發話撥桿 (U6.F = Latch, U6.G = PTT) + U6.H (背板腳踏 PTT)」
  bool latch_on = !(hc_raw[5] & (1 << 5));
  bool ptt_on   = !(hc_raw[5] & (1 << 6));
  bool foot_ptt = !(hc_raw[5] & (1 << 7));
  bool any_key_ptt = false;
  for (int i = 0; i < 28; i++) if (vban.stations[i].momentary_ptt) any_key_ptt = true;

  bool now_talking = latch_on || ptt_on || foot_ptt || any_key_ptt;
  if (now_talking != global_lever_talking) {
    global_lever_talking = now_talking;
    // ★ 發話狀態改變：立即切斷內建小喇叭防嘯叫 + 開關 0.0ms 硬體類比自聽 + 點亮機箱 On-Air 紅燈！
    audio.onTalkStateChanged(global_lever_talking);
    if (global_lever_talking) mcp_portb_shadow |= (1 << 1);  // GPB1 On-Air LED ON
    else                      mcp_portb_shadow &= ~(1 << 1); // GPB1 On-Air LED OFF
  }
}

// 更新 36 顆 SK6812 智慧語音 Tally 燈號（含 VOX 來電有聲音自動閃燈）
void refreshSk6812Tally() {
  bool flash_on = ((millis() / 120) & 1); // 4.1 Hz 快閃
  for (int i = 0; i < NUM_STATION_KEYS; i++) {
    uint32_t col = leds.Color(8, 12, 18); // 待機微亮背光
    if (i < 28) {
      const IntercomStationSlot &s = vban.stations[i];
      bool is_talking_to = s.momentary_ptt || (global_lever_talking && s.selected_for_talk);
      if (s.incoming_call_req) {
        col = flash_on ? leds.Color(255, 0, 0) : leds.Color(255, 255, 255); // 🔴 呼叫爆閃
      } else if (s.vox_speaking && !s.listen_muted) {
        col = flash_on ? leds.Color(255, 130, 0) : leds.Color(255, 255, 160); // 🟠 來電有聲音自動閃燈 (VOX)
      } else if (is_talking_to) {
        col = leds.Color(0, 230, 40); // 🟢 發話中綠燈
      } else if (s.listen_muted) {
        col = leds.Color(100, 0, 120); // 🟣 單獨 Mute 對方紫燈 (🔇)
      } else if (s.selected_for_talk) {
        col = leds.Color(0, 180, 220); // 🩵 已勾選「部分同發」青燈
      }
    } else if (i == 33 || i == 34) {
      if (vban.split_routes[i - 33].enabled) col = leds.Color(20, 90, 255); // 🔵 拆分轉發啟用
    } else if (i == 35 && vban.auto_ducking_enabled) {
      col = leds.Color(180, 140, 0);
    }
    leds.setPixelColor(i, col);
  }
  leds.show();
}

void setup() {
  Serial.begin(115200);
  pinMode(PIN_HC165_CLK, OUTPUT);
  pinMode(PIN_HC165_LOAD, OUTPUT);
  pinMode(PIN_HC165_QH, INPUT_PULLUP);
  digitalWrite(PIN_HC165_LOAD, HIGH);

  Wire.begin(8, 9, 800000);
  leds.begin();

  // 初始化 SPI3 彩屏群與 MCP23017 (0x20)
  spi3_lcd.begin(2, -1, 1, -1);
  lcd_ui.begin(&spi3_lcd, 42, false);

  // 初始化 WM8960 (I2S0: BCLK=G4, LRCK=G5, DOUT=G38, DIN=G39; Relay=MCP 0x20 PortB GPB3/4/5)
  audio.begin(4, 5, 38, 39, 0x20, true, 3, 4, 5);

  // 載入 NVS 設定並啟動內建 HTTP Port 80 Web UI 伺服器與 VBAN 引擎
  webui.begin(applySystemConfig);
  applySystemConfig();

  // 啟動 Wi-Fi (支援固定 IP 或 DHCP)
  if (webui.cfg.eth_static_enable) {
    WiFi.config(IPAddress(webui.cfg.eth_ip), IPAddress(webui.cfg.eth_gw),
                IPAddress(webui.cfg.eth_mask), IPAddress(webui.cfg.eth_dns));
  }
  WiFi.mode(WIFI_STA);
  WiFi.begin(webui.cfg.wifi_ssid, webui.cfg.wifi_pass);
  vban.begin(VBAN_UDP_PORT);

  Serial.println("✅ 36 鍵獨立 VBAN Intercom 主站已啟動！Web UI 於 Port 80 就緒。");
}

int16_t i2s_rx_stereo[256];
int16_t mic_mono_frame[128];
int16_t vban_out_stereo[256];

void loop() {
  // 1. 掃描 6 片 74HC165 (36 鍵 + 導覽鍵 + 三段式發話撥桿)
  scan6xHc165();
  processKeyGestures();

  // 2. 讀取本機麥克風 I2S0 ➔ 更新 4.0" 螢幕 Mic VU 音量表 ➔ 若處於發話狀態則打出 VBAN
  size_t bytes_read = 0;
  i2s_read(I2S_NUM_0, i2s_rx_stereo, sizeof(i2s_rx_stereo), &bytes_read, 0);
  if (bytes_read > 0) {
    size_t frames = bytes_read / 4;
    audio.updateMicLevelMeter(i2s_rx_stereo, frames, true);
    if (global_lever_talking) {
      for (size_t f = 0; f < frames; f++) mic_mono_frame[f] = i2s_rx_stereo[f * 2];
      vban.transmitMicToSelectedTargets(mic_mono_frame, frames, global_lever_talking);
    }
  }

  // 3. 接收區網 VBAN 音訊與遠端裝置名稱 ➔ VOX 閃燈偵測、過濾被 Mute 的人、8ch 拆分轉發、送入耳機/小喇叭
  size_t rx_frames = vban.pollAndProcessIncomingVban(vban_out_stereo, 128);
  if (rx_frames > 0) {
    size_t bytes_written = 0;
    i2s_write(I2S_NUM_0, vban_out_stereo, rx_frames * 4, &bytes_written, 0);
  }

  // 4. 每 2 秒廣播本機 VBAN-TEXT 中文裝置名稱，並處理內建 Web UI HTTP 請求
  vban.broadcastIdentityBeacon();
  webui.poll();

  // 5. 30fps 更新 SK6812 智慧語音閃燈與 4.0" 大螢幕儀表板
  static uint32_t last_ui_ms = 0;
  if (millis() - last_ui_ms >= 33) {
    last_ui_ms = millis();
    refreshSk6812Tally();
    lcd_ui.renderBigLcdDashboard(audio, vban, webui.cfg, global_lever_talking);
  }
}
