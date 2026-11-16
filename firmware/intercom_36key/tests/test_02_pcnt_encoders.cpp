#include <Arduino.h>
#include <driver/pcnt.h>

// ============================================================================
// 【階段 2 測試】36 鍵 Intercom 主站：3 顆直連硬體 PCNT 旋轉編碼器測試
//   E1 (耳機/內建小喇叭總音量) : G6 (A相), G7 (B相)  -> PCNT_UNIT_0
//   E2 (PGM 背景音 / 0ms 自聽) : G15 (A相), G16 (B相) -> PCNT_UNIT_1
//   ENC3 (Config 選單 / Mic增益): G17 (A相), G18 (B相) -> PCNT_UNIT_2
// ============================================================================

struct HwEncoder {
  pcnt_unit_t unit;
  int pin_a;
  int pin_b;
  const char *name;
  int16_t last_detent = 0;
};

HwEncoder encs[3] = {
  {PCNT_UNIT_0, 6,  7,  "E1 (耳機/小喇叭總音量)"},
  {PCNT_UNIT_1, 15, 16, "E2 (PGM/0ms自聽混音比)"},
  {PCNT_UNIT_2, 17, 18, "ENC3 (Config選單/Mic增益/虛擬鍵盤)"}
};

void setupPcntUnit(const HwEncoder &e) {
  pinMode(e.pin_a, INPUT_PULLUP);
  pinMode(e.pin_b, INPUT_PULLUP);

  pcnt_config_t cfg = {};
  cfg.pulse_gpio_num = e.pin_a;
  cfg.ctrl_gpio_num  = e.pin_b;
  cfg.channel        = PCNT_CHANNEL_0;
  cfg.unit           = e.unit;
  cfg.pos_mode       = PCNT_COUNT_INC;
  cfg.neg_mode       = PCNT_COUNT_DEC;
  cfg.lctrl_mode     = PCNT_MODE_REVERSE;
  cfg.hctrl_mode     = PCNT_MODE_KEEP;
  cfg.counter_h_lim  = 30000;
  cfg.counter_l_lim  = -30000;
  pcnt_unit_config(&cfg);

  // 開啟硬體毛刺濾波器 (1000 APB cycles ≈ 12.5us)
  pcnt_set_filter_value(e.unit, 1000);
  pcnt_filter_enable(e.unit);
  pcnt_counter_pause(e.unit);
  pcnt_counter_clear(e.unit);
  pcnt_counter_resume(e.unit);
}

void setup() {
  Serial.begin(115200);
  delay(800);
  Serial.println("\n=======================================================");
  Serial.println(" [STAGE 2 TEST] 3x Hardware PCNT Rotary Encoders");
  Serial.println("=======================================================");
  for (int i = 0; i < 3; i++) setupPcntUnit(encs[i]);
  Serial.println("請轉動 E1, E2, ENC3 旋鈕，觀察計數與方向...");
}

void loop() {
  for (int i = 0; i < 3; i++) {
    int16_t raw_cnt = 0;
    pcnt_get_counter_value(encs[i].unit, &raw_cnt);
    int16_t detent = raw_cnt / 2; // EC11 半週期或全週期依手感可設 /2 或 /4
    if (detent != encs[i].last_detent) {
      int diff = detent - encs[i].last_detent;
      encs[i].last_detent = detent;
      Serial.printf("🔄 [%s] 方向=%s | 累計刻度=%d\n",
                    encs[i].name, (diff > 0) ? "順時針 CW (+1)" : "逆時針 CCW (-1)", detent);
    }
  }
  delay(10);
}
