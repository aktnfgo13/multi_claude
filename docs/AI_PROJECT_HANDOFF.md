# AI 프로젝트 인수인계 (MFX Claude)

- 갱신일: 2026-11-25 · GitHub `aktnfgo13/multi_claude` `main` · 버전 **0.5.0** (구현 커밋 `1e8d10d`, 이후 문서·로그 커밋은 `git log`로 확인)
- 대상: ChatGPT / Claude / Codex 등 후속 개발 AI. 추측하지 말고 이 문서와 링크된 파일을 근거로 진행하세요.

## Quick Context
- 제품: 기타 멀티이펙터 **MFX Claude 0.5.0** (AU/VST3/Standalone, JUCE 8, 코어 JUCE·OS 독립 C++20). 필수 규칙 `AGENTS.md` R1–R6.
- v0.5 구현 DSP: ① AB763 **M2**(LTP 위상반전, 6V6 푸시풀 Koren, 그리드 블로킹, 정류관 Sag, 지연 없는 NFB 루프, OT + 반응성 스피커 부하) — 새 인스턴스 기본, 기존 저장은 M1
  ② Custom Drive `adaa_comp`(ADAA 고역·드라이/웻 정렬 보정) ③ 새 리버브 `reverb_plate_premium`(Dattorro 탱크 + 주파수별 RT60 + ER + 변조) ④ 프리셋 07.
- 기존 기능 보존: 팩토리 01–06, Custom Drive v1/v2/v2+ADAA(보정 Off), AB763 M1, Legacy reverb 출력이 v0.4와 **비트 동일**(cmp 확인). 파라미터 id·AU ID 불변.
- 테스트 플랫폼: Linux x86_64 (C++ 97/97, ASan audio 0건, pluginval strictness 7 PASS), Linux arm64 VM on 사용자 Mac (97/97, `tests/results/arm64_linux_vm_tests_v05.txt`). macOS 네이티브 v0.5 미확인.
- Mac/Logic: v0.4는 사용자 Mac에서 빌드·86/86·auval PASS(2026-11-25). **v0.5 macOS·Logic GUI 미확인**. 구형 Claude AU(Mfxp) 중복은 플러그인 관리자 비활성화 안내.
- 품질 Level(`docs/AUDIO_QUALITY_TARGETS.md`): AB763 M2 = 3(실측 전), Custom Drive = 2, Premium Plate = 2–3, Legacy amp = 1–2. Level 4 이상은 실측·청취 필요.
- 남은 문제: AB763 M2 파라미터 문헌/근사값·SPICE·실측 없음(K-032), Plate 초기 밀도(K-035), parity 미러 범위(K-027), 원문 연구 자료 미확보(K-038).
- 다음 목표: v0.5 Mac/Logic 확인 → 리앰프/DI 측정 인프라 + AB763 SPICE 비교 → Plate 청취 튜닝·Shimmer/Freeze → Delay/Dynamics → 출시 준비.
- 보호 대상: 위 비트 동일 항목, 설치된 AU(Claude·Codex·구형), Codex 프로젝트, Logic 프로젝트, 스냅샷.
- 읽을 파일: `AGENTS.md` → 이 문서 → `docs/KNOWN_ISSUES.md` → `docs/DSP_SPECIFICATION.md`(AB763 M2·ADAA·Plate) → `docs/TECHNICAL_RESEARCH.md`.

## A. 프로젝트 개요
| 항목 | 내용 |
|---|---|
| 목표 | Win/Mac 공용 기타 멀티이펙터 플러그인 + 같은 코어를 쓰는 ARM 하드웨어 |
| 장기 방향 | 제품 트랙(Custom Drive, 이펙트, AU/VST3, 프리셋/UI, 하드웨어)과 연구 트랙(SPICE·DDSP·노브 매핑·Gray-box) 분리. AI는 개발 도구, 제품은 검증된 DSP만 실행 |
| 버전/브랜치 | 0.2.0 / 로컬 `integration/reference-project-review` = 원격 `main` (`3f823fa`) |
| Claude 프로젝트 | 이 저장소. 사용자 Mac 사본 `~/multi_effects_claude`(git 아님, `SOURCE_ID` 파일), 권장 작업 위치 `git clone` 사본 |
| Codex 프로젝트 | `~/multi_effects` (2개월 개발, 청취 승인 이력). 읽기 전용 비교 대상 |
| 공통 설계 원칙 | 단일 DSP 소스, 물리값 JSON 프리셋, 오디오 경로 float·무할당·무락, 선택 모듈(NAM) 분리 — `AGENTS.md` |

## B. 현재 구현 기능
| 기능 | 상태 | 이전/이번 | 주요 파일 |
|---|---|---|---|
| 엔진(직렬+병렬 A/B, 락프리 라우팅, 지연 정렬, 트레일) | 구현 | 이전 | `src/engine/Engine.*`, `RtPublisher.h` |
| 이펙트 25종 + Custom Drive v2 | 구현 | 이전 | `src/dsp/**`, `src/engine/EffectFactory.cpp` |
| 프리셋(JSON, 물리값) | 구현 | 이전 | `src/presets/Preset.*`, `presets/factory/*.json` |
| 호스트 파라미터(16슬롯×16, seqlock) | 구현 | 이전 | `src/plugin/PluginProcessor.*` |
| **실시간 분석 탭 + 워커** | 구현 | **이번** | `src/engine/AnalysisTap.h`, `src/engine/AnalysisWorker.*` |
| **Waveform / Spectrum (Visualize 화면)** | 구현, Logic 미검증 | **이번** | `src/ui/AnalysisView.*`, `src/plugin/PluginEditor.*` |
| 입력 시스템: 입력 게인 ±24 dB, 입력 모드(Auto/Mono L/Mono L+R/Stereo), IN 미터·CLIP | 구현 | 이전 | `Engine::process` 입력부, `PluginEditor` |
| **입력 시스템 보완: 원본 피크, HOT(−3 dBFS), NaN/Inf 가드, 공칭 −20 dBFS 눈금** | 구현 | **이번** | `Engine.h`(`InputLevels`, `MeterValues`), `PluginEditor.*` |
| 캐비닛 IR(분할 컨볼루션, 메시지 스레드 로드) | 구현 | 이전 | `src/dsp/cabinet/*` |
| **캐비닛 IR 프리셋 참조(해시·검색 폴더·누락 대체)** | 구현 | **이번** | `Cabinet.*`, `Effect.h`(`StateContext`), `Preset.*`, `PluginProcessor.cpp` |
| NAM 로더 | 구현(SR 불일치 시 바이패스) | 이전 | `src/neural/NamAmp.*` |
| **Custom Drive Model (v1/v2/v2+ADAA)** | 구현 | **v0.4** | `src/dsp/drive/CustomDrive.*`, `src/dsp/nonlinear/Nonlinear.*`(`kneeAntiderivative`) |
| **AB763 Normal M1 앰프** (`amp_ab763`) | 구현, Logic 미검증 | **v0.4** | `src/dsp/amp/AB763Amp.*`, `presets/factory/06_Blackface_Normal.json` |
| **AB763 M2** (`model` = 1) | 구현, Logic 미검증 | **v0.5** | `AB763Amp::m2Power/m2Forward`, `presets/factory/07_Blackface_M2_Plate.json` |
| **Custom Drive ADAA 보정** (`adaa_comp`) | 구현 | **v0.5** | `CustomDrive::processNonlinearAdaa` |
| **Premium Plate v1** (`reverb_plate_premium`) | 구현, 청취 미검증 | **v0.5** | `src/dsp/reverb/PremiumPlate.*`, `tools/benchmark/reverb_metrics.py` |
| **저장 호환 훅** | 구현 | **v0.5** | `Effect::legacyValueForMissingParam`, `src/presets/Preset.cpp` |

## C. 아키텍처
```
src/core      Effect 인터페이스, ParameterSet, 스무딩, 공용 타입        (JUCE·OS 무의존)
src/dsp       필터·오버샘플러·비선형·이펙트 구현                       (무의존)
src/engine    Engine(라우팅·미터·MIDI·분석 탭), EffectFactory, AnalysisWorker
src/presets   JSON, 프리셋 저장/로드(PresetLoadContext: 외부 파일 검색 폴더)
src/platform  WAV 파일 I/O (유일한 파일 시스템 접근)
src/plugin    JUCE AudioProcessor/Editor (얇은 래퍼)       src/ui  JUCE 컴포넌트(ChainView, ParamPanel, AnalysisView)
```
- 데이터 흐름: 호스트 → `Processor::processBlock` → `Engine::process`(입력 가드·게인 → 레인 0~3 슬롯 → 출력 게인·NaN 가드) → 호스트.
  UI는 atomic 미터(`readMeters`)와 분석 탭(SPSC)만 읽음. 파라미터는 `SlotParameter`(seqlock) → 메시지 스레드 적용.
- 분석 탭: 패킷 256프레임(= `Engine::kChunk`) × 64슬롯, 입력·출력·SR·채널 수·프레임 인덱스·체인 지연 포함. 가득 차면 Drop(오디오 무영향).
  `AnalysisWorker`(스레드 없음)를 UI 타이머가 30 Hz로 poll → 입력을 체인 지연만큼 늦춰 출력과 정렬 → waveform(min/max) / spectrum(Hann, 2048/4096, dBFS, 로그 표시점, 스무딩).
- 실시간 규칙: 오디오 콜백에서 할당·해제·락·파일 I/O·로그·예외 금지. 스레드 공유는 atomic/RtPublisher/seqlock/SPSC만. 해제는 그레이브야드.
- 새 Amp/Effect 추가: `Effect`(또는 `ControlRateEffect`/`OversampledDrive`) 구현 → `EffectFactory.cpp` 등록 → `CMakeLists.txt` 소스 추가.
  type/param id 불변, 새 파라미터는 끝에만. 소리를 바꾸는 변경은 기존 구현을 남기고 버전 분리(`AGENTS.md` R6). Choice 항목 추가는 DAW 자동화 영향(K-021).

## D. 이번 작업 변경사항 (2026-11-24, 미커밋)
| 파일 | 변경 | 목적 |
|---|---|---|
| `src/engine/AnalysisTap.h` (신규) | `AnalysisTap`: `beginWrite/commit/advance`(오디오), `peek/pop`(소비자), `setEnabled`, `droppedPackets`, `prepare`(세대 증가) | 실시간 분석 데이터 전달 |
| `src/engine/AnalysisWorker.h/.cpp` (신규) | `poll`, `latest`, `waveform`, `spectrum`, `smoothedSpectrum`, `setPaused`, `info` | 비실시간 분석 계산 |
| `src/engine/Engine.h/.cpp` | `analysisTap()`, `routingLatency()`, `InputLevels`, `MeterValues.srcPeak/inHot/inNonFinite`, 입력 NaN/Inf→0 | 탭 연결, 입력 시스템 보완 |
| `src/core/Effect.h` | `StateContext`, `setExtraStateWithContext()`(기본: 기존 `setExtraState` 호출) | 외부 파일 검색 정보 전달(이펙트 공통) |
| `src/dsp/cabinet/Cabinet.h/.cpp` | `mfx-ir:1` 상태 형식, `irHash()`, `hashSamples()`(FNV-1a 64), 검색·검증·누락 대체, 미해결 참조 보존 | IR 재현성 |
| `src/presets/Preset.h/.cpp` | `PresetLoadContext`, `presetFromJson(..., ctx)`, `loadPresetFile`이 프리셋 폴더를 검색 폴더에 추가 | IR 검색 |
| `src/plugin/PluginProcessor.*` | `loadPresetJson(json, err, presetDir)`, `userIrDirectory()` = `~/Music/MFX Claude/IR` | 플러그인 IR 검색 |
| `src/plugin/PluginEditor.*` | Effects/Visualize 전환, IN 미터 HOT·공칭 눈금, Source peak 표시 | UI |
| `src/ui/AnalysisView.h/.cpp` (신규) | Input/Output/Overlay, Mono/L/R/Stereo, Time·Amp 줌, Pause, FFT 2048/4096 | 시각화 |
| `apps/mfx_ui_snapshot.cpp` (신규, 기본 OFF) | `-DMFX_BUILD_UI_SNAPSHOT=ON` 오프스크린 렌더 도구 | UI 확인용(배포 안 함) |
| `tests/integration/test_analysis.cpp` (신규), `tests/performance/test_performance.cpp` | 분석·입력·IR 테스트 7개, 해제 카운트 추가 | 검증 |
| `CMakeLists.txt`, `src/plugin/CMakeLists.txt` | 새 소스 등록 | |
- 호환성: 유한 입력에서 DSP 출력 비트 동일(팩토리 5종 골든 렌더). 플러그인 ID·파라미터 id·프리셋 형식(버전 1) 유지. 기존 IR 상태(경로 문자열)는 그대로 읽힘.
  새 `mfx-ir:1` 형식은 이전 빌드에서 읽히지 않음(K-025). 입력 게인 범위·프리셋 로드 동작은 바꾸지 않음(D-032).

## D-2. v0.4 변경 (2026-11-25)
| 파일 | 변경 |
|---|---|
| `src/dsp/drive/CustomDrive.*` | `model` 파라미터(index 10, 기본 v2), `processNonlinearV1/V2/Adaa`, 모델 전환 시 상태 리셋+5 ms 페이드 |
| `src/dsp/nonlinear/Nonlinear.*` | `kneeAntiderivativeInit/kneeAntiderivative/kneeCurveD` (K4·K6 Hermite 표) |
| `src/dsp/amp/AB763Amp.*` (신규) | `TriodeStageTable`, `BlackfaceToneStack`, `AB763Amp`(OversampledDrive 기반) |
| `src/engine/EffectFactory.cpp`, `CMakeLists.txt` | `amp_ab763` 등록, 버전 0.4.0 |
| `src/presets/Preset.cpp` | `app_version` 기록 |
| `apps/mfx_compare_drive.cpp` | `--claude-model` |
| `tests/audio/test_amp_ab763.cpp`(신규), `tests/performance/test_performance.cpp` | 테스트 7 + 할당 1 |
| `presets/factory/06_Blackface_Normal.json` (신규) | gate → AB763 → cabinet → reverb |
호환성: 기존 파라미터 index·type id 불변, 팩토리 01–05 출력 비트 동일, `model` 없는 상태 → v2.

## D-3. v0.5 변경 (2026-11-25)
| 파일 | 변경 |
|---|---|
| `src/core/Effect.h`, `src/presets/Preset.cpp` | `legacyValueForMissingParam` 훅, 누락 파라미터 처리 |
| `src/dsp/amp/AB763Amp.*` | `model` 파라미터(index 6), M2 전력단·Sag·NFB(지연 없는 해)·부하, NaN 가드 |
| `src/dsp/drive/CustomDrive.*` | `adaa_comp`(index 11), FIR 보정·웻 패딩·정수 드라이 정렬, 지연 보고 |
| `src/dsp/filters/Biquad.h` | `state1()` (지연 없는 루프 해법용, 동작 변화 없음) |
| `src/dsp/reverb/PremiumPlate.*` (신규), `src/engine/EffectFactory.cpp`, `CMakeLists.txt` | 새 리버브, 버전 0.5.0 |
| `apps/mfx_compare_drive.cpp` | `--claude-adaa-comp` |
| `tests/audio/test_amp_ab763.cpp`, `tests/audio/test_reverb_premium.cpp`(신규), `tests/performance/test_performance.cpp` | 테스트 +11 |
| `tools/benchmark/reverb_metrics.py`(신규), `scripts/macos_legacy_au_disable.sh`(신규, 미실행) | 측정 도구, 구형 AU 숨김(사용자 실행) |
| `presets/factory/07_Blackface_M2_Plate.json` (신규) | |
| `docs/AUDIO_QUALITY_TARGETS.md`, `docs/TECHNICAL_RESEARCH.md` (신규) + 기존 문서 갱신 | |

## E. 테스트 및 검증 (이번 작업, Linux x86_64 클라우드, Release)
| 명령 | 결과 |
|---|---|
| `./build/mfx_tests` | PASS 78/78 (이전 70 + 신규 8) |
| `./build-tsan/mfx_tests --group integration` | PASS 22, TSan 경고 0 (`tests/results/sanitizer_tsan_analysis.txt`) |
| `./build-asan/mfx_tests --group integration` | PASS 22, ASan/UBSan 0 (`sanitizer_asan_analysis.txt`) |
| `mfx_render --preset presets/factory/*.json` 변경 전후 `cmp` | PASS 5/5 비트 동일 |
| `xvfb-run pluginval --strictness-level 7` (MFX Claude.vst3) | PASS (`pluginval_vst3_linux_strict7_v3_analysis.txt`) |
| `mfx_ui_snapshot` 오프스크린 렌더 | 확인 (`docs/images/ui_visualize_snapshot.png`) |
| 사용자 Mac의 Linux arm64 VM에서 코어+테스트 g++ 빌드·실행 | PASS 78/78 (`tests/results/arm64_linux_vm_tests.txt`) — macOS 빌드 아님 |
| macOS AU 빌드 / auval | NOT TESTED (macOS 툴체인 필요: 사용자 실행) |
| Logic 실사용(Visualize, 입력 표시, IR 복원) | NOT TESTED |
| Windows | NOT TESTED |
이전 작업 결과(별도): Custom Drive v2 비교·parity·pluginval 검증은 `docs/INTEGRATION_REPORT.md` 본문(2026-11-24 통합 고도화), v0.2.0 Logic 실행은 사용자 보고.

v0.4(Linux x86_64): `./build/mfx_tests` 86/86 PASS, ASan audio 0건, pluginval PASS, v1 비트 동일(v0.1 빌드와 cmp), 골든 01–05 동일. macOS v0.4: NOT TESTED.
gap v1 Mac(사용자 실행): 빌드 PASS, 78/78, auval PASS (`tests/results/macos_verify_20261124-174619.txt`).

## F. 미해결 문제 (전체 목록: `docs/KNOWN_ISSUES.md`)
| 우선 | ID | 내용 |
|---|---|---|
| P0/P1 | K-031, K-022 | v0.4 Mac 빌드·auval·Logic(AB763, Model 전환, Visualize) |
| P1 | K-029 | AB763 M2 범위·실측 비교 없음 |
| P1 | K-020 | 골든 렌더 테스트 미자동화(현재 수동 cmp) |
| P1 | K-001(완화), K-002 | 고게인 앨리어싱은 ADAA로 완화, 어택 압축 차이 남음 |
| P2 | K-027, K-028, K-030 | ADAA parity 미러 없음, 반 샘플 지연, double 연산 |
| P2 | K-023, K-024, K-025 | 스펙트럼 스캘럽 1.42 dB, IR 번들 내보내기 없음, 새 IR 상태 상위 호환 없음 |
| P2 | K-005, K-006, K-008 | NAM 리샘플링, Custom Drive 진단 UI, Undo/Redo |
| P2 | K-015~018, K-021 | Windows/플랫폼 일치, ARMv7 FTZ, 메모리 보고, MCU 부적합 모듈, Choice 확장 정책 |
사용자 확인 필요: 미커밋 변경 검토·커밋 승인, Logic 확인, IR 폴더 위치(`~/Music/MFX Claude/IR`) 수용 여부.

## G. 다음 개발 작업
| 순서 | 작업 | 변경 대상 | 주의 | 권장 모델 | 최소 검증 |
|---|---|---|---|---|---|
| 1 | v0.4 Mac 1회 검증 | `scripts/macos_build_install_verify.sh` | 설치 AU는 스크립트가 백업 후 교체 | (사용자) | auval PASS, Logic에서 AB763·Model 전환 |
| 2 | AB763 M2: PI(롱테일), 6V6 푸시풀, Sag, NFB, OT | `src/dsp/amp/AB763Amp.*` | M1 소리는 `model` 파라미터 추가로 보존(R6), type id 불변 | 고성능 코딩 모델 | 기존 M1 출력 비트 동일, 할당 0, SR 독립 |
| 3 | 골든 렌더 자동 테스트(K-020) | `tests/unit/test_presets.cpp` | 플랫폼 간 허용 오차 정의 | 중간급 | 팩토리 01–06 |
| 4 | 출시 준비: 제품명·ID 확정, JUCE 라이선스, 서명/공증 | `src/plugin/CMakeLists.txt`, 문서 | ID 변경은 첫 출시 전 1회만 | 중간급 | auval, pluginval |
| 5 | UI 정리(디자인 토큰·비율 레이아웃·이펙트별 전용 화면 연결부) | `src/ui/*` | DSP 무영향 | 중간급 | 스냅샷, pluginval |
"권장 모델"은 작업 난이도 기준 제안이며 특정 제품 성능은 확인하지 않았습니다.
