# JUCE plug-in target (AU on macOS, VST3 on macOS/Windows/Linux, Standalone everywhere).
# JUCE 8 is dual licensed AGPLv3 / commercial JUCE 8 licence — see THIRD_PARTY_NOTICES.md before shipping.
if(MFX_JUCE_DIR)
  add_subdirectory(${MFX_JUCE_DIR} ${CMAKE_BINARY_DIR}/juce EXCLUDE_FROM_ALL)
else()
  FetchContent_Declare(juce GIT_REPOSITORY https://github.com/juce-framework/JUCE.git GIT_TAG 8.0.4 GIT_SHALLOW TRUE)
  FetchContent_MakeAvailable(juce)
endif()

set(MFX_FORMATS VST3 Standalone)
if(APPLE)
  list(APPEND MFX_FORMATS AU)
endif()

# Identity is deliberately different from the reference (Codex) plug-in
# ("Multi Effects", com.multieffects.plugin, Mefx/Mfx6) so both can be installed side by side.
juce_add_plugin(MultiEffects
  VERSION ${PROJECT_VERSION}
  COMPANY_NAME "MFX Lab"
  BUNDLE_ID com.mfxlab.mfxclaude
  PLUGIN_MANUFACTURER_CODE MfxL
  PLUGIN_CODE McL1
  FORMATS ${MFX_FORMATS}
  PRODUCT_NAME "MFX Claude"
  IS_SYNTH FALSE
  NEEDS_MIDI_INPUT TRUE
  NEEDS_MIDI_OUTPUT FALSE
  IS_MIDI_EFFECT FALSE
  EDITOR_WANTS_KEYBOARD_FOCUS FALSE
  COPY_PLUGIN_AFTER_BUILD FALSE
  AU_MAIN_TYPE kAudioUnitType_MusicEffect
  VST3_CATEGORIES Fx Distortion
  MICROPHONE_PERMISSION_ENABLED TRUE
  MICROPHONE_PERMISSION_TEXT "MFX Claude processes your guitar input in the standalone app.")

file(GLOB MFX_FACTORY_PRESETS ${CMAKE_SOURCE_DIR}/presets/factory/*.json)
juce_add_binary_data(MultiEffectsPresets SOURCES ${MFX_FACTORY_PRESETS})

target_sources(MultiEffects PRIVATE
  PluginProcessor.cpp
  PluginEditor.cpp
  ${CMAKE_SOURCE_DIR}/src/ui/LookAndFeel.cpp
  ${CMAKE_SOURCE_DIR}/src/ui/ChainView.cpp
  ${CMAKE_SOURCE_DIR}/src/ui/ParamPanel.cpp
  ${CMAKE_SOURCE_DIR}/src/ui/AnalysisView.cpp)
target_include_directories(MultiEffects PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(MultiEffects PUBLIC
  JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0 JUCE_DISPLAY_SPLASH_SCREEN=0)
target_link_libraries(MultiEffects
  PRIVATE mfx_core MultiEffectsPresets
          juce::juce_audio_utils juce::juce_dsp
  PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)

# Developer tool: off-screen render of the Visualize panel (not built by default, never shipped).
option(MFX_BUILD_UI_SNAPSHOT "Build the mfx_ui_snapshot developer tool" OFF)
if(MFX_BUILD_UI_SNAPSHOT)
  juce_add_console_app(mfx_ui_snapshot PRODUCT_NAME "mfx_ui_snapshot")
  target_sources(mfx_ui_snapshot PRIVATE ${CMAKE_SOURCE_DIR}/apps/mfx_ui_snapshot.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/AnalysisView.cpp ${CMAKE_SOURCE_DIR}/src/ui/LookAndFeel.cpp)
  target_include_directories(mfx_ui_snapshot PRIVATE ${CMAKE_SOURCE_DIR}/src)
  target_compile_definitions(mfx_ui_snapshot PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
  target_link_libraries(mfx_ui_snapshot PRIVATE mfx_core juce::juce_gui_basics juce::juce_recommended_config_flags)
endif()
