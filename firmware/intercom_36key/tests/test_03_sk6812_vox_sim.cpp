#include <Arduino.h>
#include <Adafruit_NeoPixel.h>

// ============================================================================
// 【階段 3 測試】36 鍵 Intercom 主站：36 顆 SK6812 智慧語音 Tally 燈號與 VOX 閃爍測試
// 腳位：GPIO 48 (串聯 330Ω 電阻接 LED #0 DIN)
// 驗證重點：
//   - K0–K5  (Pod 1)：展示「🟢 發話中綠燈 (Talk Active)」
//   - K6–K11 (Pod 2)：展示「🟠 來電有聲音自動閃燈 (VOX Audio Tally > -40dBFS 橘白快閃)」
//   - K12–K17(Pod 3)：展示「🩵 可選部分同發預選燈 (Multi-Select Cyan)」
//   - K18–K23(Pod 4)：展示「🟣 單獨 Mute 對方禁聽暗紫燈 (Listen Muted 🔇)」
//   - K24–K29(Pod 5)：展示「🔴 對方按 Call 呼叫急促紅白爆閃」
// ============================================================================

#define PIN_SK6812   48
#define NUM_LEDS     36

Adafruit_NeoPixel strip(NUM_LEDS, PIN_SK6812, NEO_GRB + NEO_KHZ800);
bool flash_phase = false;

void setup() {
  Serial.begin(115200);
  delay(800);
  Serial.println("\n=======================================================");
  Serial.println(" [STAGE 3 TEST] 36x SK6812 Tally + VOX Flash Simulation");
  Serial.println("=======================================================");

  strip.begin();
  strip.setBrightness(150);
  strip.clear();
  strip.show();
}

void loop() {
  flash_phase = !flash_phase;

  for (int i = 0; i < NUM_LEDS; i++) {
    if (i < 6) {
      // Pod 1: 🟢 綠燈常亮 (Talk Active)
      strip.setPixelColor(i, strip.Color(0, 220, 40));
    } else if (i < 12) {
      // Pod 2: 🟠 橘白快閃 (VOX 對方開口講話自動閃燈)
      strip.setPixelColor(i, flash_phase ? strip.Color(255, 140, 0) : strip.Color(255, 255, 180));
    } else if (i < 18) {
      // Pod 3: 🩵 青色預選燈 (已勾選部分同發 Multi-Select)
      strip.setPixelColor(i, strip.Color(0, 190, 230));
    } else if (i < 24) {
      // Pod 4: 🟣 紫紅暗燈 (已單獨 Mute 對方來話 🔇)
      strip.setPixelColor(i, strip.Color(90, 0, 110));
    } else if (i < 30) {
      // Pod 5: 🔴 紅白交替閃爍 (Incoming Call 呼叫信令)
      strip.setPixelColor(i, flash_phase ? strip.Color(255, 0, 0) : strip.Color(40, 0, 0));
    } else {
      // Pod 6: 🔵 藍色 (VBAN 8ch 拆分路由啟用中)
      strip.setPixelColor(i, strip.Color(20, 80, 255));
    }
  }

  strip.show();
  delay(180); // 約 2.7Hz 語音閃燈節奏
}
