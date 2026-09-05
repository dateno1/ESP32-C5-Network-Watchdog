# ESP32-C5 Network Watchdog

Asynchronous Dual-Band Network Monitoring Tool Driven by **ESP32-C5**.
This Device Features to Provide RealTime Latency Diagnostics, Historical Network Telemetry Over a Rolling 10-Minute Window, and An Offline Captive Configuration Portal.

---

## Key Features

* **Dual-Band Connectivity:** Leverages the ESP32-C5's modern architecture to support dual-band operations for maximum infrastructure compatibility.
* **Smart Failover Backup:** Configured with primary and alternative Wi-Fi profiles. Jumps to fallback credentials if connection integrity fails.
* **Rolling Telemetry Logging:** Continuously computes historical data packet arrays over a **10-minute rolling window** to render exact average latency and success percentage matrices.
* **Multi-Screen Navigation:** cycle through screen layouts seamlessly via physical touchscreen interactions:
  1. *Real-Time Monitor Dashboard:* Active marquees showing connected network name and targeted domain strings.
  2. *Statistical Logs View:* Average latency performance indicators alongside percentage trackers.
  3. *Device Network Specifications:* Clean list containing live static or dynamic subnet parameters.
* **Eco-Efficient Lifespan Guard:** Protects display lifetime by holding a steady **80% hardware dimming factor**, immediately boosting to **100% full capacity** for 30 seconds upon user touch gestures.
* **Interactive AP Config Portal:** Boots an on-board Captive Web Portal combined with an on-screen hardware layout bypass mechanism to modify connectivity variables securely over an independent local channel.

---

## Hardware Specifications & SPI Pin Mapping

You Need to Buy ESP32-C5 with 2.8-Inch ST7789 Display + XPT2046 Resistive Touch Panel

This Firmware is Optimized to Run Over Standard Hardware SPI Bus Topology Configuration.

### ESP32-C5 Pin Assignments

| Component | Function | GPIO Pin | Notes |
| :--- | :--- | :--- | :--- |
| **ST7789 LCD** | SPI MOSI (DIN) | `GPIO 7` | Shared Bus Pin |
| | SPI SCK (CLK) | `GPIO 6` | Shared Bus Pin |
| | SPI MISO (DOUT) | `GPIO 2` | Shared Bus Pin |
| | Chip Select (CS) | `GPIO 23` | Dedicated CS |
| | Data / Command (DC) | `GPIO 24` | Control Bus Pin |
| | Hardware Reset (RST) | `GPIO 26` | Hardware Power Reset |
| | Backlight Dimming (BLK) | `GPIO 10` | 5kHz Hardware PWM Driver |
| **XPT2046 Touch** | SPI MOSI | `GPIO 7` | Shared Bus Pin |
| | SPI SCK | `GPIO 6` | Shared Bus Pin |
| | SPI MISO | `GPIO 2` | Shared Bus Pin |
| | Chip Select (CS) | `GPIO 1` | Dedicated CS |

---

## Operational State Engine Lifecycle

[ Power Boot ]
│Does Saved WIFI Credentials Exist?
├── No  ──> [ Initial Boot AP Mode ] ──> Web Dashboard / LCD Setup
└── Yes ──> [ Connect Primary Wi-Fi ] (30s)
│
Did Connection Succeed?
├── Yes ──> [ Monitoring Active Mode ]
└── No  ──> [ Connect Alternative WIFI Fallback ] (30s)
│
Did Connection Succeed?
├── Yes ──> [ Monitoring Active Mode ]
└── No  ──> [ Open Captive Access Portal ]

---

## Required Software Libraries

Ensure the Following Exact Library Resources are Compiled Inside Your **Arduino IDE (ESP32 Core 3.0+)** Workspace layout:

1. `Adafruit_GFX` Library (Unified Graphics Core Architecture)
2. `Adafruit_ST7789` Library (Display Processor Framework)
3. `XPT2046_Touchscreen` Library (Touch Intersect Controller)
4. `ESP32Ping` Library (Raw ICMP Packet Infrastructure Protocol)

---

## Firmware Constant Profiles Customization

You Can Quickly Tune Operational Timing Scripts Directly Inside the Top Definitions Block of the Code Structure:

```cpp
#define SCROLL_SPEED_MS        40 // Marquee Pixel Shift Translation Step Rate
#define WIFI_CHECK_INTERVAL 10000 // Background Disconnect Verification Thread Interval (10 Seconds)
#define SCREEN_RESET_TIMEOUT 15000// View Timeout Boundary Prior to Snapping Back to Main Monitor Dashboard
#define BACKLIGHT_DIM_TIMEOUT 30000// Duty Factor Drop Constraint Following Screen Input Inactivity
```

---

## Installation

1. You Need to Install Python for 'PIO'. You Can Download Lastest Python Installer from https://www.python.org.

2. Install Python -> Run 'pip install -U platformio esptool' for Download Tool.

3. Open Terminal and Move to Project Path

4. Connect Your **ESP32-C5 Board** via USB-C Interface Data Cable.

5. Run 'pio run -t upload' (If You are Using Windows Run '"%AppData%\Python\Python<span style="color:#ff6f00; font-weight:bold;">314</span>\Scripts\pio.exe" run -t upload')

6. If You Want to Debug by USB Cable Run 'pio device monitor --baud 115200' (If You are Using Windows Run '"%AppData%\Python\Python<style type="text/css>{color:blue}314</style>\Scripts\pio.exe" monitor --baud 115200')

---

## The Configuration Web Portal

When the Device Fails to Establish Connection over Known WIFIs, It Generates an Autonomous Standalone Local Network SSID Named **`ESP32-C5-NetWatchdog`**. 

1. Connect to The Local Gateway over Your Smartphone or PC.
2. Navigate to Landing Address Route **`192.168.4.1`** Inside Your Web Browser.
3. Modify WIFI Credential, Static IP Assignments, Custom Subnets, DNS servers, Screen Rotation and Monitoring Targets.
4. If You Want to Exit Setting Parameters without Re-Submitting Structural Entries, Click the **CLIENT** Button Located Directly Inside the Bottom Region of the Local Physical Screen Interface Module to Run Connection Discover Scripts Immediately.
