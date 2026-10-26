# 호스트(PC) 프로토콜 · 포트 열기 절차 · 검사 프로그램 — 전 컨트롤러 공통 (2026-10-23, RF 8키 방에서 실보드 확인)

모든 방(LED / RF / SWITCH / POWER)의 ESP32-C6 컨트롤러에 그대로 적용되는 내용만 모았다. RF 방 전용 내용은 `claude/rf_remote_cc1101.md`, 시스템 구조·ID 체계는 `claude/system_architecture_v0.3.md`, 핀·빌드 공통은 `claude/system_common.md`.

## 1. PC 프로그램이 보드를 여는 정확한 절차 (실제 EXE `lge_rfkey_receiver_for_ces_r3.exe` 분석, Python/tkinter)
1. 포트 열기: 115200 8N1, timeout 1.0 s, write_timeout 0.5 s, 흐름제어 없음
2. `setDTR(False)` → `setRTS(False)` → 입출력 버퍼 비우기
3. **1.0 s 대기** (BOOT_WAIT)
4. `read(10)` — 이미 와 있는 바이트(pre-existing). 있으면 로그에만 남김
5. `0x00` 1바이트 전송 → `read(4)` (최대 1 s) → **첫 바이트 = DEVICE_ID**
6. ID를 받으면 포트를 열어 둔 채 리더 스레드 시작: 1바이트씩 read(timeout 0.5 s), `index = b & 0x7F`, `long = b & 0x80`
7. 장치별 Disconnect(포트 닫기, 펌웨어 업로드 가능) / Connect(1~5 절차 재실행). 창: 820x580, 모드 라디오(Vision Buck: ID 1,2,3 → 존 A,B,C / Smart Kiosk: ID 1,2 → F,R), SCAN DEVICES, 장치 행(COM Port, Device ID, Zone, Last Key, Port).

**결론: 포트를 연 뒤 ID 요청 전후로 보드가 ID 외의 바이트를 하나라도 내보내면 ID가 깨진다.** 보드 쪽에서 해결해야 한다(PC 프로그램은 수정 불가).

## 2. 보드가 지켜야 할 규칙 (모든 호스트 모드 펌웨어)
- `0x00` 수신 → **DEVICE_ID 1바이트만** 응답. 부팅 문구·로그·`[STAT]` 같은 텍스트를 USB 시리얼에 절대 출력하지 않는다 (호스트 모드 환경에서는 로그 매크로 OFF).
- DTR/RTS 신호로 보드가 리셋되지 않게: `usb_disable_autoreset()` (`include/usb_noreset.h`, kiosk_rfkey에서 복사). 이 경우 펌웨어 업로드는 **BOOT + RST로 다운로드 모드**에 넣고 해야 한다.
- 칩 부팅 문구(`ESP-ROM:esp32c6-…`)는 eFuse `DIS_USB_SERIAL_JTAG_ROM_PRINT`=1 로 차단 (`claude/usb_device_id_fix.md`). **관찰(RF 방, 1회)**: eFuse 미적용 보드라도 `usb_disable_autoreset()` + 로그 OFF 펌웨어(`rfkey_host`)에서는 위 절차로 ID 0x30 한 바이트만 정상 수신(앞뒤 바이트 없음). 그래도 eFuse가 가장 확실한 보장이므로, 스캔 로그의 `pre-existing bytes`에 `ESP-ROM`이 보이면 그때 굽는다. eFuse는 **되돌릴 수 없고** 보드당 1회.
- 키 보고 형식(입력 장치): 키를 **뗄 때** 1바이트 = 인덱스(0..N-1), 0.6 s 이상 누르면 `| 0x80`. (SW/RF 보고 방식 최종안은 구조 문서 P2 미확정.)
- 호스트 환경(`HOST_PROTOCOL=1`)과 개발용 로그 환경을 분리하고, **업로드는 항상 환경 이름을 지정**: `platformio.exe run -e <env> -t upload --upload-port COMxx`. `[platformio] default_envs`는 개발용 하나로 고정.

## 3. DEVICE_ID 설정 규칙
- ID 체계(구조 v0.3): LED 0x10~, SW 0x20~, RF 0x30~, POWER MASTER 0x40, POWER SLAVE 0x50~ (자세히는 `system_common.md` §0).
- **ID는 한 곳에서만 정한다.** `#ifndef DEVICE_ID / #define DEVICE_ID 0x..` 를 `main.cpp`(또는 `board_pins.h`)에 두고, platformio.ini에는 `-DDEVICE_ID=` 를 넣지 않는다. ini 플래그는 코드의 `#define`을 조용히 덮어써서 보드가 다른 ID로 올라간다(RF 방에서 0x21 사고). 여러 대를 같은 소스로 만들 때만 ini 플래그를 쓰고, 그때는 코드 값과 반드시 일치시킨다.
- ID를 바꾸면 README/결선도 문서의 ID 표기도 함께 수정한다.

## 4. 검사 프로그램 `rf_remote_tester.exe` (어느 컨트롤러에도 사용 가능)
- 위치(PC): `C:\Users\kekem\Documents\PlatformIO\Projects\LGE_RF_REMOTE\feelax_cc1101_rf8ch_v1_1\tools\rf_remote_tester.exe` (소스 `tools\rf_remote_tester_src`, Go, 설치 불필요, 더블클릭). 다른 방에서 쓰려면 이 exe를 공용 도구 폴더(예: `LGE_DC_27\ESP32C6\tools\`)로 복사.
- 동작: §1의 절차 그대로 스캔 → COM 포트 / Device ID / Note / Last Key / Connect·Disconnect + 로그. 기대 ID 입력은 없다. 화면에서 ID가 맞는지 직접 확인한다.
- Note 열: `OK` = ID 앞뒤 추가 바이트 없음, `Stray bytes (N)` = 앞뒤에 이상한 바이트(로그의 `pre-existing bytes`·`raw response bytes` 확인), `No ID response`, `Open failed`.
- Last Key는 `idx=N, S|L, 0xNN` — 입력 장치(RF/SW)용. LED·POWER 보드는 ID 확인만 의미 있음(LED 효과 명령 테스트는 이 도구에 없음).
- Port 선택: `All ports`(전체 스캔, ID 응답 있는 포트만 표시) 또는 특정 포트(응답이 없어도 행 표시).
- 사용 전: 시리얼 모니터/다른 PC 프로그램이 같은 COM 포트를 잡고 있으면 `Open failed`. 업로드 전에는 검사 창을 닫는다.
- exe는 창을 열어 두면 Windows가 파일을 잠근다 → 새 버전 복사가 막히면 창을 닫고 다시 한다.

## 5. 증상 → 원인 표
| 증상 | 원인 | 조치 |
|---|---|---|
| ID가 0x70 'p' 등 글자, `pre-existing bytes`에 `ESP-ROM:es` | 칩 부팅 문구가 USB로 나옴 (DTR/RTS 리셋) | eFuse 굽기, `usb_disable_autoreset()` 확인 |
| ID가 0x43 'C' 등 글자, 로그에 `FEELAX…`·`[STAT]` 반복 | 개발용(텍스트 로그) 환경이 올라가 있음 | `-e <호스트 환경>` 지정해서 다시 업로드 |
| ID가 설계값과 다름(예: 0x21) | ini `-DDEVICE_ID` 가 코드 값을 덮어씀 | ini 줄 삭제, 코드 한 곳만 |
| `No ID response` | 펌웨어 미동작/다른 환경/포트 선택 오류 | 업로드 환경, LED, 포트 확인 |
| `Open failed` | 다른 프로그램이 COM 포트 점유 | 모니터/EXE 닫기 |
| 새 펌웨어 올렸는데 동작이 그대로 | 환경 미지정 업로드(전 환경 업로드, 마지막이 남음) | `default_envs` + `-e` 지정 |
| 업로드 실패(호스트 모드 보드) | 자동 리셋 비활성 | BOOT+RST로 다운로드 모드 |
| 스캔은 되는데 키 입력 없음(RF 보드) | RF 모듈(CC1101) 통신 불량 — LED 빨강 고정 | 배선 접촉 확인(1.27 mm 핀 접촉 불량이 두 번 원인) |

## 6. 작업 환경 공통 메모
- `pio`가 PATH에 없으면: `& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run -e <env> -t upload --upload-port COMxx`.
- VS Code는 에디터 탭에 열린 파일을 외부에서 바꾸면 되돌린다(platformio.ini, README가 실제로 당함). 파일을 외부에서 고친 뒤에는 탭을 닫았다 열고, 낡은 탭을 저장해 덮어쓰지 않는다. `platformio.ini`는 사용자가 직접 수정/붙여넣기가 가장 안전.
- espefuse에는 `bitstring==4.1.4` 필요(빌드/업로드 후 5.x로 돌아가므로 espefuse 직전에 설치). 확인 문구는 대문자 `BURN` 정확히 입력.
- 케이스 조립 후에는 BOOT를 누르기 어렵다 → 호스트 모드 펌웨어는 처음부터 다운로드 모드 진입 방법(BOOT 접근 또는 소프트 명령)을 확보해 둔다.
- 핀 접촉 불량(작은 피치 모듈, 점퍼)은 증상이 간헐적이므로 최종품은 납땜 또는 고정 커넥터.
- 새 컨트롤러 첫 bring-up 순서: 호스트 환경 업로드 → 검사 프로그램으로 ID/Note 확인 → (필요 시) eFuse → 기능 시험.
