# 벤치마크 프로토콜 · 엔진 비교 인터페이스

다른 AI 에이전트가 만든 엔진과 **같은 조건**으로 비교하기 위한 규약입니다. 평가 기준·데이터는 엔진별로 조정하지 않습니다.

## 1. 오프라인 렌더러 인터페이스 (`mfx_render`)

```
mfx_render -i <in.wav> -o <out.wav> (--preset <preset.json> | --effect <type> [--set id=value]...)
           [--sr <Hz>] [--block <N>] [--tail <s>] [--compensate-latency] [--bits 16|24|32]
           [--at <sample>:<id>=<value>]... [--bench <bench.json>] [--analyze-thd <f0>]
```

* 입력: WAV(PCM 16/24/32, float 32/64), 모노/스테레오. `--sr`가 다르면 Kaiser sinc로 리샘플.
* 파라미터: JSON 프리셋(`format: mfx-preset`) 또는 `--set`. 파라미터 정의는 `mfx_render --describe <type>`.
* 출력: 기본 32-bit float 스테레오 WAV.
* 재현성: 같은 입력·파라미터·블록 크기에서 결과는 결정적(난수 없음). 블록 크기 불변성은 테스트로 검증.

### bench.json 필드

| 필드 | 의미 |
|---|---|
| `sample_rate`, `block_size`, `input_channels`, `frames`, `duration_s` | 조건 |
| `process_time_s`, `realtime_factor`, `cpu_percent_of_realtime` | 전체 처리 시간 기반 (단일 스레드) |
| `worst_block_percent_of_budget` | 가장 느린 블록의 처리 시간 / 블록 길이 |
| `peak_rss_kb` | 프로세스 최대 상주 메모리(파일 I/O 버퍼 포함) |
| `latency_samples`, `latency_ms`, `latency_compensated` | 엔진 보고 지연 |
| `chain[]` | 이펙트별 타입·지연·오버샘플링·전체 파라미터 |
| `output_stats_left` | peak/rms dBFS, DC, crest factor, NaN 여부 |
| `harmonic_analysis` | `--analyze-thd f0`: 출력 후반 50 %에서 THD, THD+N, 배음 1–10(dB re 기본파), 짝/홀 비, 비배음 에너지 |

## 2. 표준 조건 (`tools/benchmark/run_benchmarks.py run`)

* 샘플레이트 44.1 / 48 / 96 kHz, 블록 32 / 128 / 512.
* 신호(각 3 s, 자동 생성): 1 kHz −12 dBFS 사인, **3953 Hz −6 dBFS 사인(fs의 약수가 아님 → 에일리어싱이 배음 사이에 떨어져 측정 가능)**,
  −20 dBFS 백색잡음, 합성 기타형 신호(Karplus-Strong, 합성임을 명시).
* 구성: Custom Drive(drive 70) × OS 1/2/4/8, 팩토리 프리셋 5종.
* 산출: `tests/results/benchmark/benchmark.json`, `.csv`, `custom_drive_oversampling.png`.
* 주의: 변조·리버브가 있는 프리셋의 "비배음 에너지"는 에일리어싱이 아니라 효과 자체의 성분을 포함합니다.

## 3. 엔진 간 비교 (`run_benchmarks.py compare --a A.wav --b B.wav [--ref R.wav]`)

1. 두 엔진에 **동일한 입력 WAV**와 의미상 동일한 설정을 적용해 렌더.
2. 상호상관으로 지연 정렬 → **RMS 라우드니스 매칭** → ESR, MRSTFT 계산.
3. 실제 장비 녹음(`--ref`)이 있으면 각 엔진의 ESR을 기준 대비로 보고.

청감 비교는 라우드니스 매칭 후 블라인드 ABX로만 결론을 냅니다(현재 미실시).

## 3-1. 참고 프로젝트 공통 벤치마크 (`apps/mfx_compare_drive`, E-013)
* 두 구현을 같은 바이너리·같은 신호로 측정(참고 소스는 `-DMFX_REFERENCE_DIR`로 경로만 지정, 읽기 전용).
* **같은 노브 값 비교**와 **매칭 동작점 비교**(소신호 이득 + 1 dB 압축점 최근접)를 반드시 분리해 보고.
* 그리드: Drive {0,25,50,75,100}(quick) / 12.5 간격(full) × Headroom 같은 값 × 입력 피크 {−60,−48,−36,−24,−18,−12,−7.2,−6,−3} dBFS.
  나머지 Tone/Tight/Crunch/Grain/Mid 50, Mix 100, Level −6, OS 2x(`--os`로 변경). 샘플레이트 `--fs` 44.1/48/96 kHz.
* 지표: 소신호 이득, onset(1 dB), THD/h2·h3·h5·h7, 출력 RMS, 3953 Hz 비배음(16 kHz 이하), 합성 프레이즈 왜곡 잔차,
  볼륨 1/8 클린업 잔차, 어택/서스테인 게인 변화, CPU(ns/샘플, 모노), 지연.
* 출력: `grid.csv`, `report.json`, `renders/*_raw.wav`·`*_matched-24dBFS.wav`, `abx/trial_NN/{A,B,X}.wav`, 정답 키(청취 후 열람).
* 그림: `tools/benchmark/plot_compare_drive.py`. 프리셋 영향: `tools/benchmark/preset_mapping_impact.py`.
* CPU는 같은 기계에서 다른 무거운 작업과 동시에 돌리지 않은 실행만 비교에 사용. Mac과 Linux 수치는 섞지 않음.
* 버퍼 32–512: 엔진은 블록 크기 불변(테스트 `block_size_invariance`), 버퍼별 실시간 여유는 `run_benchmarks.py`.

## 3-2. 앰프 측정 (v0.5, `tests/audio/test_amp_ab763.cpp`)
* 게인 스테이징·볼륨별 THD/배음(220 Hz, −12 dBFS), 소신호 주파수 응답(re 1 kHz), Sag(B+ 궤적: 유휴, 버스트 강하, 63 % 시간, 200 ms 복귀),
  SR 독립(44.1/48/96 kHz, 모델별), 극단 입력(+12 dBFS 잡음 + NaN, 44.1–192 kHz, 1x/8x), 프리셋 저장·복원, 콜백 할당 0.
* 모델 간 비교는 같은 노브 값 기준. 실측 앰프·SPICE 비교는 아직 없음(Level 4 근거 아님).

## 3-3. 리버브 측정 (v0.5, `tools/benchmark/reverb_metrics.py`, `tests/audio/test_reverb_premium.cpp`)
* 단위 임펄스(mix 100 %, early 0, pre-delay 0) 12 s 렌더.
* RT60: Schroeder EDC, T20(−5…−25 dB) 선형 적합. 옥타브 대역은 영패딩 FFT 브릭월(순환 겹침 방지).
* 에코 밀도: NED(Abel & Huang), 20 ms 창, 50/150/400 ms.
* 스테레오: 0.3–1.3 s 꼬리 L/R 상관.
* 금속성(metallic) 지표: 0.3–1.3 s 꼬리 스펙트럼에서 좁은 피크 / 1/24 옥타브 평균의 최댓값(dB). 낮을수록 고름. 청감 판단을 대신하지 않음.
* 안정성: Decay 20 s·Size 150·Mod 최대에서 잡음 1 s 후 10 s, 에너지 감소·유한. 바이패스 트레일, 메모리(바이트) 기록.
* CPU: `mfx_render` 10 s 스테레오 렌더 3회 최솟값(파일 I/O 포함 상한값).

## 4. 최종 평가 항목 (실제 장비 확보 후)

ESR / Pre-emphasis ESR / MRSTFT / 배음 스펙트럼 / IMD(SMPTE 60 Hz + 7 kHz 4:1) / 엔벨로프 오차(다이내믹 응답) /
CPU / 메모리 / 지연 / 라우드니스 매칭 ABX. 측정 장비 연결·보정 절차: `tools/measurement/measure_device.py`.
