# 아키텍처

## 계층 구조

```
┌───────────────────────────── Application Layer ─────────────────────────────┐
│ src/plugin   JUCE AudioProcessor/Editor (AU · VST3 · Standalone)            │
│ src/ui       ChainView · ParamPanel · LookAndFeel (JUCE 컴포넌트)            │
│ src/presets  JSON 프리셋 (Json.h 자체 구현, 외부 의존 없음)                     │
│ apps/        mfx_render (오프라인 렌더러/벤치마크) · mfx_modeling_study         │
├───────────────────────────── Audio Engine Layer ────────────────────────────┤
│ src/engine   Engine (라우팅, 슬롯, 바이패스, 미터, CPU, 지연, MIDI CC)          │
│              RtPublisher (락프리 객체 교체) · EffectFactory                     │
├───────────────────────────── DSP Layer ─────────────────────────────────────┤
│ src/core     Effect 인터페이스 · ParameterSet(atomic) · Smoothing             │
│ src/dsp      filters · oversampling · nonlinear · drive · dynamics · amp      │
│              cabinet(convolution) · modulation · delay · reverb · utility    │
│              analysis(오프라인 분석)                                          │
├───────────────────────────── AI Layer ──────────────────────────────────────┤
│ src/neural   NamAmp (NeuralAmpModelerCore, 선택 빌드 MFX_WITH_NAM)            │
│ research/    ddsp(PyTorch) · parameter_mapping(LUT) · graybox · spice         │
├───────────────────────────── Hardware Abstraction ──────────────────────────┤
│ src/platform WavFile (파일 I/O) · ScopedNoDenormals(x86 MXCSR / ARM64 FPCR)   │
├───────────────────────────── Research & Testing ────────────────────────────┤
│ tools/       signal_generator · audio_analysis · measurement · benchmark      │
│ tests/       unit · audio · integration · performance · parity(Python)       │
└──────────────────────────────────────────────────────────────────────────────┘
```

의존 방향은 위에서 아래로만 흐릅니다. `mfx_core` 정적 라이브러리(DSP + Engine + Presets + Platform)는
JUCE·OS API·Python 어느 것도 포함하지 않으며 C++20 표준 라이브러리만 사용합니다. 같은 라이브러리를
플러그인, 오프라인 렌더러, 테스트, (향후) 임베디드 펌웨어가 공유합니다.

## 스레드 모델

| 스레드 | 허용되는 작업 |
|---|---|
| 메시지(UI) 스레드 | 이펙트 생성/삭제/이동, prepare, 프리셋, IR/NAM/LUT 로딩(파일 I/O·할당), 가비지 수거 |
| 임의 스레드 | `ParameterSet::set` (std::atomic<float>), 바이패스, 입출력 게인, 템포 |
| 오디오 스레드 | `Engine::process`, `handleMidi`, `setParameterRT` — 할당·락·파일 I/O 없음 |

* **라우팅 교체**: 메시지 스레드가 새 `Routing`(슬롯 순서 + raw `Effect*`)을 만들어 `RtPublisher`에 게시 →
  오디오 스레드가 블록 시작에서 3 ms 페이드아웃 후 교체, 3 ms 페이드인. 교체된 객체와 삭제된 이펙트는
  오디오 스레드가 새 버전을 사용했다는 것(`audioVersion_`)이 확인된 뒤 메시지 스레드에서 해제됩니다.
* **IR / NAM 모델 / 노브 LUT**: 메시지 스레드에서 읽기·리샘플·FFT·prewarm을 마친 객체를 원자적 포인터로
  넘기고, 오디오 스레드는 교체만 합니다(Cabinet은 5 ms 페이드).
* **할당 검증**: `tests/performance`가 전역 `operator new`를 가로채 `process()` 400블록(파라미터 변경,
  바이패스 토글, 오버샘플링 변경, MIDI 포함) 동안 할당 0회를 확인합니다.

## 신호 흐름 (엔진)

```
input ─ InputMode(Auto/MonoL/MonoSum/Stereo) ─ input gain ─ meter
   └─ [Pre] ─┬─ [Lane A] ─┐  (parallel=on: 지연 정렬 후 A/B 밸런스 합산)
             └─ [Lane B] ─┘  (parallel=off: Pre→A→B→Post 직렬)
   └─ [Post] ─ routing fade ─ output gain ─ NaN/Inf guard ─ meter ─ output
```

* 모노 최적화: 입력이 모노이면 `createsStereo()`가 true인 첫 이펙트(코러스, 딜레이, 리버브, 팬 등)까지는
  1채널만 처리합니다. 판단은 **토폴로지 기준**(바이패스와 무관)이라 채널 수가 바이패스로 바뀌지 않습니다.
* 슬롯 바이패스: 5 ms 크로스페이드. 드라이 경로는 이펙트 지연만큼 지연되어 **바이패스 중에도 전체 지연이 일정**합니다
  (호스트 PDC가 흔들리지 않음). 완전 바이패스된 이펙트는 처리하지 않고(CPU 절약) 재활성화 시 상태를 리셋합니다.
* 지연 보고: `Engine::latencySamples()` = Pre + max(A, B) 또는 A+B + Post. 플러그인은 타이머에서 변화를 감지해
  `setLatencySamples()`를 호출합니다(예: 오버샘플링 변경).
* 내부 처리 청크: 호스트 블록을 256 샘플 단위로 나눠 처리(바이패스 드라이 버퍼를 스택에 둘 수 있도록).

## Effect 인터페이스

```cpp
class Effect {
  prepare(spec) / reset()            // 메시지 스레드
  process(AudioBlock&)               // 오디오 스레드 (numChannels 1 또는 2)
  latencySamples(), createsStereo(), setTempo(bpm)
  params()                           // ParameterSet: id/이름/범위/단위/스케일/선택지, atomic 값
  getExtraState()/setExtraState()    // IR·.nam·LUT 경로 등 파라미터가 아닌 상태
  collectGarbage()
};
```

파생 헬퍼:
* `ControlRateEffect` — 모든 연속 파라미터를 30 ms 선형 스무딩, 32 샘플마다(움직일 때만) 계수 재계산.
* `OversampledDrive` — 기본율 전처리 → 오버샘플링 비선형 → 기본율 후처리, 지연 정렬된 Mix, Level, OS 선택.
* `CustomDrive` — 전용 구현(바이어스 상태, 2단 비선형, 지연 정렬 드라이).

## 플러그인 파라미터 (호스트 자동화)

호스트는 파라미터 목록이 고정이어야 하므로, `input_gain`, `output_gain`, `parallel_mix` +
슬롯 16개 × (Bypass + 파라미터 16개) = 275개의 고정 파라미터를 노출합니다. 이름은 현재 슬롯에 들어 있는
이펙트를 따라 바뀌고(`S3 Drive`), `updateHostDisplay(parameterInfoChanged)`로 호스트에 알립니다.
호스트 자동화 값은 오디오 스레드에서 `Engine::setParameterRT`로 **현재 라이브 라우팅**의 이펙트에 적용됩니다.

## 임베디드 이식성

* 코어는 예외·RTTI 없이도 동작하도록 오디오 경로에서 사용하지 않습니다(프리셋/파일 로딩 경로는 예외 미사용).
* 동적 할당은 `prepare()`에서만 발생 → 정적 메모리 풀로 교체 가능.
* 오버샘플러·FFT·필터는 float 기반이며 SIMD에 의존하지 않습니다(향후 NEON 최적화 지점: FIR 하프밴드, 컨볼루션).
* `ScopedNoDenormals`는 x86(MXCSR FTZ/DAZ)과 AArch64(FPCR.FZ)를 모두 처리합니다.
* 파일 시스템(`platform/WavFile`)은 `std::fstream` 기반이며, 임베디드에서는 Storage 인터페이스로 교체 대상입니다
  (현재는 인터페이스 분리 없이 직접 사용 — Known Issue로 기록).
