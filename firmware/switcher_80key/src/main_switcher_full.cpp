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
// ★ 正式完整版主程式：ESP32-S3 80 鍵 (13x[2x3]+2) + 11xEC11 + 15x2.25" + 4.0" 大屏 主導播切換台 v2
// 架構亮點：
//   - 15x 74HC165 (120 bits @ 2kHz)：80 鍵 + 11 旋鈕 S2 + E4–E10/ENC11 AB 相 + 6 顆 Config 導覽鍵
//   - 雙 MCP23017：0x20 滿載 15 塊 2.25" 小屏 CS1–15+RES；0x21 管 4.0" 大屏 CS/RST/BL、W5500 RST 與 XLR 48V/5V/Pad
//   - I2S0 全雙工音訊 (G2 BCLK / G21 LRCK / G38 DOUT / G39 DIN) 驅動 WM8960 (含 0.0ms 類比自聽)
//   - Companion Satellite TCP 16622 引擎 + VBAN 收發/名稱同步 + 內建 HTTP Port 80 Web UI (固定 IP / NVS)
// ============================================================================

#define PIN_HC165_CLK   40
#define PIN_HC165_LOAD  41
#define PIN_HC165_QH    42
#define PIN_SK6812      48
#define PIN_TBAR_ADC    1
#define NUM_MAIN_KEYS   80

Adafruit_NeoPixel leds(NUM_MAIN_KEYS, PIN_SK6812, NEO_GRB + NEO_KHZ800);
SPIClass          spi2_shared(FSPI);
WM8960ProAudio    audio;
VbanEngine        vban;
EmbeddedWebUIServer webui;
StudioLcdUiEngine lcd_ui;
WiFiClient        sat_client;

uint8_t hc_raw[15];
uint8_t hc_prev[15];
uint8_t key_debounce_cnt[NUM_MAIN_KEYS] = {0};
uint8_t key_stable_bits[10] = {0}; // 80 keys
uint8_t mcp2_porta_shadow = 0x0F;  // GPA0(大屏CS)=1, GPA1(RST)=1, GPA2(W5500_RST)=1, GPA3(BL)=1, GPA4-6(48V/5V/Pad)=0

float   tbar_ema = 0.0f;
int     last_tbar_pct = -1;

// 掃描鏈上的 8 顆旋鈕 (E4..E10 + ENC11)
static const int8_t GRAY_LUT[16] = {0,-1,1,0, 1,0,0,-1, -1,0,0,1, 0,1,-1,0};
uint8_t chain_enc_prev[8] = {0};
int8_t  chain_enc_acc[8]  = {0};
const uint8_t CHAIN_ENC_A[8] = {111, 90, 92, 94, 96, 98, 100, 102};
const uint8_t CHAIN_ENC_B[8] = {112, 91, 93, 95, 97, 99, 101, 103};

inline bool getBit120(const uint8_t *buf, uint8_t bit_idx) {
  return (buf[bit_idx >> 3] & (1 << (bit_idx & 7))) != 0;
}

void applySwitcherConfig() {
  VbanEngine::safeCopyUtf8(vban.self_device_name, webui.cfg.device_name, sizeof(vban.self_device_name));
  VbanEngine::safeCopyUtf8(vban.self_stream_name, webui.cfg.vban_stream, sizeof(vban.self_stream_name));
  audio.sidetone_db      = webui.cfg.sidetone_db;
  audio.sidetone_enabled = webui.cfg.sidetone_en;
  audio.setMicPgaGain(webui.cfg.mic_pga_db);
  // 80 鍵導播台的 XLR 48V/5V 與 6.35mm Pad 接在 MCP#2 (0x21) Port A 的 GPA4, GPA5, GPA6
  audio.selectInputSource((AudioInputMode)webui.cfg.audio_input_mode, mcp2_porta_shadow);
  leds.setBrightness(webui.cfg.led_brightness);
}

void scan15xHc165() {
  memset(hc_raw, 0, sizeof(hc_raw));
  digitalWrite(PIN_HC165_LOAD, LOW);
  delayMicroseconds(1);
  digitalWrite(PIN_HC165_LOAD, HIGH);
  for (int b = 119; b >= 0; b--) {
    if (digitalRead(PIN_HC165_QH)) hc_raw[b >> 3] |= (1 << (b & 7));
    digitalWrite(PIN_HC165_CLK, HIGH);
    digitalWrite(PIN_HC165_CLK, LOW);
  }
}

void sendSatelliteCmd(const char *fmt, ...) {
  if (!sat_client.connected()) return;
  char buf[128];
  va_list args;
  va_start(args, fmt);
  vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);
  sat_client.println(buf);
}

void process80KeysAndChainEncoders() {
  // 1. 80 顆切台機械按鍵去抖與上報 Companion (KEY 0–79)
  for (int k = 0; k < NUM_MAIN_KEYS; k++) {
    bool down = !getBit120(hc_raw, k);
    bool cur  = (key_stable_bits[k >> 3] & (1 << (k & 7))) != 0;
    if (down != cur) {
      if (++key_debounce_cnt[k] >= 6) {
        key_stable_bits[k >> 3] ^= (1 << (k & 7));
        key_debounce_cnt[k] = 0;
        sendSatelliteCmd("KEY-PRESS DEVICEID=esp32s3 KEY=%d PRESSED=%s", k, down ? "true" : "false");
      }
    } else {
      key_debounce_cnt[k] = 0;
    }
  }

  // 2. 掃描鏈上的 E4..E10 與 ENC11 旋鈕正交灰碼解碼
  int8_t enc11_step = 0;
  for (int e = 0; e < 8; e++) {
    uint8_t ab = (getBit120(hc_raw, CHAIN_ENC_A[e]) << 1) | getBit120(hc_raw, CHAIN_ENC_B[e]);
    int8_t d = GRAY_LUT[(chain_enc_prev[e] << 2) | ab];
    chain_enc_prev[e] = ab;
    if (d != 0) {
      chain_enc_acc[e] += d;
      if (chain_enc_acc[e] >= 2 || chain_enc_acc[e] <= -2) {
        int dir = (chain_enc_acc[e] > 0) ? 1 : -1;
        chain_enc_acc[e] = 0;
        if (e == 7) {
          enc11_step = dir; // ENC11 專供本機 4.0" Config 大螢幕與虛擬鍵盤使用
        } else if (e == 1) {
          // E5 = 本機 WM8960 監聽耳機音量即時調整
          audio.setHeadphoneVolume(audio.hp_volume + dir * 2);
          sendSatelliteCmd("KEY-ROTATE DEVICEID=esp32s3 KEY=%d DIRECTION=%d", 83 + e, dir);
        } else {
          sendSatelliteCmd("KEY-ROTATE DEVICEID=esp32s3 KEY=%d DIRECTION=%d", 83 + e, dir);
        }
      }
    }
  }

  // 3. 檢查 ENC11 按壓 (U14.A = Bit 104) 與 ▲▼◀▶⏎⎋ 6 顆導覽鍵 (U14.B–G = Bits 105..110)
  bool enc11_click = (!getBit120(hc_raw, 104) && getBit120(hc_prev, 104));
  bool nav_edge[6];
  for (int i = 0; i < 6; i++) {
    nav_edge[i] = (!getBit120(hc_raw, 105 + i) && getBit120(hc_prev, 105 + i));
  }
  if (enc11_step != 0 || enc11_click || nav_edge[0] || nav_edge[1] || nav_edge[2] || nav_edge[3] || nav_edge[4] || nav_edge[5]) {
    lcd_ui.handleNavigationInput(enc11_step, enc11_click,
                                 nav_edge[0], nav_edge[1], nav_edge[2], nav_edge[3], nav_edge[4], nav_edge[5],
                                 webui.cfg, audio, mcp2_porta_shadow,
                                 []() { webui.saveToNVS(); applySwitcherConfig(); });
  }
  memcpy(hc_prev, hc_raw, sizeof(hc_raw));
}

void setup() {
  Serial.begin(115200);
  pinMode(PIN_HC165_CLK, OUTPUT);
  pinMode(PIN_HC165_LOAD, OUTPUT);
  pinMode(PIN_HC165_QH, INPUT_PULLUP);
  digitalWrite(PIN_HC165_LOAD, HIGH);
  pinMode(10, OUTPUT);
  digitalWrite(10, HIGH); // W5500 CS HIGH

  Wire.begin(8, 9, 800000);
  leds.begin();

  // 初始化 SPI2 (G11 MOSI, G12 SCLK, G47 DC) 與雙 MCP23017 (0x20 + 0x21)
  spi2_shared.begin(12, 13, 11, -1);
  lcd_ui.begin(&spi2_shared, 47, true);

  // 初始化 WM8960 音訊 (I2S0: G2=BCLK, G21=LRCK, G38=DOUT, G39=DIN; Relay=MCP#2 0x21 PortA GPA4/5/6)
  audio.begin(2, 21, 38, 39, 0x21, false, 4, 5, 6);

  // 啟動內建 HTTP Port 80 Web UI 伺服器與 NVS
  webui.begin(applySwitcherConfig);
  applySwitcherConfig();

  if (webui.cfg.eth_static_enable) {
    WiFi.config(IPAddress(webui.cfg.eth_ip), IPAddress(webui.cfg.eth_gw),
                IPAddress(webui.cfg.eth_mask), IPAddress(webui.cfg.eth_dns));
  }
  WiFi.mode(WIFI_STA);
  WiFi.begin(webui.cfg.wifi_ssid, webui.cfg.wifi_pass);
  vban.begin(VBAN_UDP_PORT);

  Serial.println("✅ 80 鍵主導播切換台 v2 已啟動！Web UI (Port 80) 與 VBAN 引擎就緒。");
}

int16_t switcher_rx_pcm[256];
int16_t switcher_vban_out[256];

void loop() {
  // 1. 掃描 15 片 74HC165 (80 鍵 + 旋鈕 + 6 顆 Config 導覽鍵)
  scan15xHc165();
  process80KeysAndChainEncoders();

  // 2. 讀取 T-Bar 推桿 (G1) 並上報 Companion
  uint32_t acc = 0;
  for (int i = 0; i < 8; i++) acc += analogRead(PIN_TBAR_ADC);
  tbar_ema = tbar_ema * 0.85f + (float)(acc >> 3) * 0.15f;
  int pct = constrain((int)((tbar_ema - 40.0f) * 100.0f / 4010.0f + 0.5f), 0, 100);
  if (abs(pct - last_tbar_pct) >= 1) {
    last_tbar_pct = pct;
    sendSatelliteCmd("SET-VARIABLE-VALUE DEVICEID=esp32s3 VARIABLE=tbar VALUE=%d", pct);
  }

  // 3. 處理 WM8960 麥克風 VU 表與 VBAN 音訊收發
  size_t bytes_read = 0;
  i2s_read(I2S_NUM_0, switcher_rx_pcm, sizeof(switcher_rx_pcm), &bytes_read, 0);
  if (bytes_read > 0) {
    audio.updateMicLevelMeter(switcher_rx_pcm, bytes_read / 4, true);
  }
  size_t rx_frames = vban.pollAndProcessIncomingVban(switcher_vban_out, 128);
  if (rx_frames > 0) {
    size_t bw = 0;
    i2s_write(I2S_NUM_0, switcher_vban_out, rx_frames * 4, &bw, 0);
  }

  // 4. VBAN 裝置名稱廣播 & Web UI HTTP 服務
  vban.broadcastIdentityBeacon();
  webui.poll();
}
