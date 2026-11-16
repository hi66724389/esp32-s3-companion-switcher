#include <Arduino.h>

// ============================================================================
// 【階段 1 測試】80 鍵主導播切換台 v2：15 片 74HC165 (U1–U15 = 120 bits) 2kHz 高速掃描測試
// 腳位：G40 = CLK (Pin 2), G41 = SH/LD (Pin 1), G42 = 末片 U15 QH (Pin 9)
// 驗證內容：
//   - U1–U10 (Bits 0..79)   : 80 顆機械按鍵 KEY 0–79 (13 組 2x3 Pod + CUT/AUTO)
//   - U11    (Bits 80..87)  : E1–E8 旋鈕按壓開關 S2
//   - U12.AB (Bits 88..89)  : E9–E10 旋鈕按壓開關 S2
//   - U12.C–H & U13.A–H     : E5–E10 & ENC11 (Config 旋鈕) A/B 相正交灰碼解碼
//   - U14.A  (Bit 104)      : ENC11 按壓開關 S2
//   - U14.B–G(Bits 105..110): ▲上 / ▼下 / ◀左 / ▶右 / ⏎進入 / ⎋離開 (6 顆 Config 導覽鍵)
//   - U14.H & U15.A (111,112): E4 旋鈕 A/B 相 (v2 從 G38/39 移入掃描鏈)
// ============================================================================

#define PIN_HC165_CLK   40
#define PIN_HC165_LOAD  41
#define PIN_HC165_QH    42
#define NUM_CHIPS       15
#define TOTAL_BITS      120

uint8_t raw_bytes[NUM_CHIPS];
uint8_t prev_bytes[NUM_CHIPS];

// 正交編碼器灰碼狀態機查表 (E4..E10 + ENC11 共 8 顆在掃描鏈上的旋鈕)
static const int8_t GRAY_TABLE[16] = {0,-1,1,0, 1,0,0,-1, -1,0,0,1, 0,1,-1,0};
uint8_t enc_prev_ab[8] = {0};
int8_t  enc_accum[8]   = {0};
const uint8_t ENC_BIT_A[8] = {111, 90, 92, 94, 96, 98, 100, 102}; // E4, E5..E10, ENC11
const uint8_t ENC_BIT_B[8] = {112, 91, 93, 95, 97, 99, 101, 103};
const char   *ENC_NAMES[8] = {"E4(VTR音)", "E5(監聽耳機)", "E6(PIP-X)", "E7(PIP-Y)", "E8(PIP縮放)", "E9(DSK透明)", "E10(轉場秒)", "ENC11(Config選單)"};

inline bool getBit(const uint8_t *buf, uint8_t bit_idx) {
  return (buf[bit_idx >> 3] & (1 << (bit_idx & 7))) != 0;
}

void scan15xHc165() {
  memset(raw_bytes, 0, sizeof(raw_bytes));
  digitalWrite(PIN_HC165_LOAD, LOW);
  delayMicroseconds(1);
  digitalWrite(PIN_HC165_LOAD, HIGH);

  for (int b = TOTAL_BITS - 1; b >= 0; b--) {
    if (digitalRead(PIN_HC165_QH)) raw_bytes[b >> 3] |= (1 << (b & 7));
    digitalWrite(PIN_HC165_CLK, HIGH);
    digitalWrite(PIN_HC165_CLK, LOW);
  }
}

void setup() {
  Serial.begin(115200);
  delay(800);
  Serial.println("\n=======================================================");
  Serial.println(" [SWITCHER STAGE 1] 15x 74HC165 (120-bit) Chain Scanner");
  Serial.println("=======================================================");

  pinMode(PIN_HC165_CLK, OUTPUT);
  pinMode(PIN_HC165_LOAD, OUTPUT);
  pinMode(PIN_HC165_QH, INPUT_PULLUP);
  digitalWrite(PIN_HC165_LOAD, HIGH);

  scan15xHc165();
  memcpy(prev_bytes, raw_bytes, sizeof(raw_bytes));
  for (int e = 0; e < 8; e++) {
    enc_prev_ab[e] = (getBit(raw_bytes, ENC_BIT_A[e]) << 1) | getBit(raw_bytes, ENC_BIT_B[e]);
  }
}

void loop() {
  scan15xHc165();

  // 1. 檢查 80 顆主按鍵 (K0–K79) 與旋鈕 S2 / 6 顆 Config 導覽鍵
  for (int b = 0; b <= 110; b++) {
    // 跳過旋鈕 A/B 相 bit (90..103)
    if (b >= 90 && b <= 103) continue;
    bool down = !getBit(raw_bytes, b);
    bool was  = !getBit(prev_bytes, b);
    if (down != was) {
      if (b < 80) {
        Serial.printf("[切台按鍵 KEY %02d] (U%d.%c) -> %s\n", b, (b>>3)+1, 'A'+(b&7), down ? "按下" : "放開");
      } else if (b <= 89 || b == 104) {
        int e_id = (b == 104) ? 11 : (b - 79);
        Serial.printf("[旋鈕按壓 ENC%d S2] -> %s\n", e_id, down ? "按下" : "放開");
      } else if (b >= 105 && b <= 110) {
        const char *nav[6] = {"▲上", "▼下", "◀左", "▶右", "⏎進入", "⎋離開"};
        Serial.printf("[Config 導覽鍵 %s] (U14.%c) -> %s\n", nav[b - 105], 'A'+(b&7), down ? "按下" : "放開");
      }
    }
  }

  // 2. 解碼 8 顆在 74HC165 鏈上的旋鈕 (E4..E10 + ENC11)
  for (int e = 0; e < 8; e++) {
    uint8_t cur_ab = (getBit(raw_bytes, ENC_BIT_A[e]) << 1) | getBit(raw_bytes, ENC_BIT_B[e]);
    int8_t step = GRAY_TABLE[(enc_prev_ab[e] << 2) | cur_ab];
    enc_prev_ab[e] = cur_ab;
    if (step != 0) {
      enc_accum[e] += step;
      if (enc_accum[e] >= 2 || enc_accum[e] <= -2) {
        int dir = (enc_accum[e] > 0) ? 1 : -1;
        enc_accum[e] = 0;
        Serial.printf("🔄 [掃描鏈旋鈕 %s] 方向 = %s\n", ENC_NAMES[e], (dir > 0) ? "CW (+1)" : "CCW (-1)");
      }
    }
  }

  memcpy(prev_bytes, raw_bytes, sizeof(raw_bytes));
  delayMicroseconds(500); // 2 kHz 取樣率
}
