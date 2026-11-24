# 실험 기록

모든 수치는 이 저장소의 스크립트로 재현됩니다(괄호 안 명령). 실행 환경: Linux x86-64 클라우드 VM(2 vCPU),
GCC 13.3 `-O3`(Release), Python 3.13, PyTorch 2.15 CPU, ngspice-42. **실제 아날로그 장비 측정은 없습니다.**

## E-001 오버샘플러 특성 (`./build/mfx_tests --filter oversampler`)

| 배율 | 왕복 지연 | 통과대역 1–18 kHz | 이미지 억압(5 kHz 톤) |
|---|---|---|---|
| 2x | 31 샘플 | < 0.1 dB | 107.8 dB |
| 4x | 39 샘플 | < 0.1 dB | 86.2 dB |
| 8x | 41 샘플 | < 0.1 dB | 76.6 dB |

## E-002 Custom Drive 파라미터 독립성 (`./build/mfx_tests --filter custom_drive --json r.json`)

| 측정 | 결과 | 판정 기준 |
|---|---|---|
| Drive 0→100 소신호 이득 변화 | +47.99 dB | 48 ± 1 dB |
| Drive 60→100 포화 상한 변화 | 0.002 dB | < 3 dB |
| Headroom 0→100 소신호 레벨 변화(−70 dBFS) | < 0.5 dB | < 0.5 dB |
| Headroom 0→100 1 dB 압축점 | −58 → −34 dBFS (24 dB) | 18–30 dB |
| Headroom 0→100 포화 상한 | +24 dB | > 18 dB |
| THD(1 kHz, −20 dBFS) Drive 0/25/50/75/100 | 4.7 / 20.3 / 24.2 / 24.4 / 24.4 % | 50까지 단조 증가, 이후 포화 평탄 |
| 약한/강한 피킹 THD (220 Hz, −40/−6 dBFS) | 1.6 % / 38.4 % | 강 > 5 × 약 |
| Grain 0→100 짝/홀 배음비(레벨 매칭) | −22.9 → −4.1 dB | > +10 dB |
| Crunch 0→100 (H7−H3) | −24.6 → −19.9 dB | > +3 dB |
| 비배음(에일리어싱) 3.7 kHz, Drive 90 | 1x −14.4 dB / 8x −34.8 dB | 개선 > 15 dB |
| 파라미터 급변 곡률비(클릭 검출) | 1.48 (스무딩 제거 시 5.5) | < 2.5 |

**해석**: Drive/Headroom이 서로 다른 물리량(소신호 이득 / 포화 상한)에 대응함을 측정으로 확인. 다만 Drive 50 이상에서는
1 kHz −20 dBFS 사인이 완전 포화되어 THD가 평탄해짐(구형파 한계). 극단 설정(Drive 90, 3.7 kHz −6 dBFS)에서는
8x에서도 비배음이 −35 dB에 그쳐 ADAA 등 추가 앨리어싱 억제가 필요(R-007).

## E-003 클리퍼 모델링 4방식 비교 (`./build/mfx_modeling_study`, `research/spice/run_spice.py validate`)

회로: R 2.2 kΩ – C 10 nF ‖ 역병렬 다이오드. 기준: ngspice-42 과도해석(48 kHz × 16 시간 스텝).

| 모델 | ESR vs SPICE (4x OS, 0→4 V 220 Hz 램프) | CPU ns/샘플 (1x / 4x) | 3953 Hz 8 V 비배음 (1x / 4x) |
|---|---|---|---|
| tanh (수학적) | −22.9 dB | 16 / 65 | −14.3 / −52.7 dB |
| 정적 Shockley + Newton | −27.7 dB | 388 / 1258 | −15.2 / −46.7 dB |
| RC + 다이오드 ODE (사다리꼴 + Newton) | **−65.7 dB** | 447 / 1329 | −16.0 / −58.6 dB |
| WDF (Wright-ω + Newton 1회) | **−65.8 dB** | 86 / 338 | −16.0 / −58.6 dB |

**결론**: 메모리 없는 모델(tanh, 정적 다이오드)은 C에 의한 주파수 의존 클리핑을 표현하지 못해 −23~−28 dB에 머묾.
WDF는 ODE와 같은 정확도(SPICE 대비 −66 dB)를 약 1/4 비용으로 얻음 → 제품 `distortion`을 WDF로 교체(D-010),
스테레오 4x CPU 18.9 % → 5.1 %.

## E-004 SPICE 파이프라인 자체 검증
RC LPF(1.59 kHz) fc에서 −3.04 dB, RC HPF(72 Hz) −2.99 dB(이론 −3.01 dB). XSPICE filesource 입력 + 균일 격자 보간.

## E-005 Python/C++ parity (`python3 tests/parity/run_parity.py`)
20 케이스(Custom Drive 3설정 × 4신호, 블록 1/64/512, 파라미터 변경(스무딩 경로), 10 s 장시간, DDSP Drive 4신호).
최악: ESR 8.3e-7, 최대오차/피크 7.5e-4. 판정 기준 ESR ≤ 1e-6, 최대오차/피크 ≤ 1e-3 — 전부 PASS.
(기준 변경 이력: D-009)

## E-006 DDSP 자동 계수 학습 (`research/ddsp/train_ddsp.py --all --steps 600`)
데이터: SPICE 피드백 클리퍼(오버드라이브 토폴로지), DRIVE 0/0.25/0.5/0.75/1, 입력 진폭 0.05/0.2/0.5 V.
학습 = 합성 기타형 신호 2 s, 검증 = 처프 1 s(학습에 사용 안 함). 모델 = C++ `ddsp_drive` 구조(9 파라미터).

| DRIVE | 검증 ESR (C++ 엔진) | 학습 ESR | 학습 시간 |
|---|---|---|---|
| 0.00 | 0.0148 (−18.3 dB) | 0.0087 | 37 s |
| 0.25 | 0.0435 | 0.0258 | 38 s |
| 0.50 | 0.0519 (−12.9 dB) | 0.0426 | 37 s |
| 0.75 | 0.0541 | 0.0553 | 37 s |
| 1.00 | 0.0545 | 0.0655 | 45 s |

주파수 샘플링(학습용 근사) 모델, 실제 IIR(numpy), C++ 엔진 세 결과의 ESR 차이는 1e-8 수준 → 학습 결과가 그대로 제품 DSP로 이전됨을 확인.
**한계**: 단순 Wiener-Hammerstein 구조(HPF→비선형→LPF)는 피드백 경로 안의 클리핑(주파수 의존 클리핑)을
충분히 표현하지 못해 DRIVE가 클수록 오차가 커짐(−13 dB 수준). Gray-box(피드백 구조 포함) 연구가 필요한 근거(R-005).

## E-007 노브 매핑 LUT 일반화 (`research/parameter_mapping/build_lut.py`)
DRIVE 0/50/100 세 점의 학습 결과로 LUT(로그 영역 보간) → 학습에 쓰지 않은 25/75에서 C++ 엔진으로 평가.

| 노브 | LUT 보간 ESR | 해당 지점 직접 학습 ESR |
|---|---|---|
| 25 (홀드아웃) | 0.0285 | 0.0435 |
| 75 (홀드아웃) | 0.0534 | 0.0541 |

보간 결과가 직접 학습보다 나쁘지 않음(25에서는 오히려 좋음 — 개별 학습이 국소해에 빠진 것으로 추정). 모든 보간값 유한·양수 확인.

## E-008 베이스라인 비교 (`research/graybox/baselines.py`, DRIVE 0.5, 동일 데이터)

| 방법 | 검증 ESR | 학습 비용 | 실행 비용 |
|---|---|---|---|
| 수동 설계 `overdrive` 블록 (레벨만 매칭, 미학습) | 0.790 | — | 2.3 % CPU(스테레오) |
| 블랙박스 GRU(hidden 16) 400 스텝 | 0.438 | 320 s | ~830 MAC/샘플 |
| DDSP (`ddsp_drive`) 600 스텝 | **0.052** | 37 s | 2.7 % CPU(스테레오, 2x OS) |

**주의**: GRU는 NAM이 아니며 학습 예산이 매우 작음(NAM은 WaveNet/LSTM을 훨씬 오래 학습). "DDSP가 NAM보다 우수"하다는
결론이 아니라, **같은 작은 예산에서 물리 구조를 가진 모델이 데이터 효율적**이라는 관찰입니다.

## E-009 성능 (`./build/mfx_tests --group performance`, `tools/benchmark/run_benchmarks.py run`)
* 전체 체인(게이트→컴프→Custom Drive 2x→앰프→캐비닛→EQ→코러스→딜레이→리버브→리미터), 48 kHz:
  실시간 대비 14–15배(블록 32/128/512), 단일 코어.
* Custom Drive(드라이브 70) 오버샘플링별 CPU(블록 128, 모노): 1x 0.4 %, 2x 1.0 %, 4x 1.9 %, 8x 3.5 %.
* 팩토리 프리셋 5종: 실시간 13–25배, 지연 31–70 샘플.
* 오디오 스레드 힙 할당: 0회(파라미터 변경, 바이패스, OS 변경, MIDI 포함 400블록).

## E-010 플러그인 검증
* pluginval(Tracktion) strictness 7 + GUI 테스트, VST3(Linux): SUCCESS (`tests/results/pluginval_vst3_linux_strict7_gui.txt`).
  최초 실행에서 상태 복원 실패 3종을 발견해 수정(D-011).
* AU / Logic Pro: **NOT TESTED** (macOS 환경 없음).

## E-011 NAM
NeuralAmpModelerCore 예제 모델(WaveNet, LSTM, SlimmableContainer A2)을 엔진에서 로딩·실행: 유한 출력,
실시간 대비 10–106배. 정적 링크 시 아키텍처 등록이 사라지는 문제를 whole-archive 링크로 해결(D-012).
음질·지연 정확도는 NAM 공식 플러그인과 비교하지 않았음(NOT TESTED).
