# LGE_DC_27 — 시스템 공통 기준 (LED / RF / SWITCH / POWER 방 공용)

(2026-10-23 작성, 같은 날 시스템 구조 v0.3 및 RF 방 실보드 결과 반영. 모든 방은 이 문서를 기준으로 시작한다. 방별 세부 내용은 각 방에서 관리.)
**전체 시스템 구조(장치 ID 체계, POWER MASTER/SLAVE, UART 프레임 제안, 미확정 항목)는 `claude/system_architecture_v0.3.md` 를 기준으로 한다.**
**PC 프로그램의 포트 열기 절차, 보드가 지켜야 할 규칙, DEVICE_ID 설정 규칙, 검사 프로그램, 증상→원인 표는 `claude/host_protocol_and_tester.md` (RF 방에서 실보드로 확인한 공통 내용).**

## 0. Device ID 체계 (구조 v0.3, 요약)
| 장치 | ID | 상태 |
|---|---|---|
| LED | 0x10, 0x11, 0x12 … | 시작값 확정 |
| 택트 스위치(SW) | **0x20**, 0x21 … | 시작값 확정 |
| RF 리모컨 | **0x30**, 0x31 … | 시작값 확정 (RF 방 실보드 0x30 확인) |
| POWER MASTER | 0x40 | 확정 (PC와 직접 연결되는 전원 통합 제어기, 하위 RS-485 슬레이브 라우팅) |
| POWER SLAVE | 0x50, 0x51 … | 시작값 확정 (PC는 직접 통신하지 않음, MASTER 경유) |
- ID 조회: PC가 `0x00` 1바이트 전송 → 제어기가 1바이트 ID 회신. COM 번호는 바뀔 수 있으므로 ID가 논리 식별자.
- 새 보드의 DEVICE_ID는 위 시작값을 따른다. 같은 계열이 여러 대면 +1씩.

## 1. 시스템 개요
- 컨트롤러: Arduino Nano → **ESP32 SuperMini C6** (ESP32-C6, 네이티브 USB만 사용). Nano 기준 핀 수 이내에서 해결.
- 펌웨어: 아이템별 독립 PlatformIO 프로젝트 (폴더 1개 = 펌웨어 1개).
  - PC: `C:\Users\kekem\Documents\PlatformIO\Projects\LGE_DC_27\ESP32C6\<아이템>\`
  - 시험 중인 것: `C:\Users\kekem\Documents\PlatformIO\test\`
  - RF 8키 CC1101 신규: `C:\Users\kekem\Documents\PlatformIO\Projects\LGE_RF_REMOTE\feelax_cc1101_rf8ch_v1_1` (ID 0x30)
- 호스트 프로토콜: UART 115200 8N1 (보드에서는 USB-CDC). 호스트가 `0x00` 전송 → 보드가 1바이트 DEVICE_ID 응답.
- **PC 쪽 프로그램은 우리가 만드는 것이 아니라 수정 불가. 모든 해결은 보드(펌웨어/칩) 쪽에서 한다.** (v0.3의 확장 프레임 AA 55 … 은 제안 단계이며 기존 1바이트 방식을 깨면 안 된다.)
- 케이스 조립 후에는 BOOT 버튼을 누르기 어렵다.

## 2. 방별 담당 폴더
| 방 | 폴더 | 내용 | 구조 v0.3 ID |
|---|---|---|---|
| LED | `kiosk_neo_frt`, `kiosk_neo_rear`, `vision_buck_a/b/c`, `cxr_buck`, `cxr_puron` | WS2812 라이팅, 이펙트 명령, LED 수/DEVICE_ID | 0x10~ |
| RF | `kiosk_rfkey`, `kiosk_rfkey_toggle`, `kiosk_rfkey_no_toggle`, (신규) `LGE_RF_REMOTE` CC1101 8키 | RF 수신 모듈 4/8키, 짧게/길게(0x80) | 0x30~ |
| SWITCH | `kiosk_tacts`, `kiosk_indev` | TACT 12키, TACT 8 + 조이스틱 2개 | 0x20~ |
| POWER | `kiosk_ble_sw` (기존), 신규 POWER MASTER/SLAVE (계획) | 24V×4, 12V×4, RELAY×5, BLE, WS LED / MASTER 0x40, RS-485 SLAVE 0x50~ | 0x40, 0x50~ |
- `cxr_buck`은 버튼이 있지만 라이팅 보드라 LED 방.
- POWER: 기존 `kiosk_ble_sw`와 신규 MASTER/SLAVE 구조의 관계는 미정 (구조 문서 §11).

## 3. 반드시 지켜야 할 공통 사항
1. **eFuse (보드마다 1회, 되돌릴 수 없음)**: `DIS_USB_SERIAL_JTAG_ROM_PRINT`=1. 칩 부팅 문구(`ESP-ROM:…`)가 USB로 나가 PC 프로그램이 Device ID를 0x70 등으로 읽는 것을 막는다. `tools\burn_silent_boot.bat COMx`. 상세는 `claude/usb_device_id_fix.md`. **RF 방 실측: eFuse 없이도 `usb_disable_autoreset()` + 로그 OFF 환경에서 ID 정상 수신(1회 관찰).** 검사 프로그램 로그의 `pre-existing bytes`에 `ESP-ROM`이 보이면 그때 굽는 것을 기본으로 하되, 최종 제품은 eFuse를 굽는 쪽이 가장 확실하다(결정은 사용자).
2. **업로드는 환경 1개만, 항상 `-e <환경>` 지정**: 환경이 여러 개면 전부 올라가 마지막 환경이 남는다. `[platformio] default_envs` 지정 (`kiosk_neo_frt` 적용됨). 텍스트 로그가 나오는 개발용 환경이 올라가 있으면 PC 프로그램에서 Device ID가 글자(예: 0x43)로 나온다 — RF 방에서 실제 발생.
3. **COM 포트 점유 주의**: 업로드 전 검사용 EXE/시리얼 모니터를 닫는다.
4. **IO는 3.3 V, 5 V 비내성**: 5 V 신호(RF 수신 출력, WS2812 데이터 등)는 분압 또는 레벨시프터(74AHCT125 등).
5. **핀은 각 프로젝트 `include/board_pins.h` 한 곳에서만 수정.**
6. **DEVICE_ID는 한 곳에서만**: 코드의 `#ifndef DEVICE_ID / #define DEVICE_ID 0x..` 만 두고 platformio.ini에 `-DDEVICE_ID` 를 넣지 않는다. ini 플래그가 코드 값을 조용히 덮어써 다른 ID로 올라간다 (RF 방에서 0x21 사고). 같은 소스로 여러 대를 만들 때만 ini 플래그를 쓰고 그땐 코드 값과 일치시킨다. 기존 프로젝트(kiosk_neo_frt 등)가 ini에 `-DDEVICE_ID`를 쓰고 있으면 그 상태를 확인하고, 한 곳으로 정리할지는 방 담당이 판단.
7. **호스트 모드 환경에서는 시리얼에 ID/키 바이트 외 아무것도 출력하지 않는다**(로그 매크로 OFF), DTR/RTS 자동 리셋 비활성(`usb_disable_autoreset()`). 업로드는 BOOT+RST. (`host_protocol_and_tester.md` §2)

## 4. SuperMini C6 핀 규칙
- 헤더 핀: GPIO0~9, 12~23. **GPIO10/11은 나와 있지 않음.**
- 피할 것: GPIO4·5(스트래핑), 8(온보드 WS2812), 9(BOOT), 15(온보드 LED), 12·13(USB), 24~30(내부 플래시). GPIO6·7은 JTAG 핀(일반 GPIO로 쓰면 JTAG 불가).
- 깨끗하게 쓸 수 있는 핀: 0, 1, 2, 3, 6, 7, 14, 18, 19, 20, 21, 22, 23 (ADC1 = GPIO0~6).
- GPIO16·17은 UART0 핀. Serial이 USB일 때(`CDC_ON_BOOT=1`, 기본)만 일반 GPIO로 사용 가능.
- LED 데이터는 **GPIO6으로 통일** (GPIO5는 실측에서 구동 안 됨).
- 현재 배정 (상세는 `ESP32C6/README.md`):
  - cxr_buck 버튼 OFF=2, STATIC=3, FADE=7, MOVE=14
  - rfkey 4키 = 2, 3, 6, 7 / 8키 = 2, 3, 6, 7, 19, 20, 14, 18
  - tacts 12키 = T1~T6 2,3,6,7,23,1 / SL,SR 14,18 / LT,LB,RT,RB 19,20,21,22
  - indev TACT = 6,7,23,17,14,18,19,20 / 조이스틱 X,Y = (0,1)·(2,3) / SW = 21,22
  - ble_sw 24V = 0,1,2,3 / 12V = 6,7,22,17 / RELAY = 14,18,19,20,21 / WS LED = 23
  - LGE_RF_REMOTE(CC1101): SCK 18, MISO 19, MOSI 20, CSN 15, GDO0 14, PAIR 0, 상태 LED WS2812 8, 출력 CH1~8 = 17,1,2,3,4,5,6,7
- 작은 피치(1.27 mm) 모듈/점퍼는 접촉 불량이 간헐 증상으로 나타난다(RF 방에서 두 번 겪음). 최종품은 납땜 또는 고정 커넥터.

## 5. 빌드 설정 (공통)
- 플랫폼: pioarduino (espressif32 55.3.312, Arduino-ESP32 3.3.12), board `esp32-c6-devkitc-1`.
- build_flags: `-DARDUINO_USB_MODE=1 -DARDUINO_USB_CDC_ON_BOOT=1`
- LED 수 등은 `-DLED_COUNT=…` 로 덮어쓸 수 있음. DEVICE_ID는 §3.6 규칙을 따른다.
- 빌드/업로드: 프로젝트 폴더에서 `pio run -e <env>`, `pio run -e <env> -t upload`. `pio`가 PATH에 없으면 `& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run -e <env> -t upload --upload-port COMxx`.
- VS Code는 에디터 탭에 열려 있는 파일을 외부 변경 시 되돌릴 수 있다. 외부에서 고친 파일은 탭을 닫았다 연다.

## 6. 검증 상태 (2026-10-23 기준)
- 실보드 확인: `kiosk_neo_frt` (스캔 ID 0x01, LED 구동), eFuse 적용 후.
- **`LGE_RF_REMOTE` (CC1101 8키 RF)**: 실보드에서 배선·페어링·출력·반응속도 확인, 사양 v1.3 확정. `rfkey_host` 업로드 후 검사 프로그램(`rf_remote_tester.exe`) 스캔에서 **ID 0x30, 추가 바이트 없음(Note OK)** 확인. 8키 짧게/길게 검사와 실제 PC RF-KEY 프로그램 인식은 확인 보고 전. 상세 `claude/rf_remote_cc1101.md`.
- 컴파일만 확인: 나머지 전부. 새 핀 배선은 실보드 확인 전.
- `kiosk_rfkey`: 디바운스(15 ms)·극성 옵션(`RFKEY_ACTIVE_HIGH`) 추가, PC 시뮬레이션 확인. 결선도(분압 4.7k/10k) 작성됨. 실제 모듈 연결 전, 모듈 모델 미확인(4ch/5 V 가정).
- **BOOT 없이 다운로드 모드 명령 (`F0 A5 5A`)**: `kiosk_neo_frt`에만 적용, **실보드 검증 전**. `tools\test_download_mode.bat COM7`에서 PASS가 나오면 다른 펌웨어에 확대 적용.
- PC 쪽 도구: TEST_CONT 안의 `_r3` EXE 3종 (조명 / RF-KEY / Input Device), 원본은 그대로. 별도로 `LGE_RF_REMOTE\tools\rf_remote_tester.exe`가 **어느 컨트롤러에도 쓸 수 있는** ID/포트 검사 프로그램(기대 ID 입력 없음, 화면에서 ID 직접 확인; 다른 방 보드에 쓰려면 exe를 공용 도구 폴더로 복사).

## 7. 새 방을 시작할 때
1. 이 문서, `claude/host_protocol_and_tester.md`, `claude/system_architecture_v0.3.md`, `claude/usb_device_id_fix.md`를 먼저 읽는다.
2. 자기 방의 폴더만 수정한다. 공통 사항(§3, §4)을 바꿔야 하면 이 문서를 갱신한다.
3. 핀을 바꾸면 `board_pins.h`와 `ESP32C6/README.md` 핀 표를 함께 맞춘다.
4. 새 보드 첫 bring-up 순서: **호스트 환경을 `-e`로 지정해 업로드 → `rf_remote_tester.exe`로 ID/Note 확인 → (필요 시) eFuse → 기능 시험.** ID는 §0 표를 따른다.
5. 구조 문서의 미확정 항목(P1/P2)을 구현에 쓰려면 먼저 사용자에게 확정 여부를 확인한다.
