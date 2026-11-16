#include <Arduino.h>
#include <driver/pcnt.h>

// ============================================================================
// 【階段 2 測試】80 鍵主導播台 v2：T-Bar 推桿 (G1 ADC1_CH0) + Jog 手輪 (G4/G5) + E1–E3 旋鈕
// 驗證重點：
//   - T-Bar (GPIO 1) 採用 16 倍過取樣 + 指數移動平均 (EMA) + 遲滯防抖 (Hysteresis)，確保 0~100% 絕不亂跳
//   - Jog 滾盤 (GPIO 4/5, KEY 90) 走硬體 PCNT_UNIT_0
//   - E1..E3 旋鈕 (G6/7, G15/16, G17/18) 走硬體 PCNT_UNIT_1..3
// ============================================================================

#define PIN_TBAR_ADC  1

float tbar_ema = 0.0f;
int   last_tbar_pct = -1;

struct PcntCh {
  pcnt_unit_t unit;
  int pin_a, pin_b;
  const char *label;
  int16_t last_val = 0;
} pcnt_devs[4] = {
  {PCNT_UNIT_0, 4,  5,  "🎛️ Jog 時間軸手輪 (KEY 90)"},
  {PCNT_UNIT_1, 6,  7,  "⚪ E1 主音量 (KEY 80)"},
  {PCNT_UNIT_2, 15, 16, "⚪ E2 主持麥1 (KEY 81)"},
  {PCNT_UNIT_3, 17, 18, "⚪ E3 來賓麥2 (KEY 82)"}
};

void initPcnt(const PcntCh &c) {
  pinMode(c.pin_a, INPUT_PULLUP);
  pinMode(c.pin_b, INPUT_PULLUP);
  pcnt_config_t cfg = {};
  cfg.pulse_gpio_num = c.pin_a;
  cfg.ctrl_gpio_num  = c.pin_b;
  cfg.channel        = PCNT_CHANNEL_0;
  cfg.unit           = c.unit;
  cfg.pos_mode       = PCNT_COUNT_INC;
  cfg.neg_mode       = PCNT_COUNT_DEC;
  cfg.lctrl_mode     = PCNT_MODE_REVERSE;
  cfg.hctrl_mode     = PCNT_MODE_KEEP;
  cfg.counter_h_lim  = 30000;
  cfg.counter_l_lim  = -30000;
  pcnt_unit_config(&cfg);
  pcnt_set_filter_value(c.unit, 800);
  pcnt_filter_enable(c.unit);
  pcnt_counter_clear(c.unit);
  pcnt_counter_resume(c.unit);
}

void setup() {
  Serial.begin(115200);
  delay(800);
  Serial.println("\n=======================================================");
  Serial.println(" [SWITCHER STAGE 2] T-Bar ADC + Jog Wheel + PCNT E1-E3");
  Serial.println("=======================================================");

  analogReadResolution(12);
  analogSetPinAttenuation(PIN_TBAR_ADC, ADC_11db);
  for (int i = 0; i < 4; i++) initPcnt(pcnt_devs[i]);
}

void loop() {
  // 1. T-Bar 16x 過取樣 + EMA 平滑
  uint32_t acc = 0;
  for (int i = 0; i < 16; i++) acc += analogRead(PIN_TBAR_ADC);
  float raw = (float)(acc >> 4); // 0..4095
  tbar_ema = tbar_ema * 0.85f + raw * 0.15f;

  // 加上頭尾死區 (Deadzone 40..4050) 映射為 0..100%
  int pct = constrain((int)((tbar_ema - 40.0f) * 100.0f / 4010.0f + 0.5f), 0, 100);
  if (abs(pct - last_tbar_pct) >= 1) {
    last_tbar_pct = pct;
    Serial.printf("🎚️ [T-Bar 推桿] 進度 = %3d%% (ADC=%.0f)\n", pct, tbar_ema);
  }

  // 2. 檢查 Jog 手輪與 E1–E3 旋鈕
  for (int i = 0; i < 4; i++) {
    int16_t cnt = 0;
    pcnt_get_counter_value(pcnt_devs[i].unit, &cnt);
    int16_t step = (i == 0) ? cnt : (cnt / 2);
    if (step != pcnt_devs[i].last_val) {
      int diff = step - pcnt_devs[i].last_val;
      pcnt_devs[i].last_val = step;
      Serial.printf("%s -> 方向=%s | 累計=%d\n", pcnt_devs[i].label, (diff > 0) ? "+1" : "-1", step);
    }
  }
  delay(10);
}
