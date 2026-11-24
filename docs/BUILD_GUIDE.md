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

## 2. macOS (Apple Silicon) — AU / VST3 / Standalone   ※ 이 저장소 작업 중에는 실행하지 못함(NOT TESTED)
```bash
xcode-select --install                      # 또는 Xcode
brew install cmake ninja
cmake -S . -B build-mac -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DMFX_BUILD_PLUGIN=ON -DMFX_WITH_NAM=ON \
      -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"   # 유니버설(선택)
cmake --build build-mac
# 결과: build-mac/src/plugin/MultiEffects_artefacts/Release/{AU,VST3,Standalone}
cp -R build-mac/src/plugin/MultiEffects_artefacts/Release/AU/MultiEffects.component ~/Library/Audio/Plug-Ins/Components/
cp -R build-mac/src/plugin/MultiEffects_artefacts/Release/VST3/MultiEffects.vst3   ~/Library/Audio/Plug-Ins/VST3/
auval -v aumf Mfx1 Mfxp                     # AU 검증 (type aumf = MIDI 수신 이펙트, subtype Mfx1, manufacturer Mfxp)
killall -9 AudioComponentRegistrar          # Logic이 새 AU를 못 찾을 때
```
Logic Pro: 오디오 트랙(모노 입력) → Audio FX → Audio Units → MultiEffects Project → MultiEffects.
배포 시 코드 서명·공증(notarization)이 필요합니다(Developer ID).

> AU 타입은 MIDI CC를 받기 위해 `kAudioUnitType_MusicEffect`(aumf)입니다. Logic에서는 Audio FX 메뉴에 나타납니다.
> MIDI가 필요 없다면 `src/plugin/CMakeLists.txt`에서 `kAudioUnitType_Effect`(aufx)로 바꿀 수 있습니다.

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
```

## 5. 전체 검증
```bash
scripts/run_all_checks.sh        # C++ 테스트, parity, SPICE 검증, 벤치마크 → tests/results/
```

## 임베디드(향후)
`mfx_core`만 크로스 컴파일하면 됩니다(JUCE 불필요). ARM Cortex-A(리눅스)에서는 그대로, Cortex-M7(Daisy 등)에서는
`platform/WavFile`·프리셋 파일 I/O를 저장소 인터페이스로 교체하고 `prepare()` 할당을 정적 풀로 바꿔야 합니다.
