# 기술 연구 통합 (TECHNICAL_RESEARCH)

- 작성: 2026-11-25 (v0.5). 원문 "Premium Multi Effects — 기술 연구 및 개발 방향 통합 보고서"는 접근 불가
  (`docs/AUDIO_QUALITY_TARGETS.md` 머리말 참고). 이 문서는 v0.5 지시서의 기술 목록과 현재 코드의 실제 상태를 대조합니다.
- 상태 표기: **구현(검증)** = 코드 + 테스트 / **구현(근사)** = 동작하지만 물리 모델 아님 / **실험** = research/ 도구 /
  **미구현** / **후보**(다음 사이클). "존재"와 "음질 수준"을 분리해서 적습니다(음질은 AUDIO_QUALITY_TARGETS의 Level).

## 1. Analog / Physical Modeling
| 기술 | 상태 | 위치 / 근거 | 비고 |
|---|---|---|---|
| SPICE (ngspice) | 실험 | `research/spice/` (다이오드·RC·연산증폭기·트랜지스터·진공관 넷리스트, 스윕, 공차 MC) | 데이터셋 생성, DDSP·WDF 검증 기준 |
| WDF | 구현(검증) | `Distortion` (다이오드 쌍, Wright-ω) | ngspice 대비 ESR −65.8 dB |
| MNA | 미구현 | — | 다단 회로 자동화(Analog-to-DSP) 후보 |
| Nonlinear ODE | 구현(검증) | `nl::StaticDiodeClipper`(Newton), RC 다이오드 ODE (연구) | 제품 경로는 WDF |
| State-space | 미구현 | — | MNA와 함께 후보 |
| Triode 모델 | 구현(검증 일부) | `TriodeStageTable` (Koren 12AX7, 부하선 표, 그리드 전도) | 동작점·이득 테스트, 실측 비교 없음 |
| Pentode/Beam tetrode | 구현(근사) | AB763 M2 `tetrodeCurrent` (Koren 6V6 문헌 파라미터) | 파라미터 출처 확인 필요(K-032) |
| Dynamic Bias | 구현 | Custom Drive 그리드 전도형 바이어스 상태, AB763 M2 커플링 캡 충전 | |
| Power Supply Sag | 구현(근사) | AB763 M2: 정류관 저항 + 다이오드 충전, 20 µF 저장 캡, 관 전류 방전, 프리앰프 B+ 스케일 | 리플·정류관 비선형 미반영 |
| Blocking Distortion | 구현(근사) | AB763 M2 6V6 그리드 커플링 캡 충전/방전(Rg·C) | 프리앰프단 블로킹 미반영 |
| Feedback Clipping | 구현 | Overdrive(asinh 피드백 다이오드), Custom Drive κ 잔여 기울기 | |
| Reactive Speaker Load | 구현(근사) | AB763 M2: Re + 손실성 Le + 병렬 RLC 공진(전류 구동) | 실측 임피던스 곡선 아님 |

## 2. Digital DSP
| 기술 | 상태 | 근거 |
|---|---|---|
| ADAA (1차) | 구현(검증) | Custom Drive v2 + ADAA: 2x D100 앨리어싱 −27.4 → −47.9 dB. v0.5 보정: 10 kHz 고역 −0.95 → −0.09 dB, Mix 50 % 정렬 오차 ≤ 0.1 dB |
| Oversampling | 구현(검증) | 하프밴드 63/33/17탭, 1x–8x, 지연 0/31/39/41 |
| Anti-aliasing (클리핑 전 LPF, 엠퍼시스 축소) | 구현 | Custom Drive v2 |
| Parameter smoothing | 구현 | LinearSmoothed, 제어 블록 32, 모델 전환 페이드 |
| SIMD / ARM NEON | 미구현 | 스칼라 C++. arm64 Linux에서 테스트 86/86(v0.4) — 성능 최적화 후보 |
| Partitioned convolution | 구현(검증) | `Convolver` (Cabinet IR) |
| Python/C++ parity | 구현(검증) | `tests/parity/` (Custom Drive v2, 필터, 오버샘플러). ADAA·AB763·Plate 미러 없음 |

## 3. AI Modeling
| 기술 | 상태 | 근거 |
|---|---|---|
| DDSP | 실험 | `research/ddsp/` (ESR, pre-emph ESR, MRSTFT, Envelope loss, loss 설정 파일, train/val/test, 지연 정렬) |
| Neural Gray-box | 실험(베이스라인) | `research/graybox/` GRU 베이스라인(NAM 아님) |
| NAM | 구현(로더) | `nam_amp` (공식 NeuralAmpModelerCore) |
| Parameter mapping / LUT export | 구현(검증) | `KnobLut`(C++), `research/parameter_mapping/` (방법 비교: 구간 선형 유지) |
| Active Learning | 미구현 | 측정 장비 확보 후 |
| Knowledge Distillation | 미구현 | NAM → 경량 DSP 후보 |
| Parametric Capture-to-DSP | 미구현 | DDSP + LUT 경로가 기초 |
| Automated Analog-to-DSP Engine | 장기 연구 | SPICE 데이터 → 구조 선택(WDF/표/ODE) → 계수 피팅 → C++ 내보내기 → parity. 현 인프라: 넷리스트·스윕·DDSP·LUT·parity |

## 4. Spatial DSP
| 기술 | 상태 | 근거 |
|---|---|---|
| Dattorro-style plate | 구현(v0.5) | `reverb_plate_premium` |
| FDN | 구현 | Legacy `reverb` (8선, Householder) |
| Diffusion | 구현 | 입력 확산 올패스(Plate 4단, FDN 4단) |
| Frequency-dependent damping/decay | 구현 | Plate: 구간 RT60 이득 + 저역 배수 + HF 감쇠(테스트로 확인) |
| Spring dispersion | 구현(양식화) | Legacy Spring-style 분산 올패스 체인 — 물리 스프링 모델 아님 |
| Hybrid convolution reverb | 미구현 | 후보 |
| Shimmer / Bloom / Freeze | 미구현 | 다음 사이클 후보 |
| BBD delay | 구현(근사) | `delay` analog-style(LPF·포화), BBD 클록·컴팬더 모델 아님 |
| Tape echo | 구현(근사) | `delay` tape-style(wow/flutter·포화), 테이프 헤드 물리 모델 아님 |

## 5. 연구 우선순위 (재조정, v0.5)
1. 측정 인프라: 리앰프/DI 녹음 프로토콜, 측정 지연·레벨 보정 → AB763·Custom Drive Level 4 근거.
2. AB763 SPICE 기준 회로(12AX7 2단 + 톤스택 + LTP)로 M2 전처리단 비교 (넷리스트는 기존 tube_stage 확장).
3. Python 미러 확장: ADAA 경로, AB763 톤스택·3극관 표 (parity 범위 확대).
4. ARM NEON / 고정 소수점 검토는 하드웨어 후보 확정 후 (ADAA double 경로 포함, K-030).
5. Reverb: Plate 청취 튜닝 → Hall/Spring 정밀화 → Shimmer/Freeze.
6. Delay(BBD/Tape)·Dynamics(FET) 고도화는 Amp M2·Plate 안정화 후 (DEVELOPMENT_PLAN §Delay/Dynamics 기준).
7. Automated Analog-to-DSP: SPICE 스윕 → 구조별 피팅 → C++ 계수 → parity 파이프라인을 단일 스크립트로 묶기(장기).

## 6. Delay / Dynamics 다음 개발 기준 (이번 사이클 구현 없음)
| 항목 | 현재 | 다음 기준 |
|---|---|---|
| Digital delay | 분수 지연, 피드백 필터 | 지터 없는 tempo sync, 피드백 포화(소프트), 스필오버 |
| BBD | LPF + 포화 근사 | 클록 주파수 의존 대역폭·앨리어싱, 컴팬더 펌핑, 노이즈 |
| Tape echo | wow/flutter 근사 | 헤드 갭/바이어스 주파수 특성, 테이프 포화 히스테리시스, 다중 헤드 |
| Stereo/스필오버 | 트레일 바이패스 | 핑퐁·크로스피드, 프리셋 전환 시 스필오버 |
| Compressor | 피드포워드 | 피드백 토폴로지, Peak/RMS 선택, 프로그램 의존 릴리스, 사이드체인 HPF, FET 비선형(1176형) |

## 7. v0.8 모듈레이션 기술 메모
* 분수 지연 보간(측정): Hermite 채택, Lagrange 3차 +2.3 dB, Thiran 1차는 시변 지연 부적합(오차 +3 dB). 후속: 변조 깊이가 큰 Vibrato/Rotary에
  Lagrange 4점 또는 다상 FIR 검토(청취 후).
* 유니바이브 램프/LDR: 실측 데이터 없음 — 시상수·R(빛) 지수는 근사. 후속: 실제 LDR 소자 측정(계단 응답) 또는 공개 데이터시트.
* 로터리: 캐비닛 공진, 혼 반대쪽 개구부·실내 반사, 드럼 배플 지향성 정밀화는 미구현(경량 모델 우선).
* BBD 코러스: 클록 샘플링 대신 필터 근사 선택(짧은 지연에서 클록 > 50 kHz라 앨리어싱 영향 작음, CPU 우선).
