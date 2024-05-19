 
cmake_minimum_required(VERSION 3.15)


project(myk-tracker VERSION 0.0.1)

set(CMAKE_CXX_STANDARD 17)


########## CLI version cmake config 
# Find the ncurses package
find_package(Curses REQUIRED)
# Include the ncurses headers
include_directories(${CURSES_INCLUDE_DIR})


if(APPLE) # add home brew lib and include paths for rtmidi
    link_directories(/opt/homebrew/lib)
    include_directories(/opt/homebrew/include/rtmidi)
elseif(UNIX AND NOT APPLE) # add rtmidi include location for linux
    # Add for Linux users
    include_directories(/usr/include/rtmidi)
endif()


# build the sequencer library
add_library(seq-lib src/Sequencer.cpp src/SequencerEditor.cpp src/SequencerCommands.cpp)
# build the midi utils
add_library(midi-lib src/MidiUtils.cpp)

add_executable(MykTracker src/Gui.cpp src/main.cpp)
add_executable(CurTest src/Gui.cpp src/curtest.cpp)


# Link the ncurses library with your executable
target_link_libraries(MykTracker seq-lib midi-lib ${CURSES_LIBRARIES} panel rtmidi)
target_link_libraries(CurTest seq-lib midi-lib ${CURSES_LIBRARIES} panel rtmidi)

########## CLI version cmake config 


add_subdirectory(./JUCE ./JUCE)               

juce_add_plugin(myk-tracker-plug
    # VERSION ...                               # Set this if the plugin version is different to the project version
    # ICON_BIG ...                              # ICON_* arguments specify a path to an image file to use as an icon for the Standalone
    # ICON_SMALL ...
    COMPANY_NAME Yee-King                          # Specify the name of the plugin's author
    IS_SYNTH TRUE                       # Is this a synth or an effect?
    NEEDS_MIDI_INPUT TRUE               # Does the plugin need midi input?
    # NEEDS_MIDI_OUTPUT TRUE/FALSE              # Does the plugin need midi output?
    # IS_MIDI_EFFECT TRUE/FALSE                 # Is this plugin a MIDI effect?
    NEEDS_MIDI_OUTPUT TRUE
    # EDITOR_WANTS_KEYBOARD_FOCUS TRUE/FALSE    # Does the editor need keyboard focus?
    COPY_PLUGIN_AFTER_BUILD TRUE        # Should the plugin be installed to a default location after building?
    PLUGIN_MANUFACTURER_CODE Yeek               # A four-character manufacturer id with at least one upper-case character
    PLUGIN_CODE abc1                            # A unique four-character plugin id with exactly one upper-case character
                                                # GarageBand 10.3 requires the first letter to be upper-case, and the remaining letters to be lower-case
    FORMATS AU VST3 Standalone                  # The formats to build. Other valid formats are: AAX Unity VST AU AUv3
    PRODUCT_NAME "myk-tracker-plug")        # The name of the final executable, which can differ from the target name


juce_generate_juce_header(myk-tracker-plug)


target_sources(myk-tracker-plug
    PRIVATE
    src/PluginEditor.cpp
    src/PluginProcessor.cpp
    src/RaggedTableComponent.cpp
    src/MidiUtils.cpp src/Sequencer.cpp src/SequencerEditor.cpp src/SequencerCommands.cpp)

target_compile_definitions(myk-tracker-plug
    PUBLIC # 
        JUCE_ALSA=1
        JUCE_DIRECTSOUND=1
        JUCE_DISABLE_CAUTIOUS_PARAMETER_ID_CHECKING=1
        #JUCE_PLUGINHOST_LADSPA=1
        #JUCE_PLUGINHOST_LV2=1
        #JUCE_PLUGINHOST_VST3=1
        JUCE_USE_OGGVORBIS=1
        #JUCE_VST3_HOST_CROSS_PLATFORM_UID=1
        # JUCE_WEB_BROWSER and JUCE_USE_CURL would be on by default, but you might not need them.
        JUCE_WEB_BROWSER=0  # If you remove this, add `NEEDS_WEB_BROWSER TRUE` to the `juce_add_plugin` call
        JUCE_USE_CURL=0     # If you remove this, add `NEEDS_CURL TRUE` to the `juce_add_plugin` call
        JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(myk-tracker-plug
    PRIVATE
        # AudioPluginData           # If we'd created a binary data target, we'd link to it here
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)


