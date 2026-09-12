# ESP32-C5 Network Watchdog

Asynchronous Dual-Band Network Monitoring Tool Driven by **ESP32-C5**.
This Device Features to Provide RealTime Latency Diagnostics, Historical Network Telemetry Over a Rolling 10/60-Minute Window, and An Offline Captive Configuration Portal.

---

## Key Features

* **Dual-Band Connectivity:** Leverages the ESP32-C5's modern architecture to support dual-band operations for maximum infrastructure compatibility.
* **Smart Failover Backup:** Configured with primary and alternative Wi-Fi profiles. Jumps to fallback credentials if connection integrity fails.
* **Rolling Telemetry Logging:** Continuously computes historical data packet arrays over a **10-minute rolling window** to render exact average latency and success percentage matrices.
* **Multi-Screen Navigation:** cycle through screen layouts seamlessly via physical touchscreen interactions:
  1. *Real-Time Monitor Dashboard:* Active marquees showing connected network name and targeted domain strings.
  2. *Statistical Logs View:* Average latency performance indicators alongside percentage trackers.
  3. *Device Network Specifications:* Clean list containing live static or dynamic subnet parameters.
* **Eco-Efficient Lifespan Guard:** Protects display lifetime by holding a steady **60% hardware dimming factor**, immediately boosting to **100% full capacity** for 30 seconds upon user touch gestures.
* **Interactive AP Config Portal:** Boots an on-board Captive Web Portal combined with an on-screen hardware layout bypass mechanism to modify connectivity variables securely over an independent local channel.

---

## Hardware Specifications & SPI Pin Mapping

You Need to Buy RockBase NM-CYD-C5 with 2.8-Inch ST7789 Display + XPT2046 Resistive Touch Panel

This Firmware is Optimized to Run Over Standard Hardware SPI Bus Topology Configuration.

### ESP32-C5 Pin Assignments

| Component | Function | GPIO Pin | Notes |
| :--- | :--- | :--- | :--- |
| **Shared** |
| | SPI MISO | `GPIO 2` | Shared Bus Pin |
| | SPI SCK | `GPIO 6` | Shared Bus Pin |
| | SPI MOSI | `GPIO 7` | Shared Bus Pin |
| :--- | :--- | :--- | :--- |
| **ST7789 LCD** |
| | Chip Select (CS) | `GPIO 23` | Dedicated CS |
| | Data / Command (DC) | `GPIO 24` | Control Bus Pin |
| | Backlight Dimming (BLK) | `GPIO 25` | Hardware PWM Driver |
| **XPT2046 Touch**
| | Chip Select (CS) | `GPIO 1` | Dedicated CS |
| **WS2812 LED**
| | LED Control | `GPIO 27` | RGB Addressable |
| :--- | :--- | :--- | :--- |
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
2. `Adafruit ST7735 and ST7789` Library (Display Processor Framework)
3. `XPT2046_Touchscreen` Library (Touch Intersect Controller)
~~4. `ESP32Ping` Library (Raw ICMP Packet Infrastructure Protocol)~~ (Patched Version Included) (Debug Level Changed)

---

## Firmware Constant Profiles Customization

You Can Quickly Tune Operational Timing Scripts Directly Inside the Top Definitions Block of the Code Structure:

---

## Installation

1. You Need to Install Python for 'PIO'. You Can Download Python 3.10~13 Installer from https://www.python.org. (**pioarduino Not Work with v3.14 Yet**)

2. Install Python -> Run 'pip install -U platformio esptool' for Download Tool.

3. Open Terminal and Move to Project Path

4. Connect Your **ESP32-C5 Board** via USB-C Interface Data Cable.

5. Run 'pio run -t upload' (If You are Using Windows Run '"%AppData%\Python\Python**313**\Scripts\pio.exe" run -t upload')

6. If You Want to Debug by USB Cable Run 'pio device monitor --baud 115200' (If You are Using Windows Run '"%AppData%\Python\Python**313**\Scripts\pio.exe" device monitor --baud 115200')

---

## The Configuration Web Portal

When the Device Fails to Establish Connection over Known WIFIs, It Generates an Autonomous Standalone Local Network SSID Named **`ESP32-C5-NetWatchdog`**. 

1. Connect to The Local Gateway over Your Smartphone or PC.
2. Navigate to Landing Address Route **`192.168.4.1`** Inside Your Web Browser.
3. Modify WIFI Credential, Static IP Assignments, Custom Subnets, DNS servers, Screen Rotation and Monitoring Targets.
4. If You Want to Exit Setting Parameters without Re-Submitting Structural Entries, Click the **CLIENT** Button Located Directly Inside the Bottom Region of the Local Physical Screen Interface Module to Run Connection Discover Scripts Immediately.

`Warning : It Scan AP List 1 time only at Boot. If You Want to Refresh List Reboot Device.`
  
===============================================  

한국어  
  
# ESP32-C5 Network Watchdog

**ESP32-C5** 기반의 비동기 듀얼 밴드 네트워크 모니터링 툴입니다. 
이 장치는 실시간 지연 시간(Latency) 진단, 롤링 10/60분 윈도우 기반의 네트워크 텔레메트리 이력 기록, 그리고 오프라인 캡티브 구성 포털(Captive Configuration Portal) 기능을 제공합니다.

---

## 주요 기능

* **듀얼 밴드 연결:** ESP32-C5의 최신 아키텍처를 활용하여 듀얼 밴드 동작을 지원하므로, 인프라 호환성을 극대화합니다.
* **스마트 페일오버 백업:** 기본(Primary) 및 대체(Alternative) Wi-Fi 프로필을 설정할 수 있습니다. 연결 상태가 불량해지면 자동으로 대체 복구 자격 증명으로 전환됩니다.
* **롤링 텔레메트리 로깅:** **10분 롤링 윈도우** 동안의 데이터 패킷 배열을 지속적으로 계산하여, 정확한 평균 지연 시간 및 접속 성공률 매트릭스를 렌더링합니다.
* **멀티 스크린 내비게이션:** 물리적 터치스크린 조작을 통해 화면 레이아웃을 부드럽게 전환할 수 있습니다:
  1. *실시간 모니터 대시보드:* 연결된 네트워크 이름과 대상 도메인 문자열을 보여주는 활성 마퀴(Marquee) 화면.
  2. *통계 로그 뷰:* 평균 지연 시간 성능 지표와 성공률 트래커 화면.
  3. *장치 네트워크 사양:* 실시간 고정 또는 동적 서브넷 파라미터가 포함된 깔끔한 리스트 화면.
* **Eco-Efficient Lifespan Guard:** 디스플레이 하드웨어 밝기를 **60% 수준으로 일정하게 유지**하여 화면 수명을 보호하며, 사용자가 터치 제스처를 취하면 즉시 30초 동안 **100% 전체 밝기**로 높여줍니다.
* **대화형 AP 구성 포털:** 온보드 캡티브 웹 포털(Captive Web Portal) 기능과 화면 내 하드웨어 레이아웃 우회 메커니즘을 부팅하여, 독립된 로컬 채널을 통해 연결 변수를 안전하게 수정할 수 있습니다.

---

## 하드웨어 사양 및 SPI 핀 맵 (Pin Mapping)

2.8인치 ST7789 디스플레이와 XPT2046 저항막 방식 터치 패널이 탑재된 RockBase NM-CYD-C5를 구매해야 합니다.

이 펌웨어는 표준 하드웨어 SPI 버스 토폴로지 구성에서 실행되도록 최적화되어 있습니다.

### ESP32-C5 핀 할당 (Pin Assignments)

| 컴포넌트 | 기능 | GPIO 핀 | 비고 |
| :--- | :--- | :--- | :--- |
| **공유 핀** |
| | SPI MISO | `GPIO 2` | 공유 버스 핀 |
| | SPI SCK | `GPIO 6` | 공유 버스 핀 |
| | SPI MOSI | `GPIO 7` | 공유 버스 핀 |
| :--- | :--- | :--- | :--- |
| **ST7789 LCD** |
| | Chip Select (CS) | `GPIO 23` | 전용 CS |
| | Data / Command (DC) | `GPIO 24` | 제어 버스 핀 |
| | Backlight Dimming (BLK) | `GPIO 25` | 하드웨어 PWM 드라이버 |
| **XPT2046 터치** |
| | Chip Select (CS) | `GPIO 1` | 전용 CS |
| **WS2812 LED**
| | LED 제어 | `GPIO 27` | RGB 변경 가능 |
| :--- | :--- | :--- | :--- |

---

## 동작 상태 엔진 라이프사이클 (State Engine Lifecycle)

[ 전원 부팅 (Power Boot) ]
│저장된 WIFI 자격 증명이 존재하는가?
├── 아니오 ──> [ 초기 부팅 AP 모드 ] ──> 웹 대시보드 / LCD 설정
└── 예   ──> [ 기본 Wi-Fi 연결 시도 ] (30초)
│
연결에 성공했는가?
├── 예   ──> [ 모니터링 활성 모드 ]
└── 아니오 ──> [ 대체 WIFI 백업 연결 시도 ] (30초)
│
연결에 성공했는가?
├── 예   ──> [ 모니터링 활성 모드 ]
└── 아니오 ──> [ 설정 페이지 개방 ]

---

## 필수 소프트웨어 라이브러리

**Arduino IDE (ESP32 Core 3.0 이상)** 작업 공간 레이아웃 내에 다음과 같은 정확한 라이브러리 리소스가 컴파일되어 있는지 확인하십시오:

1. `Adafruit_GFX` 라이브러리 (통합 그래픽 코어 아키텍처)
2. `Adafruit ST7735 and ST7789` 라이브러리 (디스플레이 프로세서 프레임워크)
3. `XPT2046_Touchscreen` 라이브러리 (터치 교차 컨트롤러)
~~4. `ESP32Ping` 라이브러리 (원시 ICMP 패킷 인프라 프로토콜)~~ (패치된 버전 동봉) (디버그 레벨 변경)

---

## 펌웨어 상수 프로필 사용자 정의 (Customization)

코드 구조의 최상단 정의 블록(Definitions Block) 내부에서 동작 타이밍 스크립트를 직접 신속하게 조정할 수 있습니다.

---

## 설치 방법 (Installation)

1. 'PIO'를 사용하려면 Python을 설치해야 합니다. https://python.org 에서 Python 3.10~13 설치 프로그램을 다운로드할 수 있습니다. (**pioarduino는 아직 v3.14 버전을 지원하지 않습니다**)

2. Python을 설치한 후, 툴을 다운로드하기 위해 `pip install -U platformio esptool` 명령어를 실행합니다.

3. 터미널(콘솔)을 열고 프로젝트 경로로 이동합니다.

4. **ESP32-C5 보드**를 USB-C 데이터 케이블로 연결합니다.

5. `pio run -t upload` 명령어를 실행합니다. (Windows 사용자의 경우 `"%AppData%\Python\Python313\Scripts\pio.exe" run -t upload` 입력)

6. USB 케이블을 통해 디버깅을 원하시면 `pio device monitor --baud 115200` 명령어를 실행합니다. (Windows 사용자의 경우 `"%AppData%\Python\Python313\Scripts\pio.exe" device monitor --baud 115200` 입력)

---

## 설정 페이지 (Configuration Web Portal)

장치가 알려진 WIFI에 연결하지 못하면, **`ESP32-C5-NetWatchdog`**이라는 이름의 자체 독립형 로컬 네트워크 SSID를 생성합니다.

1. 스마트폰이나 PC를 통해 해당 로컬 게이트웨이에 연결합니다.
2. 웹 브라우저 주소창에 랜딩 주소 경로인 **`192.168.4.1`**을 입력하여 접속합니다.
3. WIFI 자격 증명, 고정 IP 할당, 사용자 정의 서브넷, DNS 서버, 화면 회전 및 모니터링 대상을 수정합니다.
4. 구조적 항목들을 다시 제출하지 않고 설정 파라미터에서 나가려면, 로컬 물리 화면 인터페이스 모듈의 맨 아래 영역에 위치한 **CLIENT** 버튼을 클릭하여 즉시 연결 감지 스크립트를 실행하십시오.

`경고: AP 리스트 스캔은 부팅 시 딱 1번만 수행됩니다. 리스트를 새로고침하려면 장치를 재부팅하십시오.`
