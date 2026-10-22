cmake_minimum_required(VERSION 3.22)
project(Arc VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(APPLE)
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
    set(PLUGIN_FORMATS VST3 AU)
else()
    set(PLUGIN_FORMATS VST3)
endif()

juce_add_plugin(Arc
    COMPANY_NAME                "Toota Audio"
    BUNDLE_ID                   "com.tootaaudio.arc"
    PLUGIN_MANUFACTURER_CODE    Toot
    PLUGIN_CODE                 Arcx
    FORMATS                     ${PLUGIN_FORMATS}
    PRODUCT_NAME                "Arc"
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD     FALSE
    VST3_CATEGORIES             Fx Tools Generator
    AU_MAIN_TYPE                kAudioUnitType_Effect)
target_sources(Arc PRIVATE Source/Processor.cpp Source/Editor.cpp)
target_compile_definitions(Arc PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)
target_link_libraries(Arc
    PRIVATE juce::juce_audio_utils juce::juce_dsp
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)

option(PLUGIN_BUILD_TESTS "Build offline tests" OFF)
if(PLUGIN_BUILD_TESTS)
    juce_add_console_app(ArcTest PRODUCT_NAME "ArcTest")
    target_sources(ArcTest PRIVATE Tests/ArcTest.cpp Source/Processor.cpp Source/Editor.cpp)
    target_compile_definitions(ArcTest PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(ArcTest PRIVATE juce::juce_audio_utils juce::juce_dsp juce::juce_audio_formats juce::juce_recommended_config_flags)
endif()
