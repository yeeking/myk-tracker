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

    // Add this editor as a key listener
    addKeyListener(this);
    setWantsKeyboardFocus(true);

    // SimpleClock clock;

    // CommandProcessor::assignMasterClock(&clock);
    // sequencer.decrementSeqParam(0, 1);
    // sequencer.decrementSeqParam(0, 1);

        
    editor.enterStepData(12, Step::noteInd);    
    // editor.moveCursorDown();
    // editor.enterStepData(13, Step::noteInd);
    editor.gotoSequenceConfigPage();
    editor.incrementSeqConfigParam();
    
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


bool PluginEditor::keyPressed(const juce::KeyPress& key, juce::Component* originatingComponent)
{
    switch (key.getTextCharacter())
    {
        case ' ':
            editor.toggleTrigger();
            if (!editor.isTriggerActive()) {
                // After a 'stop', reset all sequences to zero for next trigger
                // sequencer.nextStepFromZero();
            }
            break;

        case '\t':
            editor.nextStep();
            break;

        case '-':
            editor.removeRow();
            break;

        case '=':
            editor.addRow();
            break;

        case '_':
            // Delete the current sequence
            break;

        case '+':
            // Add a new sequence
            break;

        case '[':
            editor.decrementAtCursor();
            break;

        case ']':
            editor.incrementAtCursor();
            break;

        case ',':
            editor.decrementOctave();
            break;

        case '.':
            editor.incrementOctave();
            break;

        case 'M':
            audioProcessor.getSequencer()->toggleSequenceMute(editor.getCurrentSequence());
            // sequencer.toggleSequenceMute(editor.getCurrentSequence());
            break;

        // case juce::KeyPress::deleteKey:
        //     editor.resetAtCursor();
        //     CommandProcessor::sendAllNotesOff();
        //     break;

        case '\n':
            editor.enterAtCursor();
            break;

        case 'S':
            editor.gotoSequenceConfigPage();
            break;

        // case juce::KeyPress::upKey:
        //     editor.moveCursorUp();
        //     return true;  // Redraw might be handled elsewhere in JUCE

        // case juce::KeyPress::downKey:
        //     editor.moveCursorDown();
        //     return true;  // Redraw might be handled elsewhere in JUCE

        // case juce::KeyPress::leftKey:
        //     editor.moveCursorLeft();
        //     return true;  // Redraw might be handled elsewhere in JUCE

        // case juce::KeyPress::rightKey:
        //     editor.moveCursorRight();
        //     return true;  // Redraw might be handled elsewhere in JUCE

        case 'p':
            // if (editor.getEditMode() == SequencerEditor::EditingStep) {
            //     // sequencer.triggerStep(editor.getCurrentSequence(), editor.getCurrentStep(), editor.getCurrentStepRow());
            // }
            break;

        default:
            // Handle arrow keys
            if (key.isKeyCode(juce::KeyPress::upKey)) {
                editor.moveCursorUp();
                return true;
            }
            if (key.isKeyCode(juce::KeyPress::downKey)) {
                editor.moveCursorDown();
                return true;
            }
            if (key.isKeyCode(juce::KeyPress::leftKey)) {
                editor.moveCursorLeft();
                return true;
            }
            if (key.isKeyCode(juce::KeyPress::rightKey)) {
                editor.moveCursorRight();
                return true;
            }

            return false; // Key was not handled
    }

    return true; // Key was handled
}

bool PluginEditor::keyStateChanged(bool isKeyDown, juce::Component* originatingComponent)
{
  return false; 
}
