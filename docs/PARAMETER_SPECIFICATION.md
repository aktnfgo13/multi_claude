# 파라미터 명세 (PARAMETER_SPECIFICATION)

* 이 문서의 이펙트별 표(§3)는 엔진에서 자동 생성합니다: `python3 tools/docs/gen_parameter_table.py build`
  (`mfx_render --describe`) — 코드와 문서가 어긋나지 않게 하기 위함입니다.
* 호스트 노출: 16 슬롯 × 16 파라미터 + 슬롯별 Bypass (`SlotParameter`, 정규화 0–1). 슬롯에 바인딩된 이펙트의 메타데이터는
  seqlock으로 게시되며, 호스트 스레드에서는 이펙트 객체에 접근하지 않습니다(값은 메시지 스레드 타이머가 적용).
* 정규화: `paramToNormalized / paramFromNormalized` (`src/core/Parameter.h`). 선형·로그·이산 스케일 지원.
  이산 파라미터는 setValue 시 양자화합니다.

## 1. Custom Drive 파라미터 (지시서 표 유지)

| 파라미터 | 범위 | 기본값 | 의미 (매핑 v2) |
|---|---|---|---|
| Drive | 0–100 | 25 | 클리핑단 입력 이득 −6…+30 dB, 출력에서 15 % 보상 |
| Headroom | 0–100 | 50 | 클리핑 임계 Vth −18…0 dBFS, 출력에서 50 % 보상 |
| Tone | 0–100 | 50 | 틸트 ±6 dB @800 Hz + Fizz LPF 2.5–14 kHz |
| Tight | 0–100 | 50 | 클리핑 전 HPF 30–900 Hz |
| Crunch | 0–100 | 50 | 니 경도(p 2→6), 잔여 기울기 κ 감소, 2단 스테이지 |
| Grain | 0–100 | 50 | 동작점 β(짝수 배음), 엠퍼시스 0–5 dB, 동적 바이어스 깊이 |
| Mid | 0–100 | 50 | 클리핑 전 ±10 dB 피크/스쿱(후단 30 %) |
| Mix | 0–100 % | 100 % | 병렬 클린(지연 정렬) |
| Level | −24…+12 dB | −6 dB | 출력 |
| Oversampling | 1x / 2x / 4x / 8x | 2x | 비선형 구간만 |
| Model (v0.4, 마지막에 추가) | v1 (legacy) / v2 / v2 + ADAA | v2 | v1 = v0.1 소리(비트 동일), v2 = v0.2 소리(기본), v2 + ADAA = v2 곡선 + 1차 ADAA(고게인 앨리어싱 감소) |

## 2. 매핑 변경 이력

### v2 (2026-11-24, 통합 고도화) — 변경 이유

공통 벤치마크(E-013, `tests/results/compare_drive/`)에서 v1이 실제 기타 레벨(피크 −7.2 dBFS)에 비해 과도하게 뜨거웠습니다.

| 지표 (Headroom 50, OS 2x, 입력 −7.2 dBFS) | v1 D0 | v2 D0 | v1 D50 | v2 D50 | Codex D50 |
|---|---|---|---|---|---|
| 소신호 이득 | +6.5 dB | −7.0 dB | — | +8.3 dB | +7.2 dB |
| THD (440 Hz) | 22.4 % | 6.1 % | 32.7 % | 26.6 % | 14.5 % |
| 앨리어싱 (3953 Hz) | −51.8 dB | −92.9 dB | −22.8 dB | −51.3 dB | −74.4 dB |
| 프레이즈 왜곡 잔차 | 23.9 % | 2.7 % | 60 % | 22.5 % | 11.3 % |
| 어택 게인 변화 | −8 dB | −0.2 dB | −30.6 dB | −5.8 dB | −3.3 dB |
| 클린업(볼륨 1/8) 잔차 | — | 0.4 % | 37.9 % | 3.1 % | 0.5 % |

변경 내용:
1. **게인 스테이징**: Drive 0–48 dB → −6…+30 dB. Headroom −24…0 → −18…0 dBFS. makeup +12 dB 고정 → Headroom 50 %·Drive 15 % 보상.
   → Drive 0 근처는 실제 험버커 레벨에서 클린에 가깝고, 기타 볼륨을 줄이면 클린업됩니다.
2. **Drive와 Headroom의 역할**: Drive = 얼마나 세게 클리퍼를 미는가(소신호 이득), Headroom = 어디서 포화가 시작되는가.
   v1은 Headroom이 소신호 레벨을 전혀 바꾸지 않아 "포화 시작점만 움직이는 숨은 레벨 노브"처럼 느껴질 수 있었고,
   반대로 완전 비보상이면 볼륨 노브가 됩니다. v2는 절반만 보상합니다(소신호 9 dB, 1 dB 압축점 약 18 dB 이동).
3. **클리핑 곡선**: |z|^p 대수 시그모이드(0에서 비해석적 → 고차 배음 느린 감쇠) → 해석적 니 곡선 계열 + κ·asinh 잔여 기울기.
   포화 후에도 입력 1 dB당 출력이 조금씩 증가해 피킹 다이내믹이 남고, 과도한 벽돌형 압축이 줄어듭니다.
4. **비대칭**: Vp/Vn 분리(꺾인 기울기) → 동작점 오프셋 β(해석적, 짝수 배음 유지).
5. **Fizz/앨리어싱**: 클리핑 전 7.2 kHz LPF2(피드백 커패시터형 대역 제한), 엠퍼시스 15 → 5 dB.
6. **Crunch**: p 1.5–10 → 2–6(해석 계열), κ로 니 폭 조절, 2단 스테이지 1+2n → 1+0.3n.

남은 차이: 같은 노브 값의 중·고게인(D≥50)에서 앨리어싱은 여전히 Codex보다 20–25 dB 높습니다(2x 기준).
4x에서는 D50 −92.8 dB로 충분하며(D100은 4x −36 dB, 8x −48 dB), Claude 4x CPU(353 ns/샘플)가 Codex 2x(668 ns/샘플)보다 가볍습니다(같은 기계).
기본 OS를 2x로 유지하라는 지시를 따르되, 사용자 안내에 "Drive 60 이상은 4x 권장"을 적었습니다(KNOWN_ISSUES K-001).

### v0.4: Model 파라미터 (기존 소리 보존)
* `model`은 Custom Drive 파라미터 목록의 **끝(index 10)**에 추가되었습니다. 기존 파라미터 index·id는 그대로라 호스트 자동화가 유지됩니다.
* 프리셋/호스트 상태에 `model`이 없으면(v0.2–v0.3에서 저장) 기본값 v2로 로드됩니다 → MFX Claude(McL1)로 저장된 모든 기존 상태는 v2 소리 그대로.
* v0.1 소리는 `model = v1 (legacy)`로 선택합니다(v0.1 바이너리와 출력 비트 동일, 4개 설정 확인).
  v0.1로 만든 Logic 프로젝트는 별도 AU "MultiEffects"(Mfxp/Mfx1)를 쓰므로 그 AU가 설치되어 있는 한 v1 소리 그대로입니다.
  v0.1 시절 저장한 **JSON 프리셋 파일**은 형식이 같아 자동 구분이 불가능합니다 → 로드 후 Model을 v1로 바꾸면 됩니다.
  v0.4부터 프리셋에 `app_version`을 기록해 이후에는 구분 가능합니다.

### v0.5: 추가 파라미터와 저장 호환 (`legacyValueForMissingParam`)
| 이펙트 | 파라미터(추가 위치) | 새 인스턴스 기본 | 저장값에 없을 때 | 이유 |
|---|---|---|---|---|
| Custom Drive | `adaa_comp` (index 11, Toggle) | On | Off (v0.4 소리) | v2 + ADAA의 고역 감쇠·드라이/웻 정렬 보정. v1·v2 소리에는 영향 없음 |
| AB763 Normal | `model` (index 6, M1/M2) | M2 | M1 (v0.4 소리) | 기존 프리셋 06·v0.4 Logic 프로젝트 보존 |
| Premium Plate | 새 type `reverb_plate_premium` | — | — | 기존 `reverb` 무변경 |
프리셋 로더는 저장 데이터에 없는 파라미터에 대해 이펙트가 지정한 "레거시 값"을 넣습니다(`src/presets/Preset.cpp`).

### 기존 프리셋 영향과 조치

합성 Karplus-Strong DI(피크 −7.2 dBFS, 녹음 아님)로 Custom Drive 블록만 렌더해 측정했습니다
(`tools/benchmark/preset_mapping_impact.py`, `tests/results/compare_drive/preset_impact_*.json`).
v2에서 값을 그대로 두면 02/03 프리셋의 출력이 14–19 dB 낮아져(v1은 앰프를 +1…+9 dBFS 피크로 밀던 부스트) 프리셋 의도가 깨집니다.
그래서 Drive/Level만 다시 맞췄습니다(출력 RMS를 v1과 0.2 dB 이내로, 레벨 매칭 차이가 최소가 되도록 격자 탐색).

| 프리셋 | 조정 | 출력 레벨 차 (v1→v2) | 레벨 매칭 후 파형 차이 |
|---|---|---|---|
| 02_Crunch_Rhythm.json | drive 15 → 70, level → 3 dB | -0.20 dB | 41 % |
| 03_High_Gain_Lead.json | drive 0 → 55, level → 9 dB | -0.12 dB | 43 % |
| 05_Custom_Drive_Showcase.json | drive 55 → 95, level → -3 dB | -0.19 dB | 50 % |

* 레벨 매칭 후에도 40–50 % 차이가 남습니다. v1 음색(강한 포화·fizz)을 그대로 재현하지 않는 것이 이번 변경의 목적이기 때문입니다.
  **청감 확인이 필요합니다(사용자 확인 항목).**
* v0.1 사용자 프리셋은 마이그레이션하지 않습니다(미출시, D-017). 필요하면 `customDriveMappingV1`로 변환 도구를 만들 수 있습니다.

### v0.6: 추가 파라미터
* `delay`: `cross_fb`, `fb_lr_offset`, `width`, `time_r_mode`, `time_r_ms`를 끝에 추가. 기본값(0, 0, 100 %, Offset)은 v0.5 출력과 비트 동일이므로
  레거시 값 훅이 필요 없습니다(저장 데이터에 없으면 기본값 = 기존 동작).
* 새 타입: `delay_tape_premium`, `delay_bbd_premium`, `comp_fet`. 기존 프리셋은 이 타입을 포함하지 않으므로 자동 활성화되지 않습니다.

### v0.7: 추가 파라미터
* `delay_bbd_premium`: `model`(v1/v2), `chips`(1/2/4, v2 전용)을 끝에 추가. 저장 데이터에 `model`이 없으면 v1(= v0.6 출력 비트 동일),
  새 인스턴스는 v2·2 chips.
* 새 타입: `gate_premium`(12개), `eq_parametric_premium`(21개: 밴드별 freq/gain/Q 15개 + output = 인덱스 0–15 호스트 자동화 범위,
  밴드 타입 5개는 16–20 → Logic 자동화 대상 아님).
* 호스트 자동화는 슬롯당 앞 16개 파라미터만 노출(`kParamsPerSlot` = 16). `delay`의 `time_r_ms`(인덱스 16)는 자동화 불가(K-048).

## 3. 이펙트별 파라미터 (자동 생성, v0.7)

### `utility` — Gain / Volume

| id | 이름 | 범위 | 기본값 | 단위 | 스케일 |
|---|---|---|---|---|---|
| `gain` | Gain | -60…24 | 0 | dB | linear |
| `volume` | Volume | 0…100 | 100 | % | linear |
| `invert` | Phase Invert | 0…1 | 0 |  | linear |
| `invert_r` | Invert Right | 0…1 | 0 |  | linear |

### `pan` — Pan / Width

| id | 이름 | 범위 | 기본값 | 단위 | 스케일 |
|---|---|---|---|---|---|
| `pan` | Pan | -100…100 | 0 | % | linear |
| `width` | Width | 0…200 | 100 | % | linear |

### `tuner` — Tuner

| id | 이름 | 범위 | 기본값 | 단위 | 스케일 |
|---|---|---|---|---|---|
| `mute` | Mute Output | 0…1 | 1 |  | linear |
| `reference_hz` | Reference A4 | 430…450 | 440 | Hz | linear |

### `noise_gate` — Noise Gate

| id | 이름 | 범위 | 기본값 | 단위 | 스케일 |
|---|---|---|---|---|---|
| `threshold_db` | Threshold | -96…0 | -60 | dB | linear |
| `hysteresis_db` | Hysteresis | 0…20 | 6 | dB | linear |
| `attack_ms` | Attack | 0.100000001…50 | 1 | ms | log |
| `hold_ms` | Hold | 0…500 | 20 | ms | linear |
| `release_ms` | Release | 5…2000 | 120 | ms | log |
| `range_db` | Range | -96…0 | -80 | dB | linear |

### `compressor` — Compressor

| id | 이름 | 범위 | 기본값 | 단위 | 스케일 |
|---|---|---|---|---|---|
| `threshold_db` | Threshold | -60…0 | -20 | dB | linear |
| `ratio` | Ratio | 1…20 | 4 | :1 | log |
| `knee_db` | Knee | 0…24 | 6 | dB | linear |
| `attack_ms` | Attack | 0.100000001…200 | 10 | ms | log |
| `release_ms` | Release | 10…2000 | 150 | ms | log |
| `makeup_db` | Makeup | 0…30 | 0 | dB | linear |
| `mix` | Mix | 0…100 | 100 | % | linear |

### `limiter` — Limiter

| id | 이름 | 범위 | 기본값 | 단위 | 스케일 |
|---|---|---|---|---|---|
| `ceiling_db` | Ceiling | -24…0 | -0.300000012 | dB | linear |
| `release_ms` | Release | 5…1000 | 80 | ms | log |
| `input_db` | Input | -12…24 | 0 | dB | linear |

### `comp_fet` — FET Compressor

| id | 이름 | 범위 | 기본값 | 단위 | 스케일 |
|---|---|---|---|---|---|
| `input_db` | Input | -20…40 | 0 | dB | linear |
| `output_db` | Output | -30…20 | 0 | dB | linear |
| `attack_us` | Attack | 20…800 | 200 |  | log |
| `release_ms` | Release | 50…1100 | 250 | ms | log |
| `ratio` | Ratio | 4:1 / 8:1 / 12:1 / 20:1 / All | 0 |  | linear |
| `sc_hpf_hz` | SC High-Pass | 20…500 | 20 | Hz | log |
| `character` | Character | 0…100 | 50 | % | linear |
| `mix` | Mix | 0…100 | 100 | % | linear |

### `gate_premium` — Premium Gate

| id | 이름 | 범위 | 기본값 | 단위 | 스케일 |
|---|---|---|---|---|---|
| `threshold_db` | Threshold | -90…0 | -55 | dB | linear |
| `range_db` | Range | -90…0 | -60 | dB | linear |
| `attack_ms` | Attack | 0.0500000007…20 | 0.5 | ms | log |
| `hold_ms` | Hold | 0…500 | 40 | ms | linear |
| `release_ms` | Release | 5…3000 | 250 | ms | log |
| `ratio` | Ratio | 1.5…20 | 4 | :1 | log |
| `hysteresis_db` | Hysteresis | 0…20 | 6 | dB | linear |
| `mode` | Mode | Gate / Expander | 1 |  | linear |
| `detector` | Detector | Peak / RMS | 0 |  | linear |
| `sc_hpf_hz` | SC High-Pass | 20…500 | 80 | Hz | log |
| `stereo_link` | Stereo Link | 0…1 | 1 |  | linear |
| `adaptive_release` | Adaptive Release | 0…1 | 1 |  | linear |

### `clean_boost` — Clean Boost

| id | 이름 | 범위 | 기본값 | 단위 | 스케일 |
|---|---|---|---|---|---|
| `gain` | Gain | 0…24 | 6 | dB | linear |
| `low_cut` | Low Cut | 20…400 | 20 | Hz | log |
| `bright` | Bright | -6…6 | 0 | dB | linear |

### `overdrive` — Overdrive

| id | 이름 | 범위 | 기본값 | 단위 | 스케일 |
|---|---|---|---|---|---|
| `drive` | Drive | 0…100 | 50 | % | linear |
| `tone` | Tone | 0…100 | 50 | % | linear |
| `mix` | Mix | 0…100 | 100 | % | linear |
| `level` | Level | -40…40 | -6 | dB | linear |
| `oversampling` | Oversampling | 1x / 2x / 4x / 8x | 2 |  | linear |

### `distortion` — Distortion

| id | 이름 | 범위 | 기본값 | 단위 | 스케일 |
|---|---|---|---|---|---|
| `distortion` | Distortion | 0…100 | 50 | % | linear |
| `filter` | Filter | 0…100 | 40 | % | linear |
| `mix` | Mix | 0…100 | 100 | % | linear |
| `level` | Level | -40…40 | -10 | dB | linear |
| `oversampling` | Oversampling | 1x / 2x / 4x / 8x | 2 |  | linear |

### `fuzz` — Fuzz

| id | 이름 | 범위 | 기본값 | 단위 | 스케일 |
|---|---|---|---|---|---|
| `fuzz` | Fuzz | 0…100 | 70 | % | linear |
| `bias` | Bias | 0…100 | 60 | % | linear |
| `tone` | Tone | 0…100 | 50 | % | linear |
| `mix` | Mix | 0…100 | 100 | % | linear |
| `level` | Level | -40…40 | -12 | dB | linear |
| `oversampling` | Oversampling | 1x / 2x / 4x / 8x | 2 |  | linear |

### `custom_drive` — Custom Drive

| id | 이름 | 범위 | 기본값 | 단위 | 스케일 |
|---|---|---|---|---|---|
| `drive` | Drive | 0…100 | 25 | % | linear |
| `headroom` | Headroom | 0…100 | 50 | % | linear |
| `tone` | Tone | 0…100 | 50 | % | linear |
| `tight` | Tight | 0…100 | 50 | % | linear |
| `crunch` | Crunch | 0…100 | 50 | % | linear |
| `grain` | Grain | 0…100 | 50 | % | linear |
| `mid` | Mid Character | 0…100 | 50 | % | linear |
| `mix` | Mix | 0…100 | 100 | % | linear |
| `level` | Level | -24…12 | -6 | dB | linear |
| `oversampling` | Oversampling | 1x / 2x / 4x / 8x | 1 |  | linear |
| `model` | Model | v1 (legacy) / v2 / v2 + ADAA | 1 |  | linear |
| `adaa_comp` | ADAA Compensation | 0…1 | 1 |  | linear |

### `ddsp_drive` — DDSP Drive (research)

| id | 이름 | 범위 | 기본값 | 단위 | 스케일 |
|---|---|---|---|---|---|
| `hpf_hz` | Pre HPF | 5…2000 | 20 | Hz | log |
| `gain_db` | Gain | -12…60 | 12 | dB | linear |
| `thr_pos` | Thr + | 0.00999999978…4 | 0.5 |  | log |
| `thr_neg` | Thr - | 0.00999999978…4 | 0.5 |  | log |
| `knee` | Knee p | 0.5…16 | 2 |  | log |
| `lpf_hz` | Post LPF | 300…22000 | 8000 | Hz | log |
| `out_db` | Out Gain | -40…40 | 0 | dB | linear |
| `knob` | Knob (LUT) | 0…100 | 50 | % | linear |
| `mix` | Mix | 0…100 | 100 | % | linear |
| `level` | Level | -40…40 | 0 | dB | linear |
| `oversampling` | Oversampling | 1x / 2x / 4x / 8x | 2 |  | linear |

### `filter` — Filter

| id | 이름 | 범위 | 기본값 | 단위 | 스케일 |
|---|---|---|---|---|---|
| `type` | Type | Low Pass 12 / Low Pass 24 / High Pass 12 / High Pass 24 / Low Shelf / High Shelf / Peak / Band Pass / Notch | 0 |  | linear |
| `freq_hz` | Frequency | 20…20000 | 5000 | Hz | log |
| `q` | Q | 0.100000001…18 | 0.707099974 |  | log |
| `gain_db` | Gain | -24…24 | 0 | dB | linear |

### `parametric_eq` — Parametric EQ

| id | 이름 | 범위 | 기본값 | 단위 | 스케일 |
|---|---|---|---|---|---|
| `low_cut_hz` | Low Cut | 10…1000 | 10 | Hz | log |
| `low_hz` | Low Freq | 30…500 | 100 | Hz | log |
| `low_db` | Low Gain | -18…18 | 0 | dB | linear |
| `p1_hz` | Peak 1 Freq | 40…2000 | 250 | Hz | log |
| `p1_db` | Peak 1 Gain | -18…18 | 0 | dB | linear |
| `p1_q` | Peak 1 Q | 0.200000003…10 | 1 |  | log |
| `p2_hz` | Peak 2 Freq | 150…6000 | 800 | Hz | log |
| `p2_db` | Peak 2 Gain | -18…18 | 0 | dB | linear |
| `p2_q` | Peak 2 Q | 0.200000003…10 | 1 |  | log |
| `p3_hz` | Peak 3 Freq | 500…16000 | 3000 | Hz | log |
| `p3_db` | Peak 3 Gain | -18…18 | 0 | dB | linear |
| `p3_q` | Peak 3 Q | 0.200000003…10 | 1 |  | log |
| `high_hz` | High Freq | 1000…16000 | 6000 | Hz | log |
| `high_db` | High Gain | -18…18 | 0 | dB | linear |
| `high_cut_hz` | High Cut | 1000…22000 | 22000 | Hz | log |
| `output_db` | Output | -24…24 | 0 | dB | linear |

### `graphic_eq` — Graphic EQ

| id | 이름 | 범위 | 기본값 | 단위 | 스케일 |
|---|---|---|---|---|---|
| `b31` | 31 Hz | -12…12 | 0 | dB | linear |
| `b62` | 62 Hz | -12…12 | 0 | dB | linear |
| `b125` | 125 Hz | -12…12 | 0 | dB | linear |
| `b250` | 250 Hz | -12…12 | 0 | dB | linear |
| `b500` | 500 Hz | -12…12 | 0 | dB | linear |
| `b1k` | 1 kHz | -12…12 | 0 | dB | linear |
| `b2k` | 2 kHz | -12…12 | 0 | dB | linear |
| `b4k` | 4 kHz | -12…12 | 0 | dB | linear |
| `b8k` | 8 kHz | -12…12 | 0 | dB | linear |
| `b16k` | 16 kHz | -12…12 | 0 | dB | linear |
| `output_db` | Output | -24…12 | 0 | dB | linear |

### `eq_parametric_premium` — Premium EQ

| id | 이름 | 범위 | 기본값 | 단위 | 스케일 |
|---|---|---|---|---|---|
| `b1_hz` | Band 1 Freq | 20…20000 | 100 | Hz | log |
| `b1_db` | Band 1 Gain | -24…24 | 0 | dB | linear |
| `b1_q` | Band 1 Q | 0.100000001…18 | 0.707099974 |  | log |
| `b2_hz` | Band 2 Freq | 20…20000 | 400 | Hz | log |
| `b2_db` | Band 2 Gain | -24…24 | 0 | dB | linear |
| `b2_q` | Band 2 Q | 0.100000001…18 | 0.707099974 |  | log |
| `b3_hz` | Band 3 Freq | 20…20000 | 1500 | Hz | log |
| `b3_db` | Band 3 Gain | -24…24 | 0 | dB | linear |
| `b3_q` | Band 3 Q | 0.100000001…18 | 0.707099974 |  | log |
| `b4_hz` | Band 4 Freq | 20…20000 | 4000 | Hz | log |
| `b4_db` | Band 4 Gain | -24…24 | 0 | dB | linear |
| `b4_q` | Band 4 Q | 0.100000001…18 | 0.707099974 |  | log |
| `b5_hz` | Band 5 Freq | 20…20000 | 8000 | Hz | log |
| `b5_db` | Band 5 Gain | -24…24 | 0 | dB | linear |
| `b5_q` | Band 5 Q | 0.100000001…18 | 0.707099974 |  | log |
| `output_db` | Output | -24…24 | 0 | dB | linear |
| `b1_type` | Band 1 Type | Off / Bell / Low Shelf / High Shelf / HPF 12 / HPF 24 / LPF 12 / LPF 24 / Notch | 2 |  | linear |
| `b2_type` | Band 2 Type | Off / Bell / Low Shelf / High Shelf / HPF 12 / HPF 24 / LPF 12 / LPF 24 / Notch | 1 |  | linear |
| `b3_type` | Band 3 Type | Off / Bell / Low Shelf / High Shelf / HPF 12 / HPF 24 / LPF 12 / LPF 24 / Notch | 1 |  | linear |
| `b4_type` | Band 4 Type | Off / Bell / Low Shelf / High Shelf / HPF 12 / HPF 24 / LPF 12 / LPF 24 / Notch | 1 |  | linear |
| `b5_type` | Band 5 Type | Off / Bell / Low Shelf / High Shelf / HPF 12 / HPF 24 / LPF 12 / LPF 24 / Notch | 3 |  | linear |

### `amp` — Amp

| id | 이름 | 범위 | 기본값 | 단위 | 스케일 |
|---|---|---|---|---|---|
| `model` | Model | Clean / Crunch / High Gain / Custom | 1 |  | linear |
| `gain` | Gain | 0…100 | 50 | % | linear |
| `bass` | Bass | 0…100 | 50 | % | linear |
| `mid` | Mid | 0…100 | 50 | % | linear |
| `treble` | Treble | 0…100 | 50 | % | linear |
| `presence` | Presence | 0…100 | 40 | % | linear |
| `depth` | Depth | 0…100 | 40 | % | linear |
| `master` | Master | 0…100 | 50 | % | linear |
| `sag` | Sag | 0…100 | 50 | % | linear |
| `bright` | Bright | 0…1 | 0 |  | linear |
| `mix` | Mix | 0…100 | 100 | % | linear |
| `level` | Level | -40…40 | 0 | dB | linear |
| `oversampling` | Oversampling | 1x / 2x / 4x / 8x | 1 |  | linear |

### `amp_ab763` — AB763 Normal (M1)

| id | 이름 | 범위 | 기본값 | 단위 | 스케일 |
|---|---|---|---|---|---|
| `volume` | Volume | 0…10 | 4 |  | linear |
| `treble` | Treble | 0…10 | 6 |  | linear |
| `bass` | Bass | 0…10 | 4 |  | linear |
| `mix` | Mix | 0…100 | 100 | % | linear |
| `level` | Level | -40…40 | 0 | dB | linear |
| `oversampling` | Oversampling | 1x / 2x / 4x / 8x | 1 |  | linear |
| `model` | Model | M1 / M2 (PI + 6V6 + sag + NFB + load) | 1 |  | linear |

### `nam_amp` — NAM Amp

| id | 이름 | 범위 | 기본값 | 단위 | 스케일 |
|---|---|---|---|---|---|
| `input_db` | Input | -24…24 | 0 | dB | linear |
| `output_db` | Output | -40…24 | 0 | dB | linear |
| `normalize` | Normalize Loudness | 0…1 | 1 |  | linear |

### `cabinet` — Cabinet / IR

| id | 이름 | 범위 | 기본값 | 단위 | 스케일 |
|---|---|---|---|---|---|
| `source` | Source | Built-in Sim / User IR | 0 |  | linear |
| `sim_model` | Sim Model | 4x12 Closed / 2x12 Open / 1x12 Combo | 0 |  | linear |
| `low_cut_hz` | Low Cut | 20…500 | 60 | Hz | log |
| `high_cut_hz` | High Cut | 2000…20000 | 12000 | Hz | log |
| `ir_level_db` | IR Level | -24…24 | 0 | dB | linear |
| `mix` | IR Mix | 0…100 | 100 | % | linear |
| `normalize` | Normalize | 0…1 | 1 |  | linear |

### `chorus` — Chorus

| id | 이름 | 범위 | 기본값 | 단위 | 스케일 |
|---|---|---|---|---|---|
| `rate_hz` | Rate | 0.0500000007…8 | 0.800000012 | Hz | log |
| `depth` | Depth | 0…100 | 50 | % | linear |
| `delay_ms` | Delay | 3…30 | 12 | ms | linear |
| `mix` | Mix | 0…100 | 50 | % | linear |
| `spread` | Stereo Spread | 0…100 | 50 | % | linear |

### `flanger` — Flanger

| id | 이름 | 범위 | 기본값 | 단위 | 스케일 |
|---|---|---|---|---|---|
| `rate_hz` | Rate | 0.0199999996…5 | 0.25 | Hz | log |
| `depth` | Depth | 0…100 | 70 | % | linear |
| `delay_ms` | Manual | 0.300000012…8 | 1.5 | ms | log |
| `feedback` | Feedback | -95…95 | 50 | % | linear |
| `mix` | Mix | 0…100 | 50 | % | linear |
| `spread` | Stereo Spread | 0…100 | 25 | % | linear |

### `phaser` — Phaser

| id | 이름 | 범위 | 기본값 | 단위 | 스케일 |
|---|---|---|---|---|---|
| `stages` | Stages | 4 / 6 / 8 / 12 | 1 |  | linear |
| `rate_hz` | Rate | 0.0199999996…8 | 0.5 | Hz | log |
| `depth` | Depth | 0…100 | 70 | % | linear |
| `centre_hz` | Centre | 150…3000 | 700 | Hz | log |
| `feedback` | Feedback | -90…90 | 30 | % | linear |
| `mix` | Mix | 0…100 | 50 | % | linear |
| `stereo` | Stereo | 0…100 | 50 | % | linear |

### `tremolo` — Tremolo

| id | 이름 | 범위 | 기본값 | 단위 | 스케일 |
|---|---|---|---|---|---|
| `rate_hz` | Rate | 0.200000003…16 | 4 | Hz | log |
| `depth` | Depth | 0…100 | 50 | % | linear |
| `shape` | Shape | Sine / Triangle / Square (soft) | 0 |  | linear |
| `stereo` | Stereo (Pan) | 0…1 | 0 |  | linear |
| `sync` | Sync | Off / 1/1 / 1/2 / 1/4 / 1/4 D / 1/4 T / 1/8 / 1/8 D / 1/8 T / 1/16 | 0 |  | linear |

### `vibrato` — Vibrato

| id | 이름 | 범위 | 기본값 | 단위 | 스케일 |
|---|---|---|---|---|---|
| `rate_hz` | Rate | 0.100000001…12 | 5 | Hz | log |
| `depth` | Depth | 0…100 | 40 | % | linear |

### `delay` — Delay

| id | 이름 | 범위 | 기본값 | 단위 | 스케일 |
|---|---|---|---|---|---|
| `mode` | Mode | Digital / Analog / Tape | 0 |  | linear |
| `routing` | Routing | Mono / Stereo / Ping Pong | 1 |  | linear |
| `time_ms` | Time | 10…2000 | 380 | ms | log |
| `sync` | Sync | Off / 1/1 / 1/2 / 1/4 / 1/4 D / 1/4 T / 1/8 / 1/8 D / 1/8 T / 1/16 | 0 |  | linear |
| `feedback` | Feedback | 0…100 | 35 | % | linear |
| `mix` | Mix | 0…100 | 30 | % | linear |
| `low_cut_hz` | Low Cut | 20…1000 | 80 | Hz | log |
| `high_cut_hz` | High Cut | 1000…20000 | 9000 | Hz | log |
| `mod_depth` | Mod Depth | 0…100 | 10 | % | linear |
| `mod_rate_hz` | Mod Rate | 0.0500000007…5 | 0.600000024 | Hz | log |
| `age` | Age | 0…100 | 30 | % | linear |
| `offset` | Stereo Offset | -50…50 | 12 | % | linear |
| `cross_fb` | Cross Feedback | 0…100 | 0 | % | linear |
| `fb_lr_offset` | Feedback L/R | -50…50 | 0 | % | linear |
| `width` | Width | 0…200 | 100 | % | linear |
| `time_r_mode` | Right Time | Offset / Free | 0 |  | linear |
| `time_r_ms` | Time R | 10…2000 | 380 | ms | log |

### `reverb` — Reverb

| id | 이름 | 범위 | 기본값 | 단위 | 스케일 |
|---|---|---|---|---|---|
| `type` | Type | Room / Hall / Plate / Spring / Ambient | 1 |  | linear |
| `decay_s` | Decay | 0.200000003…20 | 2.20000005 | s | log |
| `predelay_ms` | Pre-Delay | 0…200 | 15 | ms | linear |
| `size` | Size | 0…100 | 50 | % | linear |
| `damping` | Damping | 0…100 | 40 | % | linear |
| `low_cut_hz` | Low Cut | 20…1000 | 120 | Hz | log |
| `high_cut_hz` | High Cut | 1000…20000 | 9000 | Hz | log |
| `modulation` | Modulation | 0…100 | 25 | % | linear |
| `mix` | Mix | 0…100 | 25 | % | linear |
| `width` | Width | 0…100 | 100 | % | linear |

### `reverb_plate_premium` — Premium Plate

| id | 이름 | 범위 | 기본값 | 단위 | 스케일 |
|---|---|---|---|---|---|
| `decay_s` | Decay | 0.300000012…20 | 2.5 | s | log |
| `predelay_ms` | Pre-Delay | 0…250 | 20 | ms | linear |
| `size` | Size | 50…150 | 100 | % | linear |
| `damping_hz` | Damping | 1000…18000 | 6500 | Hz | log |
| `bass_mult` | Bass Decay | 0.5…2 | 1.20000005 | :1 | linear |
| `crossover_hz` | Crossover | 100…1000 | 300 | Hz | log |
| `diffusion` | Diffusion | 0…100 | 100 | % | linear |
| `mod_depth` | Mod Depth | 0…100 | 30 | % | linear |
| `mod_rate_hz` | Mod Rate | 0.100000001…3 | 0.800000012 | Hz | log |
| `early` | Early | 0…100 | 15 | % | linear |
| `lowcut_hz` | Low Cut | 20…500 | 80 | Hz | log |
| `width` | Width | 0…100 | 100 | % | linear |
| `mix` | Mix | 0…100 | 30 | % | linear |

### `delay_tape_premium` — Tape Echo

| id | 이름 | 범위 | 기본값 | 단위 | 스케일 |
|---|---|---|---|---|---|
| `time_ms` | Time | 40…1200 | 350 | ms | log |
| `sync` | Sync | Off / 1/1 / 1/2 / 1/4 / 1/4 D / 1/4 T / 1/8 / 1/8 D / 1/8 T / 1/16 | 0 |  | linear |
| `feedback` | Feedback | 0…110 | 40 | % | linear |
| `mix` | Mix | 0…100 | 35 | % | linear |
| `tone` | Tone | 0…100 | 50 | % | linear |
| `drive` | Drive | 0…100 | 40 | % | linear |
| `age` | Age | 0…100 | 30 | % | linear |
| `wow` | Wow | 0…100 | 25 | % | linear |
| `flutter` | Flutter | 0…100 | 20 | % | linear |
| `modulation` | Modulation | 0…100 | 20 | % | linear |
| `heads` | Heads | Head 1 / Heads 1+2 / Heads 1+2+3 | 0 |  | linear |
| `spread` | Spread | 0…100 | 30 | % | linear |
| `level_db` | Output Level | -24…12 | 0 | dB | linear |

### `delay_bbd_premium` — BBD Analog Delay

| id | 이름 | 범위 | 기본값 | 단위 | 스케일 |
|---|---|---|---|---|---|
| `time_ms` | Time | 20…800 | 300 | ms | log |
| `sync` | Sync | Off / 1/1 / 1/2 / 1/4 / 1/4 D / 1/4 T / 1/8 / 1/8 D / 1/8 T / 1/16 | 0 |  | linear |
| `feedback` | Feedback | 0…110 | 40 | % | linear |
| `mix` | Mix | 0…100 | 35 | % | linear |
| `tone` | Tone | 0…100 | 45 | % | linear |
| `mod_depth` | Mod Depth | 0…100 | 15 | % | linear |
| `mod_rate_hz` | Mod Rate | 0.100000001…8 | 0.5 | Hz | log |
| `compander` | Compander | 0…100 | 100 | % | linear |
| `drive` | Drive | 0…100 | 30 | % | linear |
| `spread` | Spread | 0…100 | 40 | % | linear |
| `level_db` | Output Level | -24…12 | 0 | dB | linear |
| `model` | Model | v1 (1 chip, v0.6) / v2 | 1 |  | linear |
| `chips` | Chips (v2) | 1 chip / 2 chips / 4 chips | 1 |  | linear |
