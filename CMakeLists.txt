cmake_minimum_required(VERSION 3.15)

project(myk-tracker VERSION 0.0.4)

set(CMAKE_CXX_STANDARD 17)
set (CMAKE_OSX_DEPLOYMENT_TARGET 11.0) # mac - target macos 11
set (CMAKE_OSX_ARCHITECTURES arm64;x86_64) # mac - build universal binary


########## CLI version cmake config 
# Find the ncurses package
find_package(Curses REQUIRED)
# Include the ncurses headers
include_directories(${CURSES_INCLUDE_DIR})

# Add the RtMidi subdirectory
# Set the RTMIDI_API_JACK option to OFF
# if(APPLE) 
set(RTMIDI_API_JACK OFF CACHE BOOL "Disable Jack support" FORCE)
# endif()
add_subdirectory(./libs/rtmidi ./rtmidi)



# build the sequencer library
add_library(seq-lib src/Sequencer.cpp src/SequencerEditor.cpp src/SequencerCommands.cpp src/TrackerController.cpp)

add_executable(MykTracker src/Gui.cpp src/MidiUtils.cpp src/main.cpp)
add_executable(CurTest src/Gui.cpp src/MidiUtils.cpp src/curtest.cpp)

# Link the ncurses library with your executable
target_link_libraries(MykTracker PRIVATE seq-lib  ${CURSES_LIBRARIES} panel  rtmidi)
target_link_libraries(CurTest PRIVATE seq-lib  ${CURSES_LIBRARIES} panel  rtmidi)

########## END/ CLI version cmake config 

########## JUCE version cmake config 
add_subdirectory(./libs/JUCE ./JUCE)               

juce_add_plugin(myk-tracker-plug
    # VERSION ...                               # Set this if the plugin version is different to the project version
    # ICON_BIG ...                              # ICON_* arguments specify a path to an image file to use as an icon for the Standalone
    # ICON_SMALL ...
    COMPANY_NAME Yee-King                          # Specify the name of the plugin's author
    IS_SYNTH FALSE                       # Is this a synth or an effect?
    NEEDS_MIDI_INPUT TRUE               # Does the plugin need midi input?
    # NEEDS_MIDI_OUTPUT TRUE/FALSE              # Does the plugin need midi output?
    IS_MIDI_EFFECT TRUE                 # Is this plugin a MIDI effect?
    NEEDS_MIDI_OUTPUT TRUE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE    # Does the editor need keyboard focus?
    NEEDS_WEB_BROWSER TRUE
    NEEDS_CURL TRUE
    COPY_PLUGIN_AFTER_BUILD FALSE        # Should the plugin be installed to a default location after building?
    PLUGIN_MANUFACTURER_CODE Yeek               # A four-character manufacturer id with at least one upper-case ``
    PLUGIN_CODE MykT                            # A unique four-character plugin id with exactly one upper-case character
                                                # GarageBand 10.3 requires the first letter to be upper-case, and the remaining letters to be lower-case
    FORMATS AU VST3 Standalone                  # The formats to build. Other valid formats are: AAX Unity VST AU AUv3
    PRODUCT_NAME "MYK Tracker")        # The name of the final executable, which can differ from the target name


juce_generate_juce_header(myk-tracker-plug)

juce_add_binary_data(WebUIData
    SOURCES
        ui/index.html
        ui/styles.css
        ui/palette-amber-teal.css
        ui/palette-neon-purple.css
        ui/app.js)

target_sources(myk-tracker-plug
    PRIVATE
    src/PluginEditor.cpp
    src/PluginProcessor.cpp
    src/StringTable.cpp
    src/Sequencer.cpp src/SequencerEditor.cpp src/SequencerCommands.cpp src/TrackerController.cpp
    src/HTTPServer.cpp src/HTTPServer.h src/Utils.h
)

    
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
        JUCE_WEB_BROWSER=1
        JUCE_USE_CURL=1
        JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(myk-tracker-plug
    PRIVATE
#        seq-lib
        # AudioPluginData           # If we'd created a binary data target, we'd link to it here
        juce::juce_audio_utils
        juce::juce_gui_extra
        juce::juce_opengl
        WebUIData
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

target_include_directories(myk-tracker-plug
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/libs
        ${CMAKE_CURRENT_SOURCE_DIR}/webui-juce-cmake-example/libs)
