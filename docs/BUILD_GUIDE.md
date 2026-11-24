# 빌드 가이드

## 요구사항
* CMake ≥ 3.24, Ninja(권장), C++20 컴파일러(Apple Clang 15+, MSVC 2022, GCC 11+)
* 플러그인: 인터넷(최초 JUCE 8.0.4 다운로드) 또는 로컬 JUCE (`-DMFX_JUCE_DIR=`)
* NAM: 인터넷(NAM core, Eigen 3.4 다운로드) 또는 로컬 경로(`-DMFX_NAM_DIR= -DMFX_EIGEN_DIR=`)
* Python 연구 도구: `pip install -r requirements.txt`, SPICE는 ngspice(`brew install ngspice` / `apt install ngspice`)

## 1. 코어 · 테스트 · 렌더러 (모든 OS)
```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/mfx_tests
```

## 2. macOS (Apple Silicon) — AU / VST3 / Standalone

빌드 자체는 사용자 Mac에서 성공했습니다(v0.1). **auval·Logic은 아직 사용자 실행 결과를 받지 못했습니다(NOT TESTED).**

### 자동 스크립트 (권장)
```bash
cd ~/multi_effects_claude
bash scripts/macos_build_install_verify.sh
```
* arm64 Release 빌드 → C++ 테스트 → `MFX Claude.component` / `MFX Claude.vst3`만 설치 → `auval -v aumf McL1 MfxL`.
* 결과 로그: `tests/results/macos_verify_<시각>.txt`, `auval_<시각>.txt`.
* 다른 플러그인(Codex "Multi Effects", Claude v0.1 "MultiEffects")은 건드리지 않습니다. 이전에 설치한 "MFX Claude"가 있으면
  지우지 않고 `~/Library/Audio/Plug-Ins/_mfx_claude_backup/<시각>/`로 옮깁니다.

### 플러그인 식별자 (Codex·v0.1과 충돌 없음)
| | MFX Claude (현재) | Claude v0.1 (구) | Codex (참고 프로젝트) |
|---|---|---|---|
| 제품명 | MFX Claude | MultiEffects | Multi Effects |
| Manufacturer code | MfxL | Mfxp | Mefx |
| Plugin code (AU subtype) | McL1 | Mfx1 | Mfx6 |
| Bundle ID | com.mfxlab.mfxclaude | — | com.multieffects.plugin |
| AU 타입 | aumf (MIDI CC 수신 이펙트) | aumf | — |

창 오른쪽 아래에 버전·커밋 ID가 표시됩니다(`mfx_render --version`도 동일). 저장소가 git이 아니면 `SOURCE_ID` 파일 내용을 씁니다.

### Logic 체크리스트 (사용자 확인)
1. Logic 설정 → 플러그인 관리자에서 "MFX Lab: MFX Claude"가 "성공"으로 표시되는가
2. 모노 오디오 트랙 → Audio FX → Audio Units → MFX Lab → MFX Claude 로드
3. 창 우하단 버전·커밋이 `mfx_render --version`과 같은가
4. 프리셋 02/03/05 로드 → 재생 중 노브 조작 시 클릭·끊김 없는가
5. Custom Drive Drive/Headroom 자동화 기록 → 재생 시 따라오는가
6. 버퍼 32/64/128/256/512, 44.1/48/96 kHz에서 재생 중 소리 끊김 없는가
7. 프로젝트 저장 → Logic 종료 → 다시 열었을 때 체인·값이 같은가
8. 딜레이/리버브 바이패스 시 꼬리가 자연스럽게 남는가(트레일)
9. 튜너 표시, MIDI CC 학습(설정한 경우) 동작
10. CPU 미터(Logic) 수치 기록: 프리셋 03, 버퍼 128, 48 kHz

### 수동 빌드
```bash
cmake -S . -B build-mac -G Ninja -DCMAKE_BUILD_TYPE=Release -DMFX_BUILD_PLUGIN=ON -DMFX_WITH_NAM=ON -DCMAKE_OSX_ARCHITECTURES=arm64
cmake --build build-mac
# 결과: build-mac/src/plugin/MultiEffects_artefacts/Release/{AU/MFX Claude.component, VST3/MFX Claude.vst3, Standalone}
```
배포 시 코드 서명·공증(notarization)이 필요합니다(Developer ID).

## 3. Windows — VST3 / Standalone (NOT TESTED)
```powershell
cmake -S . -B build-win -G "Visual Studio 17 2022" -A x64 -DMFX_BUILD_PLUGIN=ON -DMFX_WITH_NAM=ON
cmake --build build-win --config Release
```

## 4. Linux — VST3 / Standalone (검증됨)
```bash
sudo apt install libasound2-dev libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libfreetype-dev libeigen3-dev
cmake -S . -B build-plugin -G Ninja -DCMAKE_BUILD_TYPE=Release -DMFX_BUILD_PLUGIN=ON \
      -DMFX_WITH_NAM=ON -DMFX_EIGEN_DIR=/usr/include/eigen3
cmake --build build-plugin
pluginval --strictness-level 7 --validate "build-plugin/src/plugin/MultiEffects_artefacts/Release/VST3/MFX Claude.vst3"
```

## 4-1. Sanitizer / 참고 프로젝트 비교 하니스
```bash
cmake -S . -B build-asan -DMFX_SANITIZE=address && cmake --build build-asan && ./build-asan/mfx_tests
cmake -S . -B build-tsan -DMFX_SANITIZE=thread  && cmake --build build-tsan && ./build-tsan/mfx_tests --group integration
# Codex Custom Drive와 비교 (Codex 소스는 읽기만 함, 경로만 지정)
cmake -S . -B build -DMFX_REFERENCE_DIR=$HOME/multi_effects && cmake --build build --target mfx_compare_drive
./build/mfx_compare_drive out_dir [--quick] [--fs 44100|48000|96000] [--os 1|2|4|8] [--claude-only]
python3 tools/benchmark/plot_compare_drive.py docs/images/compare_drive "Claude v2=out_dir"
```

## 5. 전체 검증
```bash
scripts/run_all_checks.sh        # C++ 테스트, parity, SPICE 검증, 벤치마크 → tests/results/
```

## 임베디드(향후)
`mfx_core`만 크로스 컴파일하면 됩니다(JUCE 불필요). ARM Cortex-A(리눅스)에서는 그대로, Cortex-M7(Daisy 등)에서는
`platform/WavFile`·프리셋 파일 I/O를 저장소 인터페이스로 교체하고 `prepare()` 할당을 정적 풀로 바꿔야 합니다.
