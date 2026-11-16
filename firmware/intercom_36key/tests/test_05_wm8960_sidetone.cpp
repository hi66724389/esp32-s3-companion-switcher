#include <Arduino.h>
#include <Wire.h>
#include "wm8960_pro_audio.h"

// ============================================================================
// 【階段 5 測試】36 鍵 Intercom 主站：WM8960 四合一音訊前端 + 0ms 自聽 + 1W 喇叭防嘯叫測試
// 腳位：
//   I2C  : G8 (SDA), G9 (SCL) -> WM8960 (0x1A) & MCP23017 (0x20)
//   I2S0 : G4 (BCLK), G5 (LRCK), G38 (DOUT/DACDAT), G39 (DIN/ADCDAT)
//   MCP GPB3 = XLR +48V, GPB4 = XLR +5V, GPB5 = 6.35mm -20dB Pad
// 串口鍵盤指令測試：
//   輸入 '1'~'7' 切換 7 種音源模式 (3.5mm / XLR OFF,5V,48V / 6.35mm Mic,Line / 機載MEMS)
//   輸入 't' 切換模擬「壓下發話撥桿 (TALK ON-AIR)」-> 驗證小喇叭自動 Cut + 耳機開啟 0ms 自聽！
// ============================================================================

WM8960ProAudio audio;
uint8_t mcp_portb_shadow = 0x01; // GPB0(背光)=1, GPB3/4/5(48V/5V/Pad)=0
bool    sim_talking = false;
int16_t rx_pcm[256];

void setup() {
  Serial.begin(115200);
  delay(800);
  Serial.println("\n=======================================================");
  Serial.println(" [STAGE 5 TEST] WM8960 0ms Sidetone + Speaker Cut + XLR");
  Serial.println("=======================================================");

  Wire.begin(8, 9, 400000);
  audio.begin(4, 5, 38, 39, 0x20, true, 3, 4, 5);
  audio.selectInputSource(IN_MIC2_XLR_48V, mcp_portb_shadow);

  Serial.println("串口指令：[1]3.5mm [2]XLR動圈 [3]XLR+5V [4]XLR+48V [5]6.35Mic [6]6.35Line [7]機載MEMS [t]切換發話/小喇叭防嘯叫");
}

void loop() {
  if (Serial.available()) {
    char c = Serial.read();
    if (c >= '1' && c <= '7') {
      AudioInputMode m = (AudioInputMode)(c - '1');
      Serial.printf("🔀 切換音訊前端模式 -> %d (含 Break-Before-Make 防爆音互斥鎖)...\n", (int)m);
      audio.selectInputSource(m, mcp_portb_shadow);
    } else if (c == 't' || c == 'T') {
      sim_talking = !sim_talking;
      audio.onTalkStateChanged(sim_talking);
      Serial.printf("🕹️ 發話狀態 = %s | 🔊內建1W小喇叭 = %s | 🎧0.0ms耳機自聽 = %s\n",
                    sim_talking ? "ON-AIR 發話中" : "MUTE 待機",
                    sim_talking ? "🔇已自動切斷(Auto-Cut防嘯叫)" : "🔊正常監聽開啟",
                    sim_talking ? "✅已開啟(0.0ms類比直入耳機)" : "關閉");
    }
  }

  // 讀取 I2S0 麥克風資料並計算即時 VU 音量表
  size_t bytes_read = 0;
  i2s_read(I2S_NUM_0, rx_pcm, sizeof(rx_pcm), &bytes_read, 10 / portTICK_PERIOD_MS);
  if (bytes_read > 0) {
    audio.updateMicLevelMeter(rx_pcm, bytes_read / 4, true);
  }

  static uint32_t last_print = 0;
  if (millis() - last_print > 250) {
    last_print = millis();
    Serial.printf("🎙️ MIC VU: %6.1f dBFS (Peak: %6.1f dB) [%s] | PGA Gain: +%.1f dB\n",
                  audio.meter.rms_dbfs, audio.meter.peak_dbfs,
                  audio.meter.clipping ? "🔴CLIP!" : "OK", audio.current_pga_db);
  }
}
