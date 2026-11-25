# 음질 목표와 품질 Level (AUDIO_QUALITY_TARGETS)

- 작성: 2026-11-25 (v0.5) · 근거: 사용자 v0.5 지시서의 "Premium Audio Quality Standard" 절.
- **원본 자료 접근 범위:** 사용자가 언급한 원문 "Premium Audio Quality Standard"와
  "Premium Multi Effects — 기술 연구 및 개발 방향 통합 보고서"는 이 저장소·Claude 프로젝트·업로드 폴더에서 찾지 못했습니다.
  이 문서는 v0.5 지시서에 포함된 목표표·Level 정의와, 이전에 받은 연구 방향 문서(DDSP/Gray-box, APME 피드백)만 근거로 합니다.
  원문을 받으면 이 문서를 갱신하십시오(기억으로 재구성하지 않음).

## 1. 목표 벤치마크 (비교 대상이며 달성 주장 아님)
| 분야 | 벤치마크 | MFX Claude 대응 블록 |
|---|---|---|
| American Clean Amp | UAFX Dream '65 | `amp_ab763` (M1/M2) |
| British Class A | UAFX Ruby '63 | 없음 (`amp` Legacy 모드만) |
| Tweed | UAFX Woodrow '55 | 없음 |
| British Lead | UAFX Lion '68 | 없음 (`amp` High Gain은 Legacy) |
| Drive | Vemuram Jan Ray, Fractal Drive | `custom_drive`, `overdrive`, `distortion`(WDF) |
| Vintage Reverb | UAFX Golden Reverberator | `reverb` Spring-style(양식화), `reverb_plate_premium` |
| Ambient Reverb | Strymon BigSky | `reverb` Ambient/Hall(FDN) |
| Delay | UAFX Starlight, Del-Verb | `delay` (digital/analog-style/tape-style) |
| Dynamics | UAFX 1176 | `compressor` |
| Capture | NAM, Quad Cortex | `nam_amp` (공식 NAM core 로더) |
상용 제품과의 동등성은 실측·블라인드 청취 비교 없이 주장하지 않습니다.

## 2. 품질 Level 정의
| Level | 정의 | 필요 근거 |
|---|---|---|
| 1 | 기본 기능 구현 | 동작 테스트 |
| 2 | 수학적으로 검증된 DSP | 해석식/기준 구현 대비 수치 검증, parity, SR 독립 |
| 3 | 물리·음향 특성을 반영한 모델 | 회로·음향 구조를 따른 모델 + 동작 특성 측정(전달곡선, 동특성) |
| 4 | 실측·청음 기반 최적화 | 실제 장비 측정 데이터 대비 오차, 블라인드 청취 |
| 5 | 상용 고급 제품과 비교 검증 완료 | 같은 조건의 ABX·지표 비교 |

## 3. 현재 블록 평가 (2026-11-25, v0.8 코드·테스트 기준)
| 블록 | Level | 근거 / 부족한 점 |
|---|---|---|
| Custom Drive v2 | 2 | 해석적 곡선, parity(Python/C++), Codex 대비 공통 하니스 측정. 실측·청취 없음 |
| Custom Drive v2 + ADAA (+보정) | 2 | 앨리어싱 −15~−20 dB(2x), 고역 감쇠 보정 ±0.1 dB. parity 미러 없음 |
| AB763 M1 | 2–3 | Koren 12AX7 부하선, Yeh 톤스택(해석식 ±0.35 dB). 파워단 단순 포화 |
| AB763 M2 | 3 (실측 전) | LTP PI, 6V6 Koren, 그리드 전도·블로킹, 정류관 Sag, 지연 없는 NFB 루프, 반응성 스피커 부하. 회로값 문헌 기반, 실기·SPICE 비교 없음 → Level 4 아님 |
| Distortion (WDF 다이오드) | 2–3 | ngspice 대비 ESR −65.8 dB (회로 수준, 시뮬레이션) |
| Overdrive / Fuzz / Boost | 1–2 | 구조 근사, Fuzz는 현상학 모델 |
| Legacy `amp` | 1–2 | tanh 계열 + EQ, 회로 모델 아님 |
| Cabinet (IR/분할 컨볼루션) | 2 | 컨볼루션 정확성 테스트. 품질은 IR에 의존 |
| Premium Plate v1 | 2–3 | Dattorro 토폴로지, RT60 추종(±20 %), 주파수별 감쇠, NED 0.95(400 ms), L/R 상관 ≈ 0. 청취 없음 |
| Legacy `reverb` 5종 | 2 | FDN + Householder, RT60 기반 |
| Delay (기존, Stereo 보강) | 2 | 분수 지연·크로스페이드, v0.6 Cross FB·L/R FB·Width·Free R Time(측정 테스트). Tape/Analog 모드는 스타일 근사 |
| Tape Echo (`delay_tape_premium`) | 3 (청취 전) | 테이프 위치 기반 전송 모델(정확한 피치비 v(t)/v(t−τ)), 헤드 1:2:3, 속도 연동 갭 손실·헤드 범프, 프리엠퍼시스 포화. 시간 오차 < 0.05 ms, Wow 0.6 Hz / Flutter 9.3 Hz 분리, 반복마다 HF/LF −1.6 dB. 실기 측정·청취 없음 |
| BBD Analog Delay (`delay_bbd_premium`) | 3 (청취 전) | 클록 샘플링 구조(2048 버킷), 클록에 따른 대역·앨리어싱, 컴팬더(정적 오차 < 0.2 dB, 과도 오버슈트 1.6 dB), 피드백 포화. 실기 비교 없음 |
| FET Compressor (`comp_fet`) | 3 (청취 전) | 피드백 검출 지연 없는 해, 비율 정확도 4/8/12/20 → 3.97/7.77/11.3/17.4, 임계 오차 < 0.05 dB, 프로그램 의존 릴리즈(0.85 s → 1.73 s). 1176 실측 비교 없음, All 모드 근사 |
| Compressor (기존) | 1–2 | 피드포워드 기본형(보존) |
| Premium Gate (`gate_premium`) | 2–3 (청취 전) | 임계 ±0.1 dB, 히스테리시스로 개폐 0/s, 어택 −0.7 dB, 서스테인 0 dB, 026.wav 조용한 구간 −49.5 dB(기존 −10.1). 청취 없음 |
| Premium EQ (`eq_parametric_premium`) | 2 | 설계 대비 0.027 dB, 4개 레이트 일치, 스윕·타입 전환 클릭 없음. 정합 설계 미적용 |
| BBD v2 | 3 (청취 전) | 다중 칩·클록 추종 필터·컴팬더 바닥 정합·루프 레벨 조절, v1/v2 비교표. 실기 비교 없음 |
| Noise Gate / Parametric EQ / Graphic EQ (기존) | 1–2 | 보존(비트 동일) |
| Premium Chorus / Vibrato | 2–3 (청취 전) | 레이트·깊이(cents) 측정 일치, 클릭 없음. 실기 비교 없음 |
| Premium Phaser | 2 | 노치 위치 이론 대비 0.02 %, 범위 정확, 피드백 안정 |
| Premium Uni-Vibe | 3 (근사, 청취 전) | 비균일 스테이지·램프/LDR 비대칭 모델. 램프·LDR 계수는 실측 아님 |
| Premium Flanger | 2–3 | 스루제로 교차 실제 구현, 콤·피드백 피크 이론 일치 |
| Premium Tremolo | 2–3 | 세 모델 측정상 구별(깊이·THD·대역 위상) |
| Premium Rotary | 3 (청취 전) | 혼/드럼 속도·관성·도플러 이론 일치. 캐비닛 공진·반사 없음 |
| 기존 chorus/flanger/phaser/tremolo/vibrato | 1–2 | 보존(비트 동일) |
| NAM | 2 (로더) | 공식 core, 모델 품질은 캡처에 의존. SR 불일치 시 바이패스 |
| 실시간/플랫폼 | — | Linux x86_64·arm64 VM 테스트, macOS 네이티브 빌드·auval PASS(v0.4), Logic GUI는 사용자 확인 |

## 4. 다음 Level 상승 조건
- Level 3 → 4 (AB763, Custom Drive): 실제 앰프/페달 DI·리앰프 측정 데이터, 측정 지연·레벨 보정 후 ESR/MRSTFT·THD 곡선 비교, ABX.
- Reverb Level 3: IR 기반 비교(상용 플레이트 IR은 라이선스 확인 필요), 청취.
- 모든 Level 5 주장: 동일 입력·레벨 매칭 ABX와 정량 지표를 함께 공개.
