#include "SequencerUtils.h"
#include <cmath> // fmod
#include <assert.h>

SequencerEditor::SequencerEditor(Sequencer* sequencer) : sequencer{sequencer}, currentSequence{0}, currentStep{0}, currentStepIndex{0}, editMode{SequencerEditorMode::selectingSeqAndStep}, editSubMode{SequencerEditorSubMode::editCol1}, stepIncrement{0.5f}
{

}

void SequencerEditor::setSequencer(Sequencer* sequencer)
{
    this->sequencer = sequencer;
}
Sequencer* SequencerEditor::getSequencer()
{
  return this->sequencer;
}

void SequencerEditor::resetCursor()
{

  //sequencer{sequencer}, 
  currentSequence = 0; 
  currentStep = 0;
  currentStepIndex = 0; 
  editMode = SequencerEditorMode::selectingSeqAndStep; 
  editSubMode = SequencerEditorSubMode::editCol1;
  stepIncrement = 0.5f;
}

SequencerEditorMode SequencerEditor::getEditMode() const
{
    return this->editMode;
}
SequencerEditorSubMode SequencerEditor::getEditSubMode() const
{
    return this->editSubMode;
}
void SequencerEditor::setEditMode(SequencerEditorMode mode)
{
    this->editMode = mode;
}
/** cycle through the edit modes in the sequence:
 * settingSeqLength (start mode)
 * selectingSeqAndStep
 * editingStep
*/
void SequencerEditor::cycleEditMode()
{
    switch(editMode)
    {
    case SequencerEditorMode::settingSeqLength:
        editMode = SequencerEditorMode::selectingSeqAndStep;
        return;
    case SequencerEditorMode::selectingSeqAndStep:
        editMode = SequencerEditorMode::settingSeqLength;
        currentStep = 0;
        return;
    case SequencerEditorMode::editingStep: // go to next data item
        this->editSubMode = SequencerEditor::cycleSubModeRight(this->editSubMode);
        return;  
    case SequencerEditorMode::configuringSequence: 
        this->editSubMode = SequencerEditor::cycleSubModeRight(this->editSubMode);
        return;
    }
}
/** 
 * depending on the mode, whoops bad coupling again! 
 * cycles the condition of the thing under the cursor
 * 
 */
void SequencerEditor::cycleAtCursor()
{
switch(editMode)
    {
    case SequencerEditorMode::settingSeqLength:
        // change the type of sequence somehow??
        return;
    case SequencerEditorMode::selectingSeqAndStep:
        // toggle the step on or off
        // toggle all steps in current sequence to off
        for (auto i=0;i<sequencer->getSequence(currentSequence)->getLength(); ++i)
        {
        sequencer->toggleActive(currentSequence, i);
        }
        return;
    case SequencerEditorMode::editingStep:
        //std::vector<double> data = {0, 0, 0};
        //writeStepData(data);
        sequencer->toggleActive(currentSequence, currentStep);
        return;  
    }
}
/** mode dependent reset function. Might reset */
void SequencerEditor::resetAtCursor()
{
    switch(editMode)
    {
    case SequencerEditorMode::selectingSeqAndStep:
    // reset the whole sequence
    enterNoteData(0);
    //sequencer->resetSequence(currentSequence);
    break;
    case SequencerEditorMode::editingStep:
    //enterNoteData(0);
    //sequencer->resetSequence(currentSequence);
    
    break;  
    case SequencerEditorMode::settingSeqLength:
    // 
    sequencer->resetSequence(currentSequence);
      break;
    case SequencerEditorMode::configuringSequence:

    break;
    }
}

/**
 *  Go into edit mode for either the sequence or step
 */
void SequencerEditor::enterAtCursor()
{
switch(editMode)
{
    case SequencerEditorMode::selectingSeqAndStep:
    editMode = SequencerEditorMode::editingStep;
    break;
    case SequencerEditorMode::settingSeqLength:
    editMode = SequencerEditorMode::configuringSequence;
    break;
    case SequencerEditorMode::configuringSequence:
    editMode = SequencerEditorMode::settingSeqLength;
    break;

    case SequencerEditorMode::editingStep:
    editMode = SequencerEditorMode::selectingSeqAndStep;
    break;  
}
}

/**
 * Tell the editor the user entered note data. The incoming note 
 * value is assumed to be in the range 0-127
 * 
 */
void SequencerEditor::enterNoteData(double note)
{
if (note < 0 || note > 127) return;
    if (editMode == SequencerEditorMode::editingStep ||
        editMode == SequencerEditorMode::selectingSeqAndStep)
    {     
      std::vector<double> data = sequencer->getStepData(currentSequence, currentStep);
      // set a default vel and len if needed.
      if (data[Step::velInd] == 0) data[Step::velInd] = 64;
      if (data[Step::lengthInd] == 0) data[Step::lengthInd] = 1; // two ticks
      
      data[Step::note1Ind] = note; 
      writeStepData(data);
    }
    // after note update in this mode, 
    // move to the next note
    if (editMode == SequencerEditorMode::selectingSeqAndStep)
    {
    moveCursorRight();
    }
    
    if (editMode == SequencerEditorMode::configuringSequence)
    {
    // set channel on all notes for this sequence
    int channelI = (unsigned int) note;
    channelI = channelI % 16; // 16 channels
    for (int step=0; step < sequencer->howManySteps(currentSequence); ++step)
    {
        sequencer->updateStepData(currentSequence, step, Step::channelInd, channelI);
    }
    }
}

/** moves the editor cursor right - so move to next sequence/ next data field if editing step
 */

void SequencerEditor::moveCursorLeft()
{
    switch(editMode)
    {
    case SequencerEditorMode::settingSeqLength:
    {
        currentSequence -= 1;
        if (currentSequence < 0) currentSequence = 0;
        if (currentStep >= sequencer->howManySteps(currentSequence)) currentStep = sequencer->howManySteps(currentSequence) - 1;
        break;
    }
    case SequencerEditorMode::selectingSeqAndStep:
    {
        currentSequence -= 1;
        if (currentSequence < 0) currentSequence = 0;
        if (currentStep >= sequencer->howManySteps(currentSequence)) currentStep = sequencer->howManySteps(currentSequence) - 1;
        break;
    }
    case SequencerEditorMode::editingStep:
    {
        std::vector<double> data = sequencer->getStepData(currentSequence, currentStep);
        incrementStepData(data, sequencer->getSequenceType(currentSequence));
        writeStepData(data);
        break;  
    }
    case SequencerEditorMode::configuringSequence:
    {
        // increment the value of the currently selected 
        // parameter (channel, sequence type,ticks per second)
        incrementSeqConfigParam();
        break;
    }
    }
}

void SequencerEditor::moveCursorRight()
{
    switch(editMode)
    {
    case SequencerEditorMode::settingSeqLength:
    {
        currentSequence += 1;
        if (currentSequence >= sequencer->howManySequences()) currentSequence = sequencer->howManySequences() - 1;
        if (currentStep >= sequencer->howManySteps(currentSequence)) currentStep = sequencer->howManySteps(currentSequence) - 1;
        break;
    }
    case SequencerEditorMode::selectingSeqAndStep:
    {
        currentSequence += 1;
        if (currentSequence >= sequencer->howManySequences()) currentSequence = sequencer->howManySequences() - 1;
        if (currentStep >= sequencer->howManySteps(currentSequence)) currentStep = sequencer->howManySteps(currentSequence) - 1;
        break;
    }
    case SequencerEditorMode::editingStep:
    {
        std::vector<double> data = sequencer->getStepData(currentSequence, currentStep);
        decrementStepData(data, sequencer->getSequenceType(currentSequence));
        writeStepData(data);
        break;  
    }
    case SequencerEditorMode::configuringSequence:
    {
        decrementSeqConfigParam();
        break;
    }
    }
}

void SequencerEditor::moveCursorUp()
{
    switch(editMode)
    {
    case SequencerEditorMode::settingSeqLength:
    {
        sequencer->shrinkSequence(currentSequence);
        break;
    }
    case SequencerEditorMode::selectingSeqAndStep:
    {
        currentStep -= 1;
        if (currentStep < 0) currentStep = 0;
        break;
    }
    case SequencerEditorMode::editingStep:
    {
        // cycles which data field we are editing
        //this->editSubMode = SequencerEditor::cycleSubModeLeft(this->editSubMode);
        currentStep -= 1;
        if (currentStep < 0) currentStep = 0;
        
        break;  
    }
    case SequencerEditorMode::configuringSequence:
    {
        //SequencerEditor::nextSequenceType(sequencer, currentSequence);
        break;
    }
    }
}

void SequencerEditor::moveCursorDown()
{
    switch(editMode)
    {
    case SequencerEditorMode::settingSeqLength:
    {
        sequencer->extendSequence(currentSequence);
        break;
    }
    case SequencerEditorMode::selectingSeqAndStep:
        {
        currentStep += 1;
        if (currentStep >= sequencer->howManySteps(currentSequence)) currentStep = sequencer->howManySteps(currentSequence) - 1;
        break;
        }
    case SequencerEditorMode::editingStep:
    {
        currentStep += 1;
        if (currentStep >= sequencer->howManySteps(currentSequence)) currentStep = sequencer->howManySteps(currentSequence) - 1; 
        break;  
    }
    case SequencerEditorMode::configuringSequence:
    {
    // right changes the type
      //  SequencerEditor::nextSequenceType(sequencer, currentSequence);
        break;
    }
    }
}

SequencerEditorSubMode SequencerEditor::cycleSubModeLeft(SequencerEditorSubMode subMode)
{
  switch(subMode)
  {
    case SequencerEditorSubMode::editCol1:
      return SequencerEditorSubMode::editCol3;
    case SequencerEditorSubMode::editCol2:
      return SequencerEditorSubMode::editCol1;
    case SequencerEditorSubMode::editCol3:
      return SequencerEditorSubMode::editCol2;
  }
  throw -1;
  return SequencerEditorSubMode::editCol2;

}


SequencerEditorSubMode SequencerEditor::cycleSubModeRight(SequencerEditorSubMode subMode)
{
  switch(subMode)
  {
    case SequencerEditorSubMode::editCol1:
      return SequencerEditorSubMode::editCol2;
    case SequencerEditorSubMode::editCol2:
      return SequencerEditorSubMode::editCol3;
    case SequencerEditorSubMode::editCol3:
      return SequencerEditorSubMode::editCol1;
  }
  throw -1;
  return SequencerEditorSubMode::editCol1;
}

/** decreas the sent step's data
 * based on current edit mode and edit sub mode
*/
void SequencerEditor::decrementStepData(std::vector<double>& data, SequenceType seqType)
{
  double decrement{0};
  double targetIndex{Step::note1Ind};
  double min{0};

  // figure out the increment
  switch(seqType)
  {
    case SequenceType::midiNote: // octave adjust
    {
      decrement = 12;
      break;
    }
    case SequenceType::drumMidi: 
    {
      decrement = 1;
      break;
    }
    
    case SequenceType::transposer: // up 1
    {
      decrement = 1;
      min = -24;
      break;
    }
    case SequenceType::lengthChanger: // up 1
    {
      decrement = 1;
      min = -8;
      break;
    }
    case SequenceType::tickChanger: // up 1
    {
      decrement = 1;
      break;
    }
    
  }
  // figure out the target of editing, as they are cycling 
  // through the items of data
  switch(editSubMode)
  {
    case SequencerEditorSubMode::editCol1:
    {
      targetIndex = Step::note1Ind;
      break;
    }
    case SequencerEditorSubMode::editCol2:
    {
      targetIndex = Step::lengthInd;
      decrement = 1;// 1 for length
      break;
    }
    case SequencerEditorSubMode::editCol3:
    {
      targetIndex = Step::velInd;
      decrement = 10;// 10 for vel
      break;
    }
  }
  double now = data[targetIndex];
  data[targetIndex] -= decrement;
  if (data[targetIndex] < min) data[targetIndex] = min;

}


/** increase the sent step's data
 * based on current edit mode and edit sub mode
*/
void SequencerEditor::incrementStepData(std::vector<double>& data, SequenceType seqType)
{
  double increment{0};
  double targetIndex{Step::note1Ind};
  double max{127};

  // figure out the increment
  switch(seqType)
  {
    case SequenceType::midiNote: // octave adjust
    {
      increment = 12;
      break;
    }
    case SequenceType::drumMidi: // octave adjust
    {
      increment = 1;
      break;
    }
    case SequenceType::transposer: // up 1
    {
      increment = 1;
      max = 24;
      break;
    }
    case SequenceType::lengthChanger: // up 1
    {
      increment = 1;
      max = 8;
      break;
    }
    case SequenceType::tickChanger: // up 1
    {
      increment = 1;
      break;
    }
  
  }
  // figure out the target of editing, as they are cycling 
  // through the items of data
  switch(editSubMode)
  {
    case SequencerEditorSubMode::editCol1:
    {
      targetIndex = Step::note1Ind;
      break;
    }
    case SequencerEditorSubMode::editCol2:
    {
      targetIndex = Step::lengthInd;
      increment = 1;// reset the increment if it is editing the length
      break;
    }
    case SequencerEditorSubMode::editCol3:
    {
      targetIndex = Step::velInd;
      increment = 10; // vel goes up 10 at a time
      break;
    }
  }
  double now = data[targetIndex];
  data[targetIndex] += increment;
  if (data[targetIndex] > max) data[targetIndex] = max;

}
/** increase the value of the seq param relating to the 
 * current subMode
*/
void SequencerEditor::incrementSeqConfigParam()
{
  switch (editSubMode)
  {
    case SequencerEditorSubMode::editCol1: // channel
      incrementChannel(); 
      break;
    case SequencerEditorSubMode::editCol2: // type
      SequencerEditor::nextSequenceType(sequencer, currentSequence);
      break;
    case SequencerEditorSubMode::editCol3: // ticks per step
      incrementTicksPerStep();
      break;     
  } 
}

/** decrease the value of the seq param relating to the 
 * current subMode
*/
void SequencerEditor::decrementSeqConfigParam()
{
  switch (editSubMode)
  {
    case SequencerEditorSubMode::editCol1: // channel
      decrementChannel();
      break;
    case SequencerEditorSubMode::editCol2: // type
      break;
    case SequencerEditorSubMode::editCol3: // ticks per step
      decrementTicksPerStep();
      break;     
  } 
}

void SequencerEditor::incrementChannel()
{
    std::vector<double> data2 = sequencer->getStepData(currentSequence, 0);
    int channel = data2[Step::channelInd];
    channel = (channel + 1) % 16;
    for (int step=0; step < sequencer->howManySteps(currentSequence); ++step)
    {
      sequencer->updateStepData(currentSequence, step, Step::channelInd, channel);
    }
}
void SequencerEditor::decrementChannel()
{
    // set the channel based on step 0
    std::vector<double> data2 = sequencer->getStepData(currentSequence, 0);
    unsigned int channel = data2[Step::channelInd];
    channel = (channel - 1) % 16;
    if (channel > 16) channel = 16;
    if (channel < 0) channel = 0;    
    for (int step=0; step < sequencer->howManySteps(currentSequence); ++step)
    {
      sequencer->updateStepData(currentSequence, step, Step::channelInd, channel);
    }
}

void SequencerEditor::incrementTicksPerStep()
{
  int tps = sequencer->getSequence(currentSequence)->getTicksPerStep();
  tps ++;
  if (tps > 8) tps = 1; 
  sequencer->getSequence(currentSequence)->setTicksPerStep(tps) ;
}
void SequencerEditor::decrementTicksPerStep()
{
  int tps = sequencer->getSequence(currentSequence)->getTicksPerStep();
  tps --;
  if (tps == 0) tps = 1; 
  sequencer->getSequence(currentSequence)->setTicksPerStep(tps) ;
  
}


void SequencerEditor::nextSequenceType(Sequencer* seqr, unsigned int sequence)
 {
   SequenceType type = seqr->getSequenceType(sequence);
   switch (type){
     case SequenceType::midiNote:
      seqr->setSequenceType(sequence, SequenceType::drumMidi);
      break;
     case SequenceType::drumMidi:
      seqr->setSequenceType(sequence, SequenceType::transposer);
      break;
    //  case SequenceType::chordMidi:
    //   seqr->setSequenceType(sequence, SequenceType::transposer);
    //   break;
     case SequenceType::transposer:
      seqr->setSequenceType(sequence, SequenceType::lengthChanger);
      break;
     case SequenceType::lengthChanger:
      seqr->setSequenceType(sequence, SequenceType::tickChanger);   
      break;
     case SequenceType::tickChanger:
      seqr->setSequenceType(sequence, SequenceType::midiNote);   
      break;
      
   } 
 }


int SequencerEditor::getCurrentSequence() const 
{
return currentSequence;
}

int SequencerEditor::getCurrentStep() const 
{
return currentStep;
}
/** which data point in a step are we editing */
int SequencerEditor::getCurrentStepIndex() const
{
return currentStepIndex;
}
/** move the cursor to a specific sequence*/
void SequencerEditor::setCurrentSequence(int seq)
{
currentSequence = seq;
}
/** move the cursor to a specific step*/
void SequencerEditor::setCurrentStep(int step)
{
currentStep = step;
}
/** write the sent data to the current step and sequence */
void SequencerEditor::writeStepData(std::vector<double> data)
{
sequencer->setStepData(currentSequence, currentStep, data);
}
/** write the sent data to the sequence at 'currentSequence' - 1D data version for simple one value per step -style sequences*/
void SequencerEditor::writeSequenceData(std::vector<double> data)
{
std::vector<double> stepData = {0};
for (int i=0; i<sequencer->howManySteps(currentSequence); ++i)
{
    stepData[0] = data[i % data.size()]; // wrap it around :) 
    sequencer->setStepData(currentSequence, currentSequence, stepData);
}
}
/** write the sent data to a sequence - 1D data version */
void SequencerEditor::writeSequenceData(std::vector<std::vector<double>> data)
{
for (int i=0; i<sequencer->howManySteps(currentSequence); ++i)
{
    sequencer->setStepData(currentSequence, currentSequence, data[i % data.size()]); // wrap around
}
}

