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
    : AudioProcessorEditor (&p), 
    audioProcessor (p), 
    sequencer{p.getSequencer()},  
    seqEditor{p.getSequenceEditor()}, 
    trackerController{p.getTrackerController()}, 
    rowsInUI{9}, waitingForPaint{false}, updateSeqStrOnNextDraw{false}
{
    // openGLContext.attachTo (*getTopLevelComponent());
    // Make sure that before the constructor has finished, you've set the
    // editor's size to whatever you need it to be.
    setSize (1024, 768);
    addAndMakeVisible(seqViewTable);
    addAndMakeVisible(controlPanelTable);
    
    // Add this editor as a key listener
    addKeyListener(this);
    setWantsKeyboardFocus(true);

    startTimer(1000 / 25);

}

PluginEditor::~PluginEditor()
{
  stopTimer();
}

//==============================================================================
void PluginEditor::paint (juce::Graphics& g)
{
    waitingForPaint = false; 
}

void PluginEditor::resized()
{
    // This is generally where you'll want topip install tf-keras lay out the positions of any
    // subcomponents in your editor..
    int cPanelHeight = getHeight() / static_cast<int>(rowsInUI);
    controlPanelTable.setBounds(0, 0, getWidth(), cPanelHeight);
    seqViewTable.setBounds(0, 0 + cPanelHeight, getWidth(), getHeight() - cPanelHeight);
   
}


void PluginEditor::timerCallback ()
{
    if (waitingForPaint) {return;}// already waiting for a repaint
  prepareControlPanelView();  
  // check what to draw based on the state of the 
  // editor
  switch(seqEditor->getEditMode()){

      case SequencerEditorMode::selectingSeqAndStep:
      {
          prepareSequenceView();
          break;
      }
      case SequencerEditorMode::editingStep:
      {
          prepareStepView();
          break; 
      }
      case SequencerEditorMode::configuringSequence:
      {
          prepareSeqConfigView();
          break;
      }
  }
  waitingForPaint = true; 
  if (updateSeqStrOnNextDraw){
    sequencer->updateSeqStringGrid();
    updateSeqStrOnNextDraw = false; 
  }
  repaint();
}



void PluginEditor::prepareSequenceView()
{
  // sequencer->tick();
  std::vector<std::pair<int, int>> playHeads;
  for (size_t col=0;col<audioProcessor.getSequencer()->howManySequences(); ++col){  
    std::pair<int, int> colRow = {col, audioProcessor.getSequencer()->getCurrentStep(col)};
    playHeads.push_back(std::move(colRow));
  }
  seqViewTable.updateData(audioProcessor.getSequencer()->getSequenceAsGridOfStrings(), 
  rowsInUI-1, 6,
  seqEditor->getCurrentSequence(), seqEditor->getCurrentStep(), playHeads, true, seqEditor->getArmedSequence());
}
void PluginEditor::prepareStepView()
{
    // Step* step = sequencer->getStep(seqEditor->getCurrentSequence(), seqEditor->getCurrentStep());
    // std::vector<std::vector<std::string>> grid = step->toStringGrid();
    std::vector<std::pair<int, int>> playHeads;
    if (sequencer->getCurrentStep(seqEditor->getCurrentSequence()) == seqEditor->getCurrentStep()){
        int cols = static_cast<int>(sequencer->howManyStepDataCols(seqEditor->getCurrentSequence(), seqEditor->getCurrentStep()));
        for (int col=0;col<cols;++col){
            playHeads.push_back(std::pair(col, 0));
        }
    }
    std::vector<std::vector<std::string>> grid = sequencer->getStepAsGridOfStrings(seqEditor->getCurrentSequence(), seqEditor->getCurrentStep());
    seqViewTable.updateData(grid, 
        rowsInUI-1, 6, 
        seqEditor->getCurrentStepCol(), 
        seqEditor->getCurrentStepRow(), 
        playHeads);
}
void PluginEditor::prepareSeqConfigView()
{
    std::vector<std::vector<std::string>> grid = sequencer->getSequenceConfigsAsGridOfStrings();
    seqViewTable.updateData(grid, 
        rowsInUI-1, 6, 
        seqEditor->getCurrentSequence(), 
        seqEditor->getCurrentSeqParam(), 
        std::vector<std::pair<int, int>>());   
}

void PluginEditor::prepareControlPanelView()
{
    std::vector<std::vector<std::string>> grid = trackerController->getControlPanelAsGridOfStrings();
    controlPanelTable.updateData(grid, 
        1, 12, 
        0, 0, // todo - pull these from the editor which keeps track of this 
        std::vector<std::pair<int, int>>(), false); 
}

bool PluginEditor::keyPressed(const juce::KeyPress& key, juce::Component* originatingComponent)
{

    switch (key.getTextCharacter())
    {
        case 'A':// arm a sequence for live MIDI input 
            seqEditor->setArmedSequence(seqEditor->getCurrentSequence());
            break; 
        case 'R':// re-e-wind
            CommandProcessor::sendAllNotesOff();
            audioProcessor.getSequencer()->rewindAtNextZero();
            break;
        case ' ':
            CommandProcessor::sendAllNotesOff();
            if (audioProcessor.getSequencer()->isPlaying()){
                audioProcessor.getSequencer()->stop();
            }
            else{
                audioProcessor.getSequencer()->rewindAtNextZero();
                audioProcessor.getSequencer()->play();

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
            trackerController->decrementBPM();
            break;

        case '+':
            trackerController->incrementBPM();
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
            char ch = static_cast<char>(key.getTextCharacter()); // sketch as converting wchar unicode to char... 
            std::map<char, double> key_to_note = MidiUtilsAbs::getKeyboardToMidiNotes(0);
            for (const std::pair<const char, double>& key_note : key_to_note)
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
            // Handle arrow and other non character keys
            if (key.isKeyCode(juce::KeyPress::backspaceKey)) {
                seqEditor->resetAtCursor();
                CommandProcessor::sendAllNotesOff();
                break;
            }
            if (key.isKeyCode(juce::KeyPress::returnKey)) {
               seqEditor->enterAtCursor();
                break;
            }
            if (key.isKeyCode(juce::KeyPress::upKey)) {
               seqEditor->moveCursorUp();
                break;
            }
            if (key.isKeyCode(juce::KeyPress::downKey)) {

               seqEditor->moveCursorDown();
                break;
            }
            if (key.isKeyCode(juce::KeyPress::leftKey)) {
               seqEditor->moveCursorLeft();
                break;
            }
            if (key.isKeyCode(juce::KeyPress::rightKey)) {
               seqEditor->moveCursorRight();
                break;
            }

    }
    audioProcessor.getSequencer()->updateSeqStringGrid();
    return true; // Key was handled
}

bool PluginEditor::keyStateChanged(bool isKeyDown, juce::Component* originatingComponent)
{
  return false; 
}

