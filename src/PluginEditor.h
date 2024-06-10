/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin editor.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "StringTable.h"
#include "Sequencer.h"
#include "SequencerEditor.h"
#include "TrackerController.h"
//==============================================================================
/**
*/
class PluginEditor  : public juce::AudioProcessorEditor, 
                      // public juce::OpenGLAppComponent,
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
    using juce::Component::keyPressed;
    bool keyPressed(const juce::KeyPress& key, juce::Component* originatingComponent) override;
    using juce::Component::keyStateChanged;
    bool keyStateChanged(bool isKeyDown, juce::Component* originatingComponent) override;


private:

    PluginProcessor& audioProcessor;

    StringTable controlPanelTable;
    StringTable seqViewTable;
    Sequencer* sequencer; 
    SequencerEditor* seqEditor;
    TrackerController* trackerController; 

    size_t rowsInUI;
    
    void prepareControlPanelView();
    void prepareSequenceView();
    void prepareStepView();
    void prepareSeqConfigView();
    bool waitingForPaint;
    // This reference is provided as a quick way for your editor to
    // access the processor object that created it.

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginEditor)
};
