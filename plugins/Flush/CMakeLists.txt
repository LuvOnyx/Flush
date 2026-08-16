# Flush — Pro-Q4-class 24-band dynamic EQ + Flush Match + glue compressor.
# Native C++ (Visage) UI. Cross-platform: Windows (VST3), macOS (VST3/AU),
# Linux (VST3/LV2/Standalone).

# ============================================
# PLATFORM-SPECIFIC CONFIGURATION
# ============================================
if(WIN32)
    set(PLUGIN_FORMATS VST3 Standalone)
elseif(APPLE)
    set(PLUGIN_FORMATS VST3 AU Standalone)
elseif(UNIX)
    set(PLUGIN_FORMATS VST3 LV2 Standalone)
    set(LV2_URI "https://github.com/noizefield/audio-plugin-coder/flush")
else()
    message(FATAL_ERROR "Unsupported platform")
endif()

message(STATUS "Flush: building for ${PLUGIN_FORMATS} (Visage UI)")

# ============================================
# PLUGIN DEFINITION
# ============================================
juce_add_plugin(flush
    COMPANY_NAME "NoiseField"
    PLUGIN_MANUFACTURER_CODE NpsF
    PLUGIN_CODE Flsh
    FORMATS ${PLUGIN_FORMATS}
    PRODUCT_NAME "Flush"
    VST3_CATEGORIES Fx|EQ
    LV2URI ${LV2_URI}
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
)

# ============================================
# BINARY ASSETS (embedded font for consistent text across hosts)
# ============================================
juce_add_binary_data(flush_Assets
    SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/../../_tools/visage/visage_graphics/fonts/Lato-Regular.ttf
    NAMESPACE flush_BinaryData
)

# ============================================
# SOURCE FILES
# ============================================
target_sources(flush
    PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        Source/VisageControls.h
        Source/dsp/FlushCommon.h
        Source/dsp/FlushFilter.h
        Source/dsp/FlushDynamics.h
        Source/dsp/FlushMatch.h
)

# ============================================
# INCLUDE PATHS
# ============================================
target_include_directories(flush
    PRIVATE
        Source
        ${CMAKE_CURRENT_SOURCE_DIR}/../../common
)

# ============================================
# LINK LIBRARIES (JUCE + Visage)
# ============================================
target_link_libraries(flush
    PRIVATE
        flush_Assets
        visage::visage
        juce::juce_audio_basics
        juce::juce_audio_devices
        juce::juce_audio_formats
        juce::juce_audio_plugin_client
        juce::juce_audio_processors
        juce::juce_audio_utils
        juce::juce_core
        juce::juce_data_structures
        juce::juce_dsp
        juce::juce_events
        juce::juce_graphics
        juce::juce_gui_basics
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

# ============================================
# COMPILE DEFINITIONS
# ============================================
target_compile_definitions(flush
    PUBLIC
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_WEB_BROWSER=0
)

if(WIN32)
    target_compile_definitions(flush PUBLIC _USE_MATH_DEFINES=1)
endif()

if(APPLE)
    target_compile_definitions(flush PUBLIC JUCE_USE_CURL=0)
endif()

if(UNIX AND NOT APPLE)
    target_compile_definitions(flush PUBLIC JUCE_USE_CURL=0 JUCE_JACK=1)
endif()
