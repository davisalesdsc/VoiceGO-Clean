cmake_minimum_required(VERSION 3.22)

# Universal binary (Apple Silicon + Intel) unless the caller overrides it
if(APPLE AND NOT CMAKE_OSX_ARCHITECTURES)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)
endif()
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "Minimum macOS version")

project(VoiceGOClean VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

# ---- JUCE -------------------------------------------------------------------
# Use -DFETCHCONTENT_SOURCE_DIR_JUCE=/path/to/JUCE to build from a local copy.
include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.9
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(JUCE)

# ---- Plugin -----------------------------------------------------------------
set(VOICEGO_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND VOICEGO_FORMATS AU)
endif()

juce_add_plugin(VoiceGOClean
    COMPANY_NAME                "VoiceGO"
    BUNDLE_ID                   "com.voicego.clean"
    PLUGIN_MANUFACTURER_CODE    Vogo
    PLUGIN_CODE                 Vgcl
    PRODUCT_NAME                "VoiceGO Clean"
    DESCRIPTION                 "Voice / ambience / reverb separation"
    FORMATS                     ${VOICEGO_FORMATS}
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    VST3_CATEGORIES             Fx Restoration
    AU_MAIN_TYPE                kAudioUnitType_Effect
    COPY_PLUGIN_AFTER_BUILD     FALSE
    HARDENED_RUNTIME_ENABLED    TRUE
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT  "O VoiceGO Clean precisa do microfone para limpar o audio em tempo real.")

target_sources(VoiceGOClean PRIVATE
    Source/SeparationEngine.cpp
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

target_compile_definitions(VoiceGOClean PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(VoiceGOClean
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# ---- Offline test tool (not shipped) ---------------------------------------
option(VOICEGO_BUILD_TESTS "Build the offline test tool" OFF)
if(VOICEGO_BUILD_TESTS)
    juce_add_console_app(VoiceGOCleanTest PRODUCT_NAME "VoiceGOCleanTest")
    target_sources(VoiceGOCleanTest PRIVATE Tools/OfflineTest.cpp Source/SeparationEngine.cpp)
    target_include_directories(VoiceGOCleanTest PRIVATE Source)
    target_compile_definitions(VoiceGOCleanTest PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(VoiceGOCleanTest PRIVATE juce::juce_audio_formats juce::juce_dsp
                          PUBLIC juce::juce_recommended_config_flags)

    # renders the real editor to PNG (needs a display; use xvfb-run on Linux)
    juce_add_gui_app(VoiceGOCleanShot PRODUCT_NAME "VoiceGOCleanShot")
    target_sources(VoiceGOCleanShot PRIVATE Tools/Screenshot.cpp Source/SeparationEngine.cpp
                   Source/PluginProcessor.cpp Source/PluginEditor.cpp)
    target_include_directories(VoiceGOCleanShot PRIVATE Source)
    target_compile_definitions(VoiceGOCleanShot PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0
        JucePlugin_Name="VoiceGO Clean" JucePlugin_VersionString="${PROJECT_VERSION}")
    target_link_libraries(VoiceGOCleanShot PRIVATE juce::juce_audio_utils juce::juce_dsp
                          PUBLIC juce::juce_recommended_config_flags)
endif()
