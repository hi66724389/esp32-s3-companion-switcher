#pragma once
#include <Arduino.h>
#include <WebServer.h>
#include <Preferences.h>
#include "wm8960_pro_audio.h"
#include "vban_engine.h"

// ============================================================================
// ESP32-S3 內建 Web UI 設定後台 (HTTP Port 80) + NVS 斷電永久儲存
// 支援：固定 IP (Static IP / Mask / GW / DNS)、裝置中文名、VBAN 路由、四合一音訊切換
// 同時適用於：80 鍵主導播切換台 v2 & 36 鍵獨立 VBAN Intercom 主站
// ============================================================================

struct SystemConfigNVS {
  char     device_name[32]   = "ESP32-S3 廣電主站";
  char     vban_stream[17]   = "DIR_MAIN_TALK";
  char     mdns_host[24]     = "intercom-main";

  // W5500 有線網口固定 IP / DHCP 設定
  bool     eth_static_enable = true;
  uint8_t  eth_ip[4]         = {192, 168, 1, 50};
  uint8_t  eth_mask[4]       = {255, 255, 255, 0};
  uint8_t  eth_gw[4]         = {192, 168, 1, 1};
  uint8_t  eth_dns[4]        = {8, 8, 8, 8};

  // Wi-Fi 備援網路與固定 IP 設定
  char     wifi_ssid[32]     = "Studio_Control_WiFi";
  char     wifi_pass[32]     = "12345678";
  bool     wifi_static_en    = false;

  // Companion Satellite 設定 (80 鍵導播台主力使用，36 鍵亦可選用)
  uint8_t  comp_ip[4]        = {192, 168, 1, 10};
  uint16_t comp_port         = 16622;

  // 四合一音訊前端與自聽 / 喇叭設定
  uint8_t  audio_input_mode  = (uint8_t)IN_MIC2_XLR_48V; // 0=3.5mm, 1=XLR_OFF, 2=XLR_5V, 3=XLR_48V, 4=635_MIC, 5=635_LINE, 6=MEMS
  float    mic_pga_db        = 22.5f;
  int8_t   sidetone_db       = -6;
  bool     sidetone_en       = true;
  bool     speaker_on        = true;
  uint8_t  led_brightness    = 180;
};

static const char EMBEDDED_WEBUI_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html lang="zh-Hant"><head><meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>ESP32-S3 導播台 & VBAN Intercom · 內建設定後台</title>
<style>
body{margin:0;padding:18px;background:#0f172a;color:#f8fafc;font-family:"Segoe UI","Microsoft JhengHei",sans-serif}
.wrap{max-width:960px;margin:0 auto}
.card{background:#1e293b;border:1px solid #334155;border-radius:12px;padding:18px;margin-bottom:16px}
h1{font-size:20px;color:#38bdf8;margin:0 0 6px}
h2{font-size:15px;color:#fbbf24;margin:0 0 12px;border-bottom:1px solid #334155;padding-bottom:6px}
.row{display:grid;grid-template-columns:200px 1fr;gap:10px;align-items:center;margin-bottom:10px;font-size:13.5px}
@media(max-width:640px){.row{grid-template-columns:1fr}}
input,select{width:100%;padding:8px 10px;border-radius:6px;border:1px solid #475569;background:#0f172a;color:#fff;font-size:13.5px;box-sizing:border-box}
.btn{padding:10px 20px;border-radius:8px;border:none;background:#22c55e;color:#052e16;font-weight:700;cursor:pointer;font-size:14px}
.msg{display:none;padding:10px;background:#16a34a;color:#fff;border-radius:8px;margin-bottom:12px;font-weight:700}
</style></head><body><div class="wrap">
<h1>🌐 ESP32-S3 內建 Web UI 管理後台 (與本機 4.0" Config 螢幕雙向同步 + NVS 永久儲存)</h1>
<p style="font-size:12.5px;color:#94a3b8">在此修改裝置名稱、固定 IP、XLR 5V/48V 幻象電源、6.35mm Mic/Line 模式後按儲存，即寫入 NVS 並同步推播至全區網螢幕。</p>
<div id="ok" class="msg">✅ 設定已寫入 ESP32-S3 NVS 快閃記憶體，並已即時更新螢幕與硬體！</div>

<div class="card">
  <h2>🏷️ 1. 裝置名稱與 VBAN 身分廣播 (自動顯示於遠端 2.25" / 4.0" 螢幕)</h2>
  <div class="row"><label>本機顯示名稱 (支援中文)：</label><input id="dev_name"></div>
  <div class="row"><label>VBAN Header streamname (16B)：</label><input id="vban_stream" maxlength="16"></div>
  <div class="row"><label>mDNS 區網網址 (.local)：</label><input id="mdns_host"></div>
</div>

<div class="card">
  <h2>🌐 2. W5500 有線網口 & Wi-Fi 固定 IP 設定 (Static IP / DHCP)</h2>
  <div class="row"><label>W5500 有線網口 IP 模式：</label>
    <select id="eth_static"><option value="1">固定 IP (Static IP · 推薦)</option><option value="0">DHCP 自動取得 IP</option></select>
  </div>
  <div class="row"><label>固定 IP 位址 (Static IP)：</label><input id="eth_ip" placeholder="192.168.1.50"></div>
  <div class="row"><label>子網路遮罩 (Subnet Mask)：</label><input id="eth_mask" placeholder="255.255.255.0"></div>
  <div class="row"><label>預設閘道 (Gateway)：</label><input id="eth_gw" placeholder="192.168.1.1"></div>
  <div class="row"><label>DNS 伺服器：</label><input id="eth_dns" placeholder="8.8.8.8"></div>
  <div class="row"><label>Wi-Fi 備援 SSID：</label><input id="wifi_ssid"></div>
  <div class="row"><label>Wi-Fi 備援密碼：</label><input id="wifi_pass" type="password"></div>
  <div class="row"><label>Companion 主機 IP：</label><input id="comp_ip" placeholder="192.168.1.10"></div>
</div>

<div class="card">
  <h2>🎙️ 3. 四合一音訊前端 (XLR 5V·48V / 6.35mm Mic·Line) & 0ms 自聽 / 小喇叭</h2>
  <div class="row"><label>麥克風 / 線路輸入接口模式：</label>
    <select id="audio_mode">
      <option value="0">MIC 1：3.5mm 耳麥孔 (內建 2.5V MICBIAS)</option>
      <option value="1">MIC 2：XLR 平衡口 — OFF (動圈麥)</option>
      <option value="2">MIC 2：XLR 平衡口 — +5V 偏壓 (會議鵝頸麥)</option>
      <option value="3">MIC 2：XLR 平衡口 — +48V 幻象電源 (電容麥)</option>
      <option value="4">IN 3：6.35mm TRS 孔 — MIC 高增益模式 (+25dB)</option>
      <option value="5">IN 3：6.35mm TRS 孔 — LINE-IN 線路模式 (-20dB Pad)</option>
      <option value="6">MIC 4：面板機載類比 MEMS 麥克風 (免戴耳麥)</option>
    </select>
  </div>
  <div class="row"><label>Mic PGA 前級增益 (0~30 dB)：</label><input id="mic_pga" type="number" step="0.75"></div>
  <div class="row"><label>0.0ms 硬體類比自聽音量 (dB)：</label><input id="sidetone_db" type="number" step="3"></div>
  <div class="row"><label>面板內建 1W 小喇叭 (發話自動Cut)：</label>
    <select id="spk_on"><option value="1">開啟 (發話時自動靜音防嘯叫)</option><option value="0">關閉</option></select>
  </div>
</div>

<button class="btn" onclick="saveCfg()">💾 儲存設定至 NVS 並立即套用</button>
</div>
<script>
async function loadCfg(){
  const r = await fetch('/api/config'); const d = await r.json();
  document.getElementById('dev_name').value = d.dev_name;
  document.getElementById('vban_stream').value = d.vban_stream;
  document.getElementById('mdns_host').value = d.mdns_host;
  document.getElementById('eth_static').value = d.eth_static ? '1':'0';
  document.getElementById('eth_ip').value = d.eth_ip;
  document.getElementById('eth_mask').value = d.eth_mask;
  document.getElementById('eth_gw').value = d.eth_gw;
  document.getElementById('eth_dns').value = d.eth_dns;
  document.getElementById('wifi_ssid').value = d.wifi_ssid;
  document.getElementById('comp_ip').value = d.comp_ip;
  document.getElementById('audio_mode').value = d.audio_mode;
  document.getElementById('mic_pga').value = d.mic_pga;
  document.getElementById('sidetone_db').value = d.sidetone_db;
  document.getElementById('spk_on').value = d.spk_on ? '1':'0';
}
async function saveCfg(){
  const p = new URLSearchParams();
  ['dev_name','vban_stream','mdns_host','eth_static','eth_ip','eth_mask','eth_gw','eth_dns','wifi_ssid','wifi_pass','comp_ip','audio_mode','mic_pga','sidetone_db','spk_on'].forEach(k=>{
    p.append(k, document.getElementById(k).value);
  });
  await fetch('/api/config', {method:'POST', body:p});
  const m = document.getElementById('ok'); m.style.display='block';
  setTimeout(()=>m.style.display='none', 3000);
}
loadCfg();
</script></body></html>
)rawliteral";

class EmbeddedWebUIServer {
public:
  WebServer server{80};
  Preferences prefs;
  SystemConfigNVS cfg;
  void (*onConfigUpdatedCallback)() = nullptr;

  void loadFromNVS() {
    prefs.begin("s3_studio", true);
    if (prefs.isKey("dev_name")) {
      prefs.getBytes("cfg_blob", &cfg, sizeof(cfg));
    }
    prefs.end();
  }

  void saveToNVS() {
    prefs.begin("s3_studio", false);
    prefs.putString("dev_name", cfg.device_name);
    prefs.putBytes("cfg_blob", &cfg, sizeof(cfg));
    prefs.end();
  }

  static void parseIpStr(const String &s, uint8_t out[4]) {
    IPAddress ip;
    if (ip.fromString(s)) {
      for (int i = 0; i < 4; i++) out[i] = ip[i];
    }
  }

  static String ipToStr(const uint8_t ip[4]) {
    char b[20];
    snprintf(b, sizeof(b), "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
    return String(b);
  }

  void begin(void (*onUpdateCb)() = nullptr) {
    onConfigUpdatedCallback = onUpdateCb;
    loadFromNVS();

    server.on("/", HTTP_GET, [this]() {
      server.send_P(200, "text/html", EMBEDDED_WEBUI_HTML);
    });

    server.on("/api/config", HTTP_GET, [this]() {
      String json = "{";
      json += "\"dev_name\":\"" + String(cfg.device_name) + "\",";
      json += "\"vban_stream\":\"" + String(cfg.vban_stream) + "\",";
      json += "\"mdns_host\":\"" + String(cfg.mdns_host) + "\",";
      json += "\"eth_static\":" + String(cfg.eth_static_enable ? "true" : "false") + ",";
      json += "\"eth_ip\":\"" + ipToStr(cfg.eth_ip) + "\",";
      json += "\"eth_mask\":\"" + ipToStr(cfg.eth_mask) + "\",";
      json += "\"eth_gw\":\"" + ipToStr(cfg.eth_gw) + "\",";
      json += "\"eth_dns\":\"" + ipToStr(cfg.eth_dns) + "\",";
      json += "\"wifi_ssid\":\"" + String(cfg.wifi_ssid) + "\",";
      json += "\"comp_ip\":\"" + ipToStr(cfg.comp_ip) + "\",";
      json += "\"audio_mode\":" + String(cfg.audio_input_mode) + ",";
      json += "\"mic_pga\":" + String(cfg.mic_pga_db, 2) + ",";
      json += "\"sidetone_db\":" + String(cfg.sidetone_db) + ",";
      json += "\"spk_on\":" + String(cfg.speaker_on ? "true" : "false");
      json += "}";
      server.send(200, "application/json", json);
    });

    server.on("/api/config", HTTP_POST, [this]() {
      if (server.hasArg("dev_name")) VbanEngine::safeCopyUtf8(cfg.device_name, server.arg("dev_name").c_str(), sizeof(cfg.device_name));
      if (server.hasArg("vban_stream")) VbanEngine::safeCopyUtf8(cfg.vban_stream, server.arg("vban_stream").c_str(), sizeof(cfg.vban_stream));
      if (server.hasArg("mdns_host")) VbanEngine::safeCopyUtf8(cfg.mdns_host, server.arg("mdns_host").c_str(), sizeof(cfg.mdns_host));
      if (server.hasArg("eth_static")) cfg.eth_static_enable = (server.arg("eth_static") == "1");
      if (server.hasArg("eth_ip")) parseIpStr(server.arg("eth_ip"), cfg.eth_ip);
      if (server.hasArg("eth_mask")) parseIpStr(server.arg("eth_mask"), cfg.eth_mask);
      if (server.hasArg("eth_gw")) parseIpStr(server.arg("eth_gw"), cfg.eth_gw);
      if (server.hasArg("eth_dns")) parseIpStr(server.arg("eth_dns"), cfg.eth_dns);
      if (server.hasArg("wifi_ssid")) VbanEngine::safeCopyUtf8(cfg.wifi_ssid, server.arg("wifi_ssid").c_str(), sizeof(cfg.wifi_ssid));
      if (server.hasArg("wifi_pass") && server.arg("wifi_pass").length() > 0) {
        VbanEngine::safeCopyUtf8(cfg.wifi_pass, server.arg("wifi_pass").c_str(), sizeof(cfg.wifi_pass));
      }
      if (server.hasArg("comp_ip")) parseIpStr(server.arg("comp_ip"), cfg.comp_ip);
      if (server.hasArg("audio_mode")) cfg.audio_input_mode = (uint8_t)server.arg("audio_mode").toInt();
      if (server.hasArg("mic_pga")) cfg.mic_pga_db = server.arg("mic_pga").toFloat();
      if (server.hasArg("sidetone_db")) cfg.sidetone_db = (int8_t)server.arg("sidetone_db").toInt();
      if (server.hasArg("spk_on")) cfg.speaker_on = (server.arg("spk_on") == "1");

      saveToNVS();
      if (onConfigUpdatedCallback) onConfigUpdatedCallback();
      server.send(200, "application/json", "{\"ok\":true}");
    });

    server.begin();
  }

  void poll() {
    server.handleClient();
  }
};
