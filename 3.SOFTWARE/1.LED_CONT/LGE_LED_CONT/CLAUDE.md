# LGE_LED_CONT — LED 방 (DC27_FRT)

LGE_DC_27 프로젝트의 LED 컨트롤러 펌웨어(ESP32-C6 SuperMini, PlatformIO)와 검증용 프로그램. 작년 `kiosk_neo_frt`(실보드 확인본)의 기능을 새 DEVICE_ID 체계(0x10~)로 옮긴 통합 코드다. 사용자는 한국어로 작업한다.

## 최종본 확인 (작업 시작 전에 반드시, Claude 앱·VS Code 양쪽 공통)
이 폴더의 **파일 자체가 최종본**이다. 대화 기억이나 프로젝트 문서보다 폴더의 실제 파일을 믿는다.
1. 이 파일의 "작업 기록"과 "최종 파일 목록"을 읽는다.
2. 실제 파일의 해시를 구해 "최종 파일 목록"과 비교한다.
   PowerShell(폴더에서): `Get-ChildItem -Recurse -File platformio.ini,README.md,include,src,tools | Where-Object { $_.FullName -notmatch '\\\.pio\\' } | ForEach-Object { '{0}  {1}' -f (Get-FileHash $_.FullName -Algorithm SHA256).Hash.Substring(0,12).ToLower(), $_.FullName.Substring((Get-Location).Path.Length+1) }`
3. **같으면** 최종본 그대로 작업한다. **다르면** 기록되지 않은 변경이 있다는 뜻이다. 바꾸기 전에 달라진 파일의 내용을 먼저 읽고(누가 왜 바꿨는지 사용자에게 필요하면 확인), 그 변경을 "작업 기록"에 `(미기록 변경 반영)` 으로 적고 "최종 파일 목록"을 갱신한 다음 작업을 시작한다. 달라진 파일을 옛 내용으로 덮어쓰지 않는다.
4. 작업이 끝나면: 고친 파일, 해시, 작업 기록을 같은 작업 안에서 갱신한다(아래 "변경하면 반드시").
5. Claude 앱 쪽에서 쓰는 프로젝트 문서 `claude/led_dc27_frt.md`는 이 파일의 **사본**이다. 서로 다르면 이 파일(폴더)이 맞다. 프로젝트 문서를 고칠 수 있는 쪽(앱)이 이 파일의 변경을 따라 갱신한다. 프로젝트 문서에 접근할 수 없는 쪽은 이 파일만 갱신하면 된다.

## 시작할 때 (처음 읽는 순서)
1. 위 "최종본 확인"을 한다.
2. `README.md`(ID 표, 이펙트 명령 표, 빌드/업로드, bring-up 순서)를 읽는다.
3. Claude 프로젝트 "LGE_DC_27"의 문서(`claude/led_dc27_frt.md`, `system_common.md`, `host_protocol_and_tester.md`, `system_architecture_v0.3.md`)에 접근할 수 있으면 읽는다. 접근할 수 없으면 사용자에게 필요한 문서를 붙여 달라고 한다. 이 파일과 README만으로도 LED 작업은 가능하다.

## 확정된 결정
- LED DEVICE_ID는 0x10부터 +1. **작년 0x01 = 새 0x10.** 0x11~0x13의 구성은 아직 "작년 번호 순서" 가정(config.h 표) — 사용자가 확정하기 전에는 임의로 바꾸지 않는다.
- **이펙트 로직·색·시간은 작년과 동일하게 유지한다.** 사용자가 새 시나리오 파일을 줄 때만 바꾼다.
- 원본 `..\LGE_DC_27\ESP32C6\kiosk_neo_frt`는 수정하지 않는다.

## 구조
- `src/config.h`: `DEVICE_ID`(PC에 응답) → `FX_TYPE`(작년 ID 0x01~0x04) / `LED_COUNT` 대응 표. `neo_effect.cpp`는 `FX_TYPE`으로 이펙트를 구분한다.
- `platformio.ini`: 환경 `dc27_frt_10/11/12/13`, 각각 `-DDEVICE_ID=0x1x`. `default_envs = dc27_frt_10`.
- `include/board_pins.h`: LED 데이터 GPIO6 (핀은 여기 한 곳). `include/usb_noreset.h`: DTR/RTS 자동 리셋 차단 + `F0 A5 5A` 다운로드 모드.
- `tools/led_tester.exe`, 소스 `tools/led_tester_src` (이펙트 명령표는 `ui.html`의 `CMDS`/`FX`/`NEW_ID`).

## 전원 인가 표시 (2026-10-25 사용자 요청)
- 전원 인가 직후 PC 미연결이면 스카이블루 디밍 온/오프 반복(0.5 s 업 + 0.5 s 다운). PC에서 첫 바이트를 받으면 소등(`neo_effect_host_seen()`, main.cpp의 수신 루프에서 호출). 재부팅 전까지 다시 켜지지 않는다. 색 `COLOR_BOOT_WAIT`(colors.h), 주기 `NEO_CFG_BOOT_WAIT_PERIOD_MS`(neo_effect_config.h). DTR은 쓰지 않는다(PC 프로그램이 스캔 때 DTR을 끔).

## 반드시 지킬 규칙
- 호스트 프로토콜(115200 8N1, USB-CDC): `0x00` → **DEVICE_ID 1바이트만** 응답. 시리얼에 로그/텍스트를 출력하지 않는다. PC 쪽 프로그램은 수정할 수 없으므로 모든 해결은 보드(펌웨어) 쪽에서 한다.
- **업로드는 항상 환경을 지정**: `pio run -e dc27_frt_10 -t upload --upload-port COMxx`. 환경 없이 올리면 전부 올라가 마지막 환경이 남는다. (`pio`가 PATH에 없으면 `& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run -e … `)
- `DEVICE_ID`는 platformio.ini의 환경별 플래그가 정한다(같은 소스로 여러 ID를 만드는 예외). `config.h`의 기본값(0x10)은 기본 환경과 항상 같게 둔다. 코드 안에 `DEVICE_ID`를 다시 `#define`하지 않는다.
- 업로드 전에 검증 프로그램/시리얼 모니터를 닫는다(COM 포트 점유). 자동 리셋이 꺼져 있어 업로드는 BOOT+RST 또는 `F0 A5 5A`.
- 새 보드 순서: eFuse(`burn_silent_boot.bat COMx`, 보드당 1회, 되돌릴 수 없음 — 반드시 사용자에게 확인) → `-e` 지정 업로드 → `tools\led_tester.exe` 스캔으로 ID/Note 확인 → 이펙트 확인.
- 3.3 V IO(5 V 비내성). WS2812 데이터는 레벨시프터 권장.
- VS Code는 열려 있는 탭의 파일을 외부 변경 시 되돌릴 수 있다. 외부에서 고친 파일은 탭을 닫았다 연다. 낡은 탭을 저장해 덮어쓰지 않는다.

## 이펙트나 ID를 바꿀 때 같이 고칠 곳
| 바꾸는 것 | 고칠 파일 |
|---|---|
| 이펙트 동작/색/시간 | `src/neo_effect.cpp`, `src/colors.h`, `src/neo_effect_config.h` + `tools/led_tester_src/ui.html`(`CMDS`) + `README.md` 이펙트 표 + exe 다시 빌드 |
| ID ↔ 구성 대응, LED 수 | `src/config.h`, `platformio.ini`(환경), `tools/led_tester_src/ui.html`(`FX`, `NEW_ID`) + `README.md` ID 표 + exe 다시 빌드 |
| 핀 | `include/board_pins.h` + README (+ 공통 핀 표 `ESP32C6/README.md`) |
- exe 다시 빌드(Go 필요): `tools\led_tester_src`에서 `go build -ldflags "-H=windowsgui -s -w" -o ..\led_tester.exe .` (exe가 실행 중이면 창을 닫는다.)

## 변경하면 반드시
- 컴파일 확인(`pio run -e dc27_frt_10`와 필요한 ID 환경, 가능하면 4개 ID 모두).
- 위 표의 "같이 고칠 곳"을 빠뜨리지 않았는지 확인(펌웨어 ↔ exe ↔ README).
- **"최종 파일 목록"의 해시를 새 값으로 갱신하고, "작업 기록"에 한 줄 추가**(날짜, 어디서, 무엇을, 확인한 것). 이 파일 자신은 해시 목록에 넣지 않는다.
- 문서와 파일이 어긋난 채로 작업을 끝내지 않는다.

## 최종 파일 목록 (SHA-256 앞 12자리, 마지막 갱신: 2026-10-25, 확인한 쪽: Claude 앱 — PC 폴더의 실제 파일을 읽어 계산)
| 파일 | 해시 |
|---|---|
| `platformio.ini` | `a8d91576fad3` |
| `README.md` | `cd8a841f67cb` |
| `include/board_pins.h` | `66aaf9842d6f` |
| `include/usb_noreset.h` | `7b571853a92f` |
| `src/colors.h` | `1ca0dca525a8` |
| `src/config.h` | `63e99297b3b5` |
| `src/main.cpp` | `ad3ad6e34763` |
| `src/neo_effect.cpp` | `e3a2cb9feb2e` |
| `src/neo_effect.h` | `ffa77d83cd89` |
| `src/neo_effect_config.h` | `8323a36a6cbd` |
| `tools/led_tester.exe` | `7754f8c490a4` |
| `tools/led_tester_src/ui.html` | `a86f7fb8a0f2` |
| `tools/led_tester_src/main.go` | `b68f0c8f48f8` |
| `tools/led_tester_src/gui_windows.go` | `473778d54bbf` |
| `tools/led_tester_src/console_other.go` | `5d71a8edbf8c` |
| `tools/led_tester_src/console_windows.go` | `20da136eee9e` |
| `tools/led_tester_src/go.mod` | `3344f06c5972` |
| `tools/led_tester_src/go.sum` | `f643a5e49304` |
- `.pio`, `.vscode`, `.gitignore`는 빌드 산출물/편집기 설정이라 제외. 이 `CLAUDE.md`도 제외. PowerShell 출력의 경로 구분자는 `\`, 위 표는 `/` 이다(해시만 비교).
- exe(`tools/led_tester.exe`)는 소스(`led_tester_src`)와 짝이다. 소스를 고쳤는데 exe를 다시 빌드하지 않았거나 그 반대이면 어긋난 것이다.

## 검증 상태 (2026-10-25)
- 확인: PC 시뮬레이션 4개 ID, PC 빌드(`dc27_frt_10`), 실보드 0x10(COM7) 스캔 OK + led_tester.exe 이펙트 전송.
- 미확인: 0x11~0x13 실보드, LED 육안 결과, `F0 A5 5A` 다운로드 모드, 나머지 LED 펌웨어(kiosk_neo_rear, vision_buck_*, cxr_*)는 이 프로젝트에 미포함.

## 작업 기록
형식: `날짜 (어디서) — 무엇을 바꿨나 / 확인한 것`. 새 항목은 맨 아래에 추가.
- 2026-10-23 (Claude 앱) — 작년 kiosk_neo_frt 소스를 DC27_FRT로 통합: DEVICE_ID 0x10~0x13 → FX_TYPE 대응(config.h), ini 환경 4개, README. 4개 ID PC 시뮬레이션 통과.
- 2026-10-23 (Claude 앱) — tools/led_tester.exe 추가(작년 PC 조명 프로그램 화면 기준, 이펙트 명령표 0x01~0x28, Auto-run, ID 요청, 부트로더 명령, CLOSE PORTS).
- 2026-10-24 (사용자 확인) — PC에서 dc27_frt_10 빌드 성공, 0x10 보드(COM7)를 led_tester.exe로 스캔: ID 0x10, Note OK, FX1·f150x01·180 LEDs. 이펙트 명령 전송 확인.
- 2026-10-24 (Claude 앱) — 이 CLAUDE.md와 프로젝트 문서 claude/led_dc27_frt.md 추가(앱과 VS Code가 같은 기준으로 작업하도록).
- 2026-10-24 (Claude 앱) — 작업 시작 전 "최종본 확인"(해시 비교)과 변경 후 갱신 규칙, 최종 파일 목록 추가. PC 폴더의 실제 파일 18개가 Claude 앱이 넣은 것과 모두 같음을 확인(VS Code 쪽 코드 변경 없음).
- 2026-10-25 (Claude 앱) — (미기록 변경 반영) platformio.ini 해시가 목록과 달랐음: 10-24에 `[env:dc27_frt_10]`이 `-DDEVICE_ID=0x11`로 바뀌어 있던 것이 사용자에 의해 `0x10`으로 원복된 상태로 확인. 현재 4개 환경 모두 이름과 ID 일치, default_envs = dc27_frt_10.
- 2026-10-25 (Claude 앱) — 전원 인가 표시 추가: 부팅 직후 스카이블루 디밍(0.5 s 업/0.5 s 다운), PC 첫 바이트 수신 시 소등. 고친 파일: src/main.cpp, src/neo_effect.cpp, src/neo_effect.h, src/neo_effect_config.h, src/colors.h, README.md. exe 변경 없음. 확인: PC 시뮬레이션 4개 ID(디밍 주기, 0x00 응답 1바이트 + 소등, 이후 이펙트·ID 요청 영향 없음). PlatformIO 실빌드는 Claude 앱 환경에서 못 함 → PC에서 빌드/실보드 확인 필요.
