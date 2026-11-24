cmake_minimum_required(VERSION 3.24)
project(MultiEffects VERSION 0.1.0 LANGUAGES C CXX)

# ----------------------------------------------------------------------------- options
option(MFX_BUILD_TESTS   "Build C++ tests"                                   ON)
option(MFX_BUILD_APPS    "Build command line tools (offline renderer, bench)" ON)
option(MFX_WITH_NAM      "Build NAM support (NeuralAmpModelerCore, MIT + Eigen MPL2)" OFF)
option(MFX_BUILD_PLUGIN  "Build JUCE plug-in (AU/VST3/Standalone)"            OFF)
set(MFX_NAM_DIR   "" CACHE PATH "Local NeuralAmpModelerCore checkout (empty = FetchContent)")
set(MFX_EIGEN_DIR "" CACHE PATH "Eigen include dir (empty = FetchContent)")
set(MFX_JUCE_DIR  "" CACHE PATH "Local JUCE checkout (empty = FetchContent)")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()
if(APPLE)
  set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "")
endif()

include(FetchContent)

# ----------------------------------------------------------------------------- DSP core (framework independent)
add_library(mfx_core STATIC
  src/core/Common.cpp
  src/core/Parameter.cpp
  src/dsp/filters/Biquad.cpp
  src/dsp/filters/EQ.cpp
  src/dsp/oversampling/Oversampler.cpp
  src/dsp/fft/FFT.cpp
  src/dsp/nonlinear/Nonlinear.cpp
  src/dsp/drive/CustomDrive.cpp
  src/dsp/drive/Drives.cpp
  src/dsp/gain/Utility.cpp
  src/dsp/dynamics/Dynamics.cpp
  src/dsp/amp/AmpSim.cpp
  src/dsp/cabinet/Convolver.cpp
  src/dsp/cabinet/Cabinet.cpp
  src/dsp/modulation/Modulation.cpp
  src/dsp/delay/Delay.cpp
  src/dsp/reverb/Reverb.cpp
  src/dsp/utility/Tuner.cpp
  src/dsp/utility/Resample.cpp
  src/dsp/analysis/Analysis.cpp
  src/neural/NamAmp.cpp
  src/engine/Engine.cpp
  src/engine/EffectFactory.cpp
  src/presets/Json.cpp
  src/presets/Preset.cpp
  src/platform/WavFile.cpp
)
target_include_directories(mfx_core PUBLIC src)
if(MSVC)
  target_compile_options(mfx_core PRIVATE /W4 /permissive-)
else()
  target_compile_options(mfx_core PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()

# ----------------------------------------------------------------------------- NAM (optional)
if(MFX_WITH_NAM)
  if(MFX_NAM_DIR)
    set(_nam_src ${MFX_NAM_DIR})
  else()
    FetchContent_Declare(namcore GIT_REPOSITORY https://github.com/sdatkinson/NeuralAmpModelerCore.git
                         GIT_TAG d3189ed482ded880c128bf312941266c4fa5ceec GIT_SUBMODULES "")
    FetchContent_Populate(namcore)
    set(_nam_src ${namcore_SOURCE_DIR})
  endif()
  if(MFX_EIGEN_DIR)
    set(_eigen_inc ${MFX_EIGEN_DIR})
  else()
    FetchContent_Declare(eigen GIT_REPOSITORY https://gitlab.com/libeigen/eigen.git GIT_TAG 3.4.0 GIT_SHALLOW TRUE)
    FetchContent_Populate(eigen)
    set(_eigen_inc ${eigen_SOURCE_DIR})
  endif()
  file(GLOB _nam_top ${_nam_src}/NAM/*.cpp)
  file(GLOB _nam_sub ${_nam_src}/NAM/*/*.cpp)
  add_library(nam_core STATIC ${_nam_top} ${_nam_sub})
  target_include_directories(nam_core PUBLIC ${_nam_src} ${_nam_src}/NAM ${_nam_src}/Dependencies/nlohmann ${_eigen_inc})
  target_compile_definitions(nam_core PUBLIC NAM_SAMPLE_FLOAT)
  if(NOT MSVC)
    target_compile_options(nam_core PRIVATE -w)
  endif()
  # NAM registers model architectures with static initialisers -> whole-archive so the linker keeps them
  target_link_libraries(mfx_core PUBLIC "$<LINK_LIBRARY:WHOLE_ARCHIVE,nam_core>")
  target_compile_definitions(mfx_core PUBLIC MFX_WITH_NAM=1)
else()
  target_compile_definitions(mfx_core PUBLIC MFX_WITH_NAM=0)
endif()

# ----------------------------------------------------------------------------- apps
if(MFX_BUILD_APPS)
  add_executable(mfx_render apps/mfx_render.cpp)
  target_link_libraries(mfx_render PRIVATE mfx_core)
  add_executable(mfx_modeling_study apps/mfx_modeling_study.cpp)
  target_link_libraries(mfx_modeling_study PRIVATE mfx_core)
endif()

# ----------------------------------------------------------------------------- tests
if(MFX_BUILD_TESTS)
  enable_testing()
  add_executable(mfx_tests
    tests/main.cpp
    tests/unit/test_core.cpp
    tests/unit/test_filters.cpp
    tests/unit/test_nonlinear.cpp
    tests/unit/test_presets.cpp
    tests/audio/test_effects.cpp
    tests/audio/test_custom_drive.cpp
    tests/integration/test_engine.cpp
    tests/performance/test_performance.cpp)
  target_include_directories(mfx_tests PRIVATE tests)
  target_link_libraries(mfx_tests PRIVATE mfx_core)
  target_compile_definitions(mfx_tests PRIVATE MFX_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
  add_test(NAME unit COMMAND mfx_tests --group unit)
  add_test(NAME audio COMMAND mfx_tests --group audio)
  add_test(NAME integration COMMAND mfx_tests --group integration)
  add_test(NAME performance COMMAND mfx_tests --group performance)
endif()

# ----------------------------------------------------------------------------- JUCE plug-in
if(MFX_BUILD_PLUGIN)
  add_subdirectory(src/plugin)
endif()
