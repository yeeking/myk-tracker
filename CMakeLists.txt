cmake_minimum_required(VERSION 3.15)

project(myk-tracker VERSION 0.0.4)

set(CMAKE_CXX_STANDARD 17)
set (CMAKE_OSX_DEPLOYMENT_TARGET 11.0) # mac - target macos 11
set (CMAKE_OSX_ARCHITECTURES arm64;x86_64) # mac - build universal binary


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
    IS_MIDI_EFFECT FALSE                 # Is this plugin a MIDI effect?
    NEEDS_MIDI_OUTPUT TRUE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE    # Does the editor need keyboard focus?
    COPY_PLUGIN_AFTER_BUILD TRUE      # Avoid copying into system plugin dirs during local builds.
    PLUGIN_MANUFACTURER_CODE Yeek               # A four-character manufacturer id with at least one upper-case ``
    PLUGIN_CODE Trak                            # A unique four-character plugin id with exactly one upper-case character
                                                # GarageBand 10.3 requires the first letter to be upper-case, and the remaining letters to be lower-case
    FORMATS AU VST3 Standalone                  # The formats to build. Other valid formats are: AAX Unity VST AU AUv3
    PRODUCT_NAME "MYK Tracker GL")        # The name of the final executable, which can differ from the target name


juce_generate_juce_header(myk-tracker-plug)

target_sources(myk-tracker-plug
    PRIVATE
    src/TrackerMainUI.cpp
    src/TrackerMainProcessor.cpp
    # src/StringTable.cpp
    src/TrackerUIComponent.cpp
src/Sequencer.cpp src/SequencerEditor.cpp src/SequencerCommands.cpp src/TrackerController.cpp
    src/SuperSamplePlayer.cpp
#    src/SuperSamplerEditor.cpp
    src/SuperSamplerProcessor.cpp
    src/ArpeggiatorMachine.cpp
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
        # JUCE_WEB_BROWSER and JUCE_USE_CURL would be on by default, but you might not need them.
        JUCE_WEB_BROWSER=0  # If you remove this, add `NEEDS_WEB_BROWSER TRUE` to the `juce_add_plugin` call
        JUCE_USE_CURL=0     # If you remove this, add `NEEDS_CURL TRUE` to the `juce_add_plugin` call
        JUCE_VST3_CAN_REPLACE_VST2=0)


        # the font for the display
# juce_add_binary_data(AudioPluginData SOURCES src/data/segment14.regular.otf)


target_link_libraries(myk-tracker-plug
    PRIVATE
#        seq-lib
        # AudioPluginData           
        juce::juce_audio_utils
        juce::juce_opengl
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
