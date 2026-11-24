# AGENTS.md — 모든 개발 에이전트(Claude, Codex 등)가 지켜야 할 필수 요구사항

이 파일은 Multi Effects(MFX Claude) 저장소에서 작업하는 모든 코딩 에이전트와 사람에게 적용됩니다.
`CLAUDE.md`의 세부 규칙보다 우선하며, 충돌하면 이 파일을 따릅니다. 요구사항을 바꾸려면 사용자 승인과
`docs/DECISION_LOG.md` 기록이 필요합니다. (2026-11-24 사용자 지시로 제정)

## 필수 요구사항 (모든 변경에서 유지)

### R1. 유지보수성과 확장성이 높은 모듈형 아키텍처
- 이펙트·앰프는 `Effect` 인터페이스(또는 `ControlRateEffect` / `OversampledDrive`)를 구현하고 `EffectFactory`에 등록하는
  방식으로만 추가합니다. 엔진·플러그인·UI에 이펙트별 분기를 넣지 않습니다.
- 계층 의존 방향: `core` ← `dsp` ← `engine` ← `presets` ← (`plugin` / `ui` / `apps`). 역방향 의존 금지.
- 공용 블록(필터, 오버샘플러, 비선형 함수, 딜레이라인)을 재사용하고 복제 구현을 만들지 않습니다.

### R2. JUCE 및 운영체제에 독립적인 C++ DSP Core
- `mfx_core`(src/core, src/dsp, src/engine, src/presets, src/neural, src/platform)는 JUCE·OS 헤더를 포함하지 않습니다.
  예외: `src/platform`(파일 I/O 추상화)과 `ScopedNoDenormals`의 아키텍처별 레지스터 접근.
- 표준 C++20만 사용합니다. 컴파일러 확장·플랫폼 intrinsic은 `#if`로 격리하고 이식 가능한 대체 경로를 둡니다.
- 플러그인(`src/plugin`)과 UI(`src/ui`)는 코어를 감싸는 얇은 래퍼입니다. DSP 로직을 두지 않습니다.

### R3. macOS/Windows 공통 DSP 구현과 프리셋 호환성
- 플랫폼별 DSP 코드 경로를 만들지 않습니다. 같은 소스·같은 수식이 모든 OS에서 동작해야 합니다.
- 프리셋은 OS 독립 JSON(`mfx-preset`)입니다. 경로 구분자·로케일·엔디언에 의존하는 값을 저장하지 않습니다.
- 파라미터는 정규화 값이 아닌 **물리값**으로 저장합니다(호스트 정규화 변경에 영향받지 않도록).

### R4. ARM 기반 전용 하드웨어로 이식 가능한 구조
- 오디오 경로(샘플당 연산)는 `float`를 기본으로 합니다. `double`은 계수 설계·오프라인 분석에만 씁니다.
- 동적 할당은 `prepare()`와 비실시간 스레드에서만 합니다(향후 정적 풀로 대체 가능하도록 할당 지점을 한정).
- 오디오 경로에서 예외·RTTI·파일 I/O·스레드 생성에 의존하지 않습니다. 선택 모듈(NAM 등)은 빌드 옵션으로 분리합니다.
- 새 블록은 CPU 비용(ns/샘플)과 메모리(바이트)를 측정·기록합니다(하드웨어 예산 산정용).

### R5. 오디오 콜백의 실시간 안전성 및 메모리 관리
- `Effect::process`, `Engine::process`: 힙 할당·락·블로킹 I/O·로그·예외 금지. `tests/performance`의 할당 카운터 0 유지.
- 스레드 간 공유는 atomic·락프리 게시(`RtPublisher`)·seqlock만 사용합니다. 호스트 스레드는 `Effect*`에 직접 접근하지 않습니다.
- 객체 해제는 오디오 스레드 밖(그레이브야드)에서 합니다. 버퍼 크기·샘플레이트 변경은 `prepare()`에서만 처리합니다.
- 동시성 변경 시 TSan, 메모리 관련 변경 시 ASan/UBSan을 실행합니다(`-DMFX_SANITIZE=thread|address`).

### R6. 새로운 Amp/Effect 추가 시 기존 모델과 프리셋 보존
- 이펙트 type id·파라미터 id는 절대 바꾸거나 재사용하지 않습니다. 새 파라미터는 목록 끝에만 추가합니다.
- 기존 모델의 **소리를 바꾸는 변경**(매핑·곡선·필터 수식)은 기존 구현을 남기고 버전으로 구분합니다
  (예: `customDriveMappingV1` 보존). 기존 프리셋이 기존 소리로 로드되어야 합니다.
- 선택형(Choice) 파라미터에 항목을 추가하면 호스트 정규화 값이 바뀌므로, 새 앰프 모델은 기존 목록 끝에 추가하고
  호스트 자동화 영향을 문서화하거나 새 type id로 추가합니다.
- 모든 변경은 팩토리 프리셋 로드 테스트와 렌더 회귀 비교를 통과해야 하며, 소리가 바뀌면 이유와 영향을
  `docs/PARAMETER_SPECIFICATION.md`와 `DECISION_LOG`에 기록합니다.

## 작업 규칙
- 참고 프로젝트(Codex, `~/multi_effects`)는 읽기 전용입니다. 코드·프리셋 복사 금지, 플러그인 ID 재사용 금지.
- 검증하지 못한 항목은 NOT TESTED / BLOCKED로 기록합니다. 청취 결과를 지어내지 않습니다.
- 변경 후 최소 실행: `./build/mfx_tests`, DSP 수식 변경 시 `python3 tests/parity/run_parity.py`.
- 위 요구사항과 현재 코드의 차이는 `docs/DEVELOPMENT_PLAN.md` "필수 요구사항 대비 현황"과 `docs/KNOWN_ISSUES.md`에서 관리합니다.
