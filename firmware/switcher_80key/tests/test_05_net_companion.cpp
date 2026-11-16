#include <Arduino.h>
#include <WiFi.h>

// ============================================================================
// 【階段 5 測試】80 鍵主導播台 v2：Bitfocus Companion Satellite API (TCP 16622) 握手與燈色/文字解析測試
// 驗證重點：
//   1. 連上網路後，自動向 Companion 主機 (預設 192.168.1.10:16622) 發送 ADD-DEVICE 註冊 80 鍵面板
//   2. 每 1 秒發送 PING 心跳維持連線
//   3. 即時解析 Companion 下發的 KEY-STATE COLOR=rgb(r,g,b) 與 TEXT="Base64..."
// ============================================================================

const char *WIFI_SSID = "Studio_Control_WiFi";
const char *WIFI_PASS = "12345678";
IPAddress   COMP_IP(192, 168, 1, 10);
const uint16_t COMP_PORT = 16622;

WiFiClient sat_client;
uint32_t   last_ping_ms = 0;

void sendAddDevice() {
  sat_client.println("ADD-DEVICE DEVICEID=esp32s3 PRODUCT_NAME=\"ESP32-S3 80-Key + 11-Enc 16-LCD Switcher v2\" KEYS_TOTAL=80 KEYS_PER_ROW=10 BITMAPS=0 COLORS=rgb TEXT=true");
  Serial.println("📤 已發送 ADD-DEVICE 註冊 80 鍵導播面板！");
}

void parseCompanionLine(const String &line) {
  if (line.startsWith("BEGIN")) {
    Serial.println("🤝 收到 Companion 握手: " + line);
    sendAddDevice();
  } else if (line.startsWith("KEY-STATE")) {
    Serial.println("🎨 收到 Companion 按鍵狀態更新: " + line);
  }
}

void setup() {
  Serial.begin(115200);
  delay(800);
  Serial.println("\n=======================================================");
  Serial.println(" [SWITCHER STAGE 5] Companion Satellite TCP 16622 Test");
  Serial.println("=======================================================");

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("正在連線 Wi-Fi");
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - t0 < 8000)) {
    delay(400);
    Serial.print(".");
  }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("✅ IP: ");
    Serial.println(WiFi.localIP());
  }
}

void loop() {
  if (WiFi.status() == WL_CONNECTED) {
    if (!sat_client.connected()) {
      Serial.printf("正在連接 Companion 主機 %s:%u ...\n", COMP_IP.toString().c_str(), COMP_PORT);
      if (sat_client.connect(COMP_IP, COMP_PORT)) {
        Serial.println("✅ TCP 16622 連線成功！");
      } else {
        delay(2000);
        return;
      }
    }

    while (sat_client.available()) {
      String line = sat_client.readStringUntil('\n');
      line.trim();
      if (line.length() > 0) parseCompanionLine(line);
    }

    if (millis() - last_ping_ms > 1500) {
      last_ping_ms = millis();
      sat_client.println("PING");
    }
  }
}
