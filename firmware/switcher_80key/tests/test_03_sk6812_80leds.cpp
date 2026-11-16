#include <Arduino.h>
#include <Adafruit_NeoPixel.h>

// ============================================================================
// 【階段 3 測試】80 鍵主導播台 v2：80 顆 SK6812 Tally 燈鏈與三點補電 (#0/#35/#79) 壓力測試
// 腳位：GPIO 48 -> 串 330Ω -> LED #0 DIN .. LED #79
// 驗證重點：
//   - LED 0..17  (Pod 7–9 PVW 預覽區) : 🟢 綠燈
//   - LED 18..35 (Pod 4–6 PGM 節目區) : 🔴 紅燈 (#35 中段補電點)
//   - LED 36..47 (Pod 1–2 DSK 下游鍵) : 🟡 琥珀燈
//   - LED 48..59 (Pod 3,10 USER 巨集) : 🟣 紫燈
//   - LED 60..77 (Pod 11–13 TRANS/RPLY): 🔵 青藍燈
//   - LED 78..79 (T-Bar CUT / AUTO)   : 🔴 紅 / 🟢 綠 (#79 尾端補電點)
// ============================================================================

#define PIN_SK6812 48
#define NUM_LEDS   80

Adafruit_NeoPixel strip(NUM_LEDS, PIN_SK6812, NEO_GRB + NEO_KHZ800);

void setup() {
  Serial.begin(115200);
  delay(800);
  Serial.println("\n=======================================================");
  Serial.println(" [SWITCHER STAGE 3] 80x SK6812 Tally Bus Zones Test");
  Serial.println("=======================================================");
  strip.begin();
  strip.setBrightness(160);
}

void loop() {
  for (int i = 0; i < NUM_LEDS; i++) {
    if (i <= 17)      strip.setPixelColor(i, strip.Color(0, 230, 30));    // PVW 綠
    else if (i <= 35) strip.setPixelColor(i, strip.Color(255, 0, 0));     // PGM 紅
    else if (i <= 47) strip.setPixelColor(i, strip.Color(255, 150, 0));   // DSK 琥珀
    else if (i <= 59) strip.setPixelColor(i, strip.Color(160, 20, 240));  // USER 紫
    else if (i <= 77) strip.setPixelColor(i, strip.Color(0, 180, 255));   // TRANS 青藍
    else if (i == 78) strip.setPixelColor(i, strip.Color(255, 20, 20));   // K78 CUT
    else              strip.setPixelColor(i, strip.Color(20, 255, 20));   // K79 AUTO
  }
  strip.show();
  delay(1000);
}
