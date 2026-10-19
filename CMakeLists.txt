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
    COPY_PLUGIN_AFTER_BUILD FALSE     # Keep local/CI builds out of user plugin directories.
    PLUGIN_MANUFACTURER_CODE Yeek               # A four-character manufacturer id with at least one upper-case ``
    PLUGIN_CODE Trak                            # A unique four-character plugin id with exactly one upper-case character
                                                # GarageBand 10.3 requires the first letter to be upper-case, and the remaining letters to be lower-case
    FORMATS AU VST3 Standalone                  # The formats to build. Other valid formats are: AAX Unity VST AU AUv3
    PRODUCT_NAME "MYK Tracker GL")        # The name of the final executable, which can differ from the target name


juce_generate_juce_header(myk-tracker-plug)

juce_add_gui_app(tracker-hardware-emulator
    COMPANY_NAME Yee-King
    PRODUCT_NAME "MYK Tracker Hardware Emulator")

juce_generate_juce_header(tracker-hardware-emulator)

target_sources(tracker-hardware-emulator
    PRIVATE
    src/hardware_emulator/Main.cpp
    src/hardware_emulator/MainComponent.cpp
    src/hardware_emulator/MainComponent.h
)

target_compile_definitions(tracker-hardware-emulator
    PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:tracker-hardware-emulator,JUCE_PRODUCT_NAME>"
        JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:tracker-hardware-emulator,JUCE_VERSION>")

target_link_libraries(tracker-hardware-emulator
    PRIVATE
        juce::juce_gui_extra
        juce::juce_osc
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

target_sources(myk-tracker-plug
    PRIVATE
    src/TrackerMainUI.cpp
    src/PortableTrackerState.cpp
    src/TrackerFileBrowser.cpp
    src/TrackerMainProcessor.cpp
    src/TrackerControlService.cpp
    src/McpRouter.cpp
    src/HTTPServer.cpp
    src/standalone/TrackerStandaloneHost.cpp
    # src/StringTable.cpp
    src/TrackerUIComponent.cpp
src/Sequencer.cpp src/SequencerEditor.cpp src/SequencerCommands.cpp src/TrackerController.cpp
    src/SuperSamplePlayer.cpp
#    src/SuperSamplerEditor.cpp
    src/SuperSamplerProcessor.cpp
    src/machines/WavetableSynthMachine.cpp
    src/machines/DrumSynthMachine.cpp
    src/machines/WaveshaperDistortionMachine.cpp
    src/machines/DelayFxMachine.cpp
    src/machines/ChannelStripMachine.cpp
    src/machines/AuxReverbMachine.cpp
    src/machines/FilterFxMachine.cpp
    src/machines/ModulationMatrixMachine.cpp
)


target_include_directories(myk-tracker-plug
    PRIVATE
        src
        src/machines)

    
target_compile_definitions(myk-tracker-plug
    PUBLIC # 
        JUCE_ALSA=1
        JUCE_DIRECTSOUND=1
        JUCE_DISABLE_CAUTIOUS_PARAMETER_ID_CHECKING=1
        #JUCE_PLUGINHOST_LADSPA=1
        #JUCE_PLUGINHOST_LV2=1
        #JUCE_PLUGINHOST_VST3=1
        JUCE_USE_OGGVORBIS=1
        JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP=1
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
        juce::juce_dsp
        juce::juce_osc
        juce::juce_opengl
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

option(MYK_TRACKER_BUILD_PERFORMANCE_BENCHMARK "Build the standalone dynamic-performance benchmark" OFF)
if (MYK_TRACKER_BUILD_PERFORMANCE_BENCHMARK)
    target_compile_definitions(myk-tracker-plug PRIVATE MYK_TRACKER_PERFORMANCE_INSTRUMENTATION=1)
    add_executable(myk-tracker-performance-benchmark tools/PerformanceBenchmark.cpp)
    target_include_directories(myk-tracker-performance-benchmark PRIVATE
        src src/machines
        "${CMAKE_CURRENT_SOURCE_DIR}/libs/JUCE/modules"
        "${CMAKE_CURRENT_BINARY_DIR}/myk-tracker-plug_artefacts/JuceLibraryCode")
    target_compile_features(myk-tracker-performance-benchmark PRIVATE cxx_std_17)
    target_compile_definitions(myk-tracker-performance-benchmark PRIVATE MYK_TRACKER_PERFORMANCE_INSTRUMENTATION=1)
    target_link_libraries(myk-tracker-performance-benchmark PRIVATE
        myk-tracker-plug
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
endif()

include(CTest)
if (BUILD_TESTING)
    juce_add_console_app(sequence-read-head-tests PRODUCT_NAME "Sequence Read Head Tests")
    juce_generate_juce_header(sequence-read-head-tests)
    target_sources(sequence-read-head-tests PRIVATE
        tests/SequenceReadHeadTests.cpp
        src/TrackerFileBrowser.cpp
        src/Sequencer.cpp
        src/SequencerCommands.cpp
        src/SequencerEditor.cpp)
    target_include_directories(sequence-read-head-tests PRIVATE src)
    target_compile_features(sequence-read-head-tests PRIVATE cxx_std_17)
    target_compile_definitions(sequence-read-head-tests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(sequence-read-head-tests PRIVATE
        juce::juce_audio_utils
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
    add_test(NAME sequence-read-head-tests COMMAND sequence-read-head-tests)

    juce_add_console_app(sampler-tuning-tests PRODUCT_NAME "Sampler Tuning Tests")
    juce_generate_juce_header(sampler-tuning-tests)
    target_sources(sampler-tuning-tests PRIVATE
        tests/SamplerTuningTests.cpp
        src/PortableTrackerState.cpp
        src/TrackerFileBrowser.cpp
        src/SuperSamplePlayer.cpp
        src/SuperSamplerProcessor.cpp)
    target_include_directories(sampler-tuning-tests PRIVATE src)
    target_compile_features(sampler-tuning-tests PRIVATE cxx_std_17)
    target_compile_definitions(sampler-tuning-tests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JucePlugin_Name="SamplerTuningTests")
    target_link_libraries(sampler-tuning-tests PRIVATE
        juce::juce_audio_utils
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
    add_test(NAME sampler-tuning-tests COMMAND sampler-tuning-tests)

    juce_add_console_app(modulation-matrix-tests PRODUCT_NAME "Modulation Matrix Tests")
    juce_generate_juce_header(modulation-matrix-tests)
    target_sources(modulation-matrix-tests PRIVATE
        tests/ModulationMatrixTests.cpp
        src/machines/ModulationMatrixMachine.cpp
        src/machines/DelayFxMachine.cpp)
    target_include_directories(modulation-matrix-tests PRIVATE src)
    target_compile_features(modulation-matrix-tests PRIVATE cxx_std_17)
    target_compile_definitions(modulation-matrix-tests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(modulation-matrix-tests PRIVATE
        juce::juce_audio_utils
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
    add_test(NAME modulation-matrix-tests COMMAND modulation-matrix-tests)

    juce_add_console_app(wavetable-synth-tests PRODUCT_NAME "Wavetable Synth Tests")
    juce_generate_juce_header(wavetable-synth-tests)
    target_sources(wavetable-synth-tests PRIVATE
        tests/WavetableSynthTests.cpp
        src/machines/WavetableSynthMachine.cpp
        src/machines/ModulationMatrixMachine.cpp)
    target_include_directories(wavetable-synth-tests PRIVATE src)
    target_compile_features(wavetable-synth-tests PRIVATE cxx_std_17)
    target_compile_definitions(wavetable-synth-tests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(wavetable-synth-tests PRIVATE
        juce::juce_audio_utils
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
    add_test(NAME wavetable-synth-tests COMMAND wavetable-synth-tests)

    juce_add_console_app(drum-synth-tests PRODUCT_NAME "Drum Synth Tests")
    juce_generate_juce_header(drum-synth-tests)
    target_sources(drum-synth-tests PRIVATE
        tests/DrumSynthTests.cpp
        src/machines/DrumSynthMachine.cpp)
    target_include_directories(drum-synth-tests PRIVATE src src/machines)
    target_compile_features(drum-synth-tests PRIVATE cxx_std_17)
    target_compile_definitions(drum-synth-tests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(drum-synth-tests PRIVATE
        juce::juce_audio_utils
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
    add_test(NAME drum-synth-tests COMMAND drum-synth-tests)

    add_executable(drum-processor-tests)
    target_sources(drum-processor-tests PRIVATE tests/DrumProcessorTests.cpp)
    target_include_directories(drum-processor-tests PRIVATE
        src src/machines
        "${CMAKE_CURRENT_SOURCE_DIR}/libs/JUCE/modules"
        "${CMAKE_CURRENT_BINARY_DIR}/myk-tracker-plug_artefacts/JuceLibraryCode")
    target_compile_features(drum-processor-tests PRIVATE cxx_std_17)
    target_compile_definitions(drum-processor-tests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(drum-processor-tests PRIVATE
        myk-tracker-plug
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
    add_test(NAME drum-processor-tests COMMAND drum-processor-tests)
endif()
