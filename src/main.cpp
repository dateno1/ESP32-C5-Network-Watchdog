// ============================================================
//  NetWatchdog C5 — ESP32-C5 network watchdog, 2.8" TFT
//
//  Pings two targets (domain or IP) and reports availability on
//  a 320x240 TFT, with a config portal (SoftAP + LAN), dual-band
//  Wi-Fi, failover, statistics, status LED and a HW watchdog.
//
//  All user-editable settings:  config.h
//  Sections:
//    1 UI drawing      2 Validation/DNS     3 Web handlers
//    4 Ping engine     5 Scan engine        6 AP mode
//    7 Status LED      8 Lifecycle (load/connect/setup/loop)
// ============================================================
#include "config.h"

#include <WiFi.h>
#include "esp_wifi.h"
#include <esp_netif.h>
#include <WebServer.h>
#include <Preferences.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <SPI.h>
#include <ESP32Ping.h>
#include <XPT2046_Touchscreen.h>
#include <esp_task_wdt.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
// ============================================================
void pingWorkerTask(void *param);
void loadSettings(); // NVS Configuration Restore

void wifiBandAuto();
void harvestScanCache();
void handlePingLog();
void runDualBandScan();

void setLed(uint8_t r, uint8_t g, uint8_t b) { rgbLedWrite(LED_PIN, r, g, b); }

bool use_dhcp = true;

int display_rotation = DEFAULT_ROTATION;
inline int getTouchRotation(int display_rot) {
    return (display_rot == 3) ? 1 : 3;
}
unsigned long ping_interval = DEFAULT_PING_INTERVAL_MS;
// ==========================================
// Monitoring screen layout (design constants)
// ==========================================
#define LINE_1_Y         18       // SSID marquee text vertical center
#define WIFI_BAR_X      270       // Wi-Fi signal icon horizontal origin
#define WIFI_BAR_Y       15       // Wi-Fi signal icon vertical origin

#define LINE_2_Y         80       // Target A bounding region vertical layout anchor
#define LINE_2_TEXT_Y    84       // Target A domain marquee string vertical center

#define LINE_3_Y        170       // Target B bounding region vertical layout anchor
#define LINE_3_TEXT_Y   174       // Target B domain marquee string vertical center

#define PING_STATUS_X    10       // Ping value (ms/FAIL) absolute horizontal origin
#define DOMAIN_SCROLL_X 125       // Domain string marquee horizontal entry point
#define DOMAIN_WIDTH    185       // Domain layout viewport horizontal restriction barrier

// ==========================================
// Object Instances
// ==========================================
Adafruit_ST7789 tft = Adafruit_ST7789(&SPI, LCD_CS, LCD_DC, -1);
XPT2046_Touchscreen touch(TOUCH_CS);
WebServer server(80);
Preferences preferences;
// ==========================================
// Global runtime state (boot defaults come from config.h)
// ==========================================
String host_A = DEFAULT_HOST_A;
String host_B = DEFAULT_HOST_B;
String local_ip_str = DEFAULT_LOCAL_IP;
String gateway_str  = DEFAULT_GATEWAY;
String netmask_str  = DEFAULT_NETMASK;
String dns_pri_str  = DEFAULT_DNS_PRI;
String dns_alt_str  = DEFAULT_DNS_ALT;
String ssid_pri = "";
String pass_pri = "";
String ssid_sec = "";
String pass_sec = "";
String scanCacheJSON = "[]";
bool is_ap_mode = false;
bool has_saved_config = false;     // true once real settings exist in NVS
bool backlight_is_boosted = false;
bool success_A = false, success_B = false;
bool prev_success_A = false, prev_success_B = false;
int scrollOffsetSSID = 0;
int scrollOffsetHostA = 0;
int scrollOffsetHostB = 0;
int apOffSSID = 0;
int apOffPW = 0;
int display_mode = 0;   // 0: Monitoring, 1: Stats 10M, 2: Stats 1H, 3: Network Info
int current_rssi = -100;
int latency_A = -1, latency_B = -1;
int prev_latency_A = -2, prev_latency_B = -2;   // anti-flicker shadow state
int prev_bars = -1;
unsigned long network_ready_at = 0;   // 0 = not ready; set when a valid IP first appears
unsigned long last_scroll_time = 0;
unsigned long last_wifi_check_time = 0;
unsigned long last_touch_time = 0;
unsigned long last_interaction_time = 0;
// ==========================================
// Statistical Analysis Tracking Structure
// ==========================================

// Offscreen canvases for pixel-perfect marquee clipping.
// Dimensions MUST match each call site's (width, font size) pair.
GFXcanvas16 canvasSSID(240, 20);   // SSID line: width 240, size 2
GFXcanvas16 canvasHost(185, 30);   // Target lines: width 185, size 3

// ==========================================
// Statistics — 1-minute block accumulator
// ==========================================
// Blocks hold SUMS (not averages), so merging is exact. History extends
// by adding blocks, not samples: 60 blocks = 1 hour at ~12 bytes/block.
#define STATS_BLOCK_MS  60000UL   // 1 minute per block
#define STATS_BLOCKS  ((STATS_LONG_MS + STATS_BLOCK_MS - 1) / STATS_BLOCK_MS)
static_assert(STATS_LONG_MS  >= STATS_BLOCK_MS,
              "STATS_LONG_MS must be at least one block (1 minute)");
static_assert(STATS_SHORT_MS <= STATS_LONG_MS,
              "Stats short window must not exceed the long window");

struct StatBlock {
    uint32_t sum_latency;   // Σ latency of answered pings (ms)
    uint32_t samples;       // pings attempted in this block
    uint32_t successes;     // pings answered in this block
};

struct TargetStats {
    StatBlock hist[STATS_BLOCKS];   // [0] = oldest sealed block
    StatBlock cur;                  // block in progress
    uint32_t block_started;

    void addSample(bool success, int latency) {
        unsigned long now = millis();
        if (block_started == 0) block_started = now;          // fresh boot
        rollIfNeeded(now);
        cur.samples++;
        if (success) {
            cur.successes++;
            if (latency > 0) cur.sum_latency += latency;
        }
    }

    uint32_t totalSamples() const {                 // replaces .count for no-data checks
        uint32_t s = cur.samples;
        for (int i = 0; i < STATS_BLOCKS; ++i) s += hist[i].samples;
        return s;
    }

    // avgOut = mean latency of answered pings in window (-1 if none answered)
    // rateOut = success % of all pings in the window
    void getWindowStats(unsigned long window_ms, int &avgOut, float &rateOut) {
        uint32_t want = (window_ms + STATS_BLOCK_MS - 1) / STATS_BLOCK_MS;   // ceil
        uint32_t sum = cur.sum_latency, samples = cur.samples, succ = cur.successes;
        for (int i = STATS_BLOCKS - 1; i >= 0 && want > 1; --i, --want) {
            sum     += hist[i].sum_latency;
            samples += hist[i].samples;
            succ    += hist[i].successes;
        }
        if (samples == 0) { avgOut = -1; rateOut = 0.0f; return; }
        rateOut = 100.0f * succ / samples;
        avgOut  = succ ? (int)(sum / succ) : -1;
    }

  private:
    void rollIfNeeded(unsigned long now) {
        if (now - block_started < STATS_BLOCK_MS) return;
        for (int i = 0; i < STATS_BLOCKS - 1; ++i) hist[i] = hist[i + 1];
        hist[STATS_BLOCKS - 1] = cur;               // seal oldest-first
        cur = StatBlock{0, 0, 0};
        block_started = now;
    }
};

TargetStats stats_A;
TargetStats stats_B;

// Subnet Helper Function
int calculateSubnetBits(uint32_t subnet_mask) {
    int bits = 0;
    for (uint32_t mask = subnet_mask; mask != 0; mask >>= 1) {
        if (mask & 1) bits++;
        else break;
    }
    return bits;
}
int calculateSubnetBits(IPAddress subnet) {
    return calculateSubnetBits((uint32_t)subnet);
}

// ==========================================
// C++ Native Compilation Forward Declarations
// ==========================================
bool tryConnectWiFi(String target_ssid, String target_pass, bool dhcp_mode, String ip_s, String gw_s, String mask_s, String dns_p, String dns_a, int timeout_seconds);
bool pingLogEnabled = ENABLE_PING_LOG;   // runtime state — /pinglog changes this
uint16_t getPingColor(bool success, int ms);
void handleRoot();
void handleSave();
void drawStaticConfigScreen();
void drawStatisticsScreen(unsigned long window_ms);
void refreshScreenLayout();
void enterApModeRuntime();
void drawApModeScreen();
void drawWifiSignalBar(int x, int y, int rssi);
void drawFixedPingStatus(int x, int y, bool success, int latency, int targetNum);
void drawSlidingText(GFXcanvas16 &cv, String text, int x, int y, int width, int &offset, uint16_t color, int size);
void handleFactoryReset();

#if ENABLE_NETLOG
  #define NETLOG(fmt, ...) log_i("[NET] "  fmt, ##__VA_ARGS__)
  #define WLOG(fmt, ...)   log_i("[WIFI] " fmt, ##__VA_ARGS__)
  #define HLOG(fmt, ...)   log_i("[HTTP] " fmt, ##__VA_ARGS__)
  #define TLOG(fmt, ...)   log_i("[TOUCH] " fmt, ##__VA_ARGS__)
#else
  #define NETLOG(...) do {} while (0)
  #define WLOG(...)   do {} while (0)
  #define HLOG(...)   do {} while (0)
  #define TLOG(...)   do {} while (0)
#endif

// PING log: runtime-gated so the webpage can silence it without reflashing.
#if ENABLE_NETLOG
  #define PLOG(...) do { if (pingLogEnabled) { log_i("[PING] " __VA_ARGS__); } } while (0)
#else
  #define PLOG(...) do {} while (0)
#endif

// ==========================================
// Configuration Web Portal HTML Web Asset String
// ==========================================
const char CONFIG_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1">
<style>
body { font-family: Arial, sans-serif; margin: 20px; background-color: #f4f4f4; text-align: center; }
.card { background: white; padding: 20px; border-radius: 8px; box-shadow: 0 4px 8px rgba(0,0,0,0.1); display: inline-block; text-align: left; max-width: 340px; width: 100%; }
input[type=text], input[type=password], select { width: 100%; padding: 8px; margin: 6px 0; box-sizing: border-box; border: 1px solid #ccc; border-radius: 4px; }
button { width: 100%; padding: 10px; background-color: #007bff; color: white; border: none; border-radius: 4px; cursor: pointer; font-size: 16px; margin-top: 10px; }
button:hover { background-color: #0056b3; }
h3 { background: #e8e8e8; padding: 6px 10px; border-radius: 4px; margin: 15px 0 8px 0; color: #333; }
.status {
    padding: 8px;
    border-radius: 4px;
    margin-bottom: 15px;
    text-align: center;
    font-size: 13px;
    font-weight: bold;
    background: #e8e8e8;
    color: #333;
}

/* Label grid layout */
.label-grid { width: 100%; border-collapse: collapse; margin-bottom: 6px; }
.label-grid td { padding: 4px; vertical-align: middle; }
.label-grid .label-cell { width: 70px; text-align: center; background: #f0f0f0; border-radius: 4px; font-size: 12px; color: #555; padding: 8px 4px; }
.label-grid .input-cell { padding-right: 6px; }
.label-grid .input-cell input { width: 100%; margin: 4px 0; padding: 8px; box-sizing: border-box; border: 1px solid #ccc; border-radius: 4px; }
.label-grid .button-cell { width: 80px; text-align: center; }
.label-grid .button-cell button { width: 100%; padding: 8px 4px; font-size: 13px; margin: 0; background-color: #007bff; color: white; border: none; border-radius: 4px; cursor: pointer; }
.label-grid .button-cell button:hover { background-color: #0056b3; }
.label-grid .button-cell button:disabled { background-color: #999; }
/* SSID Dropdown Menu */
.ssid-wrap { position: relative; width: 100%; }
.ssid-dd {
    position: absolute; top: 100%; left: 0; right: 0;
    background: #fff; border: 1px solid #ccc; border-radius: 4px;
    max-height: 180px; overflow-y: auto; z-index: 100; display: none;
    box-shadow: 0 2px 6px rgba(0,0,0,0.15); text-align: left;
}
.ssid-dd div { padding: 8px; cursor: pointer; font-size: 13px; border-bottom: 1px solid #eee; }
.ssid-dd div:hover { background: #e8f0fe; }
.ssid-dd .meta { color: #777; font-size: 11px; }
</style>
<script>
function toggleNetworkFields() {
  var dhcp = document.getElementById("dhcp_sel").value;
  var fields = document.getElementById("network_fields");
  var inputs = fields.querySelectorAll("input");
  if (dhcp === "1") {
    fields.style.display = "none";
    inputs.forEach(function(i) {
      i.disabled = true;
      i.removeAttribute("required");
      i.removeAttribute("pattern");
    });
  } else {
    fields.style.display = "block";
    inputs.forEach(function(i) {
      i.disabled = false;
      if (i.name === "ip" || i.name === "gw") {
        i.setAttribute("required", "");
        i.setAttribute("pattern", "^(25[0-5]|2[0-4][0-9]|1[0-9][0-9]|[1-9][0-9]|[1-9])(\\.(25[0-5]|2[0-4][0-9]|1[0-9][0-9]|[1-9][0-9]|[1-9])){2}\\.(25[0-4]|2[0-4][0-9]|1[0-9][0-9]|[1-9][0-9]|[1-9])$");
      } else if (i.name === "netmask") {
        i.setAttribute("required", "");
        i.setAttribute("pattern", "^(255|254|252|248|240|224|192|128|0)(\\.(255|254|252|248|240|224|192|128|0)){3}$");
      }
    });
  }
}

var scanData = { pri: [], sec: [] };

function scanNetworks(pref) {
  if (!pref) pref = 'both';
  // 'both' has no button element; guard regardless
  var btn = (pref === 'both') ? null : document.getElementById("scan_btn_" + pref);
  var t = btn ? btn.textContent : "";
  if (btn) { btn.disabled = true; btn.textContent = "Scanning…"; }

  fetch("/scan").then(r => r.json()).then(list => {
    var targets = (pref === 'both') ? ['pri','sec'] : [pref];
    targets.forEach(function(p) {
      scanData[p] = list.filter(function(ap){ return ap.ssid; });
    });
    if (btn) { btn.disabled = false; btn.textContent = t; }
  }).catch(() => {
    if (btn) { btn.disabled = false; btn.textContent = t; }
  });
}

function renderSsidList(pref) {
  var box = document.getElementById("ssid_" + pref + "_dd");
  var inp = document.getElementById("ssid_" + pref);
  var f = inp.value.trim().toLowerCase();
  box.innerHTML = "";
  var items = scanData[pref].filter(function(ap) {
    return !f || ap.ssid.toLowerCase().indexOf(f) !== -1;
  });
  if (!items.length) { box.style.display = "none"; return; }
  items.forEach(function(ap) {
    var d = document.createElement("div");
    var b = document.createElement("b");
    b.textContent = ap.ssid;
    var s = document.createElement("span");
    s.className = "meta";
        s.textContent = "  " + ap.quality + " " + ap.lock + " " + ap.rssi + "dBm " + ap.enc + "  " + ap.band + "GHz ch " + ap.ch;
    d.appendChild(b); d.appendChild(s);
    d.onmousedown = function(e) {
      e.preventDefault();
      inp.value = ap.ssid;
      box.style.display = "none";
    };
    box.appendChild(d);
  });
  box.style.display = "block";
}
function showSsidList(pref) { renderSsidList(pref); }
function hideSsidList(pref) {
  setTimeout(function() {
    document.getElementById("ssid_" + pref + "_dd").style.display = "none";
  }, 150);
}
</script>
</head>
<body onload="toggleNetworkFields(); scanNetworks('both');">
<h2>Net Watchdog C5 Config Portal</h2>
<div class="card">
  <h3>Current Device Info (read-only)</h3>
  <table class="label-grid">%INFO_ROWS%</table>
  <div class="status" style="background:%STATUS_BG%; color:%STATUS_FG%;">
  Current Mode: %IP_MODE% <br> Active IP: %CUR_IP%
</div>
  <form action="/save" method="POST">

    <h3>Wi-Fi Connect Setting</h3>
    <table class="label-grid">
      <tr>
        <td class="label-cell" rowspan="2"><b>Primary<br>WIFI</b></td>
        <td class="input-cell">
  <div class="ssid-wrap">
    <input type="text" id="ssid_pri" name="ssid_pri" value="%SSID_PRI%" placeholder="Type or pick from scan" required
           onfocus="showSsidList('pri')" oninput="showSsidList('pri')" onblur="hideSsidList('pri')">
    <div id="ssid_pri_dd" class="ssid-dd"></div>
  </div>
</td>
      </tr>
      <tr>
        <td class="input-cell">
          <input type="password" name="pass_pri" value="%PASS_PRI%" maxlength="63" placeholder="Password"
          onclick="if(this.type==='password')this.type='text';" onblur="this.type='password';">
        </td>
      </tr>
    </table>

    <table class="label-grid">
      <tr>
        <td class="label-cell" rowspan="2"><b>Alt<br>WIFI</b></td>
        <td class="input-cell">
  <div class="ssid-wrap">
    <input type="text" id="ssid_sec" name="ssid_sec" value="%SSID_SEC%" placeholder="Type or pick from scan"
           onfocus="showSsidList('sec')" oninput="showSsidList('sec')" onblur="hideSsidList('sec')">
    <div id="ssid_sec_dd" class="ssid-dd"></div>
  </div>
</td>
      </tr>
      <tr>
        <td class="input-cell">
          <input type="password" name="pass_sec" value="%PASS_SEC%" maxlength="63" placeholder="Password"
          onclick="if(this.type==='password')this.type='text';" onblur="this.type='password';">
        </td>
      </tr>
    </table>

    <h3>Device IP Assignment</h3>
    <select name="dhcp" id="dhcp_sel" onchange="toggleNetworkFields()">
		%DHCP_SEL%
    </select>
    <div id="network_fields">
      <table class="label-grid">
        <tr>
          <td class="label-cell"><b>Static<br>IP</b></td>
          <td class="input-cell">
            <input type="text" name="ip" value="%IP%"
                   pattern="^(25[0-5]|2[0-4][0-9]|1[0-9][0-9]|[1-9][0-9]|[1-9])(\.(25[0-5]|2[0-4][0-9]|1[0-9][0-9]|[1-9][0-9]|[1-9])){2}\.(25[0-4]|2[0-4][0-9]|1[0-9][0-9]|[1-9][0-9]|[1-9])$"
                   title="Format: 1-255 . 1-255 . 1-255 . 1-254 (e.g., 192.168.100.100)"
                   required>
          </td>
        </tr>
      </table>
      <table class="label-grid">
        <tr>
          <td class="label-cell"><b>Gateway</b></td>
          <td class="input-cell">
            <input type="text" name="gw" value="%GW%"
                   pattern="^(25[0-5]|2[0-4][0-9]|1[0-9][0-9]|[1-9][0-9]|[1-9])(\.(25[0-5]|2[0-4][0-9]|1[0-9][0-9]|[1-9][0-9]|[1-9])){2}\.(25[0-4]|2[0-4][0-9]|1[0-9][0-9]|[1-9][0-9]|[1-9])$"
                   title="Format: 1-255 . 1-255 . 1-255 . 1-254"
                   required>
          </td>
        </tr>
      </table>
      <label>Subnet Mask:</label>
      <input type="text" name="netmask" value="%NM%"
             pattern="^(255|254|252|248|240|224|192|128|0)(\.(255|254|252|248|240|224|192|128|0)){3}$"
             title="Common: 255.255.255.0, 255.255.0.0, 255.0.0.0"
             required>
    </div>

    <h3>DNS Config</h3>
    <table class="label-grid">
      <tr>
        <td class="label-cell"><b>Primary<br>DNS</b></td>
        <td class="input-cell">
          <input type="text" name="dns_pri" value="%DNS_PRI%"
          pattern="^(25[0-5]|2[0-4][0-9]|1[0-9][0-9]|[1-9][0-9]|[0-9])(\.(25[0-5]|2[0-4][0-9]|1[0-9][0-9]|[1-9][0-9]|[0-9])){3}$"
          title="Valid IPv4 address (0-255 in each octet)" required>
        </td>
      </tr>
    </table>

    <table class="label-grid">
      <tr>
        <td class="label-cell"><b>Alt<br>DNS</b></td>
        <td class="input-cell">
          <input type="text" name="dns_alt" value="%DNS_ALT%"
          pattern="^(25[0-5]|2[0-4][0-9]|1[0-9][0-9]|[1-9][0-9]|[0-9])(\.(25[0-5]|2[0-4][0-9]|1[0-9][0-9]|[1-9][0-9]|[0-9])){3}$"
          title="Valid IPv4 address (0-255 in each octet)">
        </td>
      </tr>
    </table>

    <h3>Watchdog Targets</h3>
    <table class="label-grid">
      <tr>
        <td class="label-cell"><b>Target 1</b></td>
        <td class="input-cell">
          <input type="text" name="hostA" placeholder="google.com" value="%HOST_A%" required>
        </td>
      </tr>
    </table>

    <table class="label-grid">
      <tr>
        <td class="label-cell"><b>Target 2</b></td>
        <td class="input-cell">
          <input type="text" name="hostB" placeholder="github.com" value="%HOST_B%">
        </td>
      </tr>
    </table>

    <h3>Ping Loop Interval (Seconds)</h3>
    <input type="text" name="ping_int" placeholder="3" value="%PING_INT%" required>

    <h3>Screen Rotation Mode</h3>
    <select name="scr_rot">
      <option value="3" selected>Landscape</option>
      <option value="1">Landscape (Inverted)</option>
    </select>

    <button type="submit">Save Settings & Reboot</button>
  </form>
  <form action="/factory" method="POST" onsubmit="return confirm('Erase ALL settings and reboot?');">
    <button type="submit" style="background-color:#d9534f;">Factory Reset (Erase All Settings)</button>
  </form>
</div>
<div style="margin-top: 15px; color: #777; font-size: 12px; font-weight: bold;">
  <a href="%PINGLOG_URL%" style="color:#007bff; text-decoration:none;">%PINGLOG_LABEL%</a><br>
  Firmware Version : %%VERSION%%
</div>
</body></html>
)rawliteral";

// ==========================================
// 1. UI Rendering & Drawing Helper Implementations
// ==========================================

// offset states: -2 = full redraw (label+value), -1 = static drawn (skip), >=0 = scrolling
// (any negative entry is treated as "start scrolling at 0")
void drawApMarqueeValue(const char* value, int y, int &offset, uint16_t color) {
    const int charW = 12, lineH = 20;
    const int labelEndX = 70;

    int len   = strlen(value);
    int textW = len * charW;

    tft.setTextSize(FONT_SIZE_SMALL);
    tft.setTextColor(color);
    tft.setTextWrap(false);

    if (textW <= 320 - labelEndX) {
        // === Fits: draw once, then never touch again ===
        if (offset != -1) {
            tft.fillRect(labelEndX, y, 320 - labelEndX, lineH, ST77XX_BLACK);
            tft.setCursor(labelEndX, y);
            tft.print(value);
            offset = -1;
        }
        return;
    }

    // === Too long: marquee from x=320 ===
    if (offset < 0) offset = 0;

    tft.fillRect(labelEndX, y, 320 - labelEndX, lineH, ST77XX_BLACK);

    int Lx = 320 - offset;
    int skip = 0;
    if (Lx < labelEndX + 1) {
        skip = (labelEndX + 1 - Lx + charW - 1) / charW;
    }
    if (skip < len) {
        int cx = Lx + skip * charW;
        if (cx < 320) {
            tft.setCursor(cx, y);
            tft.print(&value[skip]);
        }
    }

    offset += 2;
    if (offset > textW + 310) offset = 0;
}

// Renders an adaptive 5-element RSSI bar chart based on signal strength metrics
void drawWifiSignalBar(int x, int y, int rssi) {
    int bars = 0; uint16_t barColor = ST77XX_RED;
    if (rssi > -50) { bars = 5; barColor = ST77XX_GREEN; }
	else if (rssi > -60) { bars = 4; barColor = ST77XX_GREEN; }
    else if (rssi > -70) { bars = 3; barColor = ST77XX_ORANGE; }
    else if (rssi > -80) { bars = 2; barColor = ST77XX_RED; }
	else if (rssi > -90) { bars = 1; barColor = ST77XX_RED; }  
    if (bars == prev_bars) return; prev_bars = bars;
    tft.fillRect(x, y, 40, 25, ST77XX_BLACK);
    for (int i = 0; i < 5; i++) {
        int barHeight = (i + 1) * 5; int currentY = y + 25 - barHeight;
        if (i < bars) tft.fillRect(x + (i * 7), currentY, 5, barHeight, barColor);
        else tft.drawRect(x + (i * 7), currentY, 5, barHeight, 0x7BEF);
    }
}

// Determines the graphic color theme mapped to ping response performance windows
uint16_t getPingColor(bool success, int ms) {
    if (!success || ms < 0) return ST77XX_RED;      
    if (ms >= 100) return ST77XX_ORANGE;            
    if (ms >= 50) return ST77XX_BLUE;				
    return ST77XX_GREEN;                            
}

// Renders ping result centered inside the fixed left cell of its row.
// x/y kept in the signature for call-site compatibility; cell geometry is fixed by layout.
void drawFixedPingStatus(int x, int y, bool success, int latency, int targetNum) {
    (void)x; (void)y;
    bool noData = (targetNum == 1) ? (stats_A.totalSamples() == 0)
                                   : (host_B.length() == 0 || stats_B.totalSamples() == 0);
    if (targetNum == 1) {
        if (!noData && success == prev_success_A && latency == prev_latency_A) return;
        prev_success_A = success; prev_latency_A = latency;
    } else {
        if (!noData && success == prev_success_B && latency == prev_latency_B) return;
        prev_success_B = success; prev_latency_B = latency;
    }
    int cellTop = (targetNum == 1) ? 55 : 145;
    int cellBot = (targetNum == 1) ? 145 : SCREEN_HEIGHT;
    char buf[12];
    if (noData)       snprintf(buf, sizeof(buf), "--");
    else if (success) snprintf(buf, sizeof(buf), "%dms", latency);
    else              snprintf(buf, sizeof(buf), "FAIL");
    tft.setTextSize(3);
    int16_t bx, by; uint16_t bw, bh;
    tft.getTextBounds(buf, 0, 0, &bx, &by, &bw, &bh);
    tft.fillRect(1, cellTop + 1, 113, (cellBot - cellTop) - 2, ST77XX_BLACK);
    tft.setTextColor(noData ? 0x7BEF : getPingColor(success, latency));
    tft.setCursor((115 - bw) / 2, cellTop + ((cellBot - cellTop) - bh) / 2);
    tft.print(buf);
}

void drawSlidingText(GFXcanvas16 &cv, String text, int x, int y, int width, int &offset, uint16_t color, int size) {
    int charWidth      = (size == 3) ? 18 : 12;
    int lineHeight     = (size == 3) ? 30 : 20;
    int textPixelWidth = text.length() * charWidth;
    bool needsScroll   = (textPixelWidth > width);

    if (!needsScroll && offset == -1) return;   // static frame already painted

    // Render into the offscreen viewport canvas — clipping is automatic
    cv.fillScreen(0);
    cv.setTextSize(size);
    cv.setTextColor(color);
    cv.setTextWrap(false);
    cv.setCursor(needsScroll ? -offset : 0, 0);
    cv.print(text);

    if (needsScroll) {
        offset += 2;
        if (offset > (textPixelWidth - width + charWidth)) offset = -width;
    } else {
        offset = -1;                             // static frame cached
    }
    // Single clipped blit: nothing can ever bleed outside [x, x+width)
    tft.drawRGBBitmap(x, y, cv.getBuffer(), width, lineHeight);
}

// [Mode 1/2 View] Historical statistics over `window_ms`, values centered per quadrant
void drawCenteredInCell(const char* text, int x0, int y0, int x1, int y1, uint16_t color) {
    tft.setTextSize(FONT_SIZE_SMALL);
    int16_t bx, by; uint16_t bw, bh;
    tft.getTextBounds(text, 0, 0, &bx, &by, &bw, &bh);
    tft.setTextColor(color);
    tft.setCursor(x0 + ((x1 - x0) - (int)bw) / 2, y0 + ((y1 - y0) - (int)bh) / 2);
    tft.print(text);
}

// [Mode 1 View] Historical statistics over `window_ms` (pass STATS_WINDOW_MS)
void drawStatisticsScreen(unsigned long window_ms) {
    tft.fillScreen(ST77XX_BLACK);
    tft.setTextSize(FONT_SIZE_SMALL);
    tft.setTextColor(ST77XX_WHITE);
    char title[32];
    uint32_t blocksWanted = (window_ms + STATS_BLOCK_MS - 1) / STATS_BLOCK_MS;   // ceil
    snprintf(title, sizeof(title), "Stats in Recent %lu Min",
         (unsigned long)(blocksWanted * STATS_BLOCK_MS) / 60000UL);
    int16_t bx, by; uint16_t bw, bh;
    tft.getTextBounds(title, 0, 0, &bx, &by, &bw, &bh);
    tft.setCursor((SCREEN_WIDTH - bw) / 2, 15);
    tft.print(title);
    tft.drawRect(0, 45, SCREEN_WIDTH, SCREEN_HEIGHT - 45, ST77XX_WHITE);
    tft.drawFastHLine(0, 142, SCREEN_WIDTH, ST77XX_WHITE);
    tft.drawFastVLine(160, 45, SCREEN_HEIGHT - 45, ST77XX_WHITE);

        // Quadrant labels (gray) — x=38/176 chosen so both fit at FONT_SIZE_SMALL
    tft.setTextColor(0x7BEF);
    tft.setCursor(38, 50);   tft.print("Average");
    tft.setCursor(176, 50); tft.print("Success (%)");
    tft.setCursor(38, 147);  tft.print("Average");
    tft.setCursor(176, 147);tft.print("Success (%)");

    char buf[12];
    int avgA; float rateA; stats_A.getWindowStats(window_ms, avgA, rateA);
    if (stats_A.totalSamples() == 0) {                                   // no data yet
        drawCenteredInCell("--", 0, 45, 160, 142, 0x7BEF);
        drawCenteredInCell("--", 160, 45, 320, 142, 0x7BEF);
    } else {
        if (avgA == -1) drawCenteredInCell("FAIL", 0, 45, 160, 142, ST77XX_RED);
        else { snprintf(buf, sizeof(buf), "%dms", avgA);
               drawCenteredInCell(buf, 0, 45, 160, 142, getPingColor(true, avgA)); }
        snprintf(buf, sizeof(buf), "%.1f%%", rateA);
        drawCenteredInCell(buf, 160, 45, 320, 142, ST77XX_CYAN);
    }

    if (host_B.length()==0 || stats_B.totalSamples() == 0) {           // B disabled or no data
        drawCenteredInCell("--", 0, 142, 160, 240, 0x7BEF);
        drawCenteredInCell("--", 160, 142, 320, 240, 0x7BEF);
    } else {
                int avgB; float rateB; stats_B.getWindowStats(window_ms, avgB, rateB);
        if (avgB == -1) drawCenteredInCell("FAIL", 0, 142, 160, 240, ST77XX_RED);
        else { snprintf(buf, sizeof(buf), "%dms", avgB);
               drawCenteredInCell(buf, 0, 142, 160, 240, getPingColor(true, avgB)); }
        snprintf(buf, sizeof(buf), "%.1f%%", rateB);
        drawCenteredInCell(buf, 160, 142, 320, 240, ST77XX_CYAN);
    }
}

// [Mode 2 View] Network state screen.
// Normal: full network info listing. DHCP-failure (169.254.x.x): dedicated status view.
void drawStaticConfigScreen() {
    tft.fillScreen(ST77XX_BLACK);
    tft.setTextSize(FONT_SIZE_SMALL);

    bool dhcp_failed = (use_dhcp && local_ip_str.startsWith("169.254"));

    // Header: title depends on state, version top-right (kept on both views)
    tft.setTextColor(ST77XX_CYAN);
    tft.setCursor(10, 15);
    tft.print(dhcp_failed ? "Network Status" : "Network Info");   // failure gets its own title

    char verText[20];
    snprintf(verText, sizeof(verText), "Ver:%s", FIRMWARE_VERSION);
    int16_t bx, by; uint16_t bw, bh;
    tft.getTextBounds(verText, 0, 0, &bx, &by, &bw, &bh);
    tft.setTextColor(ST77XX_YELLOW);
    tft.setCursor(SCREEN_WIDTH - bw - 8, 15);
    tft.print(verText);

    // Frame drawn ONCE for both views: divider + full border square
    tft.drawFastHLine(0, 40, SCREEN_WIDTH, ST77XX_WHITE);
    tft.drawRect(0, 45, SCREEN_WIDTH, SCREEN_HEIGHT - 45, ST77XX_WHITE);

    if (dhcp_failed) {
        // === DHCP Failed view: centered red title + hints, inside the border ===
        tft.setTextSize(3);
        tft.setTextColor(ST77XX_RED);
        tft.getTextBounds("DHCP",   0, 0, &bx, &by, &bw, &bh);
        tft.setCursor((SCREEN_WIDTH - bw) / 2, 70);
        tft.print("DHCP");
        tft.getTextBounds("Failed", 0, 0, &bx, &by, &bw, &bh);
        tft.setCursor((SCREEN_WIDTH - bw) / 2, 105);
        tft.print("Failed");

        tft.setTextSize(1);
        tft.setTextColor(ST77XX_YELLOW);
        tft.getTextBounds("No IP assignment from router.", 0, 0, &bx, &by, &bw, &bh);
        tft.setCursor((SCREEN_WIDTH - bw) / 2, 160);
        tft.print("No IP assignment from router.");
        tft.getTextBounds("Check DHCP server or LAN cable.", 0, 0, &bx, &by, &bw, &bh);
        tft.setCursor((SCREEN_WIDTH - bw) / 2, 180);
        tft.print("Check DHCP server or LAN cable.");
    }
    else {
        // === Normal view: full infrastructure profile ===
        tft.setTextColor(ST77XX_WHITE);
        tft.setCursor(10, 55);  tft.printf("IP Mode : %s", use_dhcp ? "DHCP" : "STATIC");
        tft.setCursor(10, 85);  tft.printf("IP Addr : %s", local_ip_str.c_str());
        tft.setCursor(10, 115); tft.printf("Netmask : %s", netmask_str.c_str());
        tft.setCursor(10, 145); tft.printf("Gateway : %s", gateway_str.c_str());
        tft.setCursor(10, 175); tft.printf("DNS 1   : %s", dns_pri_str.c_str());
        tft.setCursor(10, 205); tft.printf("DNS 2   : %s", dns_alt_str.c_str());
    }
}

// Handles viewport transitions and structural outline line updates
void refreshScreenLayout() {
    tft.fillScreen(ST77XX_BLACK);
    prev_latency_A = -2; prev_latency_B = -2; prev_bars = -1;
    scrollOffsetSSID = scrollOffsetHostA = scrollOffsetHostB = 0;  // clear "-1 cached" sentinel
    if (display_mode == 0) {
        tft.drawRect(0, 55, SCREEN_WIDTH, SCREEN_HEIGHT - 55, ST77XX_WHITE);
        tft.drawFastHLine(0, 145, SCREEN_WIDTH, ST77XX_WHITE); tft.drawFastVLine(115, 55, SCREEN_HEIGHT - 55, ST77XX_WHITE);
    } else if (display_mode == 1) { drawStatisticsScreen(STATS_SHORT_MS); }
      else if (display_mode == 2) { drawStatisticsScreen(STATS_LONG_MS); }
      else                        { drawStaticConfigScreen(); }
}

// ==========================================
// 2. Network Infrastructure
// ==========================================

//Check Static Input IP is Valid
bool isValidIPv4(const String& ip, bool strict_last_octet) {
    if (ip.length() < 7 || ip.length() > 15) return false;

    int octetIdx = 0, octetStart = 0;
    int octets[4] = {0, 0, 0, 0};

    for (int i = 0; i <= (int)ip.length(); i++) {
        if (i == (int)ip.length() || ip[i] == '.') {
            if (octetIdx >= 4) return false;       // too many octets
            if (i == octetStart) return false;     // empty octet ("1..2.3")

            int val = 0;
            for (int j = octetStart; j < i; j++) {
                if (ip[j] < '0' || ip[j] > '9') return false;   // non-digit
                val = val * 10 + (ip[j] - '0');
                if (val > 255) return false;                    // early overflow
            }
            // Leading-zero ban: "01" → reject ("0" alone is fine)
            if (i - octetStart > 1 && ip[octetStart] == '0') return false;

            octets[octetIdx++] = val;
            octetStart = i + 1;
        }
    }

    if (octetIdx != 4) return false;

    // Octets 0-2 (network part): 0-255 — 0 is legal ("10.0.0.1", "192.168.0.1")
    for (int k = 0; k < 3; k++) {
        if (octets[k] < 0 || octets[k] > 255) return false;    // range already checked
    }

    // Last octet (host part): 1-254
    //   0   → network address (e.g., 192.168.1.0)
    //   255 → broadcast (e.g., 192.168.1.255)
    if (octets[3] < 1 || octets[3] > 254) return false;

    if (strict_last_octet && octets[3] == 255) return false;   // already covered above; kept for API compat

    return true;
}

// Valid netmasks are contiguous 1-bits: 0,128,192,224,240,248,252,254,255 per octet,
// with 1-bits strictly before 0-bits
bool isValidIPv4Netmask(const String& nm) {
    if (nm.length() < 7 || nm.length() > 15) return false;
    
    // 1. Parsing URL with IPAddress
    IPAddress maskAddress;
    if (!maskAddress.fromString(nm)) return false;
    
    // 2. Convert IP to Single Integer
    uint32_t m = ((uint32_t)maskAddress[0] << 24) |
                 ((uint32_t)maskAddress[1] << 16) |
                 ((uint32_t)maskAddress[2] << 8)  |
                 ((uint32_t)maskAddress[3]);
                 
    // 3. Check Mask
    if (m == 0 || m == 0xFFFFFFFF) return false;
    uint32_t inverted = ~m;
    if ((inverted & (inverted + 1)) == 0) {
        return true;
    }
    return false; 
}

// Sets DNS servers on the STA interface WITHOUT stopping the DHCP client
// (WiFi.config() with an IP would freeze the lease -> use IDF API instead).
void applyManualDNS(const IPAddress& priDNS, const IPAddress& altDNS) {
    esp_netif_t *sta = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (!sta) { WLOG("WARN: STA netif not found - manual DNS skipped"); return; }
    esp_netif_dns_info_t d = {};
    d.ip.type = ESP_IPADDR_TYPE_V4;
    d.ip.u_addr.ip4.addr = (uint32_t)priDNS;
    esp_netif_set_dns_info(sta, ESP_NETIF_DNS_MAIN, &d);
    d.ip.u_addr.ip4.addr = (uint32_t)altDNS;
    esp_netif_set_dns_info(sta, ESP_NETIF_DNS_BACKUP, &d);
}

// ==========================================
// 3. Web handlers
// ==========================================

// GET /pinglog          -> status
// GET /pinglog?on=1     -> enable, redirect back to portal
// GET /pinglog?on=0     -> silence, redirect back to portal
void handlePingLog() {
    HLOG("GET /pinglog from %s (on='%s')",
         server.client().remoteIP().toString().c_str(),
         server.hasArg("on") ? server.arg("on").c_str() : "-");
    if (server.hasArg("on")) {
        pingLogEnabled = (server.arg("on") == "1");
        preferences.begin("net-watch", false);
        preferences.putBool("ping_log", pingLogEnabled);   // survive reboots
        preferences.end();
        if (pingLogEnabled) PLOG("PING log enabled via web");
    }
    // Always land back on the portal so the footer link shows the new state
    server.sendHeader("Location", "/");
    server.send(303, "text/plain", "");
}

void handleScan() {
    harvestScanCache();                        // instant reply from cache, never blocks
    server.send(200, "application/json", scanCacheJSON);
}

// Escapes user-provided values for safe inclusion in HTML attributes/text
String escapeHtml(const String& in) {
    String out = "";
    for (unsigned int i = 0; i < in.length(); i++) {
        char c = in[i];
        if      (c == '&') out += "&amp;";
        else if (c == '<') out += "&lt;";
        else if (c == '>') out += "&gt;";
        else if (c == '"') out += "&quot;";
        else if (c >= 32 && c <= 126) out += c;
    }
    return out;
}

// Builds one read-only row for the device info panel
String infoRow(const char* label, const String& value) {
    return "<tr><td class=\"label-cell\">" + String(label) +
           "</td><td class=\"input-cell\">" + escapeHtml(value) + "</td></tr>";
}

void handleRoot() {
    esp_task_wdt_reset();
    HLOG("GET / from %s (mode=%s)", server.client().remoteIP().toString().c_str(), is_ap_mode ? "AP" : "CLIENT");
    String html = String(CONFIG_HTML);
    bool dhcp_failed = (use_dhcp &&
                       (local_ip_str.startsWith("169.254") || gateway_str.startsWith("169.254")));
    String mode_str, cur_ip_str, ip_str, gw_str, nm_str, status_bg, status_fg;

    if (is_ap_mode) {
        // AP mode: no station IP exists — never present local_ip_str as active
        IPAddress apIP(AP_IP);
        mode_str   = "📡 AP Mode (Config Portal)";
        cur_ip_str = "⚠️ Not Set Yet — Device AP: " + apIP.toString();
        status_bg  = "#fff3cd"; status_fg = "#856404";
        // Prefill static fields only from genuinely saved config
        ip_str = has_saved_config ? local_ip_str : "";
        gw_str = has_saved_config ? gateway_str : "";
        nm_str = has_saved_config ? netmask_str : "";
    } else if (dhcp_failed) {
        mode_str   = "🔴 DHCP Failed";
        cur_ip_str = "❌ DHCP Failed";
        ip_str = ""; gw_str = ""; nm_str = "";
        status_bg = "#fee"; status_fg = "#c00";
    } else {
        mode_str   = String("✅ ") + (use_dhcp ? "DHCP" : "Static");
        cur_ip_str = String("✅ ") + local_ip_str;
        ip_str = local_ip_str; gw_str = gateway_str; nm_str = netmask_str;
        status_bg = "#eef"; status_fg = "#333";
    }

    // Read-only current-config table (passwords masked; revealed only via form click)
    String info;
    info += infoRow("Mode",    is_ap_mode ? "AP (Config Portal)" : (use_dhcp ? "DHCP" : "Static"));
    info += infoRow("IP",      is_ap_mode ? "192.168.4.1 (AP)" : local_ip_str);
    info += infoRow("Gateway", has_saved_config ? gateway_str : "(Not Set)");
    info += infoRow("Netmask", has_saved_config ? netmask_str : "(Not Set)");
    info += infoRow("DNS 1",   has_saved_config ? dns_pri_str : "(Not Set)");
    info += infoRow("DNS 2",   has_saved_config ? dns_alt_str : "(Not Set)");
    info += infoRow("SSID (Pri)", ssid_pri.length() ? ssid_pri : "(Not Set)");
    info += infoRow("PW (Pri)",   pass_pri.length() ? "********" : "(None)");
    info += infoRow("SSID (Alt)", ssid_sec.length() ? ssid_sec : "(Not Set)");
    info += infoRow("PW (Alt)",   pass_sec.length() ? "********" : "(None)");
    info += infoRow("Target 1", has_saved_config ? host_A : "(Not Set)");
    info += infoRow("Target 2", (has_saved_config && host_B.length()) ? host_B : "(Not Set)");
    info += infoRow("Ping Every", String(ping_interval / 1000) + "s");
	
	// Ping-log toggle link (footer) — URL flips the current state
    String pinglog_url   = String("/pinglog?on=") + (pingLogEnabled ? "0" : "1");
    String pinglog_label = pingLogEnabled
        ? "PING Log: Enabled — Click to Disable"
        : "PING Log: Disabled — Click to Enable";

    // Replace ALL placeholders (before %IP%: %IP_MODE%/%CUR_IP%; %PING_INT% also 4-char-safe)
    html.replace("%INFO_ROWS%", info);
    html.replace("%IP_MODE%",   mode_str);
    html.replace("%CUR_IP%",    cur_ip_str);
    html.replace("%STATUS_BG%", status_bg);
    html.replace("%STATUS_FG%", status_fg);
    html.replace("%DHCP_SEL%",  use_dhcp
        ? "<option value=\"1\" selected>DHCP (Auto)</option><option value=\"0\">Static IP (Manual)</option>"
        : "<option value=\"1\">DHCP (Auto)</option><option value=\"0\" selected>Static IP (Manual)</option>");
    html.replace("%SSID_PRI%",  escapeHtml(ssid_pri));
    html.replace("%SSID_SEC%",  escapeHtml(ssid_sec));
    html.replace("%PASS_PRI%",  escapeHtml(pass_pri));
    html.replace("%PASS_SEC%",  escapeHtml(pass_sec));
    html.replace("%HOST_A%",    escapeHtml(host_A));
    html.replace("%HOST_B%",    escapeHtml(host_B));
    html.replace("%DNS_PRI%",   dns_pri_str);
    html.replace("%DNS_ALT%",   dns_alt_str);
    html.replace("%PING_INT%",  String(ping_interval / 1000));
    html.replace("%IP%",        ip_str);
    html.replace("%GW%",        gw_str);
    html.replace("%NM%",        nm_str);
    html.replace("%%VERSION%%", FIRMWARE_VERSION);
    html.replace("%PINGLOG_URL%",   pinglog_url);
    html.replace("%PINGLOG_LABEL%", pinglog_label);
	
    // Sanity check: any surviving token = a replace didn't run
    const char* toks[] = {"%INFO_ROWS%","%IP_MODE%","%CUR_IP%","%STATUS_BG%","%STATUS_FG%","%DHCP_SEL%",
                          "%SSID_PRI%","%SSID_SEC%","%PASS_PRI%","%PASS_SEC%","%HOST_A%","%HOST_B%",
                          "%DNS_PRI%","%DNS_ALT%","%PING_INT%","%PINGLOG_URL%","%PINGLOG_LABEL%",
                          "%IP%","%GW%","%NM%","%%VERSION%%"};
    for (auto t : toks) {
        if (html.indexOf(t) >= 0) log_e("UNREPLACED token in page: %s", t);   // log_e = always visible
    }
	
    server.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate");
    server.sendHeader("Pragma", "no-cache");
    server.sendHeader("Expires", "0");
    server.send(200, "text/html; charset=utf-8", html);
    esp_task_wdt_reset();
}

// Parses input stream arrays and commits data securely into NVS sectors
void handleSave() {
	HLOG("POST /save from %s", server.client().remoteIP().toString().c_str());
	
    // === Validate required fields exist ===
        if (!server.hasArg("ssid_pri") || !server.hasArg("hostA")) {
		HLOG("REJECT: missing required fields");
        server.send(400, "text/html", "<h1>Missing required fields</h1>");
        return;
    }
    
    // === Get input values ===
    String ssid_pri_in = server.arg("ssid_pri");
    String ssid_sec_in = server.arg("ssid_sec"); // can be empty (no required attr)
    String dns_pri_in  = server.arg("dns_pri");
    String dns_alt_in  = server.arg("dns_alt");

    // Defensive: absent/garbage dhcp arg must NOT become Static (safe default = DHCP)
    bool dhcp_in = (server.arg("dhcp") != "0");
    HLOG("dhcp arg='%s' -> %s", server.arg("dhcp").c_str(), dhcp_in ? "DHCP" : "STATIC");

    // === Validate SSID length ===
    if (ssid_pri_in.length() > 32 || ssid_sec_in.length() > 32) {
		HLOG("REJECT: SSID too long (pri=%u sec=%u, max 32)", ssid_pri_in.length(), ssid_sec_in.length());
        server.send(400, "text/html", "<h1>SSID too long (max 32 chars)</h1>");
        return;
    }
    
    // === Validate DNS ===
    if (!isValidIPv4(dns_pri_in, false) || !isValidIPv4(dns_alt_in, false)) {
		HLOG("REJECT: invalid DNS (%s / %s)", dns_pri_in.c_str(), dns_alt_in.c_str());
        server.send(400, "text/html", "<h1>Invalid DNS IP</h1>");
        return;
    }
    
    // === Validate static IP config (if static mode) ===
    if (!dhcp_in) {
        if (!isValidIPv4(server.arg("ip"),  true)  ||
            !isValidIPv4(server.arg("gw"),  true)  ||
            !isValidIPv4Netmask(server.arg("netmask"))) {
			HLOG("REJECT: invalid static IP config");
            server.send(400, "text/html", "<h1>Invalid static IP configuration</h1>");
            return;
        }
    }
    
    // === All validation passed — save to NVS ===
    preferences.begin("net-watch", false);
    preferences.putString("ssid_pri", ssid_pri_in);
    preferences.putString("pass_pri", server.arg("pass_pri"));
    preferences.putString("ssid_sec", ssid_sec_in);
    preferences.putString("pass_sec", server.arg("pass_sec"));
    preferences.putBool("use_dhcp", dhcp_in);
    preferences.putString("local_ip", server.arg("ip"));
    preferences.putString("gateway", server.arg("gw"));
    preferences.putString("netmask", server.arg("netmask"));
    preferences.putString("dns_pri", dns_pri_in);
    preferences.putString("dns_alt", dns_alt_in);
    preferences.putString("host_A", server.arg("hostA"));
    preferences.putString("host_B", server.arg("hostB"));
    
    unsigned long input_sec = server.arg("ping_int").toInt();
    if (input_sec < 1)    input_sec = 1;
    if (input_sec > 3600) input_sec = 3600;   // sanity ceiling: 1 ping/hour max
    preferences.putULong("ping_interval", input_sec * 1000);
    
    int input_rot = server.arg("scr_rot").toInt();
    if (input_rot != 1 && input_rot != 3) input_rot = 3;
    preferences.putInt("scr_rotation", input_rot);
    
    preferences.end();
	
	HLOG("Saved: pri=\"%s\" sec=\"%s\" dhcp=%d ping=%lus rot=%d hostA=\"%s\" hostB=\"%s\"",
    ssid_pri_in.c_str(), ssid_sec_in.c_str(), (int)(!dhcp_in),
    input_sec, input_rot, server.arg("hostA").c_str(), server.arg("hostB").c_str());
    
    server.send(200, "text/html", "Settings Saved! Rebooting...");
    delay(2000);
    ESP.restart();
}

void handleFactoryReset() {
    HLOG("POST /factory from %s", server.client().remoteIP().toString().c_str());
    preferences.begin("net-watch", false);
    preferences.clear();          // wipe entire "net-watch" namespace
    preferences.end();
    server.send(200, "text/html",
        "<h1>Factory reset done. All settings erased.<br>Rebooting to AP mode...</h1>");
    delay(1500);
    ESP.restart();                // NVS empty -> boots into AP config mode
}

// ==========================================
// 4. Async Ping Engine (worker task + result queue)
// ==========================================

struct PingResult {
    uint8_t target;   // 1 = host_A, 2 = host_B
    bool    success;
    int     latency;  // ms, -1 on failure
};
QueueHandle_t pingResultQueue = nullptr;
TaskHandle_t  pingTaskHandle  = nullptr;

// NOTE: ESP32Ping's lwIP timers are not task-safe by design — this task context is the empirically stable one; see lib/ESP32Ping-Patched for the log_d patch.
void pingWorkerTask(void *param) {
    PLOG("Worker online (core %d)", xPortGetCoreID());
    if (host_B.length() == 0) PLOG("Target 2 not configured - pinging Target 1 only");
    PingResult r;
    bool wasOnline = false;
    for (;;) {
        bool online = (WiFi.status() == WL_CONNECTED);
        if (online && !wasOnline) PLOG("Wi-Fi online - resuming pings");
        if (!online && wasOnline) PLOG("Wi-Fi offline - pausing pings");
        wasOnline = online;

        if (online) {
            r.target  = 1;
            r.success = Ping.ping(host_A.c_str(), 2);
            r.latency = r.success ? Ping.averageTime() : -1;
            PLOG("T1 \"%s\" -> %s %dms", host_A.c_str(), r.success ? "OK" : "FAIL", r.latency);
            xQueueSend(pingResultQueue, &r, 0);

            if (host_B.length() > 0) {
                r.target  = 2;
                r.success = Ping.ping(host_B.c_str(), 2);
                r.latency = r.success ? Ping.averageTime() : -1;
                PLOG("T2 \"%s\" -> %s %dms", host_B.c_str(), r.success ? "OK" : "FAIL", r.latency);
                xQueueSend(pingResultQueue, &r, 0);
            }
        }
        vTaskDelay(ping_interval / portTICK_PERIOD_MS);
    }
}

// ==========================================
// 5. Scan AP (dual-band engine) — low-peak-memory version
// ==========================================
// The JSON cache is built incrementally: each band's results append straight
// into scanCacheJSON (no large temporary Strings), so the heap peak during
// scanning is ~the final cache size instead of 2-3x.

// Appends one band's results into the OPEN cache (caller wrote "[", closes "]").
// appended = total stored across bands (updated by reference); capped by SCAN_MAX_APS.
// Channel/RSSI statistics are gathered for ALL APs even past the cap.
int appendScanBandJSON(int n, bool &first, int &appended) {
    bool   seen[200] = {false};
    String chList = "";
    int    n24 = 0, n5 = 0;
    int    stored = 0;

    for (int i = 0; i < n; ++i) {
        int32_t rssi = WiFi.RSSI(i);
        int32_t chan = WiFi.channel(i);
        const char* band = (chan <= 14) ? "2.4" : "5";
        if (chan <= 14) n24++; else n5++;
        if (chan >= 0 && chan < 200 && !seen[chan]) {
            seen[chan] = true;
            if (chList.length()) chList += ",";
            chList += String(chan);
        }

        if (appended >= SCAN_MAX_APS) continue;          // cache full — stats only

        String ssid = WiFi.SSID(i);
        String safe = "";
        for (unsigned int c = 0; c < ssid.length(); ++c) {
            char ch = ssid[c];
            if (ch == '"')       safe += "\\\"";
            else if (ch == '\\') safe += "\\\\";
            else if (ch >= 32 && ch <= 126) safe += ch;
            else                 safe += '?';
        }
        bool secured = (WiFi.encryptionType(i) != WIFI_AUTH_OPEN);
        String q    = (rssi >= -60) ? "📶" : (rssi >= -75) ? "📡" : (rssi >= -85) ? "📃" : "⚠️";
        String lock = secured ? "🔒" : "🔓";
        if (!first) scanCacheJSON += ',';
        first = false;
        scanCacheJSON += "{\"ssid\":\"";  scanCacheJSON += safe;
        scanCacheJSON += "\",\"rssi\":";
        scanCacheJSON += String(rssi);
        scanCacheJSON += ",\"ch\":";
        scanCacheJSON += String(chan);
        scanCacheJSON += ",\"band\":\"";  scanCacheJSON += band;
        scanCacheJSON += "\",\"enc\":\""; scanCacheJSON += (secured ? "secured" : "open");
        scanCacheJSON += "\",\"quality\":\""; scanCacheJSON += q;
        scanCacheJSON += "\",\"lock\":\""; scanCacheJSON += lock;
        scanCacheJSON += "\"}";
        appended++;
        stored++;
    }
    NETLOG("  -> stored %d of %d APs (2.4G:%d 5G:%d, cap %d) | channels: %s",
           stored, n, n24, n5, SCAN_MAX_APS, chList.c_str());
    return stored;
}

void runDualBandScan() {
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    wifiBandAuto();
    esp_task_wdt_reset();

    scanCacheJSON = "[";                       // open fresh cache (old buffer freed)
    scanCacheJSON.reserve(SCAN_MAX_APS * 110); // ~110 bytes/AP worst case incl. escaping
    bool first = true;
    int appended = 0;
    int total24 = 0, total5 = 0;

    NETLOG("Dual-band scan phase 1/2: 2.4GHz...");
    WiFi.setBandMode(WIFI_BAND_MODE_2G_ONLY);
    int n24 = WiFi.scanNetworks(false, true, false, SCAN_MS_PER_CHAN);   // blocking
    if (n24 >= 0) total24 = appendScanBandJSON(n24, first, appended);
    WiFi.scanDelete();
    esp_task_wdt_reset();

    NETLOG("Dual-band scan phase 2/2: 5GHz...");
    WiFi.setBandMode(WIFI_BAND_MODE_5G_ONLY);
    int n5 = WiFi.scanNetworks(false, true, false, SCAN_MS_PER_CHAN);    // blocking
    if (n5 >= 0)  total5 = appendScanBandJSON(n5, first, appended);
    WiFi.scanDelete();
    WiFi.setBandMode(WIFI_BAND_MODE_AUTO);     // restore: STA must still reach 5G APs
    esp_task_wdt_reset();

    scanCacheJSON += ']';                      // close cache
    NETLOG("Dual-band scan done: 2.4G=%d 5G=%d cache=%u bytes", total24, total5, scanCacheJSON.length());
}

// Rebuilds the cache from arduino's current scan results, if any exist.
void harvestScanCache() {
    int n = WiFi.scanComplete();
    if (n < 0) return;
    scanCacheJSON = "[";
    scanCacheJSON.reserve(SCAN_MAX_APS * 110);
    bool first = true;
    int appended = 0;
    appendScanBandJSON(n, first, appended);
    scanCacheJSON += ']';
    WiFi.scanDelete();
    NETLOG("Scan cache rebuilt: %d APs stored, %u bytes", appended, scanCacheJSON.length());
}

// ==========================================
// 6. AP mode
// ==========================================

// Separated AP Mode Screen Drawing Function with Interactive Screen Button
void drawApModeScreen() {
    tft.fillScreen(ST77XX_BLACK);
    tft.drawFastHLine(0, 55, SCREEN_WIDTH, ST77XX_WHITE);
    tft.drawFastVLine(160, 0, 55, ST77XX_WHITE);
    
    // === Header (use COLOR_CYAN for visibility) ===
    tft.setTextSize(FONT_SIZE_SMALL);
    tft.setTextColor(ST77XX_RED);
    const char* apLabel = "AP Mode";
    int16_t x1, y1;
    uint16_t w, h;
    tft.getTextBounds(apLabel, 0, 0, &x1, &y1, &w, &h);
    tft.setCursor((160 - w) / 2, 18);
    tft.print(apLabel);
    
    // === Right: Version ===
    char verText[20];
	tft.setTextColor(ST77XX_WHITE);
    snprintf(verText, sizeof(verText), "Ver: %s", FIRMWARE_VERSION);
    tft.getTextBounds(verText, 0, 0, &x1, &y1, &w, &h);
    tft.setCursor(160 + (160 - w) / 2, 18);
    tft.print(verText);
    
    // === SSID Row ===
	tft.setCursor(10, 75);
	tft.setTextColor(ST77XX_CYAN);
    tft.print("SSID:");
    drawApMarqueeValue(AP_SSID,     75,  apOffSSID, ST77XX_CYAN);
    
    // === PW Row ===
    tft.setCursor(10, 100);
	tft.setTextColor(ST77XX_CYAN);
    tft.print("PW:");
    drawApMarqueeValue(AP_PASSWORD, 100, apOffPW,   ST77XX_CYAN);
    
    // === IP ===
	tft.setTextColor(ST77XX_BLUE);
    IPAddress apIP(AP_IP);
    tft.setCursor(10, 125);
    tft.printf("IP: %d.%d.%d.%d/%d",
        apIP[0], apIP[1], apIP[2], apIP[3],
        calculateSubnetBits(IPAddress(AP_SUBNET)));
    
    // === DHCP ===
	tft.setTextColor(ST77XX_BLUE);
    IPAddress dhcpStart(AP_DHCP_START);
    IPAddress dhcpEnd(AP_DHCP_END);
    tft.setCursor(10, 150);
    tft.printf("DHCP: %d.%d.%d.%d~%d",
        dhcpStart[0], dhcpStart[1], dhcpStart[2], dhcpStart[3],
        dhcpEnd[3]);
    
    // === Button ===
    tft.fillRect(170, 180, 140, 45, ST77XX_RED);
    tft.drawRect(170, 180, 140, 45, ST77XX_WHITE);
    tft.setTextColor(ST77XX_WHITE);
    tft.setTextSize(2);
    const char* btnText = "Client Mode";
    tft.getTextBounds(btnText, 0, 0, &x1, &y1, &w, &h);
    tft.setCursor(170 + (140 - w) / 2, 180 + (45 - h) / 2);
    tft.print(btnText);
}

// Emergency SoftAP access mode initialization sequence
void enterApModeRuntime() {
    esp_task_wdt_reset();
    is_ap_mode = true;
    apOffSSID = 0;
    apOffPW   = 0;

	#if ENABLE_BOOT_SCAN
		// Scan BOTH bands BEFORE any mode switch — mode changes abort in-flight scans
		runDualBandScan();
		esp_task_wdt_reset();
	#endif

    WiFi.disconnect();
    WiFi.mode(WIFI_AP);
    WiFi.softAPConfig(
        IPAddress(AP_IP),
        IPAddress(AP_GATEWAY),
        IPAddress(AP_SUBNET),
        IPAddress(AP_DHCP_START),
        IPAddress(AP_DHCP_END)
    );
    WiFi.softAP(AP_SSID, AP_PASSWORD, AP_CHANNEL, AP_HIDDEN, AP_MAX_CLIENTS);
    server.begin();
    drawApModeScreen();
    NETLOG("SoftAP \"%s\" up: ip=192.168.4.1 ch=%d maxClients=%d hidden=%d auth=%s",
           AP_SSID, AP_CHANNEL, AP_MAX_CLIENTS, AP_HIDDEN, (strlen(AP_PASSWORD) == 0 ? "OPEN" : "WPA2"));
}

// ==========================================
// 7. Status LED
// ==========================================
void updateLedStatus() {
    static unsigned long t0 = 0;
    static bool phase = false;
    static uint8_t lastR = 0xFF, lastG = 0xFF, lastB = 0xFF;
    uint8_t r, g, b;

    if (is_ap_mode) {                                    // magenta = config portal
        r = LED_LEVEL; g = 0; b = LED_LEVEL;
    }
    else if (WiFi.status() != WL_CONNECTED) {            // blue blink = reconnecting
        if (millis() - t0 >= 400) { t0 = millis(); phase = !phase; }
        r = 0; g = 0; b = phase ? LED_LEVEL : 0;
    }
    else {
        bool aFailing = (stats_A.totalSamples() > 0 && !success_A);
        bool bEnabled = (host_B.length() > 0);
        bool bFailing = (bEnabled && stats_B.totalSamples() > 0 && !success_B);
        if (aFailing && (!bEnabled || bFailing)) {       // red = configured targets failing
            r = LED_LEVEL; g = 0; b = 0;
        } else {                                         // green = healthy (incl. pre-first-ping)
            r = 0; g = LED_LEVEL; b = 0;
        }
    }

    if (r != lastR || g != lastG || b != lastB) {        // RMT write only on change
        setLed(r, g, b);
        lastR = r; lastG = g; lastB = b;
    }
}

// ==========================================
// 8. Core Arduino Application Lifecycle
// ==========================================

// NVS Configuration Restore
void loadSettings() {
    preferences.begin("net-watch", true);   // read-only
    if (!preferences.isKey("ssid_pri")) {   // fresh NVS — skip reads entirely
        preferences.end();
        has_saved_config = false;
        NETLOG("NVS: no saved config (first boot) - defaults in effect");
        return;
    }

    ssid_pri         = preferences.getString("ssid_pri",     "");
    pass_pri         = preferences.getString("pass_pri",     "");
    ssid_sec         = preferences.getString("ssid_sec",     "");
    pass_sec         = preferences.getString("pass_sec",     "");
    host_A           = preferences.getString("host_A",       "google.com");
    host_B           = preferences.getString("host_B",       "github.com");
    use_dhcp         = preferences.getBool ("use_dhcp",      true);
    local_ip_str     = preferences.getString("local_ip",     "192.168.100.100");
    gateway_str      = preferences.getString("gateway",      "192.168.100.254");
    netmask_str      = preferences.getString("netmask",      "255.255.255.0");
    dns_pri_str      = preferences.getString("dns_pri",      "1.1.1.1");
    dns_alt_str      = preferences.getString("dns_alt",      "1.0.0.1");
    ping_interval    = preferences.getULong("ping_interval", 3000);
    display_rotation = preferences.getInt  ("scr_rotation",  3);
	has_saved_config = (ssid_pri.length() > 0);   // a portal save always includes ssid_pri
    preferences.end();
}

void wifiBandAuto() {
	#if defined(CONFIG_IDF_TARGET_ESP32C5)
		esp_err_t err = esp_wifi_set_country_code(WIFI_COUNTRY_CODE, WIFI_COUNTRY_11D);
		WiFi.setBandMode(WIFI_BAND_MODE_AUTO);          // set AFTER country, in case it resets band cfg
		static bool logged = false;
		if (!logged) {
			char cc[4] = {0};
			esp_wifi_get_country_code(cc);
			NETLOG("Radio: band=AUTO activeCountry=%s setCountry=%s err=%d",
				   cc, WIFI_COUNTRY_CODE, (int)err);
			logged = true;
		}
	#endif
}

bool tryConnectWiFi(String target_ssid, String target_pass, bool dhcp_mode, String ip_s, String gw_s, String mask_s, String dns_p, String dns_a, int timeout_seconds) {
    esp_task_wdt_reset();
	
	setLed(LED_LEVEL, LED_LEVEL, 0);   // yellow = connecting

    if (target_ssid == "") { WLOG("Abort: empty SSID"); return false; }
    WLOG("Connect attempt: \"%s\" mode=%s timeout=%ds",
         target_ssid.c_str(), dhcp_mode ? "DHCP" : "STATIC", timeout_seconds);

    WiFi.disconnect();
    WiFi.mode(WIFI_STA);
    wifiBandAuto();                              // 2.4 + 5 GHz
    if (!dhcp_mode) {
        IPAddress local_ip, gateway, subnet, priDNS, altDNS;
        if (local_ip.fromString(ip_s.c_str()) && gateway.fromString(gw_s.c_str()) && subnet.fromString(mask_s.c_str())) {
            priDNS.fromString(dns_p.c_str()); altDNS.fromString(dns_a.c_str());
            WiFi.config(local_ip, gateway, subnet, priDNS, altDNS);
            WLOG("Static config applied: ip=%s gw=%s nm=%s", ip_s.c_str(), gw_s.c_str(), mask_s.c_str());
        } else {
            WLOG("WARN: static parse failed, falling back to DHCP assignment");
        }
    } else {
        IPAddress priDNS, altDNS;
        if (priDNS.fromString(dns_p.c_str()) && altDNS.fromString(dns_a.c_str())) WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE, priDNS, altDNS);
    }
    WiFi.begin(target_ssid.c_str(), target_pass.c_str());
    WLOG("Wi-Fi.begin() sent, waiting for link...");

    tft.fillScreen(ST77XX_BLACK); tft.setTextSize(FONT_SIZE_SMALL); tft.setTextColor(ST77XX_WHITE);
    tft.setCursor(10, 40); tft.print("Connecting Wi-Fi...");
    tft.setTextColor(ST77XX_CYAN); tft.setCursor(10, 75); tft.print(target_ssid);
    tft.fillRect(60, 160, 200, 45, ST77XX_RED); tft.drawRect(60, 160, 200, 45, ST77XX_WHITE);
    tft.setTextColor(ST77XX_WHITE); tft.setCursor(120, 173); tft.print("CANCEL");

    int check_count = timeout_seconds * 10;
    for (int i = 0; i < check_count; i++) {
        if (WiFi.status() == WL_CONNECTED) {
            String check_ip = WiFi.localIP().toString();
                if (!dhcp_mode || (check_ip != "0.0.0.0" && !check_ip.startsWith("169.254"))) {
					local_ip_str = check_ip;
					gateway_str  = WiFi.gatewayIP().toString();
					netmask_str  = WiFi.subnetMask().toString();
                // Manual DNS wins over DHCP-assigned DNS
                IPAddress pd, ad;
                if (pd.fromString(dns_p.c_str()) && ad.fromString(dns_a.c_str())) {
                    applyManualDNS(pd, ad);
                    dns_pri_str = dns_p;
                    dns_alt_str = dns_a;
                    WLOG("CONNECTED: ip=%s gw=%s dns=%s,%s (manual)",
                         check_ip.c_str(), gateway_str.c_str(), dns_p.c_str(), dns_a.c_str());
                } else {
                    dns_pri_str = WiFi.dnsIP(0).toString();
                    dns_alt_str = WiFi.dnsIP(1).toString();
                    WLOG("CONNECTED: ip=%s gw=%s dns=%s,%s (dhcp)",
                         check_ip.c_str(), gateway_str.c_str(), dns_pri_str.c_str(), dns_alt_str.c_str());
                }
                return true;
            }
        }
        if (touch.touched()) {
            TS_Point p = touch.getPoint();
            int px = map(p.x, TOUCH_X_MIN, TOUCH_X_MAX, 0, SCREEN_WIDTH);
            int py = map(p.y, TOUCH_Y_MIN, TOUCH_Y_MAX, 0, SCREEN_HEIGHT);
            if (px >= 60 && px <= 260 && py >= 160 && py <= 205) {
                WLOG("CANCEL pressed by user - aborting connect");
                WiFi.disconnect();
                return false;
            }
        }
        delay(100);
        esp_task_wdt_reset();
    }

    WLOG("TIMEOUT after %ds (status=%d ip=%s)", timeout_seconds, (int)WiFi.status(), WiFi.localIP().toString().c_str());
    if (WiFi.status() == WL_CONNECTED) {
        local_ip_str = WiFi.localIP().toString();
        gateway_str  = WiFi.gatewayIP().toString();
        netmask_str  = WiFi.subnetMask().toString();
        WLOG("Link up but IP invalid (link-local) - treating as failure");
    }
    WiFi.disconnect();
    return false;
}

void setup() {
    Serial.begin(115200);
    NETLOG("==========================================");
    NETLOG("NetWatchdog %s boot", FIRMWARE_VERSION);
    NETLOG("Reset reason code: %d", (int)esp_reset_reason());

    unsigned long startTime = millis();
    while (!Serial && (millis() - startTime < 2000)) { delay(10); }

    loadSettings();
    NETLOG("NVS loaded: pri=\"%s\" sec=\"%s\" dhcp=%d rot=%d ping=%lums",
           ssid_pri.c_str(), ssid_sec.c_str(), (int)use_dhcp, display_rotation, ping_interval);
    NETLOG("NVS loaded: hostA=\"%s\" hostB=\"%s\"", host_A.c_str(), host_B.c_str());
	
	preferences.begin("net-watch", true);
    if (preferences.isKey("ping_log")) {
        pingLogEnabled = preferences.getBool("ping_log");
        NETLOG("Ping log restored: %s", pingLogEnabled ? "ON" : "OFF");
    }
    preferences.end();
	
    if (!use_dhcp) {
        WLOG("Static: ip=%s gw=%s nm=%s dns=%s/%s",
             local_ip_str.c_str(), gateway_str.c_str(), netmask_str.c_str(),
             dns_pri_str.c_str(), dns_alt_str.c_str());
    }

    ledcAttach(LCD_BLK, 5000, 8);
    ledcWrite(LCD_BLK, BACKLIGHT_ECO_PWM);
    backlight_is_boosted = false;
	NETLOG("Backlight -> 60%% (30s idle eco)");

    SPI.begin(LCD_CLK, LCD_DOUT, LCD_DIN, LCD_CS);
    tft.init(240, 320);
    tft.invertDisplay(false);
    tft.setRotation(display_rotation);
    tft.fillScreen(ST77XX_BLACK);

    touch.begin();
    touch.setRotation(getTouchRotation(display_rotation));
    NETLOG("TFT + touch ready (rot=%d touch_rot=%d)", display_rotation, getTouchRotation(display_rotation));

    if (esp_reset_reason() == ESP_RST_TASK_WDT) {
        NETLOG("!! Previous reboot caused by TASK WATCHDOG TIMEOUT");
    }
    esp_task_wdt_config_t twdt_config = {
        .timeout_ms     = 45000,
        .idle_core_mask = (1 << 0),
        .trigger_panic  = true
    };
    // Arduino core pre-initializes the TWDT -> init() returns ESP_ERR_INVALID_STATE.
    // Reconfigure instead, init only as fallback.
    if (esp_task_wdt_reconfigure(&twdt_config) != ESP_OK) {
        esp_task_wdt_init(&twdt_config);
    }
    esp_task_wdt_add(NULL);
    NETLOG("HW watchdog armed: 45s, panic=on");

    pingResultQueue = xQueueCreate(4, sizeof(PingResult));
    if (pingResultQueue) {
        xTaskCreatePinnedToCore(pingWorkerTask, "pingTask", 10240, NULL, 1, &pingTaskHandle, 0);
        NETLOG("Async ping task started (core 0, interval %lums)", ping_interval);
    } else {
        NETLOG("ERROR: failed to create ping result queue");
    }

    if (ssid_pri == "") {
        is_ap_mode = true;
        WLOG("No saved SSID -> AP mode");
    } else {
        runDualBandScan();                   // radio still idle: safe for band switching
        WLOG("Trying PRIMARY \"%s\"...", ssid_pri.c_str());
        bool connected = tryConnectWiFi(ssid_pri, pass_pri, use_dhcp,
            local_ip_str, gateway_str, netmask_str, dns_pri_str, dns_alt_str, 30);
		if (!connected && ssid_sec != "") {
            WLOG("Primary failed -> trying SECONDARY \"%s\"...", ssid_sec.c_str());
            connected = tryConnectWiFi(ssid_sec, pass_sec, use_dhcp,
                local_ip_str, gateway_str, netmask_str, dns_pri_str, dns_alt_str, 30);
        }
        if (!connected) {
            is_ap_mode = true;
            WLOG("All Wi-Fi attempts failed -> AP mode");
        }
    }

    server.on("/", handleRoot);
    server.on("/save", HTTP_POST, handleSave);
    server.on("/scan", handleScan);
	server.on("/factory", HTTP_POST, handleFactoryReset);
	server.on("/pinglog", handlePingLog);

    if (is_ap_mode) {
        enterApModeRuntime();
    } else {
        refreshScreenLayout();
        server.begin();
        WLOG("Config portal live at http://%s/ (client mode)", local_ip_str.c_str());
    }

    last_interaction_time = millis();
    NETLOG("setup() complete, entering loop");
}

void loop() {
    esp_task_wdt_reset();                          // Reset Hardware Watchdog Timer

    unsigned long current_time = millis();
    updateLedStatus();                             // LED state machine (writes only on change)

    // Health heartbeat every 30s (heap + backlight state visible in log)
    static unsigned long last_health_time = 0;
    if (current_time - last_health_time >= 30000) {
        last_health_time = current_time;
            NETLOG("health: mode=%s heap=%u minHeap=%u rssi=%d bl=%s pl=%s",
               is_ap_mode ? "AP" : "CLIENT", ESP.getFreeHeap(), ESP.getMinFreeHeap(),
               current_rssi, backlight_is_boosted ? "100%" : "eco",
               pingLogEnabled ? "ON" : "OFF");
    }

    // Backlight eco-dim — covers BOTH AP and client modes (30s after last interaction)
    if (backlight_is_boosted && (current_time - last_interaction_time >= BACKLIGHT_DIM_TIMEOUT)) {
        ledcWrite(LCD_BLK, BACKLIGHT_ECO_PWM);
        backlight_is_boosted = false;
        NETLOG("Backlight -> eco (%d/255 duty, 30s idle)", BACKLIGHT_ECO_PWM);
    }

    if (is_ap_mode) {
        // ==========================================
        // AP MODE
        // ==========================================
        static int prev_client_count = -1;

        int client_count = WiFi.softAPgetStationNum();
        if (client_count != prev_client_count) {
            prev_client_count = client_count;
            NETLOG("AP clients: %d", client_count);
            tft.fillRect(10, 195, 150, 16, ST77XX_BLACK);
            tft.setCursor(10, 195);
            tft.setTextColor(ST77XX_CYAN);
            tft.setTextSize(FONT_SIZE_SMALL);
            tft.printf("Clients: %d", client_count);
        }

        server.handleClient();

        // Scroll SSID/PW (only visually changes if text overflows)
        if (current_time - last_scroll_time >= SCROLL_SPEED_MS) {
            last_scroll_time = current_time;
            drawApMarqueeValue(AP_SSID,     75,  apOffSSID, ST77XX_CYAN);
            drawApMarqueeValue(AP_PASSWORD, 100, apOffPW,   ST77XX_CYAN);
        }

        // Touch handling
        if (touch.touched()) {
            TS_Point p = touch.getPoint();
            int pixelX = map(p.x, TOUCH_X_MIN, TOUCH_X_MAX, 0, SCREEN_WIDTH);
            int pixelY = map(p.y, TOUCH_Y_MIN, TOUCH_Y_MAX, 0, SCREEN_HEIGHT);

            // Any touch boosts backlight + resets idle timer, even outside the button
            if (!backlight_is_boosted) TLOG("Backlight -> 100%% (AP touch)");
            ledcWrite(LCD_BLK, BACKLIGHT_FULL_PWM);
            backlight_is_boosted = true;

            if (current_time - last_touch_time > 500) {
                last_touch_time = current_time;
                last_interaction_time = current_time;

                if (pixelX >= 170 && pixelX <= 310 && pixelY >= 180 && pixelY <= 225) {
                    TLOG("AP 'Client Mode' button pressed");
                    if (ssid_pri == "") {
                        tft.fillScreen(ST77XX_BLACK);
                        tft.setTextSize(2);
                        tft.setTextColor(ST77XX_RED);
                        tft.setCursor(10, 100);
                        tft.print("No SSID saved!");
                        tft.setCursor(10, 130);
                        tft.print("Configure via web");
                        delay(3000);
                        drawApModeScreen();        // Redraw AP screen
                        return;
                    }

                    // Button pressed - exit AP mode
                    ledcWrite(LCD_BLK, BACKLIGHT_FULL_PWM);
                    backlight_is_boosted = true;
                    TLOG("Backlight -> 100%% (touch)");

                    tft.fillScreen(ST77XX_BLACK);
                    tft.setTextSize(2);
                    tft.setTextColor(ST77XX_WHITE);
                    tft.setCursor(10, 100);
                    tft.print("Exiting AP Mode...");
                    delay(500);
                    server.close();

                    bool connected = tryConnectWiFi(ssid_pri, pass_pri, use_dhcp,
                        local_ip_str, gateway_str, netmask_str, dns_pri_str, dns_alt_str, 30);
                    if (!connected && ssid_sec != "") {
                        connected = tryConnectWiFi(ssid_sec, pass_sec, use_dhcp,
                            local_ip_str, gateway_str, netmask_str, dns_pri_str, dns_alt_str, 30);
                    }

                    if (!connected) {
                        enterApModeRuntime();
                        return;
                    }

                    is_ap_mode = false;
                    last_wifi_check_time = millis();
                    server.begin();                // re-open config portal in client mode
                    WLOG("Config portal live at http://%s/", WiFi.localIP().toString().c_str());
                    refreshScreenLayout();
                    return;
                }
            }
        }

        esp_task_wdt_reset();                      // Feed watchdog before delay
        delay(2);
        esp_task_wdt_reset();                      // Feed watchdog after delay
    } else {
        // ==========================================
        // CLIENT / MONITORING MODE
        // ==========================================
        server.handleClient();                     // config portal reachable over LAN

        // Touch to cycle display modes
        if (touch.touched()) {
            if (current_time - last_touch_time > 400) {
                last_touch_time = current_time;
                last_interaction_time = current_time;
                ledcWrite(LCD_BLK, BACKLIGHT_FULL_PWM);
                backlight_is_boosted = true;
                TLOG("Backlight -> 100%% (touch)");
                display_mode = (display_mode + 1) % 4;
                TLOG("Mode switch -> %d (0=Monitor 1=Stats10M 2=Stats1H 3=NetInfo)", display_mode);
                refreshScreenLayout();
            }
        }

        // Auto-return to monitoring after 15s
        if (display_mode != 0 && (current_time - last_interaction_time >= SCREEN_RESET_TIMEOUT)) {
            display_mode = 0;
            refreshScreenLayout();
        }

        // WiFi check every 10s (+ reconnect, + manual-DNS re-assert)
        if (current_time - last_wifi_check_time >= WIFI_CHECK_INTERVAL) {
            last_wifi_check_time = current_time;
            if (WiFi.status() != WL_CONNECTED) {
                WLOG("Wi-Fi lost (status=%d) - reconnect cycle", (int)WiFi.status());
                bool reconnected = tryConnectWiFi(ssid_pri, pass_pri, use_dhcp,
                    local_ip_str, gateway_str, netmask_str, dns_pri_str, dns_alt_str, 15);
                if (!reconnected && ssid_sec != "") {
                    reconnected = tryConnectWiFi(ssid_sec, pass_sec, use_dhcp,
                        local_ip_str, gateway_str, netmask_str, dns_pri_str, dns_alt_str, 15);
                }
                if (!reconnected) {
                    WLOG("Reconnect failed on both SSIDs -> AP mode");
                    enterApModeRuntime();
                    return;
                }
                WLOG("Reconnected OK");
                refreshScreenLayout();
            } else if (use_dhcp) {
                IPAddress pd, ad;
                if (pd.fromString(dns_pri_str) && ad.fromString(dns_alt_str) &&
                    (WiFi.dnsIP(0) != pd || WiFi.dnsIP(1) != ad)) {
                    applyManualDNS(pd, ad);
                }
            }
        }

        // Drain async ping results — settle-gated: drop samples until
        // (valid IP) + PING_START_DELAY_MS, so slow DHCP never pollutes stats
        PingResult r;
        bool netReady = false;
        if (WiFi.status() == WL_CONNECTED) {
            String ipNow = WiFi.localIP().toString();
            netReady = (ipNow != "0.0.0.0" && !ipNow.startsWith("169.254"));
            if (netReady && network_ready_at == 0) {
                network_ready_at = current_time;
                PLOG("Network ready (ip=%s) - stats open in %dms", ipNow.c_str(), PING_START_DELAY_MS);
            }
        } else {
            network_ready_at = 0;      // network lost: settle timer restarts on reconnect
        }

        while (xQueueReceive(pingResultQueue, &r, 0) == pdTRUE) {
            current_rssi = WiFi.RSSI();
            if (!netReady || (current_time - network_ready_at < PING_START_DELAY_MS)) {
                PLOG("settle: dropping early sample (T%d %s)", r.target, r.success ? "OK" : "FAIL");
                continue;                          // DHCP/DNS still settling — discard
            }
            if (r.target == 1) {
                success_A = r.success; latency_A = r.latency;
                stats_A.addSample(r.success, r.latency);
            } else {
                success_B = r.success; latency_B = r.latency;
                stats_B.addSample(r.success, r.latency);
            }
            if (display_mode == 1)      drawStatisticsScreen(STATS_SHORT_MS);
            else if (display_mode == 2) drawStatisticsScreen(STATS_LONG_MS);
        }

        // Scroll/refresh monitoring display
        if (display_mode == 0 && (current_time - last_scroll_time >= SCROLL_SPEED_MS)) {
            last_scroll_time = current_time;

            drawSlidingText(canvasSSID, WiFi.SSID(), 10, LINE_1_Y, 240, scrollOffsetSSID, ST77XX_CYAN, FONT_SIZE_SMALL);
            drawWifiSignalBar(WIFI_BAR_X, WIFI_BAR_Y, current_rssi);

            drawFixedPingStatus(PING_STATUS_X, LINE_2_Y, success_A, latency_A, 1);
            drawSlidingText(canvasHost, host_A, DOMAIN_SCROLL_X, LINE_2_TEXT_Y, DOMAIN_WIDTH, scrollOffsetHostA, ST77XX_WHITE, FONT_SIZE_LARGE);

            drawFixedPingStatus(PING_STATUS_X, LINE_3_Y, success_B, latency_B, 2);
            drawSlidingText(canvasHost, host_B, DOMAIN_SCROLL_X, LINE_3_TEXT_Y, DOMAIN_WIDTH, scrollOffsetHostB, ST77XX_WHITE, FONT_SIZE_LARGE);
        }
    }

    esp_task_wdt_reset();                          // Final watchdog reset
}
