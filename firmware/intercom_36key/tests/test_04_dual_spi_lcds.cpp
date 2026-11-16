#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>

// ============================================================================
// 【階段 4 測試】36 鍵 Intercom 主站：MCP23017 (0x20) + 獨立 SPI3 7 塊彩屏片選巡檢測試
// 腳位：
//   I2C  : G8 (SDA), G9 (SCL) -> MCP23017 (0x20)
//   SPI3 : G1 (MOSI/SDA), G2 (SCLK/SCL), G42 (共用 DC)
//   MCP23017 Port A: GPA0–GPA5 = 6 塊 2.25" ST7789P CS1–CS6
//                    GPA6      = 4.0" ST7796S Config 大屏 CS_BIG
//                    GPA7      = 全部 7 塊螢幕共用 RES 硬體復位
//   MCP23017 Port B: GPB0      = 4.0" 大屏背光 BL (HIGH 點亮)
// ============================================================================

#define PIN_I2C_SDA   8
#define PIN_I2C_SCL   9
#define PIN_SPI3_MOSI 1
#define PIN_SPI3_SCLK 2
#define PIN_LCD_DC    42
#define MCP_ADDR      0x20

SPIClass spi3(HSPI);

void writeMcp(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(MCP_ADDR);
  Wire.write(reg);
  Wire.write(val);
  Wire.endTransmission();
}

// 單獨拉低第 idx (0..6) 塊螢幕的 CS，其餘 CS 維持 HIGH，且 GPA7 (RES) 維持 HIGH
void selectLcdCs(int idx) {
  uint8_t porta = 0xFF; // Bit7(RES)=1, Bit0..6(CS)=1
  if (idx >= 0 && idx <= 6) {
    porta &= ~(1 << idx);
  }
  writeMcp(0x12, porta);
}

void lcdCmd(uint8_t cmd) {
  digitalWrite(PIN_LCD_DC, LOW);
  spi3.transfer(cmd);
  digitalWrite(PIN_LCD_DC, HIGH);
}

void setup() {
  Serial.begin(115200);
  delay(800);
  Serial.println("\n=======================================================");
  Serial.println(" [STAGE 4 TEST] MCP23017 (0x20) + SPI3 7x LCD CS Sweep");
  Serial.println("=======================================================");

  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, 800000);
  Wire.beginTransmission(MCP_ADDR);
  if (Wire.endTransmission() != 0) {
    Serial.println("❌ 找不到 MCP23017 (0x20)！請檢查 G8/G9、A0–A2 接地與 Pin 18 RESET 上拉 10kΩ！");
  } else {
    Serial.println("✅ MCP23017 (0x20) I2C 連線成功！");
  }

  // 設定 GPA0–7 與 GPB0–7 全部為輸出 (預設 GPB3/4/5 = 0 關閉 48V/5V 繼電器！)
  writeMcp(0x00, 0x00); // IODIRA = Output
  writeMcp(0x01, 0x00); // IODIRB = Output
  writeMcp(0x13, 0x01); // GPB0 = 1 (開啟 4.0" 背光)，GPB3/4/5 = 0 (48V OFF)

  // 透過 GPA7 執行全部 7 塊彩屏同步硬體復位脉冲
  writeMcp(0x12, 0x7F); // GPA7(RES) = 0
  delay(50);
  writeMcp(0x12, 0xFF); // GPA7(RES) = 1
  delay(120);

  pinMode(PIN_LCD_DC, OUTPUT);
  spi3.begin(PIN_SPI3_SCLK, -1, PIN_SPI3_MOSI, -1);
  spi3.beginTransaction(SPISettings(26000000, MSBFIRST, SPI_MODE0));

  // 喚醒全部 7 塊螢幕 (SLPOUT 0x11 + DISPON 0x29)
  for (int i = 0; i < 7; i++) {
    selectLcdCs(i);
    lcdCmd(0x11);
    selectLcdCs(-1);
  }
  delay(120);
  for (int i = 0; i < 7; i++) {
    selectLcdCs(i);
    lcdCmd(0x29);
    selectLcdCs(-1);
  }
  Serial.println("✅ 已完成 6 塊 2.25\" ST7789P + 1 塊 4.0\" ST7796S 喚醒序列！");
}

void loop() {
  for (int i = 0; i < 7; i++) {
    Serial.printf("🖥️ 正在測試片選 -> %s (MCP GPA%d = LOW)\n",
                  (i == 6) ? "4.0\" ST7796S Config 大螢幕" : "2.25\" ST7789P 工位小屏", i);
    selectLcdCs(i);
    lcdCmd(0x21); // INVON 反白閃爍測試，肉眼可確認每塊螢幕 CS 是否正確對應
    selectLcdCs(-1);
    delay(400);

    selectLcdCs(i);
    lcdCmd(0x20); // INVOFF
    selectLcdCs(-1);
  }
}
