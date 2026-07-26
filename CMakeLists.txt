cmake_minimum_required(VERSION 3.22)

project(ChugGate VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(
    CHUGGATE_CONFIGURE_ONLY
    "Configure the top-level project without requiring the JUCE submodule"
    OFF
)

if(CHUGGATE_CONFIGURE_ONLY)
    message(STATUS "ChugGate configure-only mode enabled")
    return()
endif()

if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/JUCE/CMakeLists.txt")
    message(
        FATAL_ERROR
        "The JUCE submodule is missing. Run git submodule update --init --recursive before configuring."
    )
endif()

add_subdirectory(JUCE)

juce_add_plugin(ChugGate
    PRODUCT_NAME ChugGate
    COMPANY_NAME "nSpace Systems"
    VERSION 1.0.0
    PLUGIN_MANUFACTURER_CODE NSPC
    PLUGIN_CODE ChGt
    BUNDLE_ID com.nspacesystems.chuggate
    FORMATS VST3 Standalone
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    COPY_PLUGIN_AFTER_BUILD TRUE
)

target_sources(ChugGate
    PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        Source/StateManager.cpp
        Source/DSP/KeySourceRouter.cpp
        Source/DSP/KeyConditioner.cpp
        Source/DSP/PowerlineShield.cpp
        Source/DSP/DetectorEngine.cpp
        Source/DSP/PerformanceStateMachine.cpp
        Source/DSP/GainComputer.cpp
        Source/DSP/GainSmoother.cpp
)

target_compile_definitions(ChugGate
    PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0
)

target_link_libraries(ChugGate
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

enable_testing()

add_executable(ChugGateTests
    tests/TestMain.cpp
)

if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/DetectorTests.cpp")
    add_custom_command(
        OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/tests/DetectorTests.cpp"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/tests"
        COMMAND ${CMAKE_COMMAND} -E touch "${CMAKE_CURRENT_BINARY_DIR}/tests/DetectorTests.cpp"
        COMMENT "Generating stub DetectorTests.cpp"
    )
    target_sources(ChugGateTests
        PRIVATE
            "${CMAKE_CURRENT_BINARY_DIR}/tests/DetectorTests.cpp"
            Source/DSP/KeySourceRouter.cpp
            Source/DSP/KeyConditioner.cpp
            Source/DSP/PowerlineShield.cpp
            Source/DSP/DetectorEngine.cpp
    )
else()
    target_sources(ChugGateTests
        PRIVATE
            tests/DetectorTests.cpp
            Source/DSP/KeySourceRouter.cpp
            Source/DSP/KeyConditioner.cpp
            Source/DSP/PowerlineShield.cpp
            Source/DSP/DetectorEngine.cpp
    )
endif()

if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/StateMachineTests.cpp")
    add_custom_command(
        OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/tests/StateMachineTests.cpp"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/tests"
        COMMAND ${CMAKE_COMMAND} -E touch "${CMAKE_CURRENT_BINARY_DIR}/tests/StateMachineTests.cpp"
        COMMENT "Generating stub StateMachineTests.cpp"
    )
    target_sources(ChugGateTests
        PRIVATE
            "${CMAKE_CURRENT_BINARY_DIR}/tests/StateMachineTests.cpp"
            Source/DSP/PerformanceStateMachine.cpp
            Source/DSP/GainComputer.cpp
            Source/DSP/GainSmoother.cpp
    )
else()
    target_sources(ChugGateTests
        PRIVATE
            tests/StateMachineTests.cpp
            Source/DSP/PerformanceStateMachine.cpp
            Source/DSP/GainComputer.cpp
            Source/DSP/GainSmoother.cpp
    )
endif()

target_compile_definitions(ChugGateTests
    PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0
)

target_link_libraries(ChugGateTests
    PRIVATE
        juce::juce_dsp
        juce::juce_events
        juce::juce_core
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)

add_test(NAME ChugGateTests COMMAND ChugGateTests)
