/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin editor.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "RaggedTableComponent.h"
#include "Sequencer.h"
#include "SequencerEditor.h"
//==============================================================================
/**
*/
class PluginEditor  : public juce::AudioProcessorEditor, 
                      public juce::Timer,
                      public juce::KeyListener 

{
public:
    PluginEditor (PluginProcessor&);
    ~PluginEditor() override;

    //==============================================================================
    void paint (juce::Graphics&) override;
    void resized() override;

    void timerCallback () override; 

       // KeyListener overrides
    bool keyPressed(const juce::KeyPress& key, juce::Component* originatingComponent) override;
    bool keyStateChanged(bool isKeyDown, juce::Component* originatingComponent) override;


private:

    RaggedTableComponent rTable;
    Sequencer* sequencer;
    SequencerEditor editor;
  

    // This reference is provided as a quick way for your editor to
    // access the processor object that created it.
    PluginProcessor& audioProcessor;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginEditor)
};
