cmake_minimum_required(VERSION 3.22)
project(TrapVocalPro VERSION 1.0.0)

include(FetchContent)
FetchContent_Declare(JUCE
  GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
  GIT_TAG 8.0.4
  GIT_SHALLOW ON)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(TrapVocalPro
  COMPANY_NAME "Privat"
  IS_SYNTH FALSE
  NEEDS_MIDI_INPUT FALSE
  NEEDS_MIDI_OUTPUT FALSE
  PLUGIN_MANUFACTURER_CODE Priv
  PLUGIN_CODE Tvpr
  FORMATS VST3
  PRODUCT_NAME "TRAPVOCAL PRO")

target_sources(TrapVocalPro PRIVATE
  Source/DSP.h
  Source/PluginProcessor.h
  Source/PluginProcessor.cpp
  Source/PluginEditor.h
  Source/PluginEditor.cpp)

target_compile_features(TrapVocalPro PUBLIC cxx_std_17)

target_compile_definitions(TrapVocalPro PUBLIC
  JUCE_WEB_BROWSER=0
  JUCE_USE_CURL=0
  JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(TrapVocalPro
  PRIVATE juce::juce_audio_utils juce::juce_dsp
  PUBLIC juce::juce_recommended_config_flags)
