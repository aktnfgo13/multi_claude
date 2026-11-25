# AI 프로젝트 인수인계 (MFX Claude)

- 작성일: 2026-11-24 · 기준 커밋: `3f823fa` (GitHub `aktnfgo13/multi_claude`, `main`) + **미커밋 변경**(아래 D절)
- 대상: ChatGPT / Claude / Codex 등 후속 개발 AI. 추측하지 말고 이 문서와 링크된 파일을 근거로 진행하세요.

## Quick Context
- 제품: 기타 멀티이펙터 플러그인 **MFX Claude v0.2.0** (AU/VST3/Standalone, JUCE 8) + 향후 ARM 전용 하드웨어. 상업 판매 목표.
- 코어: `mfx_core` = JUCE·OS 독립 C++20 DSP/엔진/프리셋. 플러그인·UI는 얇은 래퍼.
- 필수 요구사항: `AGENTS.md` R1–R6(모듈형, 코어 독립, Mac/Win 공통·프리셋 호환, ARM 이식, 실시간 안전, 기존 모델·프리셋 보존).
- Codex 프로젝트(`~/multi_effects`)는 **읽기 전용 참고**. 코드 복사·병합 금지, 플러그인 ID 재사용 금지.
- 사용자 Mac Logic Pro에서 v0.2.0 정상 실행 확인됨(사용자 보고).
- 최근 구현(미커밋): 실시간 분석 탭 + 파형/스펙트럼(Visualize 화면), 입력 시스템 보완(원본 피크·HOT·NaN 가드·공칭 눈금), 캐비닛 IR 참조 복원(해시 검증·검색 폴더·누락 시 안전 대체).
- 최신 테스트: C++ 78개 PASS(Linux x86_64 + 사용자 Mac의 Linux arm64 VM, `tests/results/arm64_linux_vm_tests.txt`), TSan/ASan(integration) 경고 0, 콜백 할당/해제 0, 팩토리 프리셋 5종 렌더 변경 전과 비트 동일, pluginval strictness 7 PASS.
- 미검증: Logic에서 Visualize·입력 표시(K-022), macOS AU 후보 빌드·auval(K-026 — 이 환경에서 macOS 툴체인 실행 불가), Windows 빌드(K-015).
- 사용자 Mac 사본 `~/multi_effects_claude`에 이번 변경 반영 완료(`SOURCE_ID` = `3f823fa+gap-v1-uncommitted`, 반영 전 스냅샷 `_snapshots/pre-sync-gapv1_*.tar.gz`).
- 미해결 우선: R6 보완(프리셋 모델 버전 K-019, 골든 렌더 테스트 자동화 K-020), Custom Drive 고게인 앨리어싱 K-001.
- 다음 순서: ① 미커밋 변경 사용자 확인 → 커밋 ② Mac 빌드·Logic 확인 ③ K-019/K-020 ④ K-001(ADAA).
- 보호 대상: Custom Drive v2 음색, 기존 25종 이펙트·팩토리 프리셋, 플러그인 ID(MfxL/McL1), 설치된 AU(Claude·Codex), Codex 프로젝트, Logic 프로젝트, 스냅샷/백업.
- 주요 문서: `AGENTS.md`, `CLAUDE.md`, `docs/DEVELOPMENT_PLAN.md`, `docs/INTEGRATION_REPORT.md`, `docs/KNOWN_ISSUES.md`, 이 문서.

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

## F. 미해결 문제 (전체 목록: `docs/KNOWN_ISSUES.md`)
| 우선 | ID | 내용 |
|---|---|---|
| P0/P1 | K-026, K-022 | Mac 후보 빌드·Logic에서 Visualize/입력 표시/IR 복원 확인 |
| P1 | K-019, K-020 | 프리셋 모델 버전 없음(Custom Drive v1 선택 불가), 골든 렌더 테스트 미자동화 |
| P1 | K-001, K-002 | Custom Drive 고게인 앨리어싱·어택 압축(Codex 대비) |
| P2 | K-023, K-024, K-025 | 스펙트럼 스캘럽 1.42 dB, IR 번들 내보내기 없음, 새 IR 상태 상위 호환 없음 |
| P2 | K-005, K-006, K-008 | NAM 리샘플링, Custom Drive 진단 UI, Undo/Redo |
| P2 | K-015~018, K-021 | Windows/플랫폼 일치, ARMv7 FTZ, 메모리 보고, MCU 부적합 모듈, Choice 확장 정책 |
사용자 확인 필요: 미커밋 변경 검토·커밋 승인, Logic 확인, IR 폴더 위치(`~/Music/MFX Claude/IR`) 수용 여부.

## G. 다음 개발 작업
| 순서 | 작업 | 변경 대상 | 주의 | 권장 모델 | 최소 검증 |
|---|---|---|---|---|---|
| 1 | 미커밋 변경 커밋·Mac 동기화 | git | Codex 저장소·설치 AU 건드리지 않음 | 경량 모델 | `git status`, 빌드 |
| 2 | Mac 빌드·auval·Logic | `scripts/macos_build_install_verify.sh` | 기존 AU는 백업 후 교체(스크립트가 처리) | — (사용자) | auval PASS, Visualize 동작 |
| 3 | 프리셋 모델 버전(K-019) + 골든 렌더 테스트(K-020) | `Preset.*`, `CustomDrive.*`, `tests/unit/test_presets.cpp` | 없는 필드 = 현재 동작, id 불변 | 고성능 코딩 모델(예: Claude Opus급 / GPT 상위 모델) | 팩토리 렌더 비트 동일, v1 선택 시 v1 출력 |
| 4 | Custom Drive 고게인 ADAA(K-001) | `CustomDrive.cpp`, `research/common/mfx_dsp.py` | v2 기본 음색 유지 또는 버전 분리, parity 갱신 | 고성능 모델 | parity, `mfx_compare_drive`, 앨리어싱 수치 |
| 5 | IR 번들 내보내기(K-024) | `Preset.*`, `PluginEditor.cpp` | IR 샘플 JSON 내장 금지(D-034) | 중간급 모델 | 이동 후 복원 테스트 |
"권장 모델"은 작업 난이도 기준의 제안이며, 특정 제품명(Astra/Sol 등)의 실제 성능은 확인하지 않았습니다.
