cmake_minimum_required(VERSION 3.22)

project(Augur6 VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(APPLE)
    # Universal binary: runs natively on Intel and Apple Silicon Macs
    if(NOT CMAKE_OSX_ARCHITECTURES)
        set(CMAKE_OSX_ARCHITECTURES "x86_64;arm64" CACHE STRING "" FORCE)
    endif()
    if(NOT CMAKE_OSX_DEPLOYMENT_TARGET)
        set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "" FORCE)
    endif()
endif()

include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.9
    GIT_SHALLOW    ON)
FetchContent_MakeAvailable(JUCE)

if(APPLE)
    set(AUGUR_FORMATS VST3 AU)
else()
    set(AUGUR_FORMATS VST3)
endif()

juce_add_plugin(Augur6
    COMPANY_NAME                "Toota Audio"
    BUNDLE_ID                   "com.tootaaudio.augur6"
    PLUGIN_MANUFACTURER_CODE    Toot
    PLUGIN_CODE                 Agr6
    FORMATS                     ${AUGUR_FORMATS}
    PRODUCT_NAME                "Augur-6"
    IS_SYNTH                    TRUE
    NEEDS_MIDI_INPUT            TRUE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD     FALSE
    VST3_CATEGORIES             Instrument Synth
    AU_MAIN_TYPE                kAudioUnitType_MusicDevice)

set(AUGUR_SOURCES
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/Presets.cpp)

target_sources(Augur6 PRIVATE ${AUGUR_SOURCES})

target_compile_definitions(Augur6 PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(Augur6
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# Offline render test (used to check every preset before shipping)
option(AUGUR_BUILD_TESTS "Build the offline preset render test" OFF)
if(AUGUR_BUILD_TESTS)
    juce_add_console_app(AugurRenderTest PRODUCT_NAME "AugurRenderTest")
    target_sources(AugurRenderTest PRIVATE Tests/RenderTest.cpp ${AUGUR_SOURCES})
    target_compile_definitions(AugurRenderTest PRIVATE
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 AUGUR_TEST_BUILD=1)
    target_link_libraries(AugurRenderTest PRIVATE
        juce::juce_audio_utils juce::juce_dsp juce::juce_audio_formats
        juce::juce_recommended_config_flags)
endif()
