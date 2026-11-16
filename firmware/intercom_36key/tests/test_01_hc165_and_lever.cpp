#include <Arduino.h>

// ============================================================================
// 【階段 1 測試】36 鍵 Intercom 主站：6 片 74HC165 (U1–U6 = 48 bits) 菊花鏈與三段式發話撥桿測試
// 腳位：G40 = CLK (Pin 2), G41 = SH/LD (Pin 1), G21 = 末片 U6 QH (Pin 9)
// 驗證重點：
//   1. 未按任何鍵時，U1–U5 與 U6.A–E 應全部讀到 1 (0xFF)；若有某片全為 0，檢查 Pin 15 (/CE) 是否接地！
//   2. 測試 K0–K35 工位鍵、E1/E2/ENC3 按壓 S2 (U5.E–G)、6 顆導覽鍵 ▲▼◀▶⏎⎋ (U5.H, U6.A–E)
//   3. 測試「🕹️ 三段式發話撥桿 (U6.F = LATCH, U6.G = PTT)」：
//      - 往上扳緊 -> U6.F 變 0 -> 串口顯示 [LEVER: ▲ LATCH 持續發話]
//      - 放回中間 -> U6.F=1, U6.G=1 -> 串口顯示 [LEVER: ● MUTE 強制靜音]
//      - 往下壓住 -> U6.G 變 0 -> 串口顯示 [LEVER: ▼ PTT 瞬時發話]
// ============================================================================

#define PIN_HC165_CLK   40
#define PIN_HC165_LOAD  41
#define PIN_HC165_QH    21
#define NUM_CHIPS       6
#define TOTAL_BITS      (NUM_CHIPS * 8)

uint8_t raw_bytes[NUM_CHIPS];
uint8_t prev_bytes[NUM_CHIPS];
uint8_t prev_lever_state = 255;

void scanHc165Chain() {
  memset(raw_bytes, 0, sizeof(raw_bytes));
  digitalWrite(PIN_HC165_LOAD, LOW);
  delayMicroseconds(2);
  digitalWrite(PIN_HC165_LOAD, HIGH);
  delayMicroseconds(1);

  // 先移出的是末片 U6 的 H 腳 (Bit 47)，最後移出的是 U1 的 A 腳 (Bit 0)
  for (int bit_pos = TOTAL_BITS - 1; bit_pos >= 0; bit_pos--) {
    if (digitalRead(PIN_HC165_QH)) {
      raw_bytes[bit_pos >> 3] |= (1 << (bit_pos & 7));
    }
    digitalWrite(PIN_HC165_CLK, HIGH);
    delayMicroseconds(1);
    digitalWrite(PIN_HC165_CLK, LOW);
    delayMicroseconds(1);
  }
}

void setup() {
  Serial.begin(115200);
  delay(800);
  Serial.println("\n=======================================================");
  Serial.println(" [STAGE 1 TEST] 6x 74HC165 (48-bit) + 3-Way Talk Lever");
  Serial.println("=======================================================");

  pinMode(PIN_HC165_CLK, OUTPUT);
  pinMode(PIN_HC165_LOAD, OUTPUT);
  pinMode(PIN_HC165_QH, INPUT_PULLUP);
  digitalWrite(PIN_HC165_CLK, LOW);
  digitalWrite(PIN_HC165_LOAD, HIGH);

  scanHc165Chain();
  memcpy(prev_bytes, raw_bytes, sizeof(raw_bytes));

  Serial.print("初始 6 片 74HC165 原始數值 (正常未按應接近 FF): ");
  for (int i = 0; i < NUM_CHIPS; i++) {
    Serial.printf("U%d=0x%02X ", i + 1, raw_bytes[i]);
  }
  Serial.println();
}

void loop() {
  scanHc165Chain();

  // 1. 檢查 48 個輸入腳位是否有變化
  for (int b = 0; b < TOTAL_BITS; b++) {
    bool cur_down  = !(raw_bytes[b >> 3]  & (1 << (b & 7)));
    bool prev_down = !(prev_bytes[b >> 3] & (1 << (b & 7)));
    if (cur_down != prev_down) {
      int chip = (b >> 3) + 1;
      char pin_letter = 'A' + (b & 7);
      if (b < 36) {
        Serial.printf("[工位鍵 K%02d] (U%d.%c) -> %s\n", b, chip, pin_letter, cur_down ? "按下 PRESSED" : "放開 RELEASED");
      } else if (b >= 36 && b <= 38) {
        const char *enc_n[3] = {"E1(耳機/小喇叭)", "E2(PGM/自聽)", "ENC3(選單/增益)"};
        Serial.printf("[旋鈕按壓 %s] (U%d.%c) -> %s\n", enc_n[b - 36], chip, pin_letter, cur_down ? "按下" : "放開");
      } else if (b >= 39 && b <= 44) {
        const char *nav_n[6] = {"▲上", "▼下", "◀左", "▶右", "⏎進入", "⎋離開"};
        Serial.printf("[Config 導覽鍵 %s] (U%d.%c) -> %s\n", nav_n[b - 39], chip, pin_letter, cur_down ? "按下" : "放開");
      } else if (b == 47) {
        Serial.printf("[🦶 背板腳踏 PTT] (U6.H) -> %s\n", cur_down ? "踩下發話 TALK" : "放開");
      }
    }
  }

  // 2. 解碼「🕹️ 三段式發話撥桿 (U6.F = Bit 45 LATCH, U6.G = Bit 46 PTT)」
  bool latch_low = !(raw_bytes[5] & (1 << 5)); // U6.F
  bool ptt_low   = !(raw_bytes[5] & (1 << 6)); // U6.G
  uint8_t cur_lever = latch_low ? 2 : (ptt_low ? 1 : 0); // 2=LATCH, 1=PTT, 0=MUTE

  if (cur_lever != prev_lever_state) {
    prev_lever_state = cur_lever;
    if (cur_lever == 2)      Serial.println("🕹️ [三段式撥桿] ➔ ▲ 上扳自鎖：持續發話 (LATCH ON-AIR)");
    else if (cur_lever == 1) Serial.println("🕹️ [三段式撥桿] ➔ ▼ 下壓彈回：瞬時發話 (MOMENTARY PTT)");
    else                     Serial.println("🕹️ [三段式撥桿] ➔ ● 中間位置：麥克風強制靜音 (MUTE)");
  }

  memcpy(prev_bytes, raw_bytes, sizeof(raw_bytes));
  delay(5);
}
