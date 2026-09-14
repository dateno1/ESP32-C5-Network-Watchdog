// ============================================================
//  config.h — NetWatchdog C5 user-editable settings
//
//  Everything a deployer touches lives here. Behaviour/code
//  changes belong in main.cpp.
//  These values are BOOT DEFAULTS: the web portal overrides
//  most of them and persists the result to NVS (flash).
// ============================================================
#pragma once   // include guard: read this file only once per build

// ---------------- Firmware identity ----------------
#define FIRMWARE_VERSION          "1.0"

// ---------------- Hardware pins (board wiring) ----------------
// NM-CYD-C5: 2.8" 320x240 ST7789 + XPT2046 on shared SPI
#define LCD_DIN                   7     // SPI MOSI (shared with touch)
#define LCD_CLK                   6     // SPI SCK  (shared with touch)
#define LCD_DOUT                  2     // SPI MISO (shared with touch)
#define LCD_CS                    23    // TFT chip select
#define LCD_DC                    24    // TFT data/command
#define LCD_BLK                   25    // Backlight (PWM)
#define TOUCH_CS                  1     // XPT2046 chip select

// Touch calibration — raw ADC ranges for Your Panel
#define TOUCH_X_MIN               185
#define TOUCH_X_MAX               3700
#define TOUCH_Y_MIN               250
#define TOUCH_Y_MAX               3800

// ---------------- Status LED ----------------
#define LED_PIN                   27    // WS2812 RGB
#define LED_LEVEL                 32    // 0-255 per channel (keep low)

// ---------------- Display / Typography ----------------
#define SCREEN_WIDTH              320
#define SCREEN_HEIGHT             240
#define DEFAULT_ROTATION          3     // 3 = landscape, 1 = inverted (portal overrides)
#define FONT_SIZE_SMALL           2     // SSID line
#define FONT_SIZE_LARGE           3     // Target lines

// ---------------- Config-portal Access Point ----------------
#define AP_SSID                   "ESP32-C5-NetWatchdog"  // max 31 chars
#define AP_PASSWORD               "NetWatchdog"           // "" = open; 8-63 = WPA2
#define AP_CHANNEL                10                      // 2.4GHz portal channel ONLY
#define AP_MAX_CLIENTS            4                       // 1-8
#define AP_HIDDEN                 0                       // 0 = broadcast SSID

// SoftAP addressing (AP_IP/AP_GATEWAY are comma lists for IPAddress())
#define AP_IP                     192, 168, 4, 1
#define AP_GATEWAY                192, 168, 4, 1
#define AP_SUBNET                 255, 255, 255, 0
#define AP_DHCP_START             192, 168, 4, 2
#define AP_DHCP_END               192, 168, 4, 254

// ---------------- Radio / region ----------------
// Country code decides WHICH 5GHz channels are scanned:
//   "US" -> 36-48, 52-64(DFS), 100-144(DFS), 149-165
//   "KR" -> 36-64, 100-144(DFS), 149-165
//   "01" -> world-safe minimum
#define WIFI_COUNTRY_CODE         "US"
#define WIFI_COUNTRY_11D          false   // adopt AP country IE after connect

// ---------------- Scan behaviour ----------------
#define SCAN_MS_PER_CHAN          300     // dwell per channel (<120ms misses 5GHz APs)
#define SCAN_MAX_APS              200     // stored AP cap (strongest first; memory guard)

// ---------------- Boot-default targets & network ----------------
// Used only when NVS has no saved config (fresh device / after factory reset).
// The web portal overrides these and persists to NVS.
#define DEFAULT_HOST_A            "google.com"
#define DEFAULT_HOST_B            "github.com"       // "" = Target 2 disabled
#define DEFAULT_LOCAL_IP          "192.168.100.100"  // static-mode values
#define DEFAULT_GATEWAY           "192.168.100.254"
#define DEFAULT_NETMASK           "255.255.255.0"
#define DEFAULT_DNS_PRI           "1.1.1.1"
#define DEFAULT_DNS_ALT           "1.0.0.1"
// Warning : Don't Use Too Short Interval (Example : If You Use 1000ms, Max Static Time will be 20mins)
// Also If You Use Short Internal for Check Internet. You Will Blocked
#define DEFAULT_PING_INTERVAL_MS  3000UL             // ms between ping cycles

// ---------------- Ping behaviour ----------------
#define PING_START_DELAY_MS       5000    // pings may still start during this window; the drain discards samples until it elapses.

// ---------------- Statistics ----------------
#define STATS_SHORT_MS            600000UL    // touch mode 1: 10 minutes
#define STATS_LONG_MS             3600000UL   // touch mode 2: 1 hour = ~744 bytes/target. 24 hours = ~17.3 KB/target. (Don't Use Too Long Period)

// ---------------- Backlight ----------------
#define BACKLIGHT_ECO_PWM         153     // idle duty (153 = 60%)
#define BACKLIGHT_FULL_PWM        255     // touch-boost duty
#define BACKLIGHT_DIM_TIMEOUT     30000   // ms idle before dimming

// ---------------- UI timing ----------------
#define SCROLL_SPEED_MS           40      // marquee step rate (ms)
#define WIFI_CHECK_INTERVAL       10000   // link watchdog poll (ms)
#define SCREEN_RESET_TIMEOUT      15000   // sub-page auto-return (ms)

// ---------------- Build-time feature switches ----------------
#define ENABLE_BOOT_SCAN          1       // dual-band scan before AP portal opens
#define ENABLE_NETLOG             1       // master: NET/WIFI/HTTP/TOUCH logs
#define ENABLE_PING_LOG           0       // boot default for [PING] lines (web toggles)
