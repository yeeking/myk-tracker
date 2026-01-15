/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include <atomic>
#include <memory>
#include <mutex>
#include <thread>
#include <type_traits>
#include <vector>

#include "MachineUtilsAbs.h"
#include "ClockAbs.h"
#include "Sequencer.h"
#include "SequencerEditor.h"
#include "TrackerController.h"
#include "SuperSamplerProcessor.h"
#include "ArpeggiatorMachine.h"


//==============================================================================
/**
*/
// Main plugin processor that owns the sequencer, samplers, and timing.
class TrackerMainProcessor  :    public MachineUtilsAbs, 
                            public ClockAbs, 
                            public juce::AudioProcessor, 
                            public juce::ChangeBroadcaster,
                            public MachineHost

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
    void setInternalClockEnabled(bool enabled);
    bool isInternalClockEnabled() const;
    bool isHostClockActive() const;
    

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
    std::size_t getMachineCount(CommandType type) const override;
    MachineInterface* getMachine(CommandType type, std::size_t index) override;
    const MachineInterface* getMachine(CommandType type, std::size_t index) const override;

    template <typename Fn>
    auto withAudioThreadExclusive(Fn&& fn) -> decltype(fn())
    {
        using ReturnType = decltype(fn());
        for (;;)
        {
            while (processing.load(std::memory_order_acquire))
                std::this_thread::yield();
            std::unique_lock<std::mutex> lock(audioMutex);
            if (!processing.load(std::memory_order_acquire))
            {
                if constexpr (std::is_void_v<ReturnType>)
                {
                    fn();
                    return;
                }
                else
                {
                    return fn();
                }
            }
        }
    }
    
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
    std::vector<std::unique_ptr<MachineInterface>> samplers;
    std::vector<std::unique_ptr<ArpeggiatorMachine>> arpeggiators;
    std::mutex audioMutex;
    std::atomic<bool> processing { false };


    // unsigned long elapsedSamples;
    /** position between 0 and maxHorizon */
    int elapsedSamples;
    int maxHorizon;   
    unsigned int samplesPerTick; 
    double lastHostPpqPosition {0.0};
    bool hostPpqValid {false};
    bool hostWasPlaying {false};
    bool pendingHostBeatReset {false};
    std::atomic<bool> internalClockEnabled { true };
    std::atomic<bool> hostClockActive { false };
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
    MachineInterface* getSamplerForIndex(std::size_t samplerIndex);
    const MachineInterface* getSamplerForIndex(std::size_t samplerIndex) const;
    ArpeggiatorMachine* getArpeggiatorForIndex(std::size_t machineIndex);
    const ArpeggiatorMachine* getArpeggiatorForIndex(std::size_t machineIndex) const;
    //==============================================================================
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TrackerMainProcessor)
};
