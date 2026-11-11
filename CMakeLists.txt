cmake_minimum_required(VERSION 3.22)

# Universal binary (Apple Silicon + Intel) unless the caller overrides it
if(APPLE AND NOT CMAKE_OSX_ARCHITECTURES)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)
endif()
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "Minimum macOS version")

project(VoiceGOClean VERSION 1.1.0 LANGUAGES C CXX)

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

# ---- Neural voice model (DeepFilterNet3 via Rust, see dfn/) ---------------------
#   VOICEGO_DFN=neural : builds dfn/ with cargo (default; needs Rust)
#   VOICEGO_DFN=stub   : delay-only stand-in, to test the plugin plumbing
#   VOICEGO_DFN=none   : no model, the plugin uses its DSP separation
set(VOICEGO_DFN "neural" CACHE STRING "Voice model: neural | stub | none")
#   VOICEGO_DFN_PREBUILT=/path/libvoicego_dfn.a : use an already built library (CI on macOS)
set(VOICEGO_DFN_PREBUILT "" CACHE FILEPATH "Prebuilt libvoicego_dfn.a (skips cargo)")

if(VOICEGO_DFN STREQUAL "neural" AND VOICEGO_DFN_PREBUILT)
    add_library(voicego_dfn STATIC IMPORTED GLOBAL)
    set_target_properties(voicego_dfn PROPERTIES IMPORTED_LOCATION "${VOICEGO_DFN_PREBUILT}")
    if(UNIX AND NOT APPLE)
        set_property(TARGET voicego_dfn PROPERTY INTERFACE_LINK_LIBRARIES pthread dl m)
    endif()
elseif(VOICEGO_DFN STREQUAL "neural")
    find_program(CARGO_EXECUTABLE cargo HINTS "$ENV{HOME}/.cargo/bin" REQUIRED)
    set(DFN_DIR "${CMAKE_CURRENT_SOURCE_DIR}/dfn")
    set(DFN_TARGET_DIR "${CMAKE_BINARY_DIR}/dfn-target")
    set(DFN_LIB "${CMAKE_BINARY_DIR}/libvoicego_dfn.a")
    set(DFN_ENV MACOSX_DEPLOYMENT_TARGET=${CMAKE_OSX_DEPLOYMENT_TARGET})

    if(APPLE)
        set(DFN_COMMANDS)
        set(DFN_SLICES)
        foreach(arch IN LISTS CMAKE_OSX_ARCHITECTURES)
            if(arch STREQUAL "arm64")
                set(triple aarch64-apple-darwin)
            else()
                set(triple ${arch}-apple-darwin)
            endif()
            list(APPEND DFN_COMMANDS COMMAND ${CMAKE_COMMAND} -E env ${DFN_ENV}
                 ${CARGO_EXECUTABLE} build --release --locked --manifest-path "${DFN_DIR}/Cargo.toml"
                 --target-dir "${DFN_TARGET_DIR}" --target ${triple})
            list(APPEND DFN_SLICES "${DFN_TARGET_DIR}/${triple}/release/libvoicego_dfn.a")
        endforeach()
        add_custom_command(OUTPUT "${DFN_LIB}"
            ${DFN_COMMANDS}
            COMMAND lipo -create ${DFN_SLICES} -output "${DFN_LIB}"
            DEPENDS "${DFN_DIR}/Cargo.toml" "${DFN_DIR}/Cargo.lock" "${DFN_DIR}/src/lib.rs"
            COMMENT "Building DeepFilterNet3 (Rust) for ${CMAKE_OSX_ARCHITECTURES}"
            VERBATIM USES_TERMINAL)
    else()
        add_custom_command(OUTPUT "${DFN_LIB}"
            COMMAND ${CARGO_EXECUTABLE} build --release --locked --manifest-path "${DFN_DIR}/Cargo.toml"
                    --target-dir "${DFN_TARGET_DIR}"
            COMMAND ${CMAKE_COMMAND} -E copy "${DFN_TARGET_DIR}/release/libvoicego_dfn.a" "${DFN_LIB}"
            DEPENDS "${DFN_DIR}/Cargo.toml" "${DFN_DIR}/Cargo.lock" "${DFN_DIR}/src/lib.rs"
            COMMENT "Building DeepFilterNet3 (Rust)"
            VERBATIM USES_TERMINAL)
    endif()

    add_custom_target(voicego_dfn_build DEPENDS "${DFN_LIB}")
    add_library(voicego_dfn STATIC IMPORTED GLOBAL)
    set_target_properties(voicego_dfn PROPERTIES IMPORTED_LOCATION "${DFN_LIB}")
    add_dependencies(voicego_dfn voicego_dfn_build)
    if(UNIX AND NOT APPLE)
        set_property(TARGET voicego_dfn PROPERTY INTERFACE_LINK_LIBRARIES pthread dl m)
    endif()
elseif(VOICEGO_DFN STREQUAL "stub")
    add_library(voicego_dfn STATIC dfn/stub/voicego_dfn_stub.c)
elseif(VOICEGO_DFN STREQUAL "none")
    add_library(voicego_dfn STATIC dfn/stub/voicego_dfn_none.c)
else()
    message(FATAL_ERROR "VOICEGO_DFN must be neural, stub or none")
endif()
target_include_directories(voicego_dfn INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/dfn")
if(NOT VOICEGO_DFN STREQUAL "neural")
    set_target_properties(voicego_dfn PROPERTIES POSITION_INDEPENDENT_CODE ON)
endif()
message(STATUS "VoiceGO Clean voice model: ${VOICEGO_DFN}")

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
    Source/NeuralVoice.cpp
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
        voicego_dfn
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# ---- Offline test tool (not shipped) ---------------------------------------
option(VOICEGO_BUILD_TESTS "Build the offline test tool" OFF)
if(VOICEGO_BUILD_TESTS)
    juce_add_console_app(VoiceGOCleanTest PRODUCT_NAME "VoiceGOCleanTest")
    target_sources(VoiceGOCleanTest PRIVATE Tools/OfflineTest.cpp Tools/NeuralTests.cpp Source/SeparationEngine.cpp Source/NeuralVoice.cpp)
    target_include_directories(VoiceGOCleanTest PRIVATE Source)
    target_compile_definitions(VoiceGOCleanTest PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(VoiceGOCleanTest PRIVATE juce::juce_audio_formats juce::juce_dsp voicego_dfn
                          PUBLIC juce::juce_recommended_config_flags)

    # renders the real editor to PNG (needs a display; use xvfb-run on Linux)
    juce_add_gui_app(VoiceGOCleanShot PRODUCT_NAME "VoiceGOCleanShot")
    target_sources(VoiceGOCleanShot PRIVATE Tools/Screenshot.cpp Source/SeparationEngine.cpp Source/NeuralVoice.cpp
                   Source/PluginProcessor.cpp Source/PluginEditor.cpp)
    target_include_directories(VoiceGOCleanShot PRIVATE Source)
    target_compile_definitions(VoiceGOCleanShot PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0
        JucePlugin_Name="VoiceGO Clean" JucePlugin_VersionString="${PROJECT_VERSION}")
    target_link_libraries(VoiceGOCleanShot PRIVATE juce::juce_audio_utils juce::juce_dsp voicego_dfn
                          PUBLIC juce::juce_recommended_config_flags)
endif()
