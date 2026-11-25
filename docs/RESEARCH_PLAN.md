# 연구 계획 (연구 트랙)

제품 트랙과 분리되어 진행되며, **검증된 구조와 계수만** 공통 DSP 코어로 넘어갑니다. 연구가 실패해도 제품은 계속됩니다.

## 목표
실제 아날로그 회로(또는 SPICE 모델)의 특성을 AI로 학습하되, 최종 제품에서는 신경망 없이 실행되는 경량 DSP
(계수, 물리 파라미터, LUT, 보간 함수)를 만든다 — 가칭 APME(Adaptive Physical Modeling Engine).

## 데이터 원천 (구분 엄수)

| 종류 | `data_kind` | 현재 |
|---|---|---|
| SPICE 시뮬레이션 | `spice_simulation` | ✅ 피드백 클리퍼 DRIVE 5점 × 진폭 3, 다이오드 클리퍼, 연산증폭기, 트랜지스터 퍼즈, 삼극관 단 넷리스트 |
| 하드웨어 측정 | `hardware_measurement` | ❌ 없음 (Jan Ray 복각, Dualist 복각 측정 예정 — 해당 개체 자체가 기준) |
| 사용자 녹음(오프라인) | `offline_recording_user_supplied` | ❌ 없음 |
| 상용 디지털 비교 대상 | UAFX 1176 / Dream 65 / Del-Verb | 아날로그 정답으로 사용하지 않음 |

## 단계와 상태

| ID | 항목 | 상태 | 근거 |
|---|---|---|---|
| R-001 | 측정·분석 프레임워크 | 구현(하드웨어 미검증) | tools/measurement, tools/audio_analysis |
| R-002 | SPICE 환경 | PASS | E-004, research/spice |
| R-003 | Python/C++ parity | PASS | E-005 |
| R-004 | DDSP 자동 계수 학습 | PARTIAL — loss 설정 파일·Envelope loss·train/val/test·지연 정렬 추가(E-016), 정확도 개선 없음 | E-006, E-016 |
| R-005 | Neural Gray-box | NOT IMPLEMENTED | E-006이 필요성 제시: 피드백 클리핑 구조 + 잔차 소형 신경망 |
| R-006 | 노브 매핑 LUT | PASS(SPICE), 방법 비교 완료 → 구간 선형 유지 | E-007, E-017 |
| R-011 | SPICE 부품 공차 스윕 | PASS(시뮬레이션) | E-018 |
| R-007 | 앨리어싱 억제(ADAA) | NOT IMPLEMENTED — Custom Drive v2 해석적 곡선으로 준비됨(K-001) | E-002, E-013 |
| R-008 | Active Learning | NOT IMPLEMENTED | — |
| R-009 | DSP Architecture Search | NOT IMPLEMENTED (타깃 하드웨어 미정) | 수동 후보 비교(E-003)만 수행 |
| R-010 | 소자 단위 신경망 / 범용 회로 설계 플랫폼 | 장기 후보(범위 외) | — |

## v0.5 우선순위 재조정
세부는 `docs/TECHNICAL_RESEARCH.md` §5. 요약: ① 측정 인프라(리앰프/DI) ② AB763 SPICE 기준 회로 비교 ③ parity 확장(ADAA·AB763 톤스택/3극관) ④ ARM NEON·float ADAA(하드웨어 확정 후) ⑤ Reverb 청취 튜닝 → Shimmer/Freeze ⑥ Delay/Dynamics 고도화 ⑦ Automated Analog-to-DSP 파이프라인.

## parity 범위 (과장 금지)
parity는 "Python 미러와 C++가 같은 수식을 같은 결과로 계산한다"만 증명합니다. 실제 회로·장비와의 일치는 증명하지 않습니다.
GRU 베이스라인은 NAM이 아닙니다(NAM은 공식 core로 로딩만 검증).

## 다음 실험 (우선순위)
1. **Jan Ray 복각 실측**: `measure_device.py loopback` → 노브 3–5점 × 진폭 3 → `train_ddsp.py`. 같은 데이터로 NAM 학습(공식 트레이너) → ESR/MRSTFT/CPU 비교.
2. **Gray-box 구조**: 클리퍼를 연산증폭기 피드백 루프 안에 둔 미분 가능한 구조(WDF 또는 상태공간 + 내부 다이오드)를 학습 → E-006 대비 개선 여부.
3. **ADAA**(1차 반도함수) 적용 후 E-002 극단 설정 비배음 재측정, CPU 비교.
4. 하드웨어 후보(Daisy Seed / CM4·CM5 계열) 확정 후 블록별 사이클 측정 → Architecture Search 비용 함수.
