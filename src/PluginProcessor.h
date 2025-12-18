/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include <atomic>
#include <memory>
#include <vector>
#include "MidiUtilsAbs.h"
#include "ClockAbs.h"
#include "Sequencer.h"
#include "SequencerEditor.h"
#include "TrackerController.h"

class HttpServerThread;


//==============================================================================
/**
*/
class PluginEditor;

class PluginProcessor  :    public MidiUtilsAbs, 
                            public ClockAbs, 
                            public juce::AudioProcessor,
                            public juce::ChangeBroadcaster
                            #if JucePlugin_Enable_ARA
                             , public juce::AudioProcessorARAExtension
                            #endif
{
public:
    //==============================================================================
    PluginProcessor();
    ~PluginProcessor() override;

    // the MidiUtils interface 
    void allNotesOff() override;
    void playSingleNote(unsigned short channel, unsigned short note, unsigned short velocity, unsigned short durInTicks) override; 
    void sendQueuedMessages(long tick) override; 
    // the ClockAbs interface
    void setBPM(double bpm) override; 
    double getBPM() override; 
    

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

   #ifndef JucePlugin_PreferredChannelConfigurations
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
   #endif

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    //==============================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    //==============================================================================
    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    //==============================================================================
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    //==============================================================================
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;
    /** wipes midiToSend  */
    void clearPendingEvents();

    Sequencer* getSequencer();
    SequencerEditor* getSequenceEditor();
    TrackerController* getTrackerController();
    /** serialises the sequencer/editor state for the web UI */
    juce::var getUiState();
    /** handle a command sent from the HTTP API */
    bool handleCommand(const juce::var& body, juce::String& error);
    /** read the latest serialized UI state if available (non-blocking, reader gives way to writer) */
    bool tryGetLatestSerializedUiState(juce::String& outJson);
    void setStateUpdateIntervalMs(double ms);
    
private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    bool handleKeyCommand(const juce::var& payload, juce::String& error);
    juce::var stringGridToVar(const std::vector<std::vector<std::string>>& grid);
    juce::var numberGridToVar(const std::vector<std::vector<double>>& grid);
    void syncSequenceStrings();
    void maybeUpdateUiState(double blockDurationMs);
    void applyPendingSequenceChanges();
    void addSequenceImmediate();
    void removeSequenceImmediate();
    juce::var serializeSequencerState();
    void restoreSequencerState(const juce::var& stateVar);

    Sequencer sequencer;
  /** keep the seq editor in the processor as the plugineditor
     * can be deleted but the processor persists and we want to retain state on the seqeditor
    */
    SequencerEditor seqEditor; 
    /** as for the seqeditor, this is here for easy statefulness*/
    TrackerController trackerController;
    juce::MidiBuffer midiToSend; 


    // unsigned long elapsedSamples;
    /** position between 0 and maxHorizon */
    int elapsedSamples;
    int maxHorizon;   
    unsigned int samplesPerTick; 
    double bpm; 
    int outstandingNoteOffs;

    juce::AudioProcessorValueTreeState apvts;
    std::unique_ptr<HttpServerThread> apiServer;
    // UI state push helpers
    double stateUpdateIntervalMs { 50.0 };
    double msSinceLastStateUpdate { 0.0 };
    juce::SpinLock stateLock; // writer (audio thread) prioritized; readers should try-lock
    juce::String latestStateJson;
    std::atomic<bool> stateDirty { false };

    //==============================================================================
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginProcessor)
};
