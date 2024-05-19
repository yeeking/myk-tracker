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
    : AudioProcessorEditor (&p), audioProcessor (p), sequencer{p.getSequencer()},  seqEditor{p.getSequenceEditor()}
{
    // Make sure that before the constructor has finished, you've set the
    // editor's size to whatever you need it to be.
    setSize (1024, 768);
    addAndMakeVisible(rTable);
    startTimer(1000 / 25);

    // Add this editor as a key listener
    addKeyListener(this);
    setWantsKeyboardFocus(true);

    // SimpleClock clock;

    // CommandProcessor::assignMasterClock(&clock);
    // sequencer.decrementSeqParam(0, 1);
    // sequencer.decrementSeqParam(0, 1);

        
   seqEditor->enterStepData(12, Step::noteInd);    
    // editor.moveCursorDown();
    // editor.enterStepData(13, Step::noteInd);
   seqEditor->gotoSequenceConfigPage();
   seqEditor->incrementSeqConfigParam();
   seqEditor->setEditMode(SequencerEditorMode::selectingSeqAndStep);    
}

PluginEditor::~PluginEditor()
{
  stopTimer();
}

//==============================================================================
void PluginEditor::paint (juce::Graphics& g)
{
    // // (Our component is opaque, so we must completely fill the background with a solid colour)
    // g.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId));

    // g.setColour (juce::Colours::white);
    // g.setFont (15.0f);
    // g.drawFittedText ("Hello World!", getLocalBounds(), juce::Justification::centred, 1);
}

void PluginEditor::resized()
{
    // This is generally where you'll want to lay out the positions of any
    // subcomponents in your editor..
    rTable.setBounds(0, 0, getWidth(), getHeight());
}


void PluginEditor::timerCallback ()
{

  // check what to draw based on the state of the 
  // editor
  switch(seqEditor->getEditMode()){

      case SequencerEditorMode::selectingSeqAndStep:
      {
          drawSequenceView();
          break;
      }
      case SequencerEditorMode::editingStep:
      {
          drawStepView();
          break; 
      }
      case SequencerEditorMode::configuringSequence:
      {
          drawSeqConfigView();
          break;
      }
  }

  repaint();

}


bool PluginEditor::keyPressed(const juce::KeyPress& key, juce::Component* originatingComponent)
{

    switch (key.getTextCharacter())
    {
        case ' ':
           seqEditor->toggleTrigger();
            if (!seqEditor->isTriggerActive()) {
                // After a 'stop', reset all sequences to zero for next trigger
                // sequencer.nextStepFromZero();
            }
            break;
        case '\t':
           seqEditor->nextStep();
            break;

        case '-':
           seqEditor->removeRow();
            break;

        case '=':
           seqEditor->addRow();
            break;

        case '_':
            // Delete the current sequence
            break;

        case '+':
            // Add a new sequence
            break;

        case '[':
           seqEditor->decrementAtCursor();
            break;

        case ']':
           seqEditor->incrementAtCursor();
            break;

        case ',':
           seqEditor->decrementOctave();
            break;

        case '.':
           seqEditor->incrementOctave();
            break;

        case 'M':
            audioProcessor.getSequencer()->toggleSequenceMute(seqEditor->getCurrentSequence());
            // sequencer.toggleSequenceMute(seqEditor->getCurrentSequence());
            break;

        // case juce::KeyPress::deleteKey:
        //    seqEditor->resetAtCursor();
        //     CommandProcessor::sendAllNotesOff();
        //     break;

        case '\n':
           seqEditor->enterAtCursor();
            break;

        case 'S':
           seqEditor->gotoSequenceConfigPage();
            break;


        case 'p':
            // if (seqEditor->getEditMode() == SequencerEditor::EditingStep) {
            //     // sequencer.triggerStep(seqEditor->getCurrentSequence(),seqEditor->getCurrentStep(),seqEditor->getCurrentStepRow());
            // }
            break;

        default:
            char ch = key.getTextCharacter();
            std::map<char, double> key_to_note = MidiUtilsAbs::getKeyboardToMidiNotes(0);
            for (const std::pair<char, double>& key_note : key_to_note)
            {
              if (ch == key_note.first){ 
                // key_note_match = true;
                seqEditor->enterStepData(key_note.second, Step::noteInd);
                break;// break the for loop
              }
            }
            // do the velocity controls
            for (int num=1;num<5;++num){
              if (ch == num + 48){
                seqEditor->enterStepData(num * (128/4), Step::velInd);
                break; 
              }
            }
            // for (int i=0;i<lenKeys.size();++i){
            //   if (lenKeys[i] == ch){
            //     editor.enterStepData(i+1, Step::lengthInd);
            //     break;
            //   }
            // }
            // Handle arrow keys
            if (key.isKeyCode(juce::KeyPress::returnKey)) {
               seqEditor->enterAtCursor();
                // return true;
            }
            if (key.isKeyCode(juce::KeyPress::upKey)) {
               seqEditor->moveCursorUp();
                // return true;
            }
            if (key.isKeyCode(juce::KeyPress::downKey)) {

               seqEditor->moveCursorDown();
                // return true;
            }
            if (key.isKeyCode(juce::KeyPress::leftKey)) {
               seqEditor->moveCursorLeft();
                // return true;
            }
            if (key.isKeyCode(juce::KeyPress::rightKey)) {
               seqEditor->moveCursorRight();
                // return true;
            }

            // return false; // Key was not handled
    }
    audioProcessor.getSequencer()->updateSeqStringGrid();
    return true; // Key was handled
}

bool PluginEditor::keyStateChanged(bool isKeyDown, juce::Component* originatingComponent)
{
  return false; 
}


void PluginEditor::drawSequenceView()
{
  // sequencer->tick();
  std::vector<std::pair<int, int>> playHeads;
  for (int col=0;col<audioProcessor.getSequencer()->howManySequences(); ++col){  
    std::pair<int, int> colRow = {col, audioProcessor.getSequencer()->getCurrentStep(col)};
    playHeads.push_back(std::move(colRow));
  }
  rTable.draw(audioProcessor.getSequencer()->getSequenceAsGridOfStrings(), 8, 6,seqEditor->getCurrentSequence(), seqEditor->getCurrentStep(), playHeads);
}
void PluginEditor::drawStepView()
{
    // Step* step = sequencer->getStep(seqEditor->getCurrentSequence(), seqEditor->getCurrentStep());
    // std::vector<std::vector<std::string>> grid = step->toStringGrid();
    std::vector<std::pair<int, int>> playHeads;
    if (sequencer->getCurrentStep(seqEditor->getCurrentSequence()) == seqEditor->getCurrentStep()){
        int cols = sequencer->howManyStepDataCols(seqEditor->getCurrentSequence(), seqEditor->getCurrentStep());
        for (int col=0;col<cols;++col){
            playHeads.push_back(std::pair(col, 0));
        }
    }
    std::vector<std::vector<std::string>> grid = sequencer->getStepAsGridOfStrings(seqEditor->getCurrentSequence(), seqEditor->getCurrentStep());
    rTable.draw(grid, 
        8, 6, 
        seqEditor->getCurrentStepCol(), 
        seqEditor->getCurrentStepRow(), 
        playHeads);
}
void PluginEditor::drawSeqConfigView()
{
    std::vector<std::vector<std::string>> grid = sequencer->getSequenceConfigsAsGridOfStrings();
    rTable.draw(grid, 
        8, 6, 
        seqEditor->getCurrentSequence(), 
        seqEditor->getCurrentSeqParam(), 
        std::vector<std::pair<int, int>>());   
}

