#pragma once
#include <Arduino.h>
#include <WiFiUdp.h>
#include <math.h>

// ============================================================================
// VBAN 廣電級音訊收發、8 聲道拆分 (Splitter)、VOX 來電閃燈、部分同發 & 裝置名稱自動同步引擎
// ============================================================================

#define VBAN_UDP_PORT        6980
#define VBAN_HEADER_SIZE     28
#define VBAN_MAX_STATIONS    36
#define VBAN_SPLIT_ROUTES    6

#pragma pack(push, 1)
struct VbanHeader {
  char     vban[4];        // 'V', 'B', 'A', 'N'
  uint8_t  format_SR;      // bits[4:0]=SR index (3=48000Hz), bits[7:5]=SubProtocol (0x00=AUDIO, 0x40=TEXT)
  uint8_t  format_nbs;     // samples per frame - 1 (0..255)
  uint8_t  format_nbc;     // channels - 1 (0=1ch, 1=2ch, 7=8ch)
  uint8_t  format_bit;     // 0x01 = 16-bit PCM
  char     streamname[16]; // 16-byte ASCII/UTF-8 stream name
  uint32_t nuFrame;        //递增封包計數器
};
#pragma pack(pop)

// 每個工位按鍵 (K0–K35) 的即時對講狀態與遠端 VBAN 名稱
struct IntercomStationSlot {
  IPAddress remote_ip;
  char      expected_stream[17]; // 綁定的 16B streamname
  char      display_name[28];    // 顯示在 2.25" 彩屏上的名稱（支援遠端 VBAN 自動改名！）
  bool      auto_accept_vban_name = true;

  bool      selected_for_talk = false; // 🩵 單擊勾選加入「部分同發 (Multi-Select)」
  bool      momentary_ptt     = false; // 按住單顆按鍵直通 PTT
  bool      listen_muted      = false; // 🟣 雙擊「單獨 Mute 對方來話 (🔇)」
  bool      incoming_call_req = false; // 🔴 對方主動按 Call 呼叫信令
  bool      vox_speaking      = false; // 🟠 對方有聲音進來 (VOX > -40dBFS 自動閃燈)
  float     rx_rms_dbfs       = -60.0f;// 該工位即時音柱電平 (供 2.25" 螢幕畫小音錶)
  uint32_t  last_vox_ms       = 0;
};

// 8 聲道總線切分轉發規則 (Split & Relay)
struct VbanSplitRoute {
  bool      enabled       = false;
  uint8_t   src_ch_index  = 2;     // 從 8ch 總線抽取哪一軌 (0..7 對應 Ch1..Ch8)
  IPAddress dest_ip;               // 轉發目標單播或群播 IP
  char      out_stream[17] = "SPLIT_CH3";
  uint32_t  frame_cnt     = 0;
};

class VbanEngine {
public:
  WiFiUDP udp;
  char    self_device_name[32] = "導播主站 (DIR_MAIN)";
  char    self_stream_name[17] = "DIR_MAIN_TALK";
  float   vox_threshold_dbfs   = -40.0f; // 超過 -40dBFS 觸發橘白快閃
  bool    auto_ducking_enabled = true;   // 有人講話自動壓低 PGM -15dB
  bool    any_station_talking  = false;
  uint32_t tx_frame_counter    = 0;
  uint32_t last_beacon_ms      = 0;

  IntercomStationSlot stations[VBAN_MAX_STATIONS];
  VbanSplitRoute      split_routes[VBAN_SPLIT_ROUTES];

  // UTF-8 安全字串複製（防止中文在 2.25" 螢幕截斷產生半個字的亂碼）
  static void safeCopyUtf8(char *dst, const char *src, size_t max_bytes) {
    if (!dst || !src || max_bytes == 0) return;
    size_t len = strlen(src);
    if (len < max_bytes) {
      memcpy(dst, src, len + 1);
      return;
    }
    size_t cut = max_bytes - 1;
    while (cut > 0 && ((src[cut] & 0xC0) == 0x80)) {
      cut--; // 退回到 UTF-8 多位元組字元的起始 byte
    }
    memcpy(dst, src, cut);
    dst[cut] = '\0';
  }

  void begin(uint16_t port = VBAN_UDP_PORT) {
    udp.begin(port);
    // 初始化預設 36 個工位名稱與測試 IP
    const char *def_names[36] = {
      "DIR 導播", "AD 副導", "PROD 製作", "AUD 音控", "CG 字幕", "VTR 慢動",
      "CAM1 一機", "CAM2 二機", "CAM3 三機", "CAM4 四機", "CAM5 五機", "CAM6 六機",
      "CAM7 七機", "CAM8 八機", "JIB 搖臂", "STEAD 穩定", "DRONE 航拍", "POV 微距",
      "STG 舞台", "LGT 燈光", "LED 大屏", "HOST 主持", "GUEST 來賓", "FLOOR 場控",
      "ALL 全體發", "GRP1 攝影群", "GRP2 技術群", "GRP3 舞台群", "IFB1 耳返1", "IFB2 耳返2",
      "MULTI 同發", "MUTE 禁聽", "CLR 清空", "SPL1 拆分1", "SPL2 拆分2", "DUCK 閃避"
    };
    for (int i = 0; i < VBAN_MAX_STATIONS; i++) {
      stations[i].remote_ip = IPAddress(192, 168, 1, 100 + i);
      snprintf(stations[i].expected_stream, 17, "STN_%02d_TALK", i);
      safeCopyUtf8(stations[i].display_name, def_names[i], sizeof(stations[i].display_name));
    }
  }

  // ==========================================================================
  // ★ 核心功能 1：發送本機 VBAN-TEXT 裝置名稱廣播包（每 2 秒 1 次）
  // 讓區網內所有導播台與 Intercom 分站的 2.25" / 4.0" 螢幕自動更新為本機設定的中文名稱！
  // ==========================================================================
  void broadcastIdentityBeacon(IPAddress broadcast_ip = IPAddress(255, 255, 255, 255)) {
    uint32_t now = millis();
    if (now - last_beacon_ms < 2000) return;
    last_beacon_ms = now;

    uint8_t pkt[VBAN_HEADER_SIZE + 128];
    VbanHeader *hdr = (VbanHeader *)pkt;
    memcpy(hdr->vban, "VBAN", 4);
    hdr->format_SR  = 0x40 | 0x03; // 0x40 = VBAN_PROTOCOL_TXT
    hdr->format_nbs = 0;
    hdr->format_nbc = 0;
    hdr->format_bit = 0;
    memset(hdr->streamname, 0, 16);
    strncpy(hdr->streamname, "VBAN_ID_SYNC", 15);
    hdr->nuFrame    = tx_frame_counter++;

    char *json = (char *)(pkt + VBAN_HEADER_SIZE);
    int jlen = snprintf(json, 127, "{\"stream\":\"%s\",\"name\":\"%s\"}", self_stream_name, self_device_name);

    udp.beginPacket(broadcast_ip, VBAN_UDP_PORT);
    udp.write(pkt, VBAN_HEADER_SIZE + jlen);
    udp.endPacket();
  }

  // ==========================================================================
  // ★ 核心功能 2：根據「三段式發話撥桿 (Latch / PTT) + 可選部分同發 / 群組群發」打出麥克風 VBAN
  // ==========================================================================
  void transmitMicToSelectedTargets(const int16_t *mic_mono_pcm, uint16_t num_samples, bool lever_talking) {
    if (num_samples == 0 || num_samples > 256) return;

    uint8_t pkt[VBAN_HEADER_SIZE + 512];
    VbanHeader *hdr = (VbanHeader *)pkt;
    memcpy(hdr->vban, "VBAN", 4);
    hdr->format_SR  = 3; // 48000 Hz Audio
    hdr->format_nbs = (uint8_t)(num_samples - 1);
    hdr->format_nbc = 0; // 1ch Mono Talkback
    hdr->format_bit = 1; // 16-bit PCM
    memset(hdr->streamname, 0, 16);
    strncpy(hdr->streamname, self_stream_name, 15);
    hdr->nuFrame    = tx_frame_counter++;

    memcpy(pkt + VBAN_HEADER_SIZE, mic_mono_pcm, num_samples * sizeof(int16_t));
    size_t total_len = VBAN_HEADER_SIZE + num_samples * sizeof(int16_t);

    // 檢查 K24 (ALL 全體廣播)
    if ((lever_talking && stations[24].selected_for_talk) || stations[24].momentary_ptt) {
      udp.beginPacket(IPAddress(255, 255, 255, 255), VBAN_UDP_PORT);
      udp.write(pkt, total_len);
      udp.endPacket();
      return;
    }

    // 逐一發送給所有「被勾選部分同發 (selected_for_talk)」或「正按住直通 PTT」的工位
    for (int i = 0; i < 24; i++) {
      bool group_hit = false;
      // K25 = GRP1 攝影群 (K6..K17), K26 = GRP2 技術群 (K0..K5), K27 = GRP3 舞台群 (K18..K23)
      if (((lever_talking && stations[25].selected_for_talk) || stations[25].momentary_ptt) && (i >= 6 && i <= 17)) group_hit = true;
      if (((lever_talking && stations[26].selected_for_talk) || stations[26].momentary_ptt) && (i >= 0 && i <= 5))  group_hit = true;
      if (((lever_talking && stations[27].selected_for_talk) || stations[27].momentary_ptt) && (i >= 18 && i <= 23)) group_hit = true;

      bool target_active = group_hit || stations[i].momentary_ptt || (lever_talking && stations[i].selected_for_talk);
      if (target_active) {
        udp.beginPacket(stations[i].remote_ip, VBAN_UDP_PORT);
        udp.write(pkt, total_len);
        udp.endPacket();
      }
    }
  }

  // ==========================================================================
  // ★ 核心功能 3：接收 VBAN 封包 ➔ 自動更新遠端裝置名稱、計算 VOX 來電閃燈、過濾被 Mute 的人、執行 8ch 拆分轉發
  // 回傳混音後的立體聲 PCM 幀數供送入 WM8960 耳機/小喇叭播放
  // ==========================================================================
  size_t pollAndProcessIncomingVban(int16_t *out_stereo_buf, size_t max_frames) {
    int pkt_size = udp.parsePacket();
    if (pkt_size < VBAN_HEADER_SIZE) {
      // 清除逾時的 VOX 閃燈狀態 (> 350ms 無聲音自動熄滅閃燈)
      uint32_t now = millis();
      any_station_talking = false;
      for (int i = 0; i < 24; i++) {
        if (stations[i].vox_speaking && (now - stations[i].last_vox_ms > 350)) {
          stations[i].vox_speaking = false;
          stations[i].rx_rms_dbfs  = -60.0f;
        }
        if (stations[i].vox_speaking) any_station_talking = true;
      }
      return 0;
    }

    uint8_t buf[1500];
    int len = udp.read(buf, sizeof(buf) - 1);
    buf[len] = '\0';
    VbanHeader *hdr = (VbanHeader *)buf;
    if (memcmp(hdr->vban, "VBAN", 4) != 0) return 0;

    uint8_t sub_proto = hdr->format_SR & 0xE0;
    IPAddress sender_ip = udp.remoteIP();

    // ------------------------------------------------------------------------
    // 分支 A：收到 VBAN-TEXT 裝置名稱廣播包 ➔ 自動更新本機 2.25" 螢幕對應工位名稱！
    // ------------------------------------------------------------------------
    if (sub_proto == 0x40) {
      const char *json = (const char *)(buf + VBAN_HEADER_SIZE);
      const char *name_pos = strstr(json, "\"name\":\"");
      if (name_pos) {
        name_pos += 8;
        const char *end_q = strchr(name_pos, '\"');
        if (end_q && (end_q - name_pos) > 0) {
          char new_name[32] = {0};
          size_t nlen = min((size_t)(end_q - name_pos), sizeof(new_name) - 1);
          memcpy(new_name, name_pos, nlen);
          for (int i = 0; i < 24; i++) {
            if (stations[i].remote_ip == sender_ip && stations[i].auto_accept_vban_name) {
              safeCopyUtf8(stations[i].display_name, new_name, sizeof(stations[i].display_name));
            }
          }
        }
      }
      return 0;
    }

    // ------------------------------------------------------------------------
    // 分支 B：收到 VBAN 音訊封包 (1ch 工位對講 或 8ch 導播總線)
    // ------------------------------------------------------------------------
    uint16_t nbs = (uint16_t)hdr->format_nbs + 1;
    uint8_t  nbc = hdr->format_nbc + 1;
    const int16_t *pcm = (const int16_t *)(buf + VBAN_HEADER_SIZE);
    size_t valid_frames = min((size_t)nbs, max_frames);

    // 若是 8 聲道總線封包 (nbc == 8) ➔ 執行切分轉發規則 (Split Routes 1..6)
    if (nbc >= 4) {
      processMultiChannelSplitter(pcm, valid_frames, nbc);
    }

    // 找出是哪個工位打過來的封包
    int stn_idx = -1;
    for (int i = 0; i < 24; i++) {
      if (stations[i].remote_ip == sender_ip) {
        stn_idx = i;
        // 若該工位尚未收到中文名，先用 Header 的 16B streamname 更新螢幕
        if (stations[i].auto_accept_vban_name && hdr->streamname[0] != '\0') {
          char sname[17] = {0};
          memcpy(sname, hdr->streamname, 16);
          // 僅當使用者沒被 JSON 覆蓋時可作 fallback
        }
        break;
      }
    }

    // 計算來話 RMS 語音能量 ➔ 決定是否觸發「🟠 VOX 來電有聲自動閃燈」
    uint64_t sum_sq = 0;
    for (size_t f = 0; f < valid_frames; f++) {
      int32_t s = pcm[f * nbc];
      sum_sq += (uint64_t)(s * s);
    }
    float rms = sqrtf((float)sum_sq / (float)valid_frames) / 32768.0f;
    float db  = (rms > 0.0001f) ? (20.0f * log10f(rms)) : -60.0f;

    if (stn_idx >= 0) {
      stations[stn_idx].rx_rms_dbfs = db;
      if (db >= vox_threshold_dbfs) {
        stations[stn_idx].vox_speaking = true;
        stations[stn_idx].last_vox_ms  = millis();
        any_station_talking = true;
      }
      // ★ 如果這個工位被你「雙擊單獨 Mute (listen_muted == true)」，直接丟棄他的聲音不放進耳機！
      if (stations[stn_idx].listen_muted) {
        return 0;
      }
    }

    // 將聲音寫入輸出立體聲緩衝（若開啟 Auto-Ducking 且有人講話，PGM 背景音自動衰減 -15dB ≈ 0.18x）
    for (size_t f = 0; f < valid_frames; f++) {
      int16_t L = pcm[f * nbc];
      int16_t R = (nbc >= 2) ? pcm[f * nbc + 1] : L;
      out_stereo_buf[f * 2]     = L;
      out_stereo_buf[f * 2 + 1] = R;
    }
    return valid_frames;
  }

private:
  // 將多聲道 (例如 8ch) VBAN 封包拆出指定單聲道並轉發給其他工位 IP
  void processMultiChannelSplitter(const int16_t *interleaved_pcm, size_t frames, uint8_t total_ch) {
    uint8_t out_pkt[VBAN_HEADER_SIZE + 512];
    VbanHeader *ohdr = (VbanHeader *)out_pkt;
    memcpy(ohdr->vban, "VBAN", 4);
    ohdr->format_SR  = 3; // 48kHz
    ohdr->format_nbs = (uint8_t)(frames - 1);
    ohdr->format_nbc = 0; // 拆成 1ch 單聲道轉發
    ohdr->format_bit = 1;

    int16_t *out_samples = (int16_t *)(out_pkt + VBAN_HEADER_SIZE);

    for (int r = 0; r < VBAN_SPLIT_ROUTES; r++) {
      if (!split_routes[r].enabled || split_routes[r].src_ch_index >= total_ch) continue;
      uint8_t ch = split_routes[r].src_ch_index;
      for (size_t f = 0; f < frames; f++) {
        out_samples[f] = interleaved_pcm[f * total_ch + ch];
      }
      memset(ohdr->streamname, 0, 16);
      strncpy(ohdr->streamname, split_routes[r].out_stream, 15);
      ohdr->nuFrame = split_routes[r].frame_cnt++;

      udp.beginPacket(split_routes[r].dest_ip, VBAN_UDP_PORT);
      udp.write(out_pkt, VBAN_HEADER_SIZE + frames * sizeof(int16_t));
      udp.endPacket();
    }
  }
};
