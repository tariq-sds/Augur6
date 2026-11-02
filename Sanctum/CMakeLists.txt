cmake_minimum_required(VERSION 3.22)
project(Sanctum VERSION 1.0.0 LANGUAGES C CXX)

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

# ---- Sanctum: choir and harmony generator, goes on a vocal
juce_add_plugin(Sanctum
    COMPANY_NAME                "Toota Audio"
    BUNDLE_ID                   "com.tootaaudio.sanctum"
    PLUGIN_MANUFACTURER_CODE    Toot
    PLUGIN_CODE                 Snct
    FORMATS                     ${PLUGIN_FORMATS}
    PRODUCT_NAME                "Sanctum"
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            TRUE
    NEEDS_MIDI_OUTPUT           FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD     FALSE
    VST3_CATEGORIES             Fx Pitch Shift Reverb
    AU_MAIN_TYPE                kAudioUnitType_MusicEffect)
target_sources(Sanctum PRIVATE Source/Sanctum.cpp Source/Editor.cpp)
target_compile_definitions(Sanctum PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)
target_link_libraries(Sanctum
    PRIVATE juce::juce_audio_utils juce::juce_dsp TootaAssets
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)

option(PLUGIN_BUILD_TESTS "Build offline tests" OFF)
if(PLUGIN_BUILD_TESTS)
    juce_add_console_app(SanctumTest PRODUCT_NAME "SanctumTest")
    target_sources(SanctumTest PRIVATE Tests/SanctumTest.cpp Source/Sanctum.cpp Source/Editor.cpp)
    target_compile_definitions(SanctumTest PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 SANCTUM_NO_FACTORY=1)
    target_link_libraries(SanctumTest PRIVATE juce::juce_audio_utils juce::juce_dsp juce::juce_audio_formats juce::juce_recommended_config_flags TootaAssets)
endif()
