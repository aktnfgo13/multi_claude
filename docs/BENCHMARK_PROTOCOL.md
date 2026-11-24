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

## 4. 최종 평가 항목 (실제 장비 확보 후)

ESR / Pre-emphasis ESR / MRSTFT / 배음 스펙트럼 / IMD(SMPTE 60 Hz + 7 kHz 4:1) / 엔벨로프 오차(다이내믹 응답) /
CPU / 메모리 / 지연 / 라우드니스 매칭 ABX. 측정 장비 연결·보정 절차: `tools/measurement/measure_device.py`.
