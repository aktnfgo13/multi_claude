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
| 네거티브 피드백 | 8 Ω 탭 → 820 Ω / 47 Ω (β 0.054) → PI 두 번째 그리드. **지연 없는 루프 해**: 부하·피드백 필터의 직접항(b0)과 상태로 순방향을 선형화(할선 G, 제한·평활)하여 vd를 닫힌 식으로 구한 뒤 비선형 경로를 1회 평가. 피드백 경로는 회로처럼 저항성(광대역) — v0.6.1에서 2차 LPF 20 kHz 제거(K-046) | 루프 해법은 수치 근사, 회로값은 문헌 |
| Sag | 460 V 무부하, 정류관+권선 500 Ω(충전만, 다이오드), 20 µF, 방전 = 1.1·(Ia+Ib) + 10 mA → B+가 판·스크린 전압을 결정, 프리앰프는 50 ms RC로 B+ 비율만큼 스케일 | 상태 모델, 리플·정류관 비선형 없음 |
측정(테스트·`tests/results/cpp_tests.json`):
* −12 dBFS 220 Hz THD: Volume 2 1.1 % / 6 2.1 % / 10 13.3 % (M1: 1.2 / 1.3 / 24.3 %) — NFB가 왜곡을 낮춤, 짝/홀 −13 dB(볼륨 10, 홀수 우세).
* 소신호 응답(볼륨 4, re 1 kHz, M2 − M1): 95 Hz +6.0 dB(스피커 공진), 10 kHz +4.0 dB(보이스코일 인덕턴스; v0.5–0.6은 피드백 LPF 때문에 +4.7 dB, 18–20 kHz +10–15 dB).
* Sag: 유휴 B+ 419.5 V, 110 Hz 0.7 FS 버스트에서 −7.6 V, 63 % 도달 약 1 ms, 200 ms 후 유휴로 복귀.
* SR 독립: 소신호 이득 44.1/48/96 kHz × 1x–8x 차 < 0.1 dB, THD 차 < 20 %. v0.6.1: 18 kHz 응답(re 1 kHz)의 레이트·OS 간 차 0.54 dB, 18 kHz − 10 kHz < 1.5 dB.
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

## Tape Echo (v0.6, type `delay_tape_premium`)
기존 `delay`의 Tape 모드와 별개의 새 타입. 회로/기계 동작을 근거로 한 **음향 모델**이며 특정 제품 복제 아님.
* **전송(Transport)**: 테이프 속도 v(t) = v_base(t)·(1+ε(t)), 위치 P = ∫v. 재생 헤드 k의 지연 τ_k는 P(t) − P(t−τ_k) = k(갭 단위)를 만족.
  매 샘플 Newton 1회로 추적(위치 기록은 8샘플 간격 double, 선형 보간). 결과:
  - Wow/Flutter 피치비가 물리적으로 v(t)/v(t−τ)(재생 속도/녹음 속도) — 지연에 LFO를 더하는 방식과 다름.
  - Time 변경 = 모터 속도 변경(관성 120 ms 1극) → 피치 글라이드, 크로스페이드·클릭 없음(측정: 최대 샘플 차분 비 1.00).
  - 헤드 1:2:3 간격이 속도 변화 중에도 유지. Heads = 1 / 1+2 / 1+2+3, Spread로 헤드 1(L)·2(R) 패닝.
* **ε(t)**: Wow = 0.55 Hz 사인 + 1.2 Hz 2극 저역 잡음, Flutter = 9.3 Hz + 2차 고조파 + 30–55 Hz 스크레이프 잡음,
  Modulation = 주기/무작위 비율과 0.25 Hz 드리프트. 최대 ±0.4 %(wow), ±0.12 %(flutter), ±0.15 %(drift), Age가 ×1.5까지 키움.
  잡음원은 시드 고정(재현 가능), RMS는 prepare에서 정규화.
  측정(1 kHz, 300 ms): Wow 100 % → 변조 피크 0.6 Hz, 5.5 cents RMS; Flutter 100 % → 9.3 Hz, 1.6 cents RMS; 0 %일 때 0.002 cents.
* **녹음**: 입력(스테레오는 모노 합) + 피드백 → 프리엠퍼시스(하이셸프 +9 dB @3 kHz) → 비대칭 tanh(Drive 0–18 dB, Age로 바이어스 0.06–0.24,
  소신호 이득 1로 정규화) → 테이프. 고역이 먼저 포화(측정: 0.3 vs 0.01 진폭 압축 200 Hz −3.1 dB, 4 kHz −8.2 dB).
* **재생**: 디엠퍼시스(−9 dB) → 갭 손실 LPF(7 kHz × (속도비)^0.6 × (1 − 0.55·Age), Q 0.6) → 헤드 범프(110 Hz × 속도비^0.8, +1.5–3.5 dB)
  → 70 Hz HPF → Tone(300 Hz/2.5 kHz 틸트 ±6 dB). 필터는 실제 속도가 0.3 % 이상 바뀌면 32샘플마다 갱신(블록 크기 무관).
* **피드백**: 재생 신호 → 30 Hz HPF → ×Feedback(0–110 %) → 녹음단. 반복마다 전체 체인 통과(측정 HF/LF: 1회 −5.0, 3회 −8.4, 5회 −11.2 dB).
  110 %·Drive 100·40 ms에서 자기 발진하되 포화로 제한(20 s 피크 0.32).
* **분수 지연 보간**: Hermite 4점 유지. frac 0.5 최악 조건 이론값: 10 kHz −0.54 dB(선형 −2.0 dB), 15 kHz −2.5 dB(선형 −5.1 dB).
  갭 손실 LPF(≤ 9 kHz)가 지배적이어서 더 높은 차수(올패스·sinc)의 이득이 들리지 않을 것으로 판단, CPU 차이는 헤드당 곱셈 약 6회.
* 히스테리시스(Jiles-Atherton 등) 미도입: 품질 개선 근거(측정)가 없어서. 후보로만 기록.
* 믹스: 드라이는 Mix 50 %까지 1, 웻은 50 %에서 1. Output Level은 전체 출력.
* 메모리: 3.7 s 테이프(모노 float) + 위치 기록 → 48 kHz 1.25 MiB, 192 kHz 5 MiB. CPU 스테레오 48 kHz 0.59 %(단일 코어, x86).

## BBD Analog Delay (v0.6, type `delay_bbd_premium`)
* **구조 선택**: 클록 샘플링 모델(채택) vs LPF + 디지털 지연(경량 근사).
  | 항목 | 클록 샘플링(채택) | LPF 근사 |
  |---|---|---|
  | 지연·클록 관계 | 정확히 2048 클록 주기 | 별도 매핑 |
  | 시간에 따른 대역·앨리어싱 | 구조에서 발생 | 필터 컷오프 매핑만 |
  | 시간 변경 | 클록 변경 → 피치 글라이드 | 읽기 위치 이동(클릭 처리 필요) |
  | 메모리 | 2 × 4096 float = 32 KiB, 샘플레이트 무관 | 최대 지연 × fs |
  | CPU(스테레오 48 kHz) | 0.61 % | 기존 delay 0.64 %와 비슷 |
  | 지연 | 0(필터 군지연만, 0.2–0.5 ms) | 0 |
* 4096단 BBD(버킷 2048/통과), 클록 r = 2048/T: 20 ms → 102 kHz, 300 ms → 6.8 kHz, 800 ms → 2.6 kHz.
  입력은 클록 순간에 선형 보간으로 샘플(AA 필터를 통과한 r/2 이상 성분은 앨리어싱 — 긴 지연의 "grit"),
  출력은 버킷 수열을 오디오 레이트에서 Hermite로 복원 후 재구성 필터.
* 주변 회로: 컴팬더(2:1 압축 → AA 4차 버터워스 → 버킷 과부하 비대칭 소프트 클립 → 샘플링 → 복원 → 재구성 4차 → 1:2 확장),
  검출기는 정류 평균(어택 4 ms/릴리즈 40 ms)이고 압축기는 칩 입력, 확장기는 칩 출력을 보므로 과도 구간에서 숨쉬기 발생.
  Compander 0–100 %로 지수 조절(정적 전달은 항상 1). Drive는 칩 입력 레벨(−6…+12 dB, 출력에서 역보상).
  Tone = AA/재구성 컷오프 2–9 kHz(고정 필터, 클록 추종 안 함 — 실제 페달 방식).
* 피드백: 출력 → 40 Hz HPF → ×Feedback(0–110 %) → asinh 포화(±1.2) → 입력. 반복마다 8극 필터 통과 → 어두워짐.
* 클록 변조: 사인 LFO, 클록 ±1 %×Depth, Spread가 R 칩의 LFO 위상(최대 180°)과 시간(+3 %)을 바꿈. 시간 노브는 60 ms 1극(VCO 지연).
* 측정: 지연 오차 +0.23 ms(20/300 ms), +0.48 ms(800 ms) = 재구성 필터 군지연; 3 kHz 톤 이미지/신호 100 ms −81 dB, 800 ms +49 dB(완전 앨리어싱);
  컴팬더 정적 오차 −0.003/−0.05/−0.15 dB(−40/−20/−8 dBFS), −40 → −6 dBFS 스텝 오버슈트 +1.6 dB;
  변조 피크 2.0 Hz(설정 2 Hz); Spread 100 % L/R 상관 0.00; 110 % 피드백 15 s 피크 0.29; 시간 300 → 150 ms 변경 시 최대 샘플 차분 1.9배(피치 상승분).
* 미구현: 클록 누설·히스, 칩별 비선형 전하 전송 손실, 다중 칩 직렬(긴 지연 고음질화).

## FET Compressor (v0.6, type `comp_fet`)
* 단순 컴프레서(기존 `compressor`)와 차이:
  | 항목 | `compressor` | `comp_fet` |
  |---|---|---|
  | 검출 위치 | 입력(피드포워드) | **출력(피드백)**, 지연 없는 해 |
  | 임계값 | 노브 | 고정, Input 노브로 신호를 밀어 넣음(게인 스테이징) |
  | 시간 | 어택 0.1–200 ms | 어택 20–800 µs(유효값), 릴리즈 50–1100 ms + 프로그램 의존 |
  | 비율 | 연속 | 4/8/12/20/All, 비율별 임계·니 |
  | 왜곡 | 없음 | FET 2차항(감쇠량 비례), 출력단 A급 포화, 빠른 릴리즈 시 파형 추종 왜곡 |
* 검출: 스테레오 링크 샘플 피크. gr = a·gr' + (1−a)·C(x − gr)를 Newton(최대 6회, 오목·단조 → 수렴 보장)으로 매 샘플 해결.
  C는 출력 레벨축 이득 계산기(기울기 R−1, 니 폭 W/R → 입력축 약 W). 분기: C(x − gr') > gr'이면 어택.
* 어택 유효 시간: 피드백 루프의 선형화 시정수가 τ/R이므로 내부 τ = 노브 × R. 측정 63 % 시간: 20 µs 설정 → 0.021 ms, 800 µs → 0.79 ms.
* 프로그램 의존 릴리즈: 임계 초과(요구 감쇠 > 2 dB)가 지속되면 charge가 0.8 s로 충전, 아니면 2 s로 방전. 유효 릴리즈 = 노브 × (1 + 3·charge).
  측정(8:1, 300 ms): 50 ms 버스트 후 1 dB까지 0.85 s, 2 s 버스트 후 1.73 s.
* 비율/임계(입력 0 dB 기준, dBFS 피크): 4:1 −24, 8:1 −22, 12:1 −20, 20:1 −18, All −26(비율 30, 니 12 dB, 내부 시간 ×0.6, FET 왜곡 ×2.5 — **근사**).
  측정 기울기: 3.97 / 7.77 / 11.3 / 17.4, T+6 dB 입력에서 출력 오차 < 0.04 dB.
* 비선형: y = g·x + DC차단(0.15·Character·(1−g)·u²/(1+u²)), 출력단 tanh 혼합(Character), 입력단(+12 dBFS)·출력단(+18 dBFS) 소프트 리밋,
  NaN/Inf 입력은 0으로 처리.
* 측정 THD(입력 +12 dB, 20:1): 100 Hz 빠름(20 µs/50 ms) 2.46 % vs 느림 0.76 %; 1 kHz Character 0 → 0.75 %, 100 → 1.06 %;
  SMPTE IMD(60 Hz + 7 kHz, 빠름) 0.84 %. CPU 스테레오 48 kHz 0.54 %. 지연 0.
* 실제 1176과의 차이(문서화): 입력/출력 트랜스·회로값을 시뮬레이션하지 않음, 비율별 임계·니·All 모드는 공개 자료 기반 추정,
  측정 비교 없음 → Level 3(청취 전).

## Premium Gate / Expander (v0.7, type `gate_premium`)
기존 `noise_gate` 감사: 피크 검출(0.2 ms/20 ms), 히스테리시스·Hold·Range 있음, 사이드체인 HPF 60 Hz 고정, 항상 스테레오 링크,
선형 이득 1극 평활, 익스팬더 없음. 026.wav에서 60–100 Hz 현 울림이 게이트를 열어 둬 조용한 구간 감쇠가 10 dB에 그침.
* 사이드체인: 2차 HPF 20–500 Hz(오디오 경로는 필터링하지 않음 — 열린 상태 출력 = 입력 비트 동일, 테스트).
* 검출기: Peak(즉시 어택, 20 ms 감쇠) / RMS(3 ms 평균제곱, 사인 피크 등가 +3.01 dB). 채널별, Stereo Link 선택.
* 히스테리시스: Threshold에서 열림, Threshold − Hysteresis에서 닫힘, Hold 0–500 ms. 리셋 시 열린 상태로 시작(첫 음 절단 방지).
* Gate 모드: 감쇠 0 또는 Range. Expander 모드: 상태 의존 임계 아래 기울기 (Ratio − 1), 6 dB 2차 소프트 니, Range로 제한.
* 이득 제어(dB 영역): Attack/Release는 60 dB 변화 시간(선형 dB 램프 = 일정한 지각 페이드 속도) + 0.2 ms 모서리 평활.
  완전히 열리면 정확히 이득 1.
* Adaptive Release(기본 On, 측정으로 채택): Hold 이후 감쇠 속도 = clamp(신호 레벨 하강률(30 ms 평활), 설정 Release, 4 × 설정).
  측정: 급정지(4 ms) 후 잔여 히스 −9.2 → −15.4 dB(고정 vs 적응), 천천히 감쇠하는 음(τ 250 ms) 보존 차 0.0 dB.
* 측정(합성 신호): 열림 −50.0 dBFS(설정 −50), 닫힘 −55.9(설정 −56), 임계 부근 개폐 횟수 12/s(히스 0) → 0/s(히스 6),
  익스팬더 감쇠 −46 dBFS 11.8 dB·−52 dBFS 23.8 dB(곡선 ±0.6 dB), 노트 어택(첫 5 ms) −0.7 dB(Gate)/−0.4 dB(Expander),
  서스테인 0.0 dB, 히스 감쇠 60 dB(Gate=Range)/20.5 dB(Expander 4:1), 감쇠 램프 최대 0.005 dB/샘플(클릭 없음),
  50 Hz 험 −45 dBFS: SC HPF 20 Hz에서 게이트 열림, 150 Hz에서 60 dB 감쇠. CPU 스테레오 48 kHz 0.15 %(기존 0.04 %).

## Premium EQ (v0.7, type `eq_parametric_premium`)
기존 `parametric_eq` 감사: RBJ 바이쿼드 TDF-II 7밴드(고정 타입), 파라미터 30 ms 선형(Hz) 평활 + 32샘플마다 계수 교체,
밴드 On/Off·타입 선택 없음, High Cut 0.45 fs 제한. 기존 블록은 무변경(비트 동일).
* 구조 비교와 선택:
  | 후보 | 정적 응답 | 파라미터 변화 | CPU(스테레오 48 kHz) | 선택 |
  |---|---|---|---|---|
  | RBJ 바이쿼드 TDF-II(기존) | 쌍선형 + 프리워프 | 계수 계단 교체(32샘플) — 상태가 다른 계수 기준이라 큰 변화에서 과도 | 0.08 %(7밴드) | 유지(기존 블록) |
  | TPT SVF(Simper/Zavalishin) | 동일(쌍선형 + 프리워프) | 상태가 적분기 값 → 샘플별 계수 램프에도 안정 | 0.32 %(5밴드, 정적 시 나눗셈 캐시) | **채택** |
  | 정합(matched) 2차(Vicanek) | Nyquist 부근 왜곡 감소 | 설계 비용 큼 | — | 보류(v0.8 후보) |
* 밴드 5개 × {Off, Bell, Low/High Shelf, HPF/LPF 12·24(버터워스 캐스케이드), Notch}. 0 dB 기본은 m1 = m2 = 0 → 비트 투명.
* 평활: 주파수(log)·이득(dB)·Q(log) 20 ms 1극을 16샘플마다, g/k/m0..m2는 샘플마다 선형 램프. 타입/On-Off 변경은 20 ms 이중 필터 크로스페이드.
* 제한(이유): 10 Hz ≤ f ≤ 0.49 fs(tan 발산 방지), 0.1 ≤ Q ≤ 18(k ≥ 0.055, 과도한 링잉·수치 오차), ±24 dB.
* 측정: 측정 응답 vs 설계 응답 최대 0.027 dB(44.1/48/96/192 kHz × 9종), 벨 중심 이득 오차 < 0.01 dB, 중심 위상 0.0°,
  반이득점 781/1279 Hz(기하 대칭), fs/8 이하 아날로그 원형 대비 최대 0.06 dB(그 위는 쌍선형 압축 — 문서화된 한계),
  빠른 스윕(200 Hz–5 kHz/50 ms, +12 dB, Q 2) 8 kHz 이상 잡음 −70.4 dB re 톤(기존 −68.2 dB — 둘 다 평활되어 차이 작음),
  타입 전환(Bell↔HPF24↔Off↔Notch) 최대 샘플 차분 = 정상 신호 기울기의 1.005배(클릭 없음), Nyquist·극단 Q/이득·블록 1/17 유한,
  무음 후 정확히 0(디노멀 없음).
* UI: 파라미터 패널에 응답 곡선(DSP와 같은 `targetResponseDb()` 계산). GUI 화면 확인은 NOT TESTED(헤드리스 빌드만).

## BBD Analog Delay v2 (v0.7, `delay_bbd_premium` `model = v2`)
* 다중 칩 수식(모델 값): 칩당 4096단(버킷 2048/통과), 직렬 칩 수 C → 버킷 N = 2048·C, 클록 r = N/T, 표본 스트림 Nyquist r/2 = 1024·C/T.
  같은 지연에서 C배 높은 클록 → 앨리어싱 시작 주파수 C배. 실제 제품의 단수는 가정하지 않음(모델 값).
  | T | C = 1 | C = 2 | C = 4 |
  |---|---|---|---|
  | 100 ms | r 20.5 kHz, r/2 10.2 kHz | 41 / 20.5 kHz | 82 / 41 kHz |
  | 300 ms | 6.8 / 3.4 kHz | 13.7 / 6.8 kHz | 27.3 / 13.7 kHz |
  | 800 ms | 2.6 / 1.3 kHz | 5.1 / 2.6 kHz | 10.2 / 5.1 kHz |
* 클록 추종 AA/재구성 필터: 컷오프 = min(Tone, 0.4 r)(4차 버터워스, 32샘플마다 1 % 이상 변할 때 재설계).
* 컴팬더 바닥 정합: 압축기 검출 바닥(1e-3) 아래에서 확장기 이득을 대응값으로 고정 → 저레벨에서도 정적 이득 1
  (측정 −60/−70 dBFS: v1 −7.2/−17.2 dB, v2 0.0/0.0 dB). 과도 오버슈트 등 동적 거동은 v1과 같음.
* 피드백: 재순환 신호 포락선(5 ms/150 ms)이 −6 dBFS를 넘으면 루프 이득을 낮춘 뒤 asinh 포화 → 110 %에서 자기 발진이
  0.49(−6.2 dBFS)로 유지(44.1/96 kHz). v1은 잡음 버스트 시작 시 컴팬더 바닥 불일치로 발진이 사라짐(0).
* 측정(백색 잡음, Tone 100, 컴팬더·변조 Off, 결맞음/비결맞음 분석): 표 = 결맞음 대역폭(−3 dB re 300 Hz) / 비결맞음(앨리어싱+왜곡) 비
  | T | v1 | v2 1 chip | v2 2 chips | v2 4 chips |
  |---|---|---|---|---|
  | 100 ms | 7.0 kHz / −22.0 dB | 6.6 / −26.9 | 7.9 / −32.0 | 8.0 / −35.7 |
  | 300 ms | 3.0 kHz / +1.7 dB | 2.2 / −21.0 | 4.4 / −23.8 | 7.6 / −33.8 |
  | 500 ms | 1.7 kHz / +4.7 dB | 1.3 / −20.3 | 2.7 / −21.5 | 5.3 / −25.4 |
  | 800 ms | 1.2 kHz / +7.0 dB | 0.84 / −19.8 | 1.7 / −20.6 | 3.4 / −22.2 |
  v1의 "대역폭"은 접힌 성분이 섞인 값(비결맞음이 결맞음보다 큼). v2 1 chip은 같은 클록에서 앨리어싱을 필터로 제거(대역은 물리 한계 r/2 쪽으로 줄어듦),
  2·4 chips는 클록을 올려 **대역을 유지/확장하면서** 앨리어싱을 줄임 — 고역을 깎아 앨리어싱을 숨긴 것과 구분됨.
* 메모리 128 KiB(16384 × 2 float, v1 포함 고정, 샘플레이트 무관). CPU 스테레오 48 kHz 0.84 %(v2 2 chips; v1 0.61 %).
* 미구현: BBD 히스/클록 누설(잡음 바닥 상호작용은 컴팬더 정합까지만), 칩별 전하 전송 손실.

## Premium Modulation (v0.8)
### 기존 모듈레이션 감사 (변경 없음, 출력 비트 동일)
| 타입 | 클래스 | 구조 | 한계 |
|---|---|---|---|
| `chorus` | Chorus(ModDelayEffect) | Hermite 지연 1탭, 사인 LFO, L/R 위상 오프셋 | 피드백·톤·캐릭터 없음 |
| `flanger` | Flanger | 같은 코어, 8 kHz 1극 LPF 피드백 | 스루제로 없음, 피드백 루프가 전체 지연 |
| `vibrato` | Vibrato | 1 ms + 최대 3 ms 스윙 | 깊이가 ms(레이트에 따라 음정 편차 변함), 비대칭·드리프트 없음 |
| `phaser` | Phaser | 1차 올패스 4/6/8/12 동일, 8샘플마다 계수 | 범위가 중심 ±2옥타브 고정, 스테이지 불일치 없음 |
| `tremolo` | Tremolo | 선형 이득 LFO(사인/삼각/소프트 사각), 팬 | 광학/바이어스/하모닉 없음 |
| (없음) | — | 로터리·유니바이브 | — |

### 공통 기반 (`src/dsp/modulation/PremiumMod.h`)
* `ModLfo`: double 위상 누적(`cyc_`), 증분을 50 ms 1극으로 평활(레이트 변경 시 위상 점프 없음), Sine/Triangle/Asym(skew)/Smooth Random
  (정수 사이클 해시 + smoothstep — 블록 크기·재생 위치 무관, 재현 가능), 채널별 위상 오프셋, `maxSlope()`로 깊이 보정. 템포 싱크 공통.
* 분수 지연 보간 비교(6 kHz 사인, 5 Hz ±40샘플 변조, 해석해 대비 오차 / x86 ns/샘플):
  선형 −25.3 dB / 37, **Hermite −42.2 dB / 46(채택: 기존 `DelayLine` 재사용)**, Lagrange 3차 −44.5 dB / 43, Thiran 1차 +3.0 dB / 39
  (Thiran은 시변 지연에서 상태 과도 — 부적합). Lagrange는 +2.3 dB 개선이지만 기존 검증된 경로 재사용을 우선(D-066).
* 필터: 1차 TPT 올패스(`ApTpt`, 제어율 8샘플 목표 + 샘플별 G 선형 램프), `Lr4` 크로스오버(합 = 평탄 올패스).

### Premium Chorus (`chorus_premium`)
지연 = Delay + Depth·6 ms·(1+m)/2 + Drift(최대 1.5 ms, 0.13 Hz 무작위). Voices 1–3(위상 균등 분배, 1/√V 정규화), Stereo Phase 0–180°,
Feedback ±70 %(tanh), Tone. Character BBD: 칩 입력 소프트 리밋 + 4차 AA + 4차 재구성(지연 경로 앞뒤), Clean: 2차 LPF.
측정: 레이트 1.000 Hz(설정 1), 피크 음정 16.35 cents(이론 16.24), L/R 상관 0.75, 모노 합 레벨 변동 0.43 dB, 빠른 파라미터 변경 클릭 없음(1.30배).
장기 스펙트럼 콤 피크도(1/24 옥타브): 1 voice 2.31 dB, 2 voices 2.72, 3 voices 3.05, 기존 chorus 2.30 → 기본 1 voice(D-067). 32 KiB(48 kHz).

### Premium Vibrato (`vibrato_premium`)
100 % 웻 피치 변조. 깊이는 **cents**: 스윙 A = (2^(c/1200) − 1)/(f·maxSlope)·fs → 레이트·파형과 무관하게 피크 음정 편차 = 설정값
(측정 오차 ≤ 3 %, 사인/삼각/비대칭, 2–9 Hz, 10–50 cents). 중심 지연 5 ms 정수 샘플 = 보고 지연(48 kHz 240샘플), 깊이 0이면 지연된 입력과 비트 동일.
스윙 한계(중심 지연 − 3샘플)로 느린 레이트에서 최대 깊이 제한. Analog Drift(깊이 ±30 %·위상 흔들림), Stereo Variation. 레이트 급변 클릭 없음(1.02배).

### Premium Phaser (`phaser_premium`)
2/4/6/8/12단 1차 TPT 올패스, 스윕 f = Lo·(Hi/Lo)^((1+depth·m)/2)(지수), 깊이·범위는 log 영역 50 ms 평활(점프 클릭 8.2 → 1.33배로 수정),
Feedback ±95 %(포화 앰프 tanh(k v)/k, Character가 k와 스테이지 불일치 ±12 % 결정), Stereo Phase.
측정: 정적 노치 = 이론 fc·tan((2k+1)π/2N)(쌍선형 보정) 대비 −0.02 %(4·8단), 스윕 300.0–3000.0 Hz(설정 300–3000), ±95 % 피크 < 1.1, 180° L/R 상관 0.32.

### Premium Uni-Vibe (`univibe_premium`) — 페이저 재명명이 아님
4단 커패시터 15 nF / 220 nF / 470 pF / 4.7 nF(공개 회로도 값)에 **하나의 LDR 저항** → 스테이지 코너 비 468배(페이저는 동일 스테이지).
비대칭 위상천이 발진기 → 램프(가열 8 ms / 냉각 30 ms) → 포토셀(어택 5 ms / 릴리즈 40 ms) → R = 4 kΩ·(1/c)^0.7(근사, 실측 아님).
측정(1 Hz, 100 %): 스윕 상승 0.21 s vs 하강 0.38 s(비대칭), 범위 5.2배; Vibrato 모드 피크 6.3 cents vs Chorus 모드 3.1 cents(노치 중심).

### Premium Flanger (`flanger_premium`)
같은 선에서 고정 드라이 탭 D = 4.5 ms(보고 지연 216샘플@48 kHz)와 변조 웻 탭 D + rel.
Through-Zero: rel ∈ [−D, D] — 웻과 드라이의 상대 시간이 실제로 교차(측정 −2.16…+3.16 ms), rel = 0이면 출력 = 지연된 입력(콤 소멸).
Classic: rel > 0, 피드백 루프가 상대 지연 주위(w(t) = x(t−D−rel) + fb·w(t−rel)) — 양/음 피드백 피크가 k/τ, (k+½)/τ에 정확히(±18.2 dB).
TZ 모드의 피드백은 rel ≤ 0 때문에 전체 탭(D + rel) 주위(문서화된 차이). 정적 노치 500/1500 Hz(τ 1 ms) 완전 소거, ±95 % 유한.

### Premium Tremolo (`tremolo_premium`)
* Optical: 램프 가열 4 ms / 냉각 12–42 ms(Character) → 광 ^0.6 → 이득. 깊이가 얕고 둥근 엔벨로프(측정 100 %에서 11.0 dB, 상승 443 < 하강 480 프레임).
* Bias: 이득 곡선 1 − d·((1−m)/2)^1.4 + 바이어스 이동에 비례한 2차 고조파(THD 1.30 % vs Optical 0.00 %), 깊이 62.9 dB.
* Harmonic: LR4 크로스오버(200–2000 Hz), 저역/고역 반대 위상 이득(엔벨로프 상관 −0.92), 깊이 0에서 평탄도 0.00 dB.
레이트 정확도 5.00 Hz(세 모델), Square 모양 최대 이득 계단 0.004/샘플, Pan 모드 L/R 균형 −0.08 dB.

### Premium Rotary (`rotary_premium`)
LR4 800 Hz → 혼(고역) / 드럼(저역). 속도 Slow/Fast/Brake: 혼 0.80/6.80 rev/s, 드럼 0.66/5.90(공개 자료의 대표값), 관성 1차
(혼 0.7 s 가속/0.9 s 감속, 드럼 3.5/4.5 s, Acceleration으로 배율). 도플러 지연 r/c·cos θ(혼 0.15 m, 드럼 0.10 m), 지향성 AM
(혼 0.6, 드럼 0.35 × Depth), 가상 마이크 2개 ±Width·90°, Drive(전단 tanh). 기본 지연 69샘플@48 kHz(보고).
측정: 혼 0.79/6.80 Hz, 드럼 0.66/5.90 Hz, 63 % 가속 혼 0.70 s / 드럼 3.50 s, 혼 도플러 32.6 cents(이론 32.0), Fast L/R 상관 0.03,
모노 합 레벨 변동 2.98 dB(실제 로터리처럼 진폭 변조 남음), 깊이 0 크로스오버 평탄 0.00 dB.

### CPU / 메모리 / 지연 (스테레오 48 kHz, x86 단일 코어, `cpu_percent_stereo_48k_*`)
| 타입 | CPU | 메모리(48 kHz / 192 kHz) | 지연 |
|---|---|---|---|
| chorus_premium | 0.74 % (기존 0.36) | 32 / 128 KiB | 0 |
| vibrato_premium | 0.59 % (기존 0.36) | 4 / 16 KiB | 5 ms (240) |
| phaser_premium | 0.67 % (기존 0.21) | < 1 KiB | 0 |
| univibe_premium | 0.21 % | < 1 KiB | 0 |
| flanger_premium | 0.22 % (기존 0.35) | 16 / 64 KiB | 4.5 ms (216) |
| tremolo_premium | 0.42 % (기존 0.16) | < 1 KiB | 0 |
| rotary_premium | 0.52 % | 4 / 16 KiB | 1.4 ms (69) |

## Premium Reverb & Ambient (v0.9, `src/dsp/reverb/PremiumSpace.*`)
### 기존 리버브 감사 (v0.8 기준, 무변경)
| type | 구조 | 측정(48 kHz IR, mix 100) | 한계 |
|---|---|---|---|
| `reverb` Room/Hall/Plate/Ambient | 8선 FDN(Householder), 선별 감쇠, 입력 확산 4단, ER 탭(Room/Hall) | Hall T=3: RT60 500 Hz 2.85 s, 8 kHz 0.57 s, NED50 0.53, 피키니스 7.37 dB | 대역별 RT60 직접 제어 없음, Freeze·Shimmer·Ducking 없음, 8선 밀도 |
| `reverb` Spring | 분산 올패스 16단 × 2 루프("스타일라이즈드") | RT60 1.62 s, NED150 0.33, 피키니스 13.4 dB | 물리 분산 길이·전이 주파수 없음, Drive 없음 |
| `reverb_plate_premium` (v1) | Dattorro + 구간 RT60·저역 배수·ER·변조 | RT60 500 Hz 2.85 s, NED150 0.745, 피키니스 8.9 dB | 초기 밀도 낮음(→ v2 시험, 채택 안 함 D-072) |

### 공통 부품 (`PremiumSpace.h`)
* `FdnCore`: 16선(라인 37.3–107.9 ms × size 0.4–1.6), 4단 스테레오 입력 확산(3.31/4.87/7.13/10.29 ms, g = 0.45 + 0.3·diffusion),
  선별 이득 gM = 10^(−3L/(fs·T)) + 1차 셸프 250 Hz(저역 배수)·4 kHz(고역 배수), FWHT 혼합(×1/4), 출력 = Hadamard 3·12행(×1/4, 역상 트릭 없음).
  변조: smooth-random(라인별 위상), 프리즈 중 70 % 감소. Size: 32샘플마다 one-pole 목표(τ 0.3 s), 이동은 샘플 단위 글라이드·슬루 ≤ 0.06 samples/sample(D-077).
  Freeze: 80 ms 램프 — 입력 × (1−f), 루프 필터 출력 → 원신호로 보간(손실 0), `softGuard`(> 2에서만).
* `PitchShifter`: 두 탭(반주기 위상차), 지연 dMin + p·w(w = 50 ms), sin² 창(합 = 1), 위상 −= (ratio−1)/w. 내부 지연 ≈ 25 ms(웻 전용, 보고 0).
* `StretchedAp`: z^−K 기반 1차 올패스(K ≤ 64) — 스프링 분산. `WetDucker`: 입력 |x| 엔벨로프(5 ms/250 ms) → 임계 초과량에 비례, 최대 감쇠 duck_db, 웻 게인에만 곱.
* 믹스: dry = min(1, 2(1−mix)), wet = min(1, 2·mix) (D-076). Width: M/S.

### Premium Spring (`reverb_spring_premium`)
탱크 2개(L/R, 두 번째 Td×1.12·a+0.03). 전이 주파수 fC = 3500 + 2500·character Hz, K = round(fs/2fC), a = 0.55 + 0.1(1−character).
루프: read(Td + 지터) → StretchedAp 60단 → fC 4차 LP → Tone 1극 → push(x + g·s), g는 Td + DC 군지연 M·K(1−a)/(1+a)로 계산한 RT60 이득.
HF 보조 루프: 1차 AP(−0.6) 20단, tdH = 0.42·Td, 입력 HP fC, ×0.2. 입력 Drive = tanh(dbToGain(18·drive)·x)/gain(소신호 이득 1). 출력 HP 90 Hz.
측정: 주파수별 첫 도착 300 Hz 50.0 / 1 kHz 53.1 / 3 kHz 55.3 ms, RT60(500 Hz) 1.5→1.45 s, 4→3.68 s, 3차 고조파 −33.9(drive 0) / −24.1(30) / −3.5 dB(100), width 0 → L = R.

### Premium Hall (`reverb_hall_premium`)
입력 HP/LP → 평활 프리딜레이 → ER 12탭/채널(0.55/(1+ms/12), 크기 비례, L/R 상이, 8 kHz LP) + FdnCore. 기본 변조 50 % / 0.5 Hz(피키니스 9.1 → 8.0 dB).
측정: RT60 1 kHz 설정 1/3/8 s → 오차 < 15 %, 대역 RT60(500 Hz 3.16 s, 125 Hz 3.69 s, 8 kHz 1.49 s at T=3, bass 1.2·treble 0.45), 첫 ER 17.1 ms(프리 10 ms + 7.1 ms),
꼬리 L/R 상관 −0.06, 모노 파워 비 0.47, NED150 0.97, Size 0↔100 반복 시 > 3 kHz 성분 −81 dBFS(클릭 없음), 무음 → 38 s 뒤 < −200 dBFS.

### Premium Shimmer (`reverb_shimmer`)
Feedback 구조: 이전 웻 출력 → 시프터 → HP 250 Hz / LP tone → × shimmer × 2.4·√(1−ḡ²) → 레벨 조절기(> 0.3) → FDN 입력에 합산 (패스마다 간격 누적).
Pre 구조: 입력만 시프트(게인 1). FDN: setDecay(T, 1.0, 0.6), 확산 0.85, 변조 0.25 Hz.
측정: 시프트 오차 +12/+7/+19/−12 → 0.00/0.02/−0.007/0.00 cents, AM 리플 0.14 dB; 3.5 s 뒤 옥타브 대역(330 Hz 기준) Feedback +3.3/+3.6 dB, Pre −1.7/−30.9 dB;
잡음 20 s + 무음 40 s(shimmer 100, decay 30) 피크 1.90, 유한, 55–60 s RMS −73 dBFS.

### Freeze / Ducking 측정 (`test_reverb_v09`)
Freeze(잡음 재생 중 전환): 전환 스텝 비 1.02(Hall)/0.89(Shimmer), 60 s 드리프트 −2.6/−3.3 dB/분(K-059), 프리즈 중 새 3 kHz 프레이즈 미포착(−66/−85 dB), 드라이 통과, 해제 후 −200 dBFS까지 감쇠.
Ducking(−12 dB, 임계 −30 dB): 드라이 오차 0, 연주 중 −12.0 dB, 꼬리 −0.19 dB, 게인 스텝 ≤ 0.0009/샘플.

### CPU / 메모리 / 지연 (스테레오 48 kHz, x86, `cpu_percent_stereo_48k_*`)
| type | CPU | 메모리 | 지연 |
|---|---|---|---|
| `reverb_hall_premium` | 2.81 % | 1.26 MiB (192 kHz 5.0 MiB) | 0 |
| `reverb_shimmer` | 2.38 % | 1.3 MiB | 0 (웻 내부 25 ms) |
| `reverb_spring_premium` | 1.70 % | NOT MEASURED(지연선 2×수십 ms 수준) | 0 |

## Delay 스테레오 보강 (v0.6, 기존 `delay`)
확인 결과 기존: Mono/Stereo(R 시간 = L × (1 + Offset))/Ping-Pong, 템포 싱크, Mix/Feedback 평활, 트레일 있음.
추가(끝에 덧붙임, 기본값 = 기존 동작, v0.5와 출력 비트 동일 확인):
`cross_fb`(L↔R 피드백 교차, Stereo 라우팅), `fb_lr_offset`(L 피드백 ×(1−o), R ×(1+o)), `width`(웻 M/S 0–200 %, 평활),
`time_r_mode` = Offset/Free, `time_r_ms`(Free일 때 R 시간; 싱크 시에도 ms 값 사용). 병렬 라우팅 재설계는 범위 외.

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
