# DC27_FRT — LED 통합 펌웨어 (ESP32-C6 SuperMini)

작년 `kiosk_neo_frt`(Nano → C6 이식, 실보드 확인본) 기능 그대로, DEVICE_ID만 새 체계(LED = 0x10~)로 바꾼 통합 코드.
DEVICE_ID에 따라 LED 수와 사용 가능한 이펙트가 달라진다. 이펙트 로직·색상은 작년과 동일.

## DEVICE_ID / 구성
| DEVICE_ID | 작년 ID | 구성 | LED 수 |
|---|---|---|---|
| **0x10** | 0x01 | f150x01 | 180 |
| 0x11 | 0x02 | f180x02 | 180 |
| 0x12 | 0x03 | f180x03 | 150 |
| 0x13 | 0x04 | f8x04 | 8 |

- 0x10 ↔ 작년 0x01 은 확정. 0x11~0x13 은 작년 번호 순서대로 대응시킨 것 → 다르면 `src/config.h` 표의 `FX_TYPE` 줄만 수정.
- 이펙트 구분은 `FX_TYPE`(= 작년 ID 값 0x01~0x04)으로 한다. `neo_effect.cpp` 는 비교 대상만 `DEVICE_ID` → `FX_TYPE` 으로 바뀌었고 나머지는 작년과 동일.
- ID 값(0x10)과 명령 값(0x10)은 다른 계층이다. 명령 0x10은 정의되어 있지 않고 무시된다.

## 호스트 프로토콜 (115200 8N1, 네이티브 USB)
- PC `0x00` → 보드 **DEVICE_ID 1바이트만** 응답. 이 펌웨어는 시리얼에 다른 것을 출력하지 않는다.
- PC `0x01`~`0x28` → 이펙트 명령 (아래 표). 해당 ID에 없는 명령은 무시.
- PC `0xF0 0xA5 0x5A` → ROM 다운로드 모드로 재부팅(BOOT 버튼 없이 업데이트). USB 없이 무선으로는 아래 "BLE 무선 펌웨어 업데이트".
- **전원 인가 직후(PC 미연결)**: 스카이블루(`COLOR_BOOT_WAIT`, 0x0B와 같은 색) 디밍 온/오프 반복(0.5 s 밝아짐 + 0.5 s 어두워짐, `NEO_CFG_BOOT_WAIT_PERIOD_MS`). PC에서 **첫 바이트**(스캔의 `0x00` 또는 이펙트 명령)를 받으면 소등하고 이후는 명령대로 동작. 한 번 소등되면 재부팅 전까지 다시 켜지지 않는다. DTR은 판단에 쓰지 않는다(PC 프로그램이 스캔 때 DTR을 끄므로).
- `usb_disable_autoreset()` 로 COM 포트를 열 때의 DTR/RTS 리셋을 막는다 (`include/usb_noreset.h`).

## 이펙트 명령 표
| 명령 | 0x10 / 0x11 | 0x12 | 0x13 |
|---|---|---|---|
| 0x01, 0x06 | OFF | OFF | OFF |
| 0x02 | 정지색(CMD_03, 즉시) | 정지색(CMD_03, 즉시) | 숨쉬기 깜빡임(CMD_03, 1.5 s) |
| 0x03~0x05 | 정지색 페이드 0.5 s (CMD_03/04/05) | 동일 | 동일 |
| 0x07 | FIRE색 페이드 | FIRE 환경(페이드인-유지-아웃 + 흔들림) | FIRE색 페이드 |
| 0x08 | SUNSET색 페이드 | TINDLE 환경(햇살 빔) | SUNSET 페이드인-유지-아웃 |
| 0x09 | FOREST색 페이드 | TINDLE 환경 | FOREST 페이드인-유지-아웃 |
| 0x0A | LAKE색 페이드 | TINDLE 환경 | LAKE 페이드인-유지-아웃 |
| 0x0B~0x0F | 정지색 페이드 | 동일 | 동일 |
| 0x11 | — | 정지색 페이드 | — |
| 0x12 | — | MUSIC(색 호흡) | — |
| 0x13 / 0x14 / 0x15 | — | — | SEQ 13 / 14 / 15 |
| 0x16~0x19 | 색 깜빡임 | — | — |
| 0x1A | 정지색 | — | — |
| 0x1B~0x22 | — | 1B·1E·1F·20·22 정지색, 1C·1D·21 색 깜빡임 | — |
| 0x23 | 정지색 | — | — |
| 0x24, 0x25 | 색 깜빡임 | — | — |
| 0x26 / 0x27 | — | 정지색 / 색 깜빡임 | — |
| 0x28 | 그라데이션 흐름 (전 ID 공통) | 동일 | 동일 |

(색 값은 `src/colors.h`, 시간 값은 `src/neo_effect_config.h`)

## 빌드 / 업로드
- 환경은 ID마다 하나: `dc27_frt_10`, `_11`, `_12`, `_13`. `default_envs` = `dc27_frt_10`.
- **업로드는 항상 환경을 지정**: `pio run -e dc27_frt_10 -t upload --upload-port COMxx`
  (`pio` 가 PATH에 없으면 `& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run -e dc27_frt_10 -t upload --upload-port COMxx`)
- DEVICE_ID는 `-DDEVICE_ID=` (ini 환경별) 로 정하고, `config.h` 의 기본값(0x10)은 기본 환경과 항상 같게 둔다.
- 업로드 전에 검사용 EXE / 시리얼 모니터를 닫는다 (COM 포트 점유 시 `액세스가 거부되었습니다`).
- 자동 리셋이 꺼져 있으므로 업로드는 BOOT+RST 로 다운로드 모드에 넣거나, 펌웨어가 돌고 있으면 `0xF0 0xA5 0x5A` 전송.
- 핀은 `include/board_pins.h` 한 곳 (LED 데이터 = GPIO6). 3.3 V → WS2812(5 V) 는 레벨시프터 권장.

## BLE 무선 펌웨어 업데이트 (2026-10-26 추가)
케이스 조립 후 USB/BOOT 버튼 없이 Bluetooth LE 로 펌웨어를 바꾼다. 보드 쪽 코드는 `src/ble_ota.cpp/.h`, PC 쪽은 `tools\led_ota.exe`(소스 `tools\led_ota_src`).

**처음 한 번은 USB 로 올려야 한다** (BLE 업데이트 기능이 들어간 이 버전). 그 다음부터는 무선.

1. PlatformIO 빌드 (`pio run -e dc27_frt_10`) → 프로젝트의 `bin\LED_0x10_v1.0.bin` 이 자동으로 생긴다 (`tools/bin_copy.py`). 이름 = 종류_ID_v버전.
   - 파일 이름은 바꿔도 된다. exe 와 보드는 이름이 아니라 **bin 안의 태그** `DC27FW|LED|0x10|1.0|` 를 읽어 확인한다.
   - 펌웨어를 고치면 `src/config.h` 의 `FW_VERSION` 을 올린다 (파일 이름과 exe 화면에 나온다).
2. 보드 전원을 껐다 켠다 → **5 분 동안** BLE 이름 `LED 0x10 DC27_FRT` 로 광고 (`BLE_OTA_WINDOW_S`). 5 분이 지나면 재부팅 전까지 BLE 가 꺼진다(전송 중이면 끝까지 진행).
3. `tools\led_ota.exe` 실행 (Windows 10/11, PC Bluetooth 켜기) → [찾아보기...]로 Windows 파일 선택 창에서 bin 선택(경로 칸에 들어감, 경로를 직접 붙여 넣고 Enter 도 가능. 처음엔 프로젝트의 `bin` 폴더에서 열림) → [보드 찾기] → 목록에서 보드 선택 (BLE 이름, 종류, ID, 신호 세기, 파일과 일치 여부 표시) → PIN 입력 → [업데이트].
4. 진행률 표시 후 보드가 SHA-256·이미지·태그를 검사하고 새 펌웨어로 재부팅한다. 약 760 KB, 30 초~1 분 정도.

| 항목 | 내용 |
|---|---|
| BLE 이름 | `<종류> 0x<ID> <설명>` → `LED 0x10 DC27_FRT`. RF/SW/POWER 보드도 같은 규칙으로 쓴다 (`RF 0x20 ...`) |
| PIN | `src/config.h` 의 `BLE_OTA_PIN` (기본 `2727`). 3 회 틀리면 재부팅 전까지 잠김 |
| ID 확인 | 보드 ID 와 bin 의 ID 가 다르면 거부. ID 를 바꾸려면 exe 의 `ID 변경 허용` 체크. 종류(LED/RF...)가 다르면 항상 거부 |
| 실패하면 | 전송 끊김·검사 실패 → 기존 펌웨어 그대로. 새 펌웨어가 `setup()` 끝까지 못 가고 죽으면 부트로더가 이전 펌웨어로 되돌린다 |
| 파티션 | 기본 `default.csv` (app0/app1 각 1.25 MB, 원래 쓰던 것과 같음). 펌웨어 약 755 KB |
| USB 프로토콜 | 변화 없음. `0x00` → ID 1바이트만, 시리얼 출력 없음(ESP-IDF 로그도 끔). eFuse 무음 부팅 설정과 무관 |
| LED | 전송 시작 시 소등. 재부팅 후 평소처럼 스카이블루 디밍 → PC 첫 바이트에 소등 |

- exe 다시 빌드(Go 1.26): `tools\led_ota_src` 에서 `go build -trimpath -ldflags "-H=windowsgui -s -w" -o ..\led_ota.exe .`

## 새 보드 bring-up 순서
1. eFuse `DIS_USB_SERIAL_JTAG_ROM_PRINT` (보드당 1회, 되돌릴 수 없음): `ESP32C6\tools\burn_silent_boot.bat COMx`
2. 원하는 ID의 환경으로 업로드 (`-e dc27_frt_1x`)
3. `tools\led_tester.exe` 로 스캔 → Device ID 가 맞고 Note 가 OK, Config 가 해당 구성(예: FX1 · f150x01 · 180 LEDs)인지 확인
4. 같은 프로그램에서 이펙트 명령(0x01~0x28)을 보내 LED 동작 확인 (Auto-run 으로 해당 ID에서 유효한 명령을 차례로 확인 가능)

## 검증 상태
- PC 시뮬레이션(가짜 Arduino/NeoPixel)으로 4개 ID 모두 확인: LED 수, ID 1바이트 응답, ID별 이펙트 구분/무시, OFF, 다운로드 모드 매직 바이트 소비.
- **실보드 확인 전.** 작년 `kiosk_neo_frt`(ID 0x01)만 실보드에서 확인된 코드이고, 이 프로젝트는 ID 대응층만 바뀌었다.

## 검증용 프로그램 `tools\led_tester.exe`
RF 검사 프로그램(`rf_remote_tester.exe`)과 같은 방식(Go, 설치 불필요, 더블클릭, Edge/Chrome 앱 창)이며 화면 구성은 작년 PC 조명 프로그램(`LGE Lighting Controller for CES`)을 따랐다. 소스는 `tools\led_tester_src`.
- 스캔: 작년 PC 프로그램과 같은 절차(포트 열기 → DTR/RTS off → 1 s 대기 → 10바이트 읽기 → `0x00` 전송 → 4바이트 읽기, 첫 바이트 = Device ID). 로그 문구도 같은 형식.
- 목록: Sel / COM Port / Device ID / Note(`OK`, `Stray bytes`, `No ID response`, `Open failed`) / Config(ID → FX 구성, LED 수) / Effect 입력 + Send / Last Effect / Connect·Disconnect.
- Effect 값은 16진수(`03`, `0x1B`). `0x00` 은 ID 요청이라 이펙트로는 보내지 않는다. 해당 ID에 정의되지 않은 명령도 보낼 수 있고(무시되는지 확인용) 로그에 경고로 남는다.
- 이펙트 명령 표(0x01~0x28): 선택한 보드의 ID에서 유효한 명령만 진하게 표시, 클릭하면 바로 전송. `Auto-run valid effects` 는 유효한 명령을 N초 간격으로 차례로 보내고 마지막에 OFF.
- `Send to Selected`(Sel 체크된 보드) / `Send to All connected`, `ID request (0x00)`(1바이트 응답 확인), `Enter bootloader (F0 A5 5A)`(확인창 후 전송 → ROM 다운로드 모드), `CLOSE PORTS`.
- 작년 ID(0x01~0x04)는 `legacy ID` 로, 0x10~0x13 이외는 `Unknown ID` 로 표시.
- 검사 전에 다른 프로그램(시리얼 모니터, PC 조명 프로그램)이 COM 포트를 잡고 있으면 안 된다. exe 를 실행 중이면 Windows 가 파일을 잠그므로 새 버전을 복사하려면 창을 닫는다.
