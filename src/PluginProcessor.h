/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "MidiUtilsAbs.h"
#include "ClockAbs.h"
#include "Sequencer.h"
#include "SequencerEditor.h"


//==============================================================================
/**
*/
class PluginProcessor  :    public MidiUtilsAbs, 
                            public ClockAbs, 
                            public juce::AudioProcessor
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
    void playSingleNote(unsigned short channel, unsigned short note, unsigned short velocity, long offTick) override; 
    void sendQueuedMessages(long tick) override; 
    // the ClockAbs interface
    void setBPM(unsigned int bpm) override; 

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


    Sequencer* getSequencer();
    SequencerEditor* getSequenceEditor();
    
    
private:
    unsigned long elapsedSamples;
    Sequencer sequencer;
    SequencerEditor seqEditor; 
    // ClockAbs ticker; 

    unsigned int samplesPerTick; 
    juce::MidiBuffer midiToSend; 

    //==============================================================================
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginProcessor)
};
