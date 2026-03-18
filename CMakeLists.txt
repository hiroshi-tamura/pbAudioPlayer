cmake_minimum_required(VERSION 3.22)
project(pbAudioPlayer VERSION 1.0.0)

add_subdirectory(C:/JUCE ${CMAKE_BINARY_DIR}/JUCE)

juce_add_gui_app(pbAudioPlayer
    PRODUCT_NAME "pbAudioPlayer"
    COMPANY_NAME "pb"
    NEEDS_CURL FALSE
    NEEDS_WEB_BROWSER FALSE
)

juce_generate_juce_header(pbAudioPlayer)

target_sources(pbAudioPlayer
    PRIVATE
        Source/Main.cpp
        Source/MainComponent.cpp
        Source/WaveformComponent.cpp
        Source/PeakMeterComponent.cpp
        Source/TimeScaleComponent.cpp
        icon.rc
)

target_compile_definitions(pbAudioPlayer
    PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        NOMINMAX=1
)

target_compile_options(pbAudioPlayer PRIVATE /utf-8)

target_link_libraries(pbAudioPlayer
    PRIVATE
        juce::juce_audio_basics
        juce::juce_audio_devices
        juce::juce_audio_formats
        juce::juce_audio_utils
        juce::juce_core
        juce::juce_events
        juce::juce_graphics
        juce::juce_gui_basics
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)
