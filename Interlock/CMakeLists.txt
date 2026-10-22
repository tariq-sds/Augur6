cmake_minimum_required(VERSION 3.22)
project(Interlock VERSION 1.0.0 LANGUAGES C CXX)

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

# TOOTA brand: Fredoka type + logo, shared by every plugin
juce_add_binary_data(TootaAssets HEADER_NAME TootaAssets.h NAMESPACE TootaAssets SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/../Common/Assets/FredokaBold.ttf
    ${CMAKE_CURRENT_SOURCE_DIR}/../Common/Assets/FredokaSemiBold.ttf
    ${CMAKE_CURRENT_SOURCE_DIR}/../Common/Assets/FredokaMedium.ttf
    ${CMAKE_CURRENT_SOURCE_DIR}/../Common/Assets/tootalogo.png)
set_target_properties(TootaAssets PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(APPLE)
    set(PLUGIN_FORMATS VST3 AU)
else()
    set(PLUGIN_FORMATS VST3)
endif()

juce_add_plugin(Interlock
    COMPANY_NAME                "Toota Audio"
    BUNDLE_ID                   "com.tootaaudio.interlock"
    PLUGIN_MANUFACTURER_CODE    Toot
    PLUGIN_CODE                 Itlk
    FORMATS                     ${PLUGIN_FORMATS}
    PRODUCT_NAME                "Interlock"
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD     FALSE
    VST3_CATEGORIES             Fx Dynamics EQ
    AU_MAIN_TYPE                kAudioUnitType_Effect)

set(IL_SOURCES Source/Processor.cpp Source/Editor.cpp)
target_sources(Interlock PRIVATE ${IL_SOURCES})
target_compile_definitions(Interlock PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)
target_link_libraries(Interlock
    PRIVATE juce::juce_audio_utils juce::juce_dsp
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)

option(PLUGIN_BUILD_TESTS "Build offline tests" OFF)
if(PLUGIN_BUILD_TESTS)
    juce_add_console_app(InterlockTest PRODUCT_NAME "InterlockTest")
    target_sources(InterlockTest PRIVATE Tests/InterlockTest.cpp ${IL_SOURCES})
    target_compile_definitions(InterlockTest PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(InterlockTest PRIVATE juce::juce_audio_utils juce::juce_dsp juce::juce_audio_formats juce::juce_recommended_config_flags)
endif()

foreach(t Interlock InterlockTest)
    if(TARGET ${t})
        target_link_libraries(${t} PRIVATE TootaAssets)
    endif()
endforeach()
