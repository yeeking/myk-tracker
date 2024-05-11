#include "SequencerEditor.h"
#include <cmath> // fmod
#include <assert.h>

SequencerEditor::SequencerEditor(Sequencer *sequencer) : sequencer{sequencer}, currentSequence{0}, currentStep{0}, currentStepRow{0}, currentStepCol{0}, currentSeqParam{0}, editMode{SequencerEditorMode::selectingSeqAndStep}, editSubMode{SequencerEditorSubMode::editCol1}, stepIncrement{0.5f}, octave{6}, triggerIsActive{true}
{
}

void SequencerEditor::setSequencer(Sequencer *sequencer)
{
  this->sequencer = sequencer;
}
Sequencer *SequencerEditor::getSequencer()
{
  return this->sequencer;
}

void SequencerEditor::resetCursor()
{
  // sequencer{sequencer},
  currentSequence = 0;
  currentStep = 0;
  currentStepRow = 0;
  currentStepCol = 0;
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
  switch (editMode)
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
  switch (editMode)
  {
  case SequencerEditorMode::settingSeqLength:
    // change the type of sequence somehow??
    return;
  case SequencerEditorMode::selectingSeqAndStep:
    // toggle the step on or off
    // toggle all steps in current sequence to off
    for (auto i = 0; i < sequencer->getSequence(currentSequence)->getLength(); ++i)
    {
      sequencer->toggleActive(currentSequence, i);
    }
    return;
  case SequencerEditorMode::editingStep:
    // std::vector<std::vector<double>> data = {0, 0, 0};
    // writeStepData(data);
    sequencer->toggleActive(currentSequence, currentStep);
    return;
  }
}
/** mode dependent reset function. Might reset */
void SequencerEditor::resetAtCursor()
{
  switch (editMode)
  {
  case SequencerEditorMode::selectingSeqAndStep:
    // delete a step
    for (int row =0;row<sequencer->howManyStepDataRows(currentSequence, currentStep); ++row){
      sequencer->resetStepRow(currentSequence, currentStep, row);
    }

    // sequencer->resetSequence(currentSequence);
    break;
  case SequencerEditorMode::editingStep:
    // enterNoteData(0);
    // sequencer->resetSequence(currentSequence);
    // enterDataAtCursor(0);
    sequencer->resetStepRow(currentSequence, currentStep, currentStepRow);

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
  switch (editMode)
  {
  case SequencerEditorMode::selectingSeqAndStep:
  {
    editMode = SequencerEditorMode::editingStep;
    // check if we are in the right bounds with our cursor
    int maxRows = sequencer->howManyStepDataRows(currentSequence, currentStep);
    if (currentStepRow >= maxRows) currentStepRow = maxRows - 1;//
    int maxCols = sequencer->howManyStepDataCols(currentSequence, currentStep);
    if (currentStepCol >= maxCols) currentStepRow = maxCols - 1;
    break;
  }
  case SequencerEditorMode::settingSeqLength:
    // editMode = SequencerEditorMode::configuringSequence;
    break;
  case SequencerEditorMode::configuringSequence:
    // editMode = SequencerEditorMode::settingSeqLength;
    editMode = SequencerEditorMode::selectingSeqAndStep;
    break;

  case SequencerEditorMode::editingStep:
    editMode = SequencerEditorMode::selectingSeqAndStep;
    break;
  }
}

void SequencerEditor::enterStepData(double value, int column)
{
  if (editMode == SequencerEditorMode::editingStep ||
      editMode == SequencerEditorMode::selectingSeqAndStep)
  {
    assert (column ==  Step::noteInd || 
            column ==  Step::velInd ||
            column ==  Step::lengthInd);
    // get a copy of the data for reference 
    std::vector<std::vector<double>> data = sequencer->getStepData(currentSequence, currentStep);
    // get the relevant parameter index
    if (editMode == SequencerEditorMode::editingStep){
      // force currentCol to be note col
      currentStepCol = column;
    }
    if (editMode == SequencerEditorMode::selectingSeqAndStep){
      currentStepRow = 0;
    }
    // if the velocity is zero, we should set it to something sensible 
    if (data[currentStepRow][Step::velInd] == 0){
       sequencer->setStepDataAt(currentSequence, currentStep, currentStepRow, Step::velInd, 64);
    }
    // same for length
    if (data[currentStepRow][Step::lengthInd] == 0){
       sequencer->setStepDataAt(currentSequence, currentStep, currentStepRow, Step::lengthInd, 1);
    }
    // and probability 
    if (data[currentStepRow][Step::probInd] == 0){
       sequencer->setStepDataAt(currentSequence, currentStep, currentStepRow, Step::probInd, 1);
    }
    // apply octave if needed
    if (column == Step::noteInd){
      value = (12 * octave) + value; 
    }
    
    // always used the mutex protected function to update the data 
    sequencer->setStepDataAt(currentSequence, currentStep, currentStepRow, column, value);
    // move to the next step down 
    moveCursorDown();
  }
}


/** Increase the octave offset applied when entering notes  */
void SequencerEditor::incrementOctave()
{
  if (octave < 9) octave ++;  
}
/** decrease the octave offset applied when entering notes  */
void SequencerEditor::decrementOctave()
{
  if (octave > 1) octave --;    
}

/**
 * Tell the editor the user entered note data. The incoming note
 * value is assumed to be in the range 0-127
 *
 */
void SequencerEditor::enterDataAtCursor(double note)
{
  if (editMode == SequencerEditorMode::editingStep ||
      editMode == SequencerEditorMode::selectingSeqAndStep)
  {
    // work out which data row we are editing
    int dataRow = 0;// default to first row in sequencer view
    int dataCol = Step::noteInd;// default to note in sequencer view
    if (editMode == SequencerEditorMode::editingStep){
      // can be different in step view
      dataRow = currentStepRow;
      dataCol = currentStepCol;
    }
    std::vector<std::vector<double>> data = sequencer->getStepData(currentSequence, currentStep);
    // set a default vel and len if needed.
    if (data[dataRow][Step::velInd] == 0)
      data[dataRow][Step::velInd] = 64;
    if (data[dataRow][Step::lengthInd] == 0)
      data[dataRow][Step::lengthInd] = 1; // two ticks
    switch (dataCol){
      case Step::noteInd:
      {
        data[dataRow][dataCol] = note;
        break;
      }
      case Step::velInd:
      {
        data[dataRow][dataCol] = note;
        break;
      }
      case Step::lengthInd:
        data[dataRow][dataCol] = fmod(note, 4) + 1;
      {
        break;
      }
      
    }
    writeStepData(data);
  }
  // after note update in this mode,
  // move to the next note
  if (editMode == SequencerEditorMode::selectingSeqAndStep)
  {
    moveCursorDown();
  }

  if (editMode == SequencerEditorMode::configuringSequence)
  {
    // set channel on all notes for this sequence
    int channelI = (unsigned int)note;
    channelI = channelI % 16; // 16 channels
    for (int step = 0; step < sequencer->howManySteps(currentSequence); ++step)
    {
      sequencer->setStepDataAt(currentSequence, step, 0, Step::chanInd, channelI);
    }
  }
}


/** add one to current step */
void SequencerEditor::nextStep()
{
  
  currentStep += 1;
  if (currentStep >= sequencer->howManySteps(currentSequence))
    {currentStep = sequencer->howManySteps(currentSequence) - 1;}
  
}
/** moves the editor cursor right - so move to next sequence/ next data field if editing step
 */

void SequencerEditor::moveCursorLeft()
{
  switch (editMode)
  {
  case SequencerEditorMode::settingSeqLength:
  {
    currentSequence -= 1;
    if (currentSequence < 0)
      currentSequence = 0;
    if (currentStep >= sequencer->howManySteps(currentSequence))
      currentStep = sequencer->howManySteps(currentSequence) - 1;
    break;
  }
  case SequencerEditorMode::selectingSeqAndStep:
  {
    currentSequence -= 1;
    if (currentSequence < 0)
      currentSequence = 0;
    if (currentStep >= sequencer->howManySteps(currentSequence))
      currentStep = sequencer->howManySteps(currentSequence) - 1;
    break;
  }
  case SequencerEditorMode::editingStep:
  {
    // std::vector<std::vector<double>> data = sequencer->getStepData(currentSequence, currentStep);
    // decrementStepData(data, sequencer->getSequenceType(currentSequence));
    // writeStepData(data);
    // move left to previous column in the step data
    currentStepCol --;
    if (currentStepCol < 0) currentStepCol = 0;
    break;
  }
  case SequencerEditorMode::configuringSequence:
  {
    // increment the value of the currently selected
    // parameter (channel, sequence type,ticks per second)
    // incrementSeqConfigParam();
    currentSequence -= 1;
    if (currentSequence < 0)
      currentSequence = 0;
    if (currentStep >= sequencer->howManySteps(currentSequence))
      currentStep = sequencer->howManySteps(currentSequence) - 1;
    break;
    break;
  }
  }
}

void SequencerEditor::moveCursorRight()
{
  switch (editMode)
  {
  case SequencerEditorMode::settingSeqLength:
  {
    currentSequence += 1;
    if (currentSequence >= sequencer->howManySequences())
      currentSequence = sequencer->howManySequences() - 1;
    if (currentStep >= sequencer->howManySteps(currentSequence))
      currentStep = sequencer->howManySteps(currentSequence) - 1;
    break;
  }
  case SequencerEditorMode::selectingSeqAndStep:
  {
    currentSequence += 1;
    if (currentSequence >= sequencer->howManySequences())
      currentSequence = sequencer->howManySequences() - 1;
    if (currentStep >= sequencer->howManySteps(currentSequence))
      currentStep = sequencer->howManySteps(currentSequence) - 1;
    break;
  }
  case SequencerEditorMode::editingStep:
  {
    // move right to next step col
    currentStepCol ++;
    // int naxCols = sequencer->getStepDataAt(currentSequence, currentStep, currentStepRow)
    int maxCols = sequencer->howManyStepDataCols(currentSequence, currentStep);
    // sequencer->getStepDataDirect(currentSequence, currentStep)->at(currentStepRow).size();
    if (currentStepCol >= maxCols) currentStepCol = maxCols - 1;
    break;
  }
  case SequencerEditorMode::configuringSequence:
  {
    // decrementSeqConfigParam();
     currentSequence += 1;
    if (currentSequence >= sequencer->howManySequences())
      currentSequence = sequencer->howManySequences() - 1;
    if (currentStep >= sequencer->howManySteps(currentSequence))
      currentStep = sequencer->howManySteps(currentSequence) - 1;
    break;
  }
  }
}

void SequencerEditor::moveCursorUp()
{
  switch (editMode)
  {
  case SequencerEditorMode::settingSeqLength:
  {
    // sequencer->shrinkSequence(currentSequence);
    break;
  }
  case SequencerEditorMode::selectingSeqAndStep:
  {
    currentStep -= 1;
    if (currentStep < 0)
      currentStep = 0;
    break;
  }
  case SequencerEditorMode::editingStep:
  {
    // cycles which data field we are editing
    // this->editSubMode = SequencerEditor::cycleSubModeLeft(this->editSubMode);
    currentStepRow -= 1;
    if (currentStepRow < 0)
      currentStepRow = 0;

    break;
  }
  case SequencerEditorMode::configuringSequence:
  {
    // SequencerEditor::nextSequenceType(sequencer, currentSequence);
    currentSeqParam --;
    if (currentSeqParam < 0) currentSeqParam = 0; 
    break;
  }
  }
}

void SequencerEditor::moveCursorDown()
{
  switch (editMode)
  {
  case SequencerEditorMode::settingSeqLength:
  {
    sequencer->extendSequence(currentSequence);
    break;
  }
  case SequencerEditorMode::selectingSeqAndStep:
  {
    currentStep += 1;
    if (currentStep >= sequencer->howManySteps(currentSequence))
      currentStep = sequencer->howManySteps(currentSequence) - 1;
    break;
  }
  case SequencerEditorMode::editingStep:
  {
    currentStepRow += 1;
    int rowsInStep = sequencer->howManyStepDataRows(currentSequence, currentStep);
    if (currentStepRow >= rowsInStep)//sequencer->howManySteps(currentSequence))
      currentStepRow = rowsInStep - 1;
    break;
  }
  case SequencerEditorMode::configuringSequence:
  {
    // moving down moves to the next parameter for this track
    currentSeqParam ++;
    int max = sequencer->getSeqConfigSpecs().size() - 1;
    if (currentSeqParam > max){
      currentSeqParam = max;
    } 
    break;
  }
  }
}

/** increase the value at the cursor */
void SequencerEditor::addRow()
{
  switch (editMode)
  {
  case SequencerEditorMode::selectingSeqAndStep:
    // expand the sequence length
    sequencer->extendSequence(getCurrentSequence());
    break;
  case SequencerEditorMode::editingStep:
    // add another row to the data at this step
    std::vector<std::vector<double>> data = sequencer->getStepData(currentSequence, currentStep);
    // decrementStepData(data, sequencer->getSequenceType(currentSequence));
    std::vector<double> newRow(data[0].size(), 0.0);
    data.push_back(newRow);
    writeStepData(data);
    break;
  }
}
/** decreae the value at the cursor */
void SequencerEditor::removeRow()
{
  switch (editMode)
  {
  case SequencerEditorMode::selectingSeqAndStep:
    // expand the sequence length
    sequencer->shrinkSequence(getCurrentSequence());
    if (currentStep >= sequencer->howManySteps(currentSequence))
      currentStep = sequencer->howManySteps(currentSequence) - 1;
    break;
  case SequencerEditorMode::editingStep:
    // add another row to the data at this step
    std::vector<std::vector<double>> data = sequencer->getStepData(currentSequence, currentStep);
    if (data.size() > 1){
      data.pop_back(); 
      writeStepData(data); // only shrink if small enough
    }
    // make sure the cursor is in range 
    int rowsInStep = data.size(); 
    if (currentStepRow >= rowsInStep)//sequencer->howManySteps(currentSequence))
      currentStepRow = rowsInStep - 1;
    break;
  }
}

/** increase the value at the current cursor position, e.g. increasing note number */
void SequencerEditor::incrementAtCursor()
{
  switch (editMode)
  {
  case SequencerEditorMode::selectingSeqAndStep:
    // nothing for now
    break;
  case SequencerEditorMode::editingStep:
  {
    sequencer->incrementStepDataAt(currentSequence, currentStep, currentStepRow, currentStepCol);
    break;
  }
  case SequencerEditorMode::configuringSequence:
  {
    sequencer->incrementSeqParam(currentSequence, currentSeqParam);
    break; 
  }
  }
}
/** decrease the value at the current cursor position, e.g. increasing note number */
void SequencerEditor::decrementAtCursor()
{
  switch (editMode)
  {
  case SequencerEditorMode::selectingSeqAndStep:
    break;
  case SequencerEditorMode::editingStep:
  {
    sequencer->decrementStepDataAt(currentSequence, currentStep, currentStepRow, currentStepCol);
    break;
  }
  case SequencerEditorMode::configuringSequence:
  {
    sequencer->decrementSeqParam(currentSequence, currentSeqParam);
    break; 
  }
  }
}



SequencerEditorSubMode SequencerEditor::cycleSubModeLeft(SequencerEditorSubMode subMode)
{
  switch (subMode)
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
  switch (subMode)
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
void SequencerEditor::decrementStepData(std::vector<std::vector<double>> &data, SequenceType seqType)
{
  double decrement{0};
  double targetIndex{Step::noteInd};
  double min{0};

  // figure out the increment
  switch (seqType)
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
  switch (editSubMode)
  {
  case SequencerEditorSubMode::editCol1:
  {
    targetIndex = Step::noteInd;
    break;
  }
  case SequencerEditorSubMode::editCol2:
  {
    targetIndex = Step::lengthInd;
    decrement = 1; // 1 for length
    break;
  }
  case SequencerEditorSubMode::editCol3:
  {
    targetIndex = Step::velInd;
    decrement = 10; // 10 for vel
    break;
  }
  }
  double now = data[0][targetIndex];
  data[0][targetIndex] -= decrement;
  if (data[0][targetIndex] < min)
    data[0][targetIndex] = min;
}

/** increase the sent step's data
 * based on current edit mode and edit sub mode
 */
void SequencerEditor::incrementStepData(std::vector<std::vector<double>> &data, SequenceType seqType)
{
  double increment{0};
  double targetIndex{Step::noteInd};
  double max{127};

  // figure out the increment
  switch (seqType)
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
  switch (editSubMode)
  {
  case SequencerEditorSubMode::editCol1:
  {
    targetIndex = Step::noteInd;
    break;
  }
  case SequencerEditorSubMode::editCol2:
  {
    targetIndex = Step::lengthInd;
    increment = 1; // reset the increment if it is editing the length
    break;
  }
  case SequencerEditorSubMode::editCol3:
  {
    targetIndex = Step::velInd;
    increment = 10; // vel goes up 10 at a time
    break;
  }
  }
  double now = data[0][targetIndex];
  data[0][targetIndex] += increment;
  if (data[0][targetIndex] > max)
    data[0][targetIndex] = max;
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
  std::vector<std::vector<double>> data2 = sequencer->getStepData(currentSequence, 0);
  int channel = data2[0][Step::chanInd];
  channel = (channel + 1) % 16;
  for (int step = 0; step < sequencer->howManySteps(currentSequence); ++step)
  {
    // note this only sets channel on first row for now
    sequencer->setStepDataAt(currentSequence, step, 0, Step::chanInd, channel);
  }
}
void SequencerEditor::decrementChannel()
{
  // set the channel based on step 0
  std::vector<std::vector<double>> data2 = sequencer->getStepData(currentSequence, 0);
  unsigned int channel = data2[0][Step::chanInd];
  channel = (channel - 1) % 16;
  if (channel > 16)
    channel = 16;
  if (channel < 0)
    channel = 0;
  for (int step = 0; step < sequencer->howManySteps(currentSequence); ++step)
  {
    // note this only sets channel on first row for now
    sequencer->setStepDataAt(currentSequence, step, 0, Step::chanInd, channel);
  }
}

void SequencerEditor::incrementTicksPerStep()
{
  int tps = sequencer->getSequence(currentSequence)->getTicksPerStep();
  tps++;
  if (tps > 8)
    tps = 1;
  sequencer->getSequence(currentSequence)->setTicksPerStep(tps);
}
void SequencerEditor::decrementTicksPerStep()
{
  int tps = sequencer->getSequence(currentSequence)->getTicksPerStep();
  tps--;
  if (tps == 0)
    tps = 1;
  sequencer->getSequence(currentSequence)->setTicksPerStep(tps);
}

void SequencerEditor::nextSequenceType(Sequencer *seqr, unsigned int sequence)
{
  SequenceType type = seqr->getSequenceType(sequence);
  switch (type)
  {
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
int SequencerEditor::getCurrentStepRow() const
{
  return currentStepRow;
}
/** which data point in a step are we editing */
int SequencerEditor::getCurrentStepCol() const
{
  return currentStepCol;
}

int SequencerEditor::getCurrentSeqParam() const
{
  return currentSeqParam;
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
void SequencerEditor::writeStepData(std::vector<std::vector<double>> data)
{
  sequencer->setStepData(currentSequence, currentStep, data);
}
/** write the sent data to the sequence at 'currentSequence' - 1D data version for simple one value per step -style sequences*/
void SequencerEditor::writeSequenceData(std::vector<std::vector<double>> data)
{
  std::vector<std::vector<double>> stepData = {{0}};
  for (int i = 0; i < sequencer->howManySteps(currentSequence); ++i)
  {
    stepData[0] = data[i % data.size()]; // wrap it around :)
    sequencer->setStepData(currentSequence, currentSequence, stepData);
  }
}
/** write the sent data to a sequence - 1D data version */
// void SequencerEditor::writeSequenceData(std::vector<std::vector<double>> data)
// {
// for (int i=0; i<sequencer->howManySteps(currentSequence); ++i)
// {
//     sequencer->setStepData(currentSequence, currentSequence, data[i % data.size()]); // wrap around
// }
// }


bool  SequencerEditor::isTriggerActive()
{
  return triggerIsActive;
}
/** activate step triggering */
void  SequencerEditor::activateTrigger()
{
  triggerIsActive = true; 
}
/** deactivate step triggering */
void  SequencerEditor::deactivateTrigger()
{
  triggerIsActive = false; 
}

void  SequencerEditor::toggleTrigger()
{
  triggerIsActive = !triggerIsActive;
}

void SequencerEditor::gotoSequenceConfigPage()
{
   setEditMode(SequencerEditorMode::configuringSequence);
   
}