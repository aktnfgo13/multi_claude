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

---
# 통합 고도화 (2026-11-24, 브랜치 `integration/reference-project-review`, 기준 태그 `baseline-v0.1`)

환경: 클라우드 x86_64 Linux, GCC, Release. **Mac/AU 결과 아님.** CPU 수치는 같은 기계·같은 빌드에서
병렬 작업이 없을 때 측정한 것만 비교에 사용합니다(전체 48 kHz 실행은 다른 측정과 동시에 돌아 CPU 수치를 쓰지 않음).

## E-012 RT 안정성 + Sanitizer
* 지시서 RT 위험 목록을 코드와 대조: 파라미터 수명(SlotParameter가 호스트 스레드에서 Effect* 접근) → seqlock 메타데이터로 수정,
  튜너 링버퍼 레이스 → atomic, 테일 잘림 → 트레일, NAM 샘플레이트 → 바이패스 유지(K-005), AU → 미검증(K-004).
* 신규 동시성 테스트(라우팅 추가/이동/삭제/병렬 + IR 로드 + 바이패스 + MIDI를 오디오 스레드 처리와 동시에).
* ASan+UBSan: 전체 통과, 경고 0. TSan: integration 15개 통과, 경고 0 (`tests/results/sanitizer_*.txt`).
* pluginval strictness 7(GUI 포함), VST3 Linux, SlotParameter 재작성 후 재실행: SUCCESS (`tests/results/pluginval_vst3_linux_strict7_v2.txt`).

## E-013 Custom Drive 공통 벤치마크: Codex vs Claude v1 vs Claude v2
* 하니스 `apps/mfx_compare_drive.cpp`: 두 구현을 같은 바이너리로 컴파일(Codex 소스는 읽기만), 같은 신호, 모노.
  Headroom 그리드 D{0..100}×H{0..100}×입력 피크{−60…−3} dBFS, 기타 노브 50, Mix 100, Level −6, OS 2x.
  - 같은 노브 값 비교(`same_parameter_summary`)와 **매칭된 동작점 비교**(Codex 각 설정에 대해 Claude 그리드에서
    소신호 이득·1 dB 압축점이 가장 가까운 설정)를 분리.
  - 지표: 소신호 이득, onset, THD·배음(h2/h3/h5/h7), 3953 Hz 비배음(앨리어싱), 합성 프레이즈 왜곡 잔차,
    볼륨 1/8 클린업 잔차, 어택(15 ms)·서스테인 게인 변화, CPU, 지연. 원본·−24 dBFS 레벨 매칭 렌더, ABX 무작위 20세트.
* 결과(48 kHz, H50, 입력 −7.2 dBFS):

| 설정 | 엔진 | 소신호 | THD | 앨리어싱 | 프레이즈 잔차 | 어택 |
|---|---|---|---|---|---|---|
| D0 | Claude v1 | +6.5 dB | 22.4 % | −51.8 dB | 23.9 % | −8.0 dB |
| D0 | Claude v2 | −7.0 dB | 6.1 % | −92.9 dB | 2.7 % | −0.2 dB |
| D0 | Codex | −2.9 dB | 2.7 % | −83.6 dB | 0.4 % | −0.1 dB |
| D50 | Claude v1 | — | 32.7 % | −22.8 dB | 60 % | −30.6 dB |
| D50 | Claude v2 | +8.3 dB | 26.6 % | −51.3 dB | 22.5 % | −5.8 dB |
| D50 | Codex | +7.2 dB | 14.5 % | −74.4 dB | 11.3 % | −3.3 dB |
| D100 | Claude v2 | +23.6 dB | 33.6 % | −27.4 dB | 49.7 % | −20.6 dB |
| D100 | Codex | +27.5 dB | 35.5 % | −52.3 dB | 38.6 % | −16.9 dB |

* 매칭 동작점: 저게인(Codex D0–25) 영역은 Claude v2가 앨리어싱 −82…−93 dB로 Codex(−71…−94)와 같거나 낮음.
  중·고게인(Codex D50–100)은 Claude v2가 앨리어싱 20–35 dB 높고 어택 압축 1.5–2배 → K-001, K-002.
* 샘플레이트(D50 H50 앨리어싱): 44.1 kHz Claude −47.3 / Codex −73.7, 48 kHz −51.3 / −74.4, 96 kHz −86.9 / −85.9 dB.
* 오버샘플링(Claude v2 최종, 앨리어싱 D50 H50 / D100 H50): 1x −28.1 / −19.6, 2x −51.3 / −27.4, 4x −92.8 / −36.4, 8x −93.2 / −48.1 dB.
  (Codex 2x D100 H50 −52.3 dB → Claude 고게인은 8x에서야 비슷해짐: K-001)
* CPU(같은 기계, quick 실행, 모노 ns/샘플): Claude v2 1x 71 / 2x 189 / 4x 353, Codex 1x 162 / 2x 668 / 4x 1284
  (`after_v2_quick/report.json`; 단독 Claude 실행에서는 2x 158 / 4x 316). 반복 간 변동 ±15 % 정도.
  지연: Claude 0/31/39, Codex 0/64/64 샘플.
* 결과 파일: `tests/results/compare_drive/{before_v1_quick, after_v2_quick, v2_full48, v2_q44, v2_q96}/`, 그림 `docs/images/compare_drive/`.
* 한계: 합성 신호만 사용. 청취 평가 없음(ABX 세트만 생성).

## E-014 Custom Drive 매핑 v2 설계 반복
1. v1 → 게인 스테이징만 변경(−6…+42 dB, Vth −18…0, 보상 makeup) + κ 잔여 기울기: D50 H50 앨리어싱 −22.8 → −34 dB.
2. |z|^p 곡선 → 해석적 니 계열 + β 오프셋: 변화 작음(±1 dB) — 곡선 꺾임보다 구동 레벨이 지배적임을 확인.
3. Drive 상한 +30 dB, 엠퍼시스 15→5 dB, 클리핑 전 7.2 kHz LPF2: D50 H50 −48 dB, D25 H50 −89 dB.
4. Crunch 매핑 완화(p=2+4n², c2=1+0.3n): 기본값에서 THD 26.6 %, −51.3 dB.
5. 기존 테스트 `crunch_hardens_knee`(h7−h3 지표)가 v2에서 불통과 → 고게인에서 두 설정 모두 사각파 배음비로 수렴해
   지표가 Crunch를 구분하지 못함. 테스트를 정적 압축곡선의 니 폭(1→6 dB 압축 입력 폭)으로 교체하고, Crunch가 실제로
   니 폭을 3 dB 이상 바꾸도록 κ 범위(1.4−1.3n)와 2단 스테이지를 조정. **테스트 기대값 변경 이유를 D-018에 기록.**
6. Python 미러 갱신, parity 재실행: 전 케이스 PASS(최대 상대오차 2.9e-4, ESR ≤ 1.6e-7).

## E-015 앰프 저역 필터 정밀도 (Codex는 저역 컷을 double로 처리)
float32 계수 양자화 오차를 계산: depth 피크 90 Hz +9 dB, bass 셸프 120 Hz +12 dB를 96/192/384 kHz(OS 8x)에서
30 Hz 오차 최대 0.17 dB, 60 Hz 이상 0.04 dB 이하. → 들리는 수준이 아니므로 **KEEP(float)**, D-020.

## E-016 DDSP: loss 설정 파일, Envelope loss, train/val/test 분리, 지연 정렬
* `research/ddsp/configs/loss_*.json`, `--loss-config`. 분할: 항목별 0–1.6 s 학습, 1.6–2.0 s 검증(모델 선택), 마지막 1 s 처프 테스트.
  교차상관 지연 추정(SPICE 데이터는 0 샘플 — 정상).
* SPICE 피드백 클리퍼 DRIVE 0.5, 300 step:

| 설정 | test(처프) ESR | 검증 ESR | 학습 ESR |
|---|---|---|---|
| v1 스크립트(학습 배치 손실로 선택, 학습 2 s) | 0.052* | — | 0.043 |
| v2 기본 loss | 0.062 | 0.130 | 0.082 |
| v2 + Envelope loss 0.05 | 0.063 | 0.136 | 0.086 |

  *v1은 처프를 "validation"으로 불렀지만 모델 선택에는 쓰지 않았으므로 test에 해당.
  Envelope loss는 이 데이터에서 ESR을 개선하지 못함(목적이 다른 지표이므로 예상 범위). 기본값 0 유지.
  v2 분할은 학습 데이터가 20 % 줄어 ESR이 약간 나빠짐 — 대신 선택 편향이 없음.
* 시간 영역 IIR 비교: 주파수 샘플링 학습 모델 / 실제 재귀 IIR(numpy) / C++ 엔진 ESR이 소수점 4자리까지 일치.

## E-017 노브 매핑 방법 비교 (`research/parameter_mapping/compare_mapping_methods.py`)
학습 위치 {0,50,100} → 보류 {25,75}, 학습 {0,25,100} → 보류 {50,75}. SPICE 대비 오디오 ESR:

| 방법 | 25 | 75 | / | 50 | 75 |
|---|---|---|---|---|---|
| 위치별 개별 피팅(상한) | 0.038 | 0.057 | | 0.050 | 0.057 |
| 선형(끝점) | 0.042 | 0.054 | | 0.050 | 0.054 |
| 구간 선형(현재 C++ KnobLut) | 0.033 | 0.057 | | 0.050 | 0.057 |
| PCHIP 스플라인 | 0.032 | 0.057 | | 0.050 | 0.057 |
| 저차원(a+b·log) | 0.033 | 0.057 | | 0.048 | 0.060 |
| 소형 MLP | 0.032 | 0.058 | | 0.057 | 0.059 |

이 회로는 DRIVE 25 % 이상에서 응답이 거의 포화돼 방법 간 차이가 작음. MLP는 데이터가 적을 때 보류 위치에서 가장 나쁨.
→ 구간 선형 KEEP(D-021). 결론의 범위: 위치 5개, 1차원 노브, 시뮬레이션 데이터.

## E-018 SPICE 부품 공차 스윕 (`run_spice.py tolerance`)
피드백 클리퍼(DRIVE 0.5), 16개 변형(0 = 공칭), 저항 ±5 %, 커패시터 ±10 %, 다이오드 IS ±30 %, N ±3 %, 팟 ±20 % 균등 분포.
seed로 분리된 train/val/test(10/3/3). 440 Hz 입력 0.03/0.1/0.3/1.0 V에서 게인 산포 0.80/0.69/0.55/0.33 dB(p-p),
THD 20.4–23.8 / 27.2–28.2 / 24.2–25.1 / 14.8–15.6 %.
→ 같은 회로의 실물 개체 차이 수준. 모델 오차가 이보다 작으면 "다른 회로"로 구분되기 어렵다는 기준선으로 사용.
