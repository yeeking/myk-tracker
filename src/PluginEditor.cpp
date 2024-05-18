/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin editor.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "SequencerCommands.h"
#include "SimpleClock.h"

//==============================================================================
PluginEditor::PluginEditor (PluginProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p), sequencer{p.getSequencer()}, editor{p.getSequencer()}
{
    // Make sure that before the constructor has finished, you've set the
    // editor's size to whatever you need it to be.
    setSize (400, 300);
    addAndMakeVisible(rTable);
    startTimer(1000 / 5);
    // SimpleClock clock;

    // CommandProcessor::assignMasterClock(&clock);
    // sequencer.decrementSeqParam(0, 1);
    // sequencer.decrementSeqParam(0, 1);
    
}

PluginEditor::~PluginEditor()
{
  stopTimer();
}

//==============================================================================
void PluginEditor::paint (juce::Graphics& g)
{
    // (Our component is opaque, so we must completely fill the background with a solid colour)
    g.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId));

    g.setColour (juce::Colours::white);
    g.setFont (15.0f);
    g.drawFittedText ("Hello World!", getLocalBounds(), juce::Justification::centred, 1);
}

void PluginEditor::resized()
{
    // This is generally where you'll want to lay out the positions of any
    // subcomponents in your editor..
    rTable.setBounds(0, 0, getWidth(), getHeight());
}


void PluginEditor::timerCallback ()
{
  // sequencer->tick();
  std::vector<std::pair<int, int>> playHeads;
  for (int col=0;col<sequencer->howManySequences(); ++col){  
    std::pair<int, int> colRow = {col, sequencer->getCurrentStep(col)};
    playHeads.push_back(std::move(colRow));
  }
  rTable.draw(sequencer->getSequenceAsGridOfStrings(), 4, 4, editor.getCurrentSequence(), editor.getCurrentStep(), playHeads);
  repaint();
}
