/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include <atomic>
#include <atomic>
#include <memory>
#include <vector>

#include "MachineUtilsAbs.h"
#include "ClockAbs.h"
#include "Sequencer.h"
#include "SequencerEditor.h"
#include "TrackerController.h"
#include "SuperSamplerProcessor.h"


//==============================================================================
/**
*/
// Main plugin processor that owns the sequencer, samplers, and timing.
class TrackerMainProcessor  :    public MachineUtilsAbs, 
                            public ClockAbs, 
                            public juce::AudioProcessor, 
                            public juce::ChangeBroadcaster,
                            public SamplerHost

                            #if JucePlugin_Enable_ARA
                             , public juce::AudioProcessorARAExtension
                            #endif
{
public:
    //==============================================================================
    TrackerMainProcessor();
    ~TrackerMainProcessor() override;

    // the MachineUtils interface 
    void allNotesOff() override;
    void sendMessageToMachine(CommandType machineType, unsigned short machineId, unsigned short note, unsigned short velocity, unsigned short durInTicks) override; 
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

    using juce::AudioProcessor::processBlock;
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
    std::size_t getSamplerCount() const override;
    juce::var getSamplerState(std::size_t samplerIndex) const override;
    void samplerAddPlayer(std::size_t samplerIndex) override;
    void samplerRemovePlayer(std::size_t samplerIndex, int playerId) override;
    void samplerRequestLoad(std::size_t samplerIndex, int playerId) override;
    void samplerTrigger(std::size_t samplerIndex, int playerId) override;
    void samplerSetRange(std::size_t samplerIndex, int playerId, int low, int high) override;
    void samplerSetGain(std::size_t samplerIndex, int playerId, float gain) override;
    
private:
    Sequencer sequencer;
  /** keep the seq editor in the processor as the plugineditor
     * can be deleted but the processor persists and we want to retain state on the seqeditor
    */
    SequencerEditor seqEditor; 
    /** as for the seqeditor, this is here for easy statefulness*/
    TrackerController trackerController;
    juce::MidiBuffer midiToSend; 
    juce::MidiBuffer midiToSendToSampler;
    std::vector<std::unique_ptr<SuperSamplerProcessor>> samplers;


    // unsigned long elapsedSamples;
    /** position between 0 and maxHorizon */
    int elapsedSamples;
    int maxHorizon;   
    unsigned int samplesPerTick; 
    std::atomic<double> bpm; 
    int outstandingNoteOffs;
    /** configure plugin params */
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    /** stores the plugin state */
    juce::AudioProcessorValueTreeState apvts;
  
    juce::var stringGridToVar(const std::vector<std::vector<std::string>>& grid);
    juce::var numberGridToVar(const std::vector<std::vector<double>>& grid);
  
    /** convert ui state into a var  */
    juce::var getUiState();
    /** convert state into 'storable' var */
    juce::var serializeSequencerState();
    /** retrieve state from var  */
    void restoreSequencerState(const juce::var& stateVar);
    SuperSamplerProcessor* getSamplerForIndex(std::size_t samplerIndex);
    const SuperSamplerProcessor* getSamplerForIndex(std::size_t samplerIndex) const;
    //==============================================================================
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TrackerMainProcessor)
};
