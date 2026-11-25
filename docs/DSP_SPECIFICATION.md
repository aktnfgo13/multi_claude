# DSP 명세

모든 파라미터 표는 `./build/mfx_render --describe <type>`으로 JSON으로도 얻을 수 있습니다(코드가 원본).

## 공통 규칙

* 내부 처리: float32, TDF-II 바이쿼드, 계수는 double로 설계 후 float로 반올림.
* 파라미터 스무딩: 연속 파라미터 30 ms 선형 램프(드라이브 계열의 Mix/Level 20 ms), 계수 재계산은 32 샘플마다
  (움직이는 동안만). 선택형/토글 파라미터는 즉시 적용.
* 오버샘플링: 선형위상 하프밴드 FIR 캐스케이드(63/33/17탭, Kaiser β 7.8/7.0/6.0).
  왕복 지연이 **정수 샘플**이 되도록 탭 수를 선택 → 2x: 31, 4x: 39, 8x: 41 샘플(48 kHz에서 0.65/0.81/0.85 ms).
  측정: 통과대역(1–18 kHz) 편차 < 0.1 dB, 이미지 억압 > 70 dB(`unit.oversampler_*`).
* DC 차단: 비대칭 비선형 뒤에는 1차 DC 블로커(5–8 Hz).
* NaN/Inf: 엔진 출력에서 0으로 치환(최후 방어), 각 이펙트는 유한 출력 테스트를 통과해야 함.

## Custom Drive (`custom_drive`) — 핵심 제품 기술

특정 상용 페달의 복제가 아니라, **각 노브가 하나의 물리량에만 대응**하도록 설계한 드라이브입니다.

### 신호 흐름 (매핑 v2, 2026-11-24 통합 고도화)

```
x → HPF1 20 Hz (입력 커플링) → Tight: HPF2 (Q 0.6) → Mid(pre): Peak
  → [OS ↑] → ×G (Drive) → Pre-clip LPF2 7.2 kHz → Emphasis: HighShelf1 +E dB @1.5 kHz
          → 바이어스 이동 (그리드 전도형 동적 상태, 2 ms 충전 / 150 ms 방전)
          → 해석적 니 곡선 + 로그 잔여 기울기:
               z = v/Vth + β
               y = Vth · [K_p(z) + κ·asinh(z) − f(β)] / f'(β)      (소신호 기울기 1, 동작점 β)
               K_2 = z/√(1+z²), K_4 = z/(1+z⁴)^¼, K_6 = z/(1+z⁶)^⅙, p∈[2,6]은 이웃 두 곡선의 선형 혼합
          → 2단 소프트 스테이지 (Crunch, c2, s = 2·Vth, K_2)
          → De-emphasis: HighShelf1 −E dB → [OS ↓]
  → DC 블로커 → Mid(post) → Tilt(±) → Fizz LPF2 → ×makeup
  → Mix(지연 정렬된 드라이와) → ×Level
```

### 노브 → 물리량 매핑 v2 (`customDriveMapping`, Python 미러 `research/common/mfx_dsp.py`)

v1은 `customDriveMappingV1`로 코드에 남겨 두었습니다(전/후 비교용, 제품 경로에서는 사용하지 않음).
자세한 표·이유·프리셋 영향은 [PARAMETER_SPECIFICATION.md](PARAMETER_SPECIFICATION.md)를 보십시오.

| 노브 (n=값/100) | 물리량 | v2 식 | 기본값 |
|---|---|---|---|
| Drive | 클리핑단 입력 소신호 이득 G | −6 + 36n dB | 25 → +3 dB |
| Headroom | 클리핑 임계 Vth | −18 + 18n dBFS | 50 → −9 dBFS |
| Crunch | 니 경도 p, 잔여 기울기 κ, 2단 c2 | p = 2+4n², κ = (0.12+0.5h)(1.4−1.3n), c2 = 1+0.3n | 50 → p 3 |
| Grain | 동작점 β(짝수 배음), 엠퍼시스 E, 바이어스 깊이 | β = 0.6n, E = 5n dB, depth = 0.1+0.2n | 50 |
| Tight / Mid / Tone | (v1과 동일) | | |
| (출력 보정) makeup | Headroom 50 %, Drive 15 % 보상 | −0.5·Vth_dB − 0.15·G_dB | 50/25 → +4.05 dB |

**측정으로 확인한 성질 (`audio.custom_drive_*`, 48 kHz, OS 2x)**

* Drive 0→100: 소신호 +30.6 dB(36 dB × 0.85), 포화 상한 변화 < 3 dB.
* Headroom 0→100: 1 dB 압축점이 14–24 dB 범위에서 이동, 소신호 레벨은 9 dB(임계 이동의 절반)만 변함
  → Headroom은 순수한 볼륨 노브도 아니고(v1처럼) 완전 보상도 아님.
* Crunch 0→100: 1 dB→6 dB 압축 사이 입력 폭(니 폭)이 3 dB 이상 좁아짐.
* Grain 0→100: 레벨 매칭 후 짝/홀 배음비 +10 dB 이상.
* Drive 0 / Headroom 50 / 입력 피크 −7.2 dBFS(실제 험버커 DI 수준): THD 6 %, 프레이즈 잔차 2.7 % — 클린에 가까움.
  (v1은 같은 조건에서 THD 22 %, 잔차 24 %.)

**v1 → v2 변경 이유 (벤치마크 E-013)**: v1은 Drive 0에서도 +6.5 dB, Headroom 기본 −12 dBFS, makeup +12 dB 고정이라
실제 기타 레벨(−7 dBFS 피크)에서 이미 강하게 포화했고, |z|^p 곡선의 0점 꺾임(비해석성) 때문에 고차 배음이 느리게
감쇠해 앨리어싱이 컸습니다. v2는 게인 스테이징을 실제 입력 레벨에 맞추고, 해석적 곡선 + 로그 잔여 기울기(피드백
클리퍼처럼 포화 이후에도 다이내믹이 남음)로 바꾸었습니다.

**진단 (`CustomDrive::setDiagnosticsEnabled`, 기본 off)**: 입력 피크/RMS, 클리퍼 입력 피크(Vth 대비 dB), 클리퍼 출력 피크,
게인 압축량, DSP에 실제 적용된 Drive/Vth/makeup/κ/p. 오디오 스레드는 relaxed atomic만 기록합니다.
UI 표시는 아직 없습니다(KNOWN_ISSUES K-006).

**단순화 / 미검증 사항**: 실제 아날로그 회로를 측정해 맞춘 것이 아니며, 회로 소자 수준 모델이 아닌
“물리적 의미가 있는 블록 모델”입니다. 청감 평가(ABX)는 사람이 수행해야 하며 아직 수행하지 않았습니다
(ABX 세트는 `tests/results/compare_drive/` 생성 절차 참고).

### Custom Drive 모델 v2 + ADAA (v0.4, `model = 2`)
* 곡선·매핑은 v2와 동일. 두 클리핑 단을 1차 ADAA로 계산: `y[n] = (G(x[n]) − G(x[n−1])) / (x[n] − x[n−1])`, |Δx| < 1e-6이면 중점값.
  1단 G = ∫(knee + κ·asinh): K2 → √(1+z²), asinh → z·asinh z − √(1+z²), K4·K6 → 3차 Hermite 표(1/32 간격, [0, 64], 노드에서 정확한 미분값;
  `nl::kneeAntiderivative`, prepare에서 1회 생성). 2단 G = √(1+w²). 차분은 double.
* 단마다 오버샘플 기준 반 샘플의 군지연(2x에서 기본 레이트 0.25 샘플 ×2)이 추가됩니다. 보고 지연(정수)은 바뀌지 않습니다.
* 측정(48 kHz, H50, 3953 Hz −7.2 dBFS, `mfx_compare_drive --claude-model`): 2x D50 −51.3 → −66.6 dB, D75 −32.0 → −51.8, D100 −27.4 → −47.9;
  4x D75 −47.9 → −65.9, D100 −36.4 → −57.0. THD·소신호 이득·어택 변화는 ±0.1 이내로 동일. CPU(모노 2x) 112 → 135 ns/샘플(+21 %).
* Python 미러(`mfx_dsp.py`)·parity는 v2만 다룹니다(ADAA 경로 미러 없음, K-027).

## AB763 Normal (M1, v0.4, type `amp_ab763`)
회로 흐름 모델(블랙페이스 AB763 '65 Deluxe Reverb 계열 Normal 채널). 문헌 회로값 기반이며 실제 앰프 측정과 맞춘 것이 아닙니다.
| 블록 | 모델 | 값/근거 |
|---|---|---|
| 입력 보정 | 0 dBFS = 1 V 피크 | `kVoltsPerFullScale` (가정) |
| V1A | Koren 12AX7, 부하선 정적 표(6001점, 비실시간 생성), 캐소드 완전 바이패스, 그리드 전도 Ig = 2e-4·Vgk^1.5 via 68k | B+ 250 V, Ra 100k, Rk 1.5k → Vp0·Vk0·이득은 테스트에 기록 |
| 톤스택 | Yeh–Smith 3차 해석식, 실근 분해 → 1차 + 2차 섹션, 오버샘플 레이트에서 쌍선형 | 100k slope, 250 pF, 0.1 µF, 0.047 µF, Treble 250k(선형), Bass 250k(오디오 테이퍼 exp((x−1)·3.4)), Mid 고정 6.8k. 아날로그 대비 ±0.35 dB 이내(40 Hz–6 kHz) |
| Volume | 1M 오디오 테이퍼 (10^(2x)−1)/99 | 톤스택 부하 효과 미반영 |
| 믹싱 | −6 dB 상수 | 220k 채널 믹싱 근사 |
| 회복단 | V1A와 같은 모델, 그리드 소스 220k | 회로 일부 가정(캐소드 바이패스) |
| 커플링 | 1차 HPF 7.2 Hz (0.022 µF/1M) | |
| 파워 M1 | PI + 6V6 푸시풀을 대칭 소프트 포화 하나로(knee p=3, 25 V 기준) | M2에서 PI·6V6·Sag·NFB·OT 모델 예정 |
* 측정(220 Hz, −12 dBFS 입력): THD Volume 2 1.2 % / 5 1.0 % / 8 7.2 % / 10 24 %. Volume 6 이하는 짝수 배음 우세(V1A), 8 이상 홀수 배음(파워단).
* 샘플레이트 독립: 소신호 이득 44.1/48/96 kHz 차 < 0.3 dB, THD 차 < 20 %(테스트).
* 기존 Cabinet(IR/시뮬레이션)과 직렬 사용. 팩토리 프리셋 06 "Blackface Normal".

### v0.5: ADAA 보정 (`adaa_comp`, v2 + ADAA에서만 동작)
* ADAA는 소신호에서 단마다 (1+z⁻¹)/2 평균 → 고역 감쇠(2x에서 10 kHz −0.95 dB, 1x −4 dB)와 반 샘플 지연을 만듭니다.
* 보정: 오버샘플 레이트에서 대칭 3탭 FIR [−¼, 3/2, −¼] (1 샘플 지연, 1/cos²(ω/2)의 저차 근사) +
  젖은 경로를 기본 레이트 정수 샘플까지 패딩(F·⌈2/F⌉ − 2 OS 샘플) → 드라이 경로를 정수 지연으로 맞춤(보간 손실 없음).
  보고 지연 +⌈2/F⌉ 샘플(2x에서 +1).
* 측정(48 kHz, tone 100, 소신호): 10 kHz 기준 v2 대비 2x −0.94 → −0.09 dB, 4x −0.23 → −0.01, 8x −0.06 → 0.00, 1x −3.98 → −1.27 dB.
  Mix 50 %: 10 kHz 오차 2x +1.44 → +0.09 dB, 4x −0.55 → +0.01 dB. 앨리어싱·THD 변화 ±0.3 dB 이내. CPU 차이는 측정 잡음 수준.
* 1x ADAA: D100 앨리어싱 −39.5 dB로 v2 2x(−27.4)보다 낮고 CPU는 절반 이하(모노 49 vs 113 ns/샘플) → 하드웨어 후보 설정.
* 수치 안정성: |Δx| < 1e-6이면 중점값, 차분은 double. 아주 작은/느린/일정한 입력에서 v2와의 차이 < 5 % (포화 영역), 스파이크 없음(테스트).
* ARM 이식성: double 경로(K-030). 추정(측정 아님): float로 G(z)를 계산하면 |z| ≈ 50에서 절대 오차 ~3e-6이고 차분 몫 오차는
  3e-6/Δz → Δz ≈ 1e-2에서 ~3e-4. 하드웨어 버전은 Δz 기준 상향 또는 국소 테일러 전개 + float를 검토(미구현).

## AB763 Normal M2 (v0.5, `model = M2`, 새 인스턴스 기본)
프리앰프(V1A → 톤스택 → Volume → 회복단)는 M1과 같고, 그 뒤를 회로 흐름으로 확장했습니다. 회로값은 AB763 계열 문헌값이며 실측 아님.
| 블록 | 모델 | 물리/근사 구분 |
|---|---|---|
| 위상 반전단 | 롱테일 페어: 차동 소프트 리밋(knee K2, ±60 V, 이득 27), 82k/100k 판 부하 불균형(0.82) | **근사**(전달곡선 형태), 불균형은 회로값 |
| PI 입력 | 회복단 출력 × 0.14 (커플링·분압 근사) | 근사 |
| 6V6 그리드 | 0.1 µF/220k 커플링 HPF, 고정 바이어스 −34 V, 그리드 전도 시 커플링 캡 충전(바이어스 이동) → 블로킹 디스토션, Rg·C로 방전 | 상태 모델(근사 계수 0.02) |
| 6V6 쌍 | Koren 빔 4극관 식(μ 10.7, ex 1.31, kg1 1672, kp 41.16, kvb 12.7), 스크린 = B+ − 15 V, 판 전압 = B+ ∓ ½ 1차 스윙(이전 샘플) → 판 니에서 포화, 각 관 차단 → AB급 교차 | 물리식(문헌 파라미터) + 1샘플 판 전압 지연 |
| 출력 트랜스 | Raa 6.6k : 8 Ω(권선비 28.7), 1차 인덕턴스 HPF 25 Hz, 누설 LPF 18 kHz | 근사(코어 포화 없음) |
| 스피커 부하 | 전류 구동: Z(s) = 6 Ω + 손실성 Le(0.4 mH ∥ 18 Ω) + 병렬 RLC 공진(95 Hz, +36 Ω, Q 2.5) | 등가회로(실측 임피던스 아님) |
| 네거티브 피드백 | 8 Ω 탭 → 820 Ω / 47 Ω (β 0.054) → PI 두 번째 그리드. **지연 없는 루프 해**: 부하·피드백 필터의 직접항(b0)과 상태로 순방향을 선형화(할선 G, 제한·평활)하여 vd를 닫힌 식으로 구한 뒤 비선형 경로를 1회 평가. 피드백 경로 2차 LPF 20 kHz(≤ 0.2·fs_OS) | 루프 해법은 수치 근사, 회로값은 문헌 |
| Sag | 460 V 무부하, 정류관+권선 500 Ω(충전만, 다이오드), 20 µF, 방전 = 1.1·(Ia+Ib) + 10 mA → B+가 판·스크린 전압을 결정, 프리앰프는 50 ms RC로 B+ 비율만큼 스케일 | 상태 모델, 리플·정류관 비선형 없음 |
측정(테스트·`tests/results/cpp_tests.json`):
* −12 dBFS 220 Hz THD: Volume 2 1.1 % / 6 2.1 % / 10 13.3 % (M1: 1.2 / 1.3 / 24.3 %) — NFB가 왜곡을 낮춤, 짝/홀 −13 dB(볼륨 10, 홀수 우세).
* 소신호 응답(볼륨 4, re 1 kHz, M2 − M1): 95 Hz +6.0 dB(스피커 공진), 10 kHz +4.7 dB(보이스코일 인덕턴스).
* Sag: 유휴 B+ 419.5 V, 110 Hz 0.7 FS 버스트에서 −7.6 V, 63 % 도달 약 1 ms, 200 ms 후 유휴로 복귀.
* SR 독립: 소신호 이득 44.1/48/96 kHz × 1x–8x 차 < 0.1 dB(1x는 피드백 대역 제한으로 고역 차 있음), THD 차 < 20 %.
* 극단 입력(+12 dBFS 잡음, NaN 포함, 44.1–192 kHz, 1x/8x): 유한, 피크 < 10.
* CPU(스테레오 48 kHz, 10 s 렌더 최솟값): M1 1.0 %, M2 3.3 %(2x), M2 1x 1.5 % 단일 코어.
* 지연: M1과 같음(오버샘플러만).
미구현(M3 후보): 정류관 리플, 프리앰프 캐소드 바이패스 동특성, 볼륨 팟의 톤스택 부하, 트랜스 코어 포화, 스크린 전류 정밀 모델, 실측 스피커 임피던스, SPICE 비교.

## Premium Plate v1 (v0.5, type `reverb_plate_premium`)
* 토폴로지: Dattorro(1997) 8자 탱크(공개 논문의 지연 길이·출력 탭 세트, 29761 Hz 기준 → fs·Size로 스케일, 분수 지연 Hermite).
* 입력: L+R → Low Cut(2차 HPF) → 대역(1극) → Pre-Delay(0–250 ms, 평활) → 확산 올패스 4단(0.75/0.75/0.625/0.625 × Diffusion).
* 탱크: 변조 올패스(−0.70, 1·사인 LFO 직교 위상, 깊이 0–16 샘플@29.761 kHz) → 지연 → HF 감쇠(1극 LPF, Damping Hz) →
  구간 감쇠(RT60에서 구간 길이로 계산한 이득, 저역은 Crossover 1극 분리 후 RT60 × Bass Decay) → 올패스(0.5) → 지연 → 구간 감쇠 → 반대편 탱크 입력.
* Early reflections: 8탭 스테레오(7–71 ms × Size), 부호 교대·0.82^k 감쇠, Early 레벨. 늦은 탱크와 분리.
* 출력: 논문 탭 7개/채널 × 0.6 + ER, Width(M/S), Mix(드라이/웻 크로스페이드). hasTail → 바이패스 시 트레일.
* 메모리: 192 kHz에서 1.68 MiB(Size 150 % 기준 최대). 48 kHz에서 약 1/4. 오디오 콜백 할당 0.
* 측정(`tools/benchmark/reverb_metrics.py`, `tests/results/reverb_metrics_v05.json`):
  RT60 설정 1 s → 1.16 s, 3 s → 2.93 s(테스트 ±20 %); Bass ×2·Damping 2.5 kHz에서 저역 5.9 s / 고역 2.0 s(중역 약 3 s);
  NED 400 ms 0.95(50 ms 0.54 — 플레이트 특성상 밀도 형성이 Legacy FDN보다 느림); L/R 상관 −0.01; 꼬리 스펙트럼 피크도 8.9 dB
  (Legacy Plate 10.8, Hall 7.3); 20 s·Size 150·Mod 100 극단 설정에서 에너지 감소·유한.
* CPU: 스테레오 48 kHz 1.1 % 단일 코어(Legacy Plate 1.0 %).
* 다음 후보: Shimmer, Bloom, Freeze, 정밀 Spring, Hybrid(IR + 알고리즘).

## 기타 드라이브

| 타입 | 모델 | 비고 |
|---|---|---|
| `clean_boost` | 선형 이득 + 저역 컷 + 브라이트 셸프, 0 dBFS 이상에서만 부드러운 상한 | |
| `overdrive` | 비반전 연산증폭기 + 피드백 다이오드: y = x + a·asinh(G·HP720(x)/a), 피드백 C에 의한 LPF | asinh는 2Is·sinh(v/nVt)를 v에 대해 푼 식 |
| `distortion` | ±4.5 V 레일 연산증폭기 → R(1 k)·C(10 nF)·역병렬 다이오드 ODE, **사다리꼴 적분 + Newton-Raphson** (double) | 회로 기반 |
| `fuzz` | 2단 비대칭 포화 + 바이어스(굶은 바이어스 데드존) + 공급 처짐 | **현상학적 모델**(트랜지스터 수준 아님) |
| `ddsp_drive` | HPF1 → G → 비대칭 시그모이드 → LPF1 → out, Mix/Level; 연구 트랙에서 학습된 물리 파라미터 또는 노브 LUT 사용 | research/ddsp 와 동일 구조 |

## 아날로그 모델링 비교 실험 (`apps/mfx_modeling_study.cpp`, `research/spice`)

회로: Vin –R 2.2 kΩ– node –C 10 nF– GND, 역병렬 다이오드(Is 2.52 nA, n 1.752).

1. 수학적 클리핑 (tanh)
2. 정적 Shockley 다이오드 방정식 (Newton-Raphson, 메모리 없음)
3. RC + 다이오드 상태 방정식 (사다리꼴 + Newton-Raphson)
4. WDF (저항성 전압원 ‖ 커패시터 병렬 어댑터, 루트 = 다이오드 쌍, Wright-ω 명시해 + Newton 1회)

결과는 `docs/EXPERIMENT_LOG.md` E-003/E-004 참고(WDF는 ODE와 동등 정확도에 약 1/4 비용, ngspice 대비 ESR −65.8 dB).

## Amp (`amp`)

블록 구성: 입력단(HPF·그리드 스토퍼 LPF·브라이트 캡) → 프리앰프 삼극관 단 N개(이득 → 커플링 HPF → 캐소드
바이패스 로우셸프 → Miller LPF → 동적 그리드 전도 바이어스 → 비대칭 삼극관 곡선, 반전) → 톤스택 → NFB 근사
(Presence 하이셸프/Depth 90 Hz 피크) → 푸시풀 포화 + 전원 처짐(Sag: 15 ms/250 ms 엔벨로프로 상한 감소) →
출력 트랜스(55 Hz HPF2, 11 kHz LPF1) → 스피커 임피던스 상호작용(105 Hz 피크, 3 kHz 셸프).

모델(캐릭터 설명이며 특정 제품이 아님): Clean(2단, 큰 헤드룸), Crunch(3단), High Gain(4단, 좁은 커플링),
Custom(3단 중립). 모델별 출력 트림은 합성 DI·기본 노브에서 체감 음량을 맞춘 값(측정값 아님).

**문서화된 단순화**: 삼극관은 Koren/Child-Langmuir 방정식이 아닌 정적 비대칭 곡선 + 동적 바이어스 1개,
톤스택은 수동 TMB 회로(노브 상호작용)가 아닌 3밴드 근사, NFB는 폐루프가 아닌 셸빙 근사,
위상 반전단은 대칭 포화에 흡수, 바이어스 시프트는 프리앰프에만 적용. 실제 앰프 측정 검증 없음.
(SPICE `tube_stage.cir`에 Koren 모델 기반 삼극관 단이 있어 향후 비교 기준으로 사용 가능.)

## Cabinet (`cabinet`)

* User IR: 제로 지연 분할 컨볼루션(앞 64탭 직접 연산 + 64샘플 균일 분할 FFT, 주파수 영역 지연선), 최대 2 s.
  IR은 메시지 스레드에서 Kaiser 윈도 sinc로 엔진 샘플레이트에 리샘플, (옵션) 에너지 정규화 후 5 ms 페이드로 교체.
  스테레오 IR은 첫 채널만 사용.
* Built-in Sim: 필터 기반 3종(4x12 Closed / 2x12 Open / 1x12 Combo) — 실제 캐비닛 측정값 아님.
* Low/High Cut(최소/최대 위치 = 실제 바이패스), IR Level, IR Mix.

## Dynamics

* Noise Gate: 60 Hz HPF 사이드체인, 피크 엔벨로프(0.2/20 ms), 히스테리시스, 홀드, 어택/릴리스, Range.
* Compressor: 피드포워드, 로그 영역 스무스 분기 피크 검출, 소프트 니 게인 컴퓨터(Giannoulis 2012), Makeup, Mix.
* Limiter: 1.5 ms 룩어헤드(지연 = 정확히 72 샘플 @48 kHz), 윈도 최소 게인, 지수 어택/릴리스 + 최종 안전 클램프.

## EQ

Filter(9 타입, 24 dB/oct는 Butterworth 2단), Parametric EQ(Low Cut, Low Shelf, Peak×3, High Shelf, High Cut, Output;
극단 위치는 실제 바이패스), Graphic EQ(옥타브 10밴드 31 Hz–16 kHz, Q 1.41).

## Modulation / Delay / Reverb

* Chorus/Flanger/Vibrato: Hermite 보간 변조 지연선, 스테레오 LFO 위상 오프셋, 플랜저 피드백(포화·LPF).
* Phaser: 4/6/8/12단 1차 올패스, 지수 스윕, 피드백, 계수 갱신 8샘플(전역 카운터 → 블록 크기 무관).
* Tremolo: Sine/Triangle/Soft Square, 스테레오 팬 모드, 템포 싱크.
* Delay: Digital(시간 변경 시 50 ms 2-탭 크로스페이드, 클릭 없음) / Analog(BBD: 어두운 루프 LPF, 포화, 시간 슬루) /
  Tape(와우·플러터, 녹음 포화, 헤드 범프). Mono/Stereo(오프셋)/Ping-Pong, 템포 싱크(1/1…1/16, 점·셋잇단).
* Reverb: Room/Hall/Plate/Ambient = 8선 FDN(하우스홀더 행렬, RT60로부터 선별 감쇠 이득, 루프 내 HF 댐핑, 변조),
  입력 확산 올패스 4개, 프리딜레이, 초기 반사(Room/Hall). Spring = 분산 올패스 16단 + 피드백 루프 2개(양식화).
  Hall T60 측정값은 목표의 0.6–1.5배 범위(테스트로 확인).

## Utility

Gain/Volume(큐빅 테이퍼)/Phase Invert, Pan/Width(정전력), Tuner(오디오 스레드는 데시메이션 링버퍼 복사만,
YIN 피치 검출은 UI 스레드), Tap Tempo(최근 탭 간격 중앙값, 2 s 공백 시 리셋), MIDI CC 매핑(엔진).
