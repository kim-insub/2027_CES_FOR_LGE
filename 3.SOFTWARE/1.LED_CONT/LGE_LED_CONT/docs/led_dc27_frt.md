# LED 방 — DC27_FRT 통합 펌웨어 + led_tester.exe (2026-10-24)

LED 방 작업의 기준 문서. 공통 사항은 `claude/system_common.md`, `claude/host_protocol_and_tester.md`, `claude/system_architecture_v0.3.md` 를 따르고, 여기에는 **LED 방에서만 정한 것**을 적는다.
**작업 폴더(PC): `C:\Users\kekem\Documents\PlatformIO\Projects\LGE_LED_CONT`** — 폴더의 `CLAUDE.md`(VS Code의 Claude가 자동으로 읽음)에 같은 내용 + 최종 파일 목록 + 작업 기록이 있다. 아래 §0 규칙에 따라 **LED 작업은 항상 폴더의 최종본을 먼저 확인하고 시작한다.**

## 0. 최종본 확인 · 동기화 규칙 (사용자 요청, 2026-10-24)
- **LED 작업의 최종본은 PC 폴더의 실제 파일**이다. 폴더의 `CLAUDE.md`가 최종 파일 목록(SHA-256 앞 12자리)과 작업 기록을 갖고 있고, 이 프로젝트 문서는 그 **사본**이다. 서로 다르면 폴더가 맞다.
- **Claude 앱(이 방)에서 LED 요청을 받으면 먼저**: ① 폴더를 읽어 `CLAUDE.md`의 작업 기록을 보고 ② 실제 파일을 가져와 해시를 목록과 비교한다. 다르면(VS Code의 Claude나 사용자가 직접 고친 것) 그 내용을 읽고 작업 기록·최종 파일 목록·이 문서에 반영한 뒤에 작업한다. 같으면 그대로 작업한다.
- **VS Code의 Claude도 작업 전에 같은 확인을 하고**, 고친 뒤 `CLAUDE.md`의 해시와 작업 기록을 갱신한다(폴더의 `CLAUDE.md`에 절차가 적혀 있다).
- 작업을 마치면 고친 파일, 해시, 작업 기록, (이 방에서는) 이 문서를 같은 작업 안에서 갱신한다. 폴더를 지켜보는 상시 감시는 할 수 없으므로 **요청을 받을 때 확인하는 방식**이다.

## 1. 확정된 결정 (사용자)
- 프로젝트명 **DC27_FRT**. 작년 `kiosk_neo_frt`(Nano → C6 이식본, 실보드 확인) 기능을 그대로 새 체계로 옮긴다. **이펙트 기능은 작년과 동일.**
- LED DEVICE_ID는 **0x10부터 +1씩** (0x10, 0x11, 0x12 …). **작년 0x01 = 새 0x10** (확정).
- 0x10/0x11 코드는 **통합 코드 하나**로 만들고, DEVICE_ID 값에 따라 LED 이펙트를 구분해 출력한다.
- 작년 시나리오가 갱신되면 사용자가 파일을 주고, **펌웨어 코드와 검증용 exe를 함께** 반영한다.
- 원본 `ESP32C6\kiosk_neo_frt` 폴더는 건드리지 않는다(작년 기준 원본).

## 2. 구조 (펌웨어)
- `DEVICE_ID`(PC에 응답하는 값)와 `FX_TYPE`(작년 ID 0x01~0x04, 이펙트/LED 수 구분)를 나눴다. `src/config.h`의 표가 대응을 정하고, `neo_effect.cpp`는 `DEVICE_ID ==` 비교 36곳만 `FX_TYPE ==` 로 바꿨다. 이펙트 로직·색·시간은 작년과 동일(`colors.h`, `neo_effect.h`, `neo_effect_config.h`, `board_pins.h`, `usb_noreset.h`는 작년 파일 그대로).
- `main.cpp`: 작년 그대로(프로토콜 주석만 갱신). `0x00` → ID 1바이트, `0x01~0x28` 이펙트 명령, `0xF0 0xA5 0x5A` → ROM 다운로드 모드.
- **전원 인가 표시(2026-10-25)**: 부팅 직후 PC 미연결이면 스카이블루(0x0B와 같은 색) 디밍 온/오프 반복(0.5 s 업 + 0.5 s 다운). PC에서 첫 바이트(스캔 `0x00` 또는 이펙트 명령)를 받으면 소등, 재부팅 전까지 다시 켜지지 않음. `neo_effect_host_seen()`(main.cpp 수신 루프), 색 `COLOR_BOOT_WAIT`, 주기 `NEO_CFG_BOOT_WAIT_PERIOD_MS`. DTR은 판단에 안 씀(PC 프로그램이 스캔 때 DTR off). exe 변경 없음.
- LED 데이터 핀 GPIO6. 호스트 링크는 네이티브 USB(`CDC_ON_BOOT=1`). 시리얼에 ID 외 출력 없음.

| DEVICE_ID | FX_TYPE(작년 ID) | 구성 | LED 수 | 상태 |
|---|---|---|---|---|
| 0x10 | 0x01 | f150x01 | 180 | **확정** |
| 0x11 | 0x02 | f180x02 | 180 | 가정(작년 번호 순서) |
| 0x12 | 0x03 | f180x03 | 150 | 가정 |
| 0x13 | 0x04 | f8x04 | 8 | 가정 |
→ 0x11~0x13이 어느 구성인지는 **사용자 확인 필요.** 바꾸려면 `config.h` 표의 `FX_TYPE` 줄과 `tools\led_tester_src\ui.html`의 `NEW_ID`를 함께 수정.

## 3. 이펙트 명령 (0x01~0x28, ID별)
- 공통: 0x01·0x06 OFF, 0x03~0x05·0x0B~0x0F 정지색 페이드(0.5 s), 0x28 그라데이션 흐름(전 ID 공통).
- 0x02: 0x10/0x11/0x12는 정지색, 0x13은 숨쉬기 깜빡임. 0x07~0x0A: 0x10/0x11은 색 페이드, 0x12는 FIRE/TINDLE 환경, 0x13은 페이드인-유지-아웃.
- 0x10·0x11 전용: 0x16~0x19 색 깜빡임, 0x1A 정지색, 0x23 정지색, 0x24·0x25 색 깜빡임.
- 0x12 전용: 0x11, 0x12 MUSIC, 0x1B~0x22, 0x26, 0x27.
- 0x13 전용: 0x13~0x15 SEQ. 0x10 은 어느 ID에도 없음. 정의 없는 명령은 보드가 무시한다.
- 상세 표는 폴더의 `README.md`, 색 값은 `src/colors.h`.

## 4. 빌드 · 업로드
- 환경: `dc27_frt_10`, `_11`, `_12`, `_13` (ID마다 하나). `default_envs = dc27_frt_10`. **업로드는 항상 `-e` 지정**: `pio run -e dc27_frt_10 -t upload --upload-port COMxx` (`pio`가 PATH에 없으면 `& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run -e … `).
- **공통 규칙 §3.6(ID는 한 곳)의 예외를 쓴다**: 같은 소스로 여러 ID를 만들기 때문에 ini 환경별 `-DDEVICE_ID=0x1x` 를 쓰고, 코드 기본값(`config.h` 0x10)은 기본 환경(`dc27_frt_10`)과 항상 같게 둔다. `-DDEVICE_ID`를 다른 곳(코드의 중복 `#define`)에 또 두지 않는다. 지원하지 않는 ID는 컴파일 오류(`#error`).
- 작년 ini의 `kiosk_neo_frt_uart0`(외부 USB-UART 어댑터 환경)은 뺐다. 필요하면 되살린다.
- 호스트 모드라 자동 리셋이 꺼져 있다: 업로드는 BOOT+RST, 또는 `F0 A5 5A`. 새 보드는 eFuse(`tools\burn_silent_boot.bat COMx`, 보드당 1회) → 업로드 → 검증 프로그램으로 ID 확인.

## 5. 검증 프로그램 `tools\led_tester.exe`
- 위치: `LGE_LED_CONT\tools\led_tester.exe` (소스 `tools\led_tester_src`, Go, 설치 불필요, 더블클릭). RF 방 `rf_remote_tester.exe`와 같은 방식, 화면 구성은 작년 PC 조명 프로그램(`LGE Lighting Controller for CES`) 기준.
- 스캔: 작년 PC 프로그램과 같은 절차(open → DTR/RTS off → 1 s → read 10 → `0x00` → read 4). 목록 열: Sel / COM Port / Device ID / Note / Config(FX 구성, LED 수) / Effect+Send / Last Effect / Connect.
- 이펙트 명령표 0x01~0x28(해당 ID에서 유효한 것만 진하게, 흐린 것도 보내서 무시 확인 가능), Auto-run, Send to Selected / All connected, ID request, Enter bootloader(`F0 A5 5A`), CLOSE PORTS. Effect 값은 16진수.
- 이펙트 명령표는 `ui.html`의 `CMDS` / `FX` / `NEW_ID` 에 들어 있다 → **펌웨어 이펙트를 바꾸면 여기도 같이 고친다.**
- 다시 빌드: `tools\led_tester_src`에서 `go build -ldflags "-H=windowsgui -s -w" -o ..\led_tester.exe .` (exe가 실행 중이면 파일이 잠기므로 창을 닫는다.)

## 5-1. BLE 무선 펌웨어 업데이트 + BLE 검사 (2026-10-26, 10-29 변경)
- 보드: `src/ble_ota.cpp/.h`. **BLE 는 USB 가 빠져 있을 때만** (USB SOF 2 초 없음) `LED 0x10 DC27_FRT` 로 광고. **USB 우선**: USB 가 돌아오면 BLE 끊고 전송 중 업데이트 중단. PC 의 연속 통신(USB)은 기존과 동일하게 처리.
- PIN `BLE_OTA_PIN`(현재 0000, 3회 오류 시 잠김). BLE 로도 USB 와 같은 1바이트 명령(0x00 ID, 0x01~0x28) 수신(PIN 인증 후) → 업데이트 직후 USB 없이 검사 가능.
- 업데이트: 4 KB 블록 + ACK, SHA-256 + ESP 이미지 검사 + bin 태그(`DC27FW|LED|0x10|1.0|`) 종류/ID 확인 후 슬롯 전환, 실패 시 기존 유지, setup() 전 크래시 시 롤백.
- PC: **`tools\led_tool.exe`** = led_tester + BLE SEARCH/Connect + Browse(bin) + UPDATE FIRMWARE + 업데이트 후 자동 재연결·버전 확인. 이후 같은 화면에서 이펙트 검사.
- 빌드하면 `bin\LED_0x1x_v<FW_VERSION>.bin` 자동 생성 (`tools/bin_copy.py`).
- 처음 한 번은 USB 업로드 필요. USB 프로토콜·DEVICE_ID·eFuse 무음 부팅은 그대로.
- 자세한 내용: README.md "BLE 무선 펌웨어 업데이트 + BLE 검사".

## 6. 검증 상태 (2026-10-25)
- 확인: PC 시뮬레이션(가짜 Arduino/NeoPixel)으로 4개 ID — LED 수, ID 1바이트 응답, ID별 이펙트 구분/무시, OFF, 매직 바이트 소비. PC에서 `dc27_frt_10` 빌드 성공(`firmware.bin` 생성). **실보드 0x10 (COM7)**: led_tester.exe 스캔에서 ID 0x10, Note OK, Config FX1·f150x01·180 LEDs, 이펙트 명령 전송 동작.
- 전원 인가 표시: PC 시뮬레이션 4개 ID 통과. **PC 빌드·실보드 육안 확인 전.**
- **미확인**: 0x11~0x13 실보드, LED 육안 동작 결과, `F0 A5 5A` 다운로드 모드(공통 문서 §6과 동일하게 실보드 검증 전), led_tester.exe의 Auto-run·Enter bootloader 실사용.
- BLE 업데이트/검사(2026-10-29 USB 우선 버전): 클라우드에서 4개 ID 실빌드 성공(약 756 KB / 1.25 MB), led_tool.exe 빌드·화면 확인. **실보드 BLE(USB 분리 감지, 전송, 재연결 검사) 확인 전.**
- 나머지 LED 펌웨어(`kiosk_neo_rear`, `vision_buck_a/b/c`, `cxr_buck`, `cxr_puron`)는 이 통합 프로젝트에 아직 포함되지 않았다. 통합 대상/ID 배정은 미정.

## 7. 열린 항목
0. (해결 2026-10-25) `[env:dc27_frt_10]`의 `-DDEVICE_ID=0x11`은 `0x10`으로 원복된 것을 확인.
1. 0x11~0x13의 구성(위 §2 표) 확정.
2. 작년 시나리오 갱신 파일 도착 시 펌웨어 + led_tester.exe + README 동시 반영.
3. 다른 LED 펌웨어(rear, vision buck, cxr)를 0x1x ID로 어떻게 묶을지.
4. PC 조명 프로그램(별도 제작 예정)의 존 매핑 — 새 ID 체계에서 어떻게 할지(작년은 Vision Buck ID 1,2,3 → 존 A,B,C / Smart Kiosk ID 1,2 → F,R).
