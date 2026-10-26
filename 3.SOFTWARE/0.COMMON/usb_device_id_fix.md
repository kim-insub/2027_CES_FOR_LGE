# ESP32-C6 USB: Device ID가 0x70 등으로 읽히는 문제 — 원인과 해결

(2026-10-16, SuperMini C6 실측 확인)

## 증상
검사용 EXE(원본 `lge_lighting_control_for_ces` 등)가 스캔할 때 Device ID를 0x70, 0x07 등 엉뚱한 값으로 표시.

## 원인
- 네이티브 USB(USB-Serial/JTAG)는 PC가 COM 포트를 열 때(DTR/RTS) 칩이 재시작되고, ROM이 `ESP-ROM:esp32c6-…` 부팅 문구를 USB 시리얼로 출력.
- EXE는 포트 오픈 → 1초 대기 → 10바이트 읽고 → `0x00` 전송 → 4바이트 읽기 순서라 부팅 문구의 글자(`p`=0x70)를 ID로 읽음. 로그: `pre-existing bytes 45 53 50 2d 52 4f 4d 3a 65 73` (= "ESP-ROM:es"), `raw response 70 33 32 63`.
- PC 프로그램은 수정할 수 없으므로 보드 쪽에서 해결해야 함.

## 해결
- eFuse `DIS_USB_SERIAL_JTAG_ROM_PRINT` = 1 (칩당 1회, 되돌릴 수 없음). 적용 후 스캔 `ID=0x01`, 응답 `01` 한 바이트.
- 스크립트: `ESP32C6\tools\burn_silent_boot.bat COMx` (PlatformIO의 espefuse 사용, `bitstring==4.1.4` 필요 — 빌드/업로드 후 5.x로 되돌아가므로 espefuse 직전에 설치).
- 코드의 `usb_disable_autoreset()` (USB_SERIAL_JTAG_CHIP_RST_DIS)는 이 보드에서 효과 미확인.

## 함께 겪은 문제
- `kiosk_neo_frt`는 환경이 2개(`kiosk_neo_frt`, `kiosk_neo_frt_uart0`). 환경 미지정 업로드는 둘 다 올려 마지막의 uart0 펌웨어가 남음 → USB 응답 없음(`Write timeout`, `No ID response`). `[platformio] default_envs = kiosk_neo_frt`로 고정.
- 업로드 중 EXE/모니터가 COM 포트를 잡고 있으면 `액세스가 거부되었습니다`로 실패.

## TEST_CONT
포트 연결/해제(Disconnect/Connect)를 추가한 `_r3` EXE 3종(조명, RF-KEY, Input Device) — 포트 여는 방식(DTR/RTS 등)은 원본과 동일. 가짜 장치 테스트만 수행, 실제 Windows/보드에서의 전체 확인은 조명 프로그램만 스캔 확인.
