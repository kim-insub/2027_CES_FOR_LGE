# 2027 LG VS CES Digital Cockpit — 시스템 구조 & UART 인터페이스 (v0.3, 2026-10-22)

출처: `LG_VS_CES_Digital_Cockpit_System_Structure_v0.3_20261022.docx` (사용자 작성, 협의용 초안 / 부분 확정). 모든 방(LED / RF / SWITCH / POWER)이 이 문서를 시스템 구조 기준으로 삼는다. 공통 기준·핀·빌드는 `claude/system_common.md`.

**확정 / 제안 / 미확정 구분**
- 확정: ID 조회 0x00, 장치군별 ID 시작값, LED 효과 명령 시 동작 규칙, POWER MASTER–슬레이브 분리 구조.
- 제안: 확장 UART 프레임(AA 55 … CRC16), 상태 레코드 포맷, 비트맵 기반 채널 제어.
- 미확정: 5V 출력 채널 실장, 장치별 실제 I/O 매핑, UART/RS-485 프레임 전환 및 오류·타임아웃 정책.

## 1. 구조 요약
관제 PC가 개별 USB/UART 포트로 LED · 택트 스위치 · RF 제어기를 식별한다. 신규 전원 제어는 **POWER MASTER 한 대(ID 0x40)만** PC와 직접 연결하고, MASTER가 하위 RS-485 슬레이브(ID 0x50~)의 명령 실행과 상태를 취합해 PC로 전달한다. **PC는 POWER SLAVE와 직접 통신하지 않는다.**

| 구성요소 | 역할 | 연결 |
|---|---|---|
| 관제 PC | 감시 대상 PC 전원/통신 상태 판단, 시퀀스 명령 생성, 제어 상태 표시 | USB/UART로 각 제어기 또는 POWER MASTER |
| LED 제어기 | PC 명령 코드에 대응하는 내장 LED 이펙트 재생 | 개별 USB/UART |
| SW 제어기 | 택트 스위치 GPIO HIGH 입력 감지, 키 상태/이벤트 PC 전달 | 개별 USB/UART |
| RF 제어기 | 페어링된 리모컨 키 입력 감지, PC 전달 | 개별 USB/UART |
| POWER MASTER | PC 명령 수신, 하위 슬레이브 폴링·라우팅·상태 집계 | PC: UART / 하위: RS-485 |
| POWER SLAVE | 12V/24V 전원 채널 등 출력 제어, 자체 상태 회신 | MASTER와 RS-485 버스 공유 |

## 2. Device ID 체계
| 장치 | ID | 상태 |
|---|---|---|
| LED | 0x10, 0x11, 0x12 … | 시작값 확정 |
| 택트 스위치(SW) | 0x20, 0x21, 0x22 … | 시작값 확정 |
| RF 리모컨 | 0x30, 0x31, 0x32 … | 시작값 확정 |
| POWER MASTER | 0x40 | 확정 |
| POWER SLAVE | 0x50, 0x51, 0x52 … | 시작값 확정 |

- ID 조회: PC가 각 COM 포트에 **단일 바이트 0x00** 전송 → 제어기가 자신의 1바이트 ID 회신. PC는 [COM 포트 ↔ 장치 ID] 매핑을 유지(COM 번호는 재연결로 바뀔 수 있으므로 ID가 논리 식별자).
- 주의: ID 0x10과 명령 0x10은 다른 계층의 값. ID 조회 0x00과 LED 이펙트 명령 0x00이 섞이지 않도록 식별 단계와 동작 단계를 분리.
- 슬레이브 주소 0x50/0x51/0x52는 PC↔MASTER 프레임의 PAYLOAD에서 대상 노드를 지정할 때 사용. 같은 RS-485 버스 안에서 중복 금지.

## 3. LED 제어기
- ID 조회 후 PC가 0x01, 0x02, 0x03 … HEX 명령으로 이펙트 선택(번호 맵은 후속 정의). 펌웨어에 저장된 해당 효과 실행.
- 새 유효 명령이 없으면 현재 효과를 무한 Loop. 새 명령이 오면 현재 효과를 중지하고 전환, **Fade-Out/Fade-In 기준 0.5초**.
- Loop·Fade는 펌웨어 내부 동작이며 UART 프로토콜 필드에 넣지 않는다.
- 미확정: 0.5초가 전체 전환인지 Out/In 각각인지, 명령 0x00을 이펙트 코드로 쓸지.

## 4. SW / RF 입력 제어기
| 장치 | PC에 보내는 정보 | 확인 사항 |
|---|---|---|
| SW (0x20~) | 택트 스위치 입력 HIGH 또는 키 이벤트, 입력 채널 번호 | GPIO 전기 입력, 디바운스, 누름 유지/1회 이벤트 기준 |
| RF (0x30~) | 리모컨 버튼 입력 이벤트, 키 번호, 필요 시 페어링 상태 | 리모컨 주소/키 매핑, 롱프레스 중복 이벤트 처리 |

HIGH 유지 중 주기적으로 1을 보내는 방식인지, 눌림 순간 1회 이벤트인지는 추후 정의(문서 기준 미확정). 참고: 현재 구현된 RF 8키(`LGE_RF_REMOTE`)는 키 **뗄 때** 1바이트(인덱스 0..7, 0.6초 이상 길게 누르면 +0x80), ID 0x30.

## 5. 신규 전원 제어 흐름 (POWER MASTER / SLAVE)
- PC가 시퀀스에 따라 ON/OFF 명령을 MASTER(0x40)로 송신 → MASTER가 슬레이브 ID 확인 후 RS-485로 대상 슬레이브(0x50~)에 전달 → 슬레이브가 릴레이/출력 채널 동작 → MASTER가 상태를 캐시해 PC에 응답.
- 예시(개념 데이터, 실제 Hex 프레임 아님): `MASTER=0x40 / SLAVE=0x51 / CH=5 / STATE=1`, `MASTER=0x40 / SLAVE=0x50 / CH=2 / STATE=0`, `MASTER=0x40 / GET_ALL_STATUS`.

**상태 항목은 구분해서 관리**
| 상태 | 의미 | 표기 |
|---|---|---|
| 통신 상태 | MASTER↔SLAVE 최근 응답 정상 여부 | ONLINE / OFFLINE / UNKNOWN |
| 제어기 가동 상태 | MCU 구동·부팅 상태 | RUNNING / FAULT / UNKNOWN |
| 채널 지령 상태 | 각 출력에 마지막으로 지시한 ON/OFF | COMMANDED_BITMAP |
| 출력 피드백 | 센서로 실제 전압/출력 확인 시 | MEASURED_BITMAP (센서 있을 때만) |
| 상태 신선도 | 마지막 갱신 시간 | AGE_MS / LAST_UPDATE |

오프라인 슬레이브는 실제로 ON을 유지 중일 수 있다 → "OFF"와 "통신 불량/상태 미확인"을 같은 값으로 처리하지 않는다. 보드 전원 OFF는 자력 보고가 불가하므로 MASTER 타임아웃 감지 또는 별도 센서 필요.

## 6. 출력 채널 비트맵
1채널=1비트, 가변 길이 `ceil(N/8)` 바이트. Bit0=CH1, Bit1=CH2 …, 0=OFF 1=ON, **LSB-first** (예: CH1·CH6 ON → 0x21, 32채널이면 `[0x21,0x00,0x00,0x00]`). 4ch=1B, 12ch=2B, 32ch=4B, 64ch=8B.

전압 그룹: 24V 4채널(J01~J04, 검토 중), 12V 4채널(J05~J08, 검토 중), 5V 4채널은 **채널 확장 설명용 예시**이며 독립 스위칭인지(Buck 공용인지) 확인 전에는 실장 사양으로 확정하지 않는다. 실제 J01~J08 ↔ 논리 CH 번호 연결은 미확정.

## 7. PC ↔ MASTER UART 프레임 (설계 제안, 현재 1바이트 ID/LED 명령 방식의 즉시 대체는 아님)
`SOF(AA 55) | VER(01) | SEQ(1) | CMD(1) | DEVICE_ID(1) | LEN(2, uint16 LE) | PAYLOAD(N) | CRC16(2, CRC16-MODBUS, VER~PAYLOAD, LE)` — 고정 오버헤드 10바이트 + N.

| CMD | 이름 | 내용 |
|---|---|---|
| 0x00 | ID_QUERY (구형 단독) | 1바이트 0x00; 프레임 명령과의 공존 정책 필요 |
| 0x10 | SET_OUTPUT | SLAVE ID, 채널, ON/OFF 또는 MASK+VALUE |
| 0x20 | GET_ALL_STATUS | MASTER가 캐시한 전체 슬레이브 상태 요청 |
| 0x21 | ALL_STATUS | 슬레이브별 ID / COMM / CHANNEL_COUNT / BITMAP / FAULT |
| 0x7F | RESULT | 처리 결과 코드 + 원 요청 SEQ |

상태 레코드(제안): `SLAVE_COUNT(1)` + 반복 `{SLAVE_ID(1), COMM_STATUS(1), CHANNEL_COUNT(2), OUTPUT_BITMAP(ceil(n/8)), FAULT_FLAGS(1), AGE_MS(2 또는 4)}`. 개별 채널 제어는 기존 비트를 보존하도록 MASK+VALUE 권장. ACK(적용 여부)와 실제 출력 전압 확인은 별도 상태로 명시.

## 8. 전송 속도 / 운영
| 구간 | 설정 | 형태 |
|---|---|---|
| PC ↔ 기존 제어기 | USB-UART 115200 8N1 | 전이중 |
| PC ↔ POWER MASTER | USB-UART 115200 8N1 | 전이중 |
| MASTER ↔ SLAVEs | RS-485 2선식, 115200은 초기 검토값 | 순차 폴링, 반이중 |

115200 8N1 ≈ 11,520 B/s (32채널 상태 + 10B 오버헤드 전송 ≈ 1.22 ms). 실제 지연에는 드라이버, RS-485 방향 전환, 슬레이브 응답, 타임아웃/재전송, 집계 시간이 추가. 전체 상태 갱신 50~100 ms는 시작 목표일 뿐 노드 수에 따라 실측·조정.

## 9. 예외 처리 / 안전 원칙
- MASTER는 슬레이브를 **주소 기반 순차 폴링**(동시 응답 방지).
- 통신 타임아웃, CRC 오류, 미존재 채널, 중복 SEQ의 거절·재시도 규칙 정의. 같은 ON/OFF 명령을 다시 받아도 토글이 일어나지 않게(멱등).
- 통신 두절 시 마지막 출력 유지 vs 전체 안전 OFF는 별도 확정(전시 시퀀스·전원 장치·안전 요구 기준).
- 채널 전원 ON ≠ 장비 정상 동작. 출력 검출 센서가 없으면 "명령을 적용했다"만 확인 가능.
- MASTER 리셋 / USB COM 재연결 시에도 ID 조회와 상태 재동기화 절차를 제공.

## 10. 후속 확정 필요 항목
| 우선 | 안건 | 결정 사항 |
|---|---|---|
| P1 | ID 핸드셰이크 | 0x00 직후 1바이트 응답 유지, 통신 모드 전환 방식 |
| P1 | UART 프레임 | SOF/CRC/SEQ/CMD/응답 시간/최대 payload/재시도 |
| P1 | POWER 채널 정의 | 슬레이브당 실제 24V·12V·5V 채널 수와 보드 핀/커넥터 매핑 |
| P1 | 출력 Fail-safe | 통신 두절 시 출력 유지/차단 정책 |
| P2 | LED 이펙트 번호 | 각 Hex 명령 ↔ 저장 효과, 0x00 사용 정책 |
| P2 | LED Fade | 0.5초 전체 전환 vs Out/In 각각 |
| P2 | SW / RF 보고 | 채널 수, 눌림/해제, 중복 방지, 이벤트 vs 상태 보고 |
| P2 | 상태 수집 | 슬레이브 수, 폴링 주기, 피드백 센서 유무 |

## 11. 현재 구현과의 대응 (참고, 2026-10-23)
- LED(0x10~): 기존 `kiosk_neo_*` 등. SW(0x20~): `kiosk_tacts`, `kiosk_indev`. RF(0x30~): `kiosk_rfkey*`, 신규 CC1101 8키 `LGE_RF_REMOTE`(ID 0x30 적용).
- POWER: 기존 `kiosk_ble_sw`(BLE, 24V×4/12V×4/RELAY×5)와 이 문서의 POWER MASTER(0x40)/SLAVE(0x50~, RS-485)의 관계는 **미정** — POWER 방에서 정리 필요.
- 이 문서의 UART 프레임(AA 55 …)은 제안 단계이며 PC 쪽 프로그램은 수정 불가이므로(§system_common 1), 기존 1바이트 프로토콜을 깨지 않는 범위에서만 신규 MASTER에 적용 가능.
