# ESP32-C5 Network Watchdog  
  
Asynchronous Dual-Band Network Monitoring Tool Driven by **ESP32-C5**.  
Provides Real-Time Latency Diagnostics, Historical Network Telemetry Over Rolling **10/60-Minute** Windows, and a Standalone Configuration Portal.  
  
---  
  
## Key Features  
  
* **Dual-Band Connectivity:** 2.4 GHz + 5 GHz scanning and connection (ESP32-C5), with per-AP band/channel details in the portal dropdown.  
* **Smart Failover Backup:** Primary + Alternative Wi-Fi profiles; jumps to fallback credentials when connection integrity fails, with a 10-second runtime link watchdog.  
* **Asynchronous Ping Engine:** Non-blocking ICMP monitoring with a 5-second settle gate so slow DHCP never pollutes statistics.  
* **Rolling Telemetry:** Continuously computes historical data over **10-minute and 60-minute rolling windows** — exact average latency and success percentage per target.  
* **Multi-Screen Navigation (Touch):**  
  1. *Real-Time Monitor Dashboard:* Connected SSID marquee, live signal bar, per-target latency and scrolling domain names.  
  2. *Statistics — Recent 10 Min:* Average latency + success percentage per target.  
  3. *Statistics — Recent 60 Min:* Same metrics over a 1-hour window.  
  4. *Device Network Specifications:* Live DHCP/Static IP, netmask, gateway and DNS listing.  
* **Eco-Efficient Lifespan Guard:** Holds a steady **60% backlight duty**, boosting to **100% for 30 seconds** after each touch.  
* **Interactive Config Portal:** SoftAP portal + LAN access, factory reset button, and a webpage toggle for ping debug logging.  
* **Optional Second Target:** Leave Target 2 blank to monitor Target 1 only.  
  
---  
  
## Hardware  
  
Requires the **RockBase NM-CYD-C5** (2.8-inch ST7789 display + XPT2046 resistive touch). Firmware is written for the standard shared-SPI topology.  
  
### ESP32-C5 Pin Assignments  
  
| Group | Function | GPIO Pin | Notes |  
| :--- | :--- | :--- | :--- |  
| Shared SPI | MISO | `GPIO 2` | Shared Bus Pin |  
| Shared SPI | SCK | `GPIO 6` | Shared Bus Pin |  
| Shared SPI | MOSI | `GPIO 7` | Shared Bus Pin |  
| ST7789 LCD | Chip Select | `GPIO 23` | Dedicated CS |  
| ST7789 LCD | Data/Command | `GPIO 24` | Control Bus Pin |  
| ST7789 LCD | Backlight | `GPIO 25` | Hardware PWM Dimming |  
| XPT2046 Touch | Chip Select | `GPIO 1` | Dedicated CS |  
| WS2812 LED | Data | `GPIO 27` | RGB Addressable |  
  
---  
  
## Status LED  
  
| Color | Meaning |  
| :--- | :--- |  
| Magenta | AP Config Portal |  
| Yellow | Connecting |  
| Green | Connected — targets healthy |  
| Red | Configured target(s) failing |  
| Blue (blink) | Wi-Fi lost — reconnecting |  
  
---  
  
## Boot / Failover Flow  
  
```
[ Power On ]  
 │ Saved Wi-Fi credentials exist?  
 ├─ No ──> [ AP Config Mode ] ──> Portal + LCD setup  
 └─ Yes ──> [ Dual-Band AP Scan ] ──> [ Connect Primary Wi-Fi ] (30s)  
      │  
      │ Success? ── Yes ──> [ Monitoring Mode ]  
      │  
      └─ No ──> [ Connect Alternative Wi-Fi ] (30s)  
           │  
           │ Success? ── Yes ──> [ Monitoring Mode ]  
           └─ No ──> [ AP Config Mode ]  
```
  
At runtime a 10-second watchdog reconnects lost links automatically; if both profiles fail, the device re-enters AP mode.  
  
---  
  
## Required Libraries  
  
Managed by PlatformIO (`platformio.ini`, ESP32 core 3.x / pioarduino):  
  
1. `Adafruit GFX Library`  
2. `Adafruit ST7735 and ST7789 Library`  
3. `XPT2046_Touchscreen`  
  
Bundled (no install needed):  
  
* `lib/ESP32Ping-Patched/` — vendored ESP32Ping 1.7 with logging demoted to debug level (no per-ping log spam).  
  
---  
  
## Configuration  
  
All deploy-time settings — pins, portal AP credentials, country code, scan behaviour, backlight, timing, boot defaults — live in **`src/config.h`**. Portal-saved settings persist in NVS and take priority over these defaults.  
  
---  
  
## Installation  
  
1. Install Python 3.10–3.13 from https://www.python.org (**pioarduino does not support 3.14 yet**).  
2. `pip install -U platformio esptool`  
3. Open a terminal in the project path.  
4. Connect the **ESP32-C5 board** via a USB-C data cable.  
5. Flash: `pio run -t upload`  
   (Windows: `"%AppData%\Python\Python313\Scripts\pio.exe" run -t upload`)  
6. Serial monitor: `pio device monitor`  
   (Windows: `"%AppData%\Python\Python313\Scripts\pio.exe" device monitor --baud 115200`)  
   * RTS/DTR are disabled in `platformio.ini`, so opening the monitor does not reboot the board — press the RST button to capture a full boot log.  
  
---  
  
## The Configuration Web Portal  
  
If no Wi-Fi profile connects, the device opens an access point:  
  
* SSID: `ESP32-C5-NetWatchdog` (WPA2, password `NetWatchdog`)  
* Portal address: `http://192.168.4.1`  
  
1. Join the AP from your smartphone or PC.  
2. Open `http://192.168.4.1` in a browser.  
3. Edit Wi-Fi credentials, DHCP/Static IP, subnet, DNS servers, ping interval, screen rotation and monitoring targets, then **Save & Reboot**.  
4. To leave without saving, press the on-screen **Client Mode** button.  
  
**Client mode:** the same portal stays reachable at the device's LAN IP (shown on the Network Info screen).  
  
Portal notes:  
  
* AP list is scanned **once at boot** (2.4 GHz + 5 GHz, capped to the 200 strongest APs) — reboot to refresh.  
* Read-only device info panel; passwords are masked (click the password box to reveal).  
* **Factory Reset** button erases all settings and reboots to AP mode.  
* Footer link toggles **PING log** output on the serial monitor (state survives reboot).  
* The portal is unauthenticated — use it on trusted networks only.  
  
===============================================  

한국어  
  
# ESP32-C5 Network Watchdog  
  
**ESP32-C5** 기반의 비동기 듀얼 밴드 네트워크 모니터링 툴입니다.  
실시간 지연 시간(Latency) 진단, **10분 및 60분** 연속 롤링 윈도우 기반의 과거 네트워크 텔레메트리 데이터, 그리고 독립형 구성 포털(Configuration Portal)을 제공합니다.  
  
---  
  
## 주요 기능  
  
* **듀얼 밴드 연결:** 2.4 GHz 및 5 GHz 대역 스캔과 연결을 지원하며(ESP32-C5), 포털 드롭다운 메뉴에서 각 AP의 밴드 및 채널 세부 정보를 확인할 수 있습니다.  
* **스마트 페일오버 백업:** 기본(Primary) 및 대체(Alternative) 와이파이 프로필을 설정할 수 있습니다. 기본 연결이 끈어지면 대체 자격 증명으로 즉시 전환되며, 10초 런타임 링크 워치독이 작동합니다.  
* **비동기 핑(Ping) 엔진:** 느린 DHCP 설정으로 인해 통계 데이터가 왜곡되지 않도록 5초의 안정화 게이트(Settle gate)를 둔 논블로킹(Non-blocking) ICMP 모니터링을 수행합니다.  
* **롤링 텔레메트리:** **10분 및 60분 롤링 윈도우**를 통해 과거 데이터를 지속적으로 계산하며, 타겟별 정확한 평균 지연 시간과 성공률(%)을 도출합니다.  
* **멀티 스크린 내비게이션 (터치):**  
  1. *실시간 모니터 대시보드:* 연결된 SSID 마키(스크롤 텍스트), 실시간 신호 세기 바, 타겟별 지연 시간 및 스크롤되는 도메인 이름을 표시합니다.  
  2. *통계 — 최근 10분:* 타겟별 평균 지연 시간과 성공률(%)을 보여줍니다.  
  3. *통계 — 최근 60분:* 1시간 윈도우 동안의 동일한 지표를 보여줍니다.  
  4. *장치 네트워크 사양:* 실시간 DHCP/고정 IP, 넷마스크, 게이트웨이 및 DNS 목록을 표시합니다.  
* **친환경 고효율 수명 보호 기능:** 평상시에는 **60% 백라이트 밝기**를 유지하다가, 화면을 터치할 때마다 **30초 동안 100% 밝기**로 부스팅합니다.  
* **대화형 구성 포털:** SoftAP 포털 및 LAN 접근, 공장 초기화 버튼, 웹페이지에서 핑 디버그 로그 출력을 켜고 끌 수 있는 토글 기능을 제공합니다.  
* **선택형 보조 타겟:** 타겟 2(Target 2)를 빈칸으로 두면 타겟 1만 모니터링합니다.  
  
---  
  
## 하드웨어  
  
**RockBase NM-CYD-C5** (2.8인치 ST7789 디스플레이 + XPT2046 감압식 터치)가 필요합니다. 펌웨어는 표준 공유 SPI 토폴로지에 맞추어 작성되었습니다.  
  
### ESP32-C5 핀 할당  
  
| 그룹 | 기능 | GPIO 핀 | 비고 |  
| :--- | :--- | :--- | :--- |  
| 공유 SPI | MISO | `GPIO 2` | 공유 버스 핀 |  
| 공유 SPI | SCK | `GPIO 6` | 공유 버스 핀 |  
| 공유 SPI | MOSI | `GPIO 7` | 공유 버스 핀 |  
| ST7789 LCD | Chip Select | `GPIO 23` | 전용 CS |  
| ST7789 LCD | Data/Command | `GPIO 24` | 제어 버스 핀 |  
| ST7789 LCD | 백라이트 | `GPIO 25` | 하드웨어 PWM 디밍 |  
| XPT2046 터치 | Chip Select | `GPIO 1` | 전용 CS |  
| WS2812 LED | 데이터 | `GPIO 27` | RGB 주소 지정 가능 |  
  
---  
  
## 상태 LED  
  
| 색상 | 의미 |  
| :--- | :--- |  
| 자홍색 | AP 구성 포털 모드 |  
| 노란색 | 연결 중 |  
| 초록색 | 연결 완료 — 타겟 상태 정상 |  
| 빨간색 | 설정된 타겟 연결 실패 발생 |  
| 파란색 (깜빡임) | 와이파이 연결 끊김 — 재연결 중 |  
  
---  
  
## 부팅 / 페일오버 흐름  
  
```
[ 전원 켬 ]  
 │ 저장된 와이파이 자격 증명이 있습니까?  
 ├─ 아니오 ──> [ AP 구성 모드 ] ──> 포털 + LCD 설정 진행  
 └─ 예 ────> [ 듀얼 밴드 AP 스캔 ] ──> [ 기본 와이파이 연결 ] (30초)  
      │  
      │ 성공했습니까? ── 예 ──> [ 모니터링 모드 ]  
      │  
      └─ 아니오 ──> [ 대체 와이파이 연결 ] (30초)  
           │  
           │ 성공했습니까? ── 예 ──> [ 모니터링 모드 ]  
           └─ 아니오 ──> [ AP 구성 모드 ]  
```
  
런타임 환경에서는 10초 워치독이 끊어진 링크를 자동으로 재연결합니다. 두 프로필이 모두 실패하면 장치는 다시 AP 모드로 진입합니다.  
  
---  
  
## 필수 라이브러리  
  
PlatformIO를 통해 관리됩니다 (`platformio.ini`, ESP32 코어 3.x / pioarduino).  
  
1. `Adafruit GFX Library`  
2. `Adafruit ST7735 and ST7789 Library`  
3. `XPT2046_Touchscreen`  
  
기본 내장 (별도 설치 불필요):  
  
* `lib/ESP32Ping-Patched/` — 핑 마다 발생하는 로그 스팸을 방지하기 위해 로깅 수준을 디버그 레벨로 낮춘 패치 버전의 ESP32Ping 1.7이 포함되어 있습니다.  
  
---  
  
## 환경 설정  
  
핀 할당, 포털 AP 자격 증명, 국가 코드, 스캔 동작, 백라이트, 타이밍, 부팅 기본값 등 배포 시 필요한 모든 설정은 **`src/config.h`** 파일에서 관리합니다. 포털을 통해 저장된 설정은 NVS(비휘발성 저장소)에 유지되며 이러한 기본값보다 우선순위를 가집니다.  
  
---  
  
## 설치 방법  
  
1. https://python.org 에서 Python 3.10–3.13 버전을 설치합니다 (**현재 pioarduino는 3.14 버전을 지원하지 않습니다**).  
2. `pip install -U platformio esptool` 명령어를 실행합니다.  
3. 프로젝트 경로에서 터미널을 엽니다.  
4. USB-C 데이터 케이블을 사용해 **ESP32-C5 보드**를 연결합니다.  
5. 플래싱 진행: `pio run -t upload`  
   (Windows 환경: `"%AppData%\Python\Python313\Scripts\pio.exe" run -t upload`)  
6. 시리얼 모니터 확인: `pio device monitor`  
   (Windows 환경: `"%AppData%\Python\Python313\Scripts\pio.exe" device monitor --baud 115200`)  
   * `platformio.ini` 파일에서 RTS/DTR이 비활성화되어 있으므로 모니터를 열어도 보드가 리부팅되지 않습니다. 전체 부팅 로그를 확인하려면 보드의 RST 버튼을 누르십시오.  
  
---  
  
## 웹 구성 포털 (Configuration Web Portal)  
  
와이파이 프로필 연결에 모두 실패하면 장치가 자체 액세스 포인트(AP)를 생성합니다.  
  
* SSID: `ESP32-C5-NetWatchdog` (WPA2 방식, 비밀번호: `NetWatchdog`)  
* 포털 주소: `http://192.168.4.1`  
  
1. 스마트폰이나 PC로 위 AP에 연결합니다.  
2. 브라우저에서 `http://192.168.4.1` 주소로 접속합니다.  
3. 와이파이 자격 증명, DHCP/고정 IP 설정, 서브넷, DNS 서버, 핑 간격, 화면 회전 방향 및 모니터링 타겟을 편집한 후 **Save & Reboot** 버튼을 누릅니다.  
4. 저장하지 않고 나가려면 화면의 **Client Mode** 버튼을 누릅니다.  
  
**클라이언트 모드:** 포털이 실행 중일 때는 장치가 할당받은 LAN IP(네트워크 정보 화면에 표시됨)를 통해 동일한 포털에 계속 접근할 수 있습니다.  
  
포털 주의 사항:  
  
* AP 목록은 **부팅 시 단 한 번만 스캔**하며(2.4 GHz + 5 GHz 대역 결합, 신호가 가장 강한 200개 AP로 제한), 새로고침하려면 리부팅해야 합니다.  
* 장치 정보 패널은 읽기 전용이며, 비밀번호는 숨김 처리되어 있습니다(비밀번호 상자를 클릭하면 표시됨).  
* **Factory Reset** 버튼을 누르면 모든 설정이 삭제되고 AP 구성 모드로 리부팅됩니다.  
* 하단 푸터 링크를 통해 시리얼 모니터의 **PING 로그** 출력을 토글할 수 있습니다(이 설정 상태는 리부팅 후에도 유지됨).  
* 본 포털은 별도의 인증 절차가 없으므로 안전하고 신뢰할 수 있는 네트워크에서만 사용하십시오.  
  
