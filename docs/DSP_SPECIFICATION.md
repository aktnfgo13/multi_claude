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

### 신호 흐름

```
x → HPF1 20 Hz (입력 커플링) → Tight: HPF2 (Q 0.6) → Mid(pre): Peak
  → [OS ↑] → ×G (Drive) → Emphasis: HighShelf1 +E dB @1.5 kHz
          → 바이어스 이동 (그리드 전도형 동적 상태)
          → 비대칭 대수 시그모이드  y = V·u/(1+|u/V|^p)^(1/p),  V = Vp(u≥0) / Vn(u<0)
          → 2단 소프트 스테이지 (Crunch, c2)
          → De-emphasis: HighShelf1 −E dB → [OS ↓]
  → DC 블로커 → Mid(post) → Tilt(±) → Fizz LPF2 → ×makeup(+12 dB)
  → Mix(지연 정렬된 드라이와) → ×Level
```

### 노브 → 물리량 매핑 (`customDriveMapping`, Python 미러: `research/common/mfx_dsp.py`)

| 노브 (0–100, n=값/100) | 물리량 | 식 | 기본값(=노브) |
|---|---|---|---|
| Drive | 클리핑단 입력 소신호 이득 G | 48·n dB | 25 → 12 dB |
| Headroom | 클리핑 임계 Vth (출력 상한, 포화 시작점) | −24 + 24·n dBFS | 50 → −12 dBFS |
| Crunch | 니(knee) 경도 p, 2단 스테이지 c2 | p = 1.5·(10/1.5)^n, c2 = 1+2n | 50 → p 3.9 |
| Grain | 비대칭 a, 엠퍼시스 E, 동적 바이어스 깊이 | a = 0.45n, Vp=Vth(1+a), Vn=Vth(1−a), E=15n dB, depth=0.15+0.25n | 50 |
| Tight | 클리핑 전 HPF 차단 주파수 | 30·30^n Hz (30–900 Hz) | 50 → 164 Hz |
| Mid Character | 클리핑 전 피크(±10 dB), 클리핑 후 30 % | m=2n−1: f = 700·2^(m/2) Hz, gain 10m dB, Q 0.9+0.5m(m≥0) / 0.7 | 50 → 0 dB |
| Tone | 클리핑 후 틸트 ±6 dB @800 Hz + Fizz LPF | tilt = 12(n−0.5) dB, fc = 2.5k·5.6^n Hz | 50 → 5.9 kHz |
| Mix | 병렬 클린(지연 정렬) | 0–100 % | 100 % |
| Level | 출력 | −24…+12 dB | −6 dB |
| Oversampling | 비선형 구간 | 1x/2x/4x/8x | 2x |

**Drive와 Headroom의 분리(측정으로 검증, `audio.custom_drive_*`)**

* Drive는 소신호 이득만 바꾸고 출력 상한은 거의 바꾸지 않음: 0→100에서 소신호 +48.0 dB, 포화 시 상한 변화 < 3 dB.
* Headroom은 소신호 이득을 바꾸지 않고(−70 dBFS 입력에서 변화 < 0.5 dB) 포화 시작점과 상한을 이동:
  1 dB 압축점이 Headroom 0→100에서 약 24 dB 이동, 상한 +18 dB 이상.
* 단순 출력 볼륨이 아님: 소신호 레벨은 그대로이고 압축이 시작되는 입력 레벨만 바뀝니다.

**Grain**은 랜덤 노이즈가 아니라 (1) 양/음 포화 레벨의 비대칭(짝수 배음), (2) 클리핑 앞뒤의 상보 셸빙
(선형 응답은 그대로, 고역에서의 비선형 상호작용만 증가 → 고차 배음 분포 변화), (3) 동적 바이어스 깊이를 조절합니다.
레벨을 맞춘 비교에서 Grain 0→100이 짝/홀 배음비를 10 dB 이상 바꾸는 것을 테스트로 확인합니다.

**동적 상태(피킹 반응)**: 양의 과구동이 커플링 상태 b를 2 ms로 충전하고 150 ms로 방전시킵니다(진공관 그리드 전도
블로킹의 단순 모델). 강하게 칠수록 동작점이 이동해 비대칭·압축이 순간적으로 증가하고 회복됩니다.

**단순화 / 미검증 사항**: 실제 아날로그 회로를 측정해 맞춘 것이 아니며, 회로 소자 수준 모델이 아닌
“물리적 의미가 있는 블록 모델”입니다. 청감 평가(ABX)는 수행하지 않았습니다.

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
