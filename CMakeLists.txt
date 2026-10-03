cmake_minimum_required(VERSION 3.22)

project(FlashRoll VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.4
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(JUCE)

# FlashRoll is a sight-reading trainer: it flashes a note on a treble / bass /
# grand staff and waits for the matching MIDI key. Same shape as hitnotedmx:
# an INSTRUMENT on a MIDI track (receives the keyboard directly) with a stereo
# output that carries the optional monitor / feedback tones. The pure
# MIDI-effect shape doesn't load in Live, so we don't use it.
juce_add_plugin(FlashRoll
    COMPANY_NAME                "HitDmx"
    BUNDLE_ID                   "com.hitdmx.FlashRoll"
    PLUGIN_MANUFACTURER_CODE    "HtDx"
    PLUGIN_CODE                 "FlRl"
    FORMATS                     VST3 Standalone
    PRODUCT_NAME                "FlashRoll"
    DESCRIPTION                 "Sight-reading flash cards answered from MIDI (VST3)"
    IS_SYNTH                    TRUE
    NEEDS_MIDI_INPUT            TRUE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD     TRUE
    VST3_CATEGORIES             "Instrument"
)

target_sources(FlashRoll PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/NoteSpelling.cpp
    Source/Drill.cpp
    Source/ToneEngine.cpp
    Source/StaffRenderer.cpp
    Source/TrainerKeyboard.cpp
)

# The Bravura SMuFL music font (SIL OFL, see assets/fonts/Bravura-OFL.txt) is
# embedded in the binary: clefs, noteheads and accidentals are real engraving
# glyphs on every platform, with no font install and no bundle-path lookups.
juce_add_binary_data(FlashRollData SOURCES
    assets/fonts/Bravura.otf
)

target_include_directories(FlashRoll PRIVATE Source)

target_compile_definitions(FlashRoll PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(FlashRoll PRIVATE
    FlashRollData
    juce::juce_audio_utils
    juce::juce_audio_processors
    juce::juce_gui_basics
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
)

# ---- drill-check ------------------------------------------------------------
# The drill engine (NoteSpelling + Drill) is plain C++ with no JUCE, so its
# regression net is a tiny plain executable: staff placement for every key,
# candidate ranges, answer scoring, timeouts, adaptive weighting and the stats
# round trip. Registered as the `drill-logic` CTest below.
add_executable(drill-check
    tools/DrillCheck.cpp
    Source/NoteSpelling.cpp
    Source/Drill.cpp
)
target_include_directories(drill-check PRIVATE Source)

# ---- staff-render -----------------------------------------------------------
# Renders a contact sheet of staff scenes through the REAL StaffRenderer to a
# PNG (default previews/staff-sheet.png) — eyeball engraving changes without a
# host. Also run as the `staff-font` CTest: it fails if the embedded music font
# can't be loaded (which would silently fall back to ugly placeholder glyphs).
juce_add_console_app(staff-render PRODUCT_NAME "staff-render")
target_sources(staff-render PRIVATE
    tools/StaffRender.cpp
    Source/StaffRenderer.cpp
    Source/NoteSpelling.cpp
    Source/Drill.cpp
)
target_include_directories(staff-render PRIVATE Source)
target_compile_definitions(staff-render PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
)
target_link_libraries(staff-render PRIVATE
    FlashRollData
    juce::juce_gui_basics
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags
)

enable_testing()
add_test(NAME drill-logic COMMAND drill-check)
add_test(NAME staff-font
         COMMAND staff-render "${CMAKE_CURRENT_BINARY_DIR}/staff-sheet.png")
