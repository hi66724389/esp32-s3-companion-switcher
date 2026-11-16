#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>

// ============================================================================
// 【階段 4 測試】80 鍵主導播台 v2：雙 MCP23017 (0x20 + 0x21) 與 16 塊彩屏 (15小+1大) 測試
// 腳位：
//   I2C  : G8 (SDA), G9 (SCL) -> MCP#1 (0x20) & MCP#2 (0x21)
//   SPI2 : G11 (MOSI), G12 (SCLK), G47 (共用 DC)
//   MCP#1 (0x20): GPA0–7 = CS1–CS8, GPB0–6 = CS9–CS15, GPB7 = 15 小屏共用 RES
//   MCP#2 (0x21): GPA0 = 4.0" 大屏 CS, GPA1 = 大屏 RST, GPA2 = W5500 RST, GPA3 = 大屏背光 BL
//                 GPA4 = XLR +48V, GPA5 = XLR +5V, GPA6 = 6.35mm -20dB Pad
// ============================================================================

#define MCP1_ADDR 0x20
#define MCP2_ADDR 0x21
#define PIN_DC    47

SPIClass spi2(FSPI);

void writeMcp16(uint8_t addr, uint8_t reg_a, uint8_t val_a, uint8_t val_b) {
  Wire.beginTransmission(addr);
  Wire.write(reg_a);
  Wire.write(val_a);
  Wire.write(val_b);
  Wire.endTransmission();
}

// 選取第 idx (0..14 為 2.25" 小屏 #1–#15；idx==15 為 4.0" Config 大屏；-1 為全釋放)
void selectSwitcherLcd(int idx) {
  uint16_t cs_mask = 0xFFFF; // Bit 15 = GPB7 (15小屏共用RES = 1)
  uint8_t  mcp2_a  = 0x0F;   // GPA0(大屏CS)=1, GPA1(大屏RST)=1, GPA2(W5500_RST)=1, GPA3(BL)=1, GPA4-6(48V)=0!

  if (idx >= 0 && idx < 15) {
    cs_mask &= ~(1 << idx);
  } else if (idx == 15) {
    mcp2_a &= ~(1 << 0); // 拉低 MCP#2 GPA0 (4.0" 大屏 CS)
  }
  writeMcp16(MCP1_ADDR, 0x12, cs_mask & 0xFF, (cs_mask >> 8) & 0xFF);
  writeMcp16(MCP2_ADDR, 0x12, mcp2_a, 0x00);
}

void setup() {
  Serial.begin(115200);
  delay(800);
  Serial.println("\n=======================================================");
  Serial.println(" [SWITCHER STAGE 4] Dual MCP23017 (0x20/0x21) + 16 LCDs");
  Serial.println("=======================================================");

  // 先將 W5500 原生 CS (G10) 拉高，避免干擾 SPI2！
  pinMode(10, OUTPUT);
  digitalWrite(10, HIGH);
  pinMode(PIN_DC, OUTPUT);
  digitalWrite(PIN_DC, HIGH);

  Wire.begin(8, 9, 800000);
  for (uint8_t a : {MCP1_ADDR, MCP2_ADDR}) {
    Wire.beginTransmission(a);
    Serial.printf("I2C 偵測 MCP23017 (0x%02X): %s\n", a, (Wire.endTransmission() == 0) ? "✅成功" : "❌失敗");
    writeMcp16(a, 0x00, 0x00, 0x00); // IODIRA/B = Output
  }

  // 硬體復位 15 塊小屏 (MCP#1 GPB7) 與 1 塊 4.0" 大屏 (MCP#2 GPA1)
  writeMcp16(MCP1_ADDR, 0x12, 0xFF, 0x7F);
  writeMcp16(MCP2_ADDR, 0x12, 0x0D, 0x00);
  delay(50);
  selectSwitcherLcd(-1);
  delay(120);

  spi2.begin(12, 13, 11, -1);
  spi2.beginTransaction(SPISettings(24000000, MSBFIRST, SPI_MODE0));
  Serial.println("✅ 15 塊 2.25\" 小彩屏 + 1 塊 4.0\" Config 大螢幕初始化完成！");
}

void loop() {
  for (int i = 0; i < 16; i++) {
    Serial.printf("🖥️ 測試片選 -> %s\n", (i == 15) ? "4.0\" Config 大螢幕 (MCP#2 GPA0)" : "2.25\" 模組小屏");
    selectSwitcherLcd(i);
    digitalWrite(PIN_DC, LOW);
    spi2.transfer(0x21); // INVON 閃爍確認
    digitalWrite(PIN_DC, HIGH);
    selectSwitcherLcd(-1);
    delay(250);

    selectSwitcherLcd(i);
    digitalWrite(PIN_DC, LOW);
    spi2.transfer(0x20); // INVOFF
    digitalWrite(PIN_DC, HIGH);
    selectSwitcherLcd(-1);
  }
}
