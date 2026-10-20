cmake_minimum_required(VERSION 3.22)
project(Clarion VERSION 1.0.0 LANGUAGES C CXX)

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

# ---- Clarion: goes on the vocal
juce_add_plugin(Clarion
    COMPANY_NAME                "Toota Audio"
    BUNDLE_ID                   "com.tootaaudio.clarion"
    PLUGIN_MANUFACTURER_CODE    Toot
    PLUGIN_CODE                 Clrn
    FORMATS                     ${PLUGIN_FORMATS}
    PRODUCT_NAME                "Clarion"
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD     FALSE
    VST3_CATEGORIES             Fx Dynamics EQ Pitch Shift
    AU_MAIN_TYPE                kAudioUnitType_Effect)
target_sources(Clarion PRIVATE Source/Processor.cpp Source/Editor.cpp)

# ---- Clarion Room: goes on the music bus, vocal on its sidechain
juce_add_plugin(ClarionRoom
    COMPANY_NAME                "Toota Audio"
    BUNDLE_ID                   "com.tootaaudio.clarionroom"
    PLUGIN_MANUFACTURER_CODE    Toot
    PLUGIN_CODE                 Clrm
    FORMATS                     ${PLUGIN_FORMATS}
    PRODUCT_NAME                "Clarion Room"
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD     FALSE
    VST3_CATEGORIES             Fx Dynamics EQ
    AU_MAIN_TYPE                kAudioUnitType_Effect)
target_sources(ClarionRoom PRIVATE Source/Room.cpp)

foreach(t Clarion ClarionRoom)
    target_compile_definitions(${t} PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)
    target_link_libraries(${t}
        PRIVATE juce::juce_audio_utils juce::juce_dsp
        PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)
endforeach()

option(PLUGIN_BUILD_TESTS "Build offline tests" OFF)
if(PLUGIN_BUILD_TESTS)
    juce_add_console_app(ClarionTest PRODUCT_NAME "ClarionTest")
    target_sources(ClarionTest PRIVATE Tests/ClarionTest.cpp Source/Processor.cpp Source/Editor.cpp Source/Room.cpp)
    target_compile_definitions(ClarionTest PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 CLARION_NO_FACTORY=1 ROOM_NO_FACTORY=1)
    target_link_libraries(ClarionTest PRIVATE juce::juce_audio_utils juce::juce_dsp juce::juce_audio_formats juce::juce_recommended_config_flags)
endif()
