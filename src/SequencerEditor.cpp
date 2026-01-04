#include "SequencerEditor.h"
#include "SequencerCommands.h"
#include <JuceHeader.h>
#include <algorithm>
#include <cmath> // fmod
#include <assert.h>

namespace
{
std::string sanitizeLabel(const juce::String &input, size_t maxLen)
{
  auto trimmed = input.trim();
  if (trimmed.isEmpty())
    return {};

  auto upper = trimmed.toUpperCase();
  std::string out;
  out.reserve(static_cast<size_t>(upper.length()));
  for (auto ch : upper)
  {
    if (out.size() >= maxLen)
      break;
    if (ch == '\n' || ch == '\r')
      continue;
    if (ch < 32 || ch > 126)
      continue;
    out.push_back(static_cast<char>(ch));
  }
  return out;
}

std::string formatGain(float gain)
{
  juce::String text = juce::String(gain, 2);
  return sanitizeLabel(text, 6);
}
} // namespace

SequencerEditor::SequencerEditor(Sequencer *sequencer) : sequencer{sequencer},
                                                         currentSequence{0},
                                                         currentStep{0},
                                                         currentStepRow{0},
                                                         currentStepCol{0},
                                                         currentSeqParam{0},
                                                         editMode{SequencerEditorMode::selectingSeqAndStep}, editSubMode{SequencerEditorSubMode::editCol1},
                                                         stepIncrement{0.5f},
                                                         octave{6},
                                                         armedSequence{Sequencer::notArmed} // default to a value higher than we'll ever have number of sequences (640k is enough, right Bill?)
{
}

void SequencerEditor::setSequencer(Sequencer *_sequencer)
{
  this->sequencer = _sequencer;
}
void SequencerEditor::setSamplerHost(SamplerHost *host)
{
  samplerHost = host;
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
  samplerEditMode = false;
  samplerEditType = SamplerCellType::None;
  samplerEditPlayerIndex = -1;
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
  if (mode != SequencerEditorMode::machineConfig)
    samplerCancelEdit();
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
  case SequencerEditorMode::editingStep: // go to next data item
    this->editSubMode = SequencerEditor::cycleSubModeRight(this->editSubMode);
    return;
  case SequencerEditorMode::configuringSequence:
    this->editSubMode = SequencerEditor::cycleSubModeRight(this->editSubMode);
    return;
  case SequencerEditorMode::machineConfig:
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
  case SequencerEditorMode::configuringSequence:
    break;
  case SequencerEditorMode::machineConfig:
    if (isSamplerMachineForCurrentSequence())
      samplerActivateCurrentCell();
    break;

  case SequencerEditorMode::selectingSeqAndStep:
    // toggle the step on or off
    // toggle all steps in current sequence to off
    for (auto i = 0; i < sequencer->getSequence(currentSequence)->getLength(); ++i)
    {
      sequencer->toggleStepActive(currentSequence, i);
    }
    return;
  case SequencerEditorMode::editingStep:
    sequencer->toggleStepActive(currentSequence, currentStep);
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
    for (int row = 0; row < sequencer->howManyStepDataRows(currentSequence, currentStep); ++row)
    {
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

  case SequencerEditorMode::configuringSequence:

    break;
  case SequencerEditorMode::machineConfig:
    // if (!isSamplerMachineForCurrentSequence())
    //   break;
    // if (samplerEditMode)
    // {
    //   adjustSamplerEditValue(-1);
    //   break;
    // }
    // moveSamplerCursor(-1, 0);
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
    if (currentStepRow >= maxRows)
      currentStepRow = maxRows - 1; //
    int maxCols = sequencer->howManyStepDataCols(currentSequence, currentStep);
    if (currentStepCol >= maxCols)
      currentStepRow = maxCols - 1;
    break;
  }
  case SequencerEditorMode::configuringSequence:
    // editMode = SequencerEditorMode::settingSeqLength;
    editMode = SequencerEditorMode::selectingSeqAndStep;
    break;

  case SequencerEditorMode::editingStep:
    editMode = SequencerEditorMode::selectingSeqAndStep;
    break;
  case SequencerEditorMode::machineConfig:
    samplerEditMode = false;
    samplerEditType = SamplerCellType::None;
    samplerEditPlayerIndex = -1;
    editMode = SequencerEditorMode::selectingSeqAndStep;
    break;
  }
}

void SequencerEditor::enterStepData(double value, int column, bool applyOctave)
{
  if (editMode == SequencerEditorMode::editingStep ||
      editMode == SequencerEditorMode::selectingSeqAndStep)
  {
    assert(column == Step::noteInd ||
           column == Step::velInd ||
           column == Step::lengthInd);
    // get a copy of the data for reference
    std::vector<std::vector<double>> data = sequencer->getStepData(currentSequence, currentStep);
    // get the relevant parameter index
    if (editMode == SequencerEditorMode::editingStep)
    {
      // force currentCol to be note col
      currentStepCol = column;
    }
    if (editMode == SequencerEditorMode::selectingSeqAndStep)
    {
      currentStepRow = 0;
    }

    std::vector<std::vector<double>> firstStep = sequencer->getStepData(currentSequence, 0);
    // set the vel, len and probability values for the
    // new step data to defaults if they are currently at zero
    std::vector<int> cols = {Step::velInd, Step::lengthInd, Step::probInd};
    for (int col : cols)
    {
      if (data[currentStepRow][col] == 0)
      {
        sequencer->setStepDataToDefault(currentSequence, currentStep, currentStepRow, col);
      }
    }
    // apply octave if needed
    if (column == Step::noteInd && applyOctave)
    {
      value = (12 * octave) + value;
    }
    // assert we are not out of bounds for this value perhaps?
    // Parameter param = CommandProcessor::getCommand(data[Step::cmdInd]).parameters[(int)column]; // -1 as the first col is the command which has no parameter

    // always used the mutex protected function to update the data
    sequencer->setStepDataAt(currentSequence, currentStep, currentStepRow, column, value);
    // move to the next step down
    moveCursorDown();
  }
}

/** Increase the octave offset applied when entering notes  */
void SequencerEditor::incrementOctave()
{
  if (octave < 9)
    octave++;
  // now check if they pressed the octave adjust whilst cursor is over a note

  switch (editMode)
  {

  case SequencerEditorMode::selectingSeqAndStep:
  {
    // default to row 0
    double note = sequencer->getStepDataAt(currentSequence, currentStep, 0, Step::noteInd);
    if (note > 0)
    { // there is a note here - put it up an octave
      enterStepData(note + 12, Step::noteInd, false);
    }
    break;
  }
  case SequencerEditorMode::editingStep:
  {
    // use current step row
    double note = sequencer->getStepDataAt(currentSequence, currentStep, currentStepRow, Step::noteInd);
    if (note > 0)
    { // there is a note here - put it up an octave
      enterStepData(note + 12, Step::noteInd, false);
    }
    break;
  }
  case SequencerEditorMode::configuringSequence:
  {
    break;
  }
  case SequencerEditorMode::machineConfig:
  {
    break;
  }
  }
}
/** decrease the octave offset applied when entering notes  */
void SequencerEditor::decrementOctave()
{
  if (octave > 1)
    octave--;

  switch (editMode)
  {
  case SequencerEditorMode::selectingSeqAndStep:
  {
    // this will automatically apply the octave shift  to the note
    double note = sequencer->getStepDataAt(currentSequence, currentStep, 0, Step::noteInd);
    if (note > 0 && note - 12 > 0)
    { // there is a note here - put it up an octave
      enterStepData(note - 12, Step::noteInd, false);
    }
    break;
  }
  case SequencerEditorMode::editingStep:
  {
    // this will automatically apply the octave shift  to the note
    double note = sequencer->getStepDataAt(currentSequence, currentStep, currentStepRow, Step::noteInd);
    if (note > 0 && note - 12 > 0)
    { // there is a note here - put it up an octave
      enterStepData(note - 12, Step::noteInd, false);
    }
    break;
  }
  case SequencerEditorMode::configuringSequence:
  {
    break;
  }
  case SequencerEditorMode::machineConfig:
  {
    break;
  }
  }
}

/**
 * Tell the editor the user entered note data. The incoming note
 * value is assumed to be in the range 0-127
 *
 */
void SequencerEditor::enterDataAtCursor(double inValue)
{
  if (editMode == SequencerEditorMode::editingStep ||
      editMode == SequencerEditorMode::selectingSeqAndStep)
  {
    // work out which data row we are editing
    size_t dataRow = 0;             // default to first row in sequencer view
    size_t dataCol = Step::noteInd; // default to note in sequencer view
    if (editMode == SequencerEditorMode::editingStep)
    {
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
    switch (dataCol)
    {
    case Step::noteInd:
    {
      data[dataRow][dataCol] = inValue;
      break;
    }
    case Step::velInd:
    {
      data[dataRow][dataCol] = inValue;
      break;
    }
    case Step::lengthInd:
      data[dataRow][dataCol] = fmod(inValue, 4) + 1;
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
    // moveCursorDown();
  }

  if (editMode == SequencerEditorMode::configuringSequence)
  {
    Sequence* sequence = sequencer->getSequence(currentSequence);
    if (currentSeqParam == Sequence::machineIdConfig){
      double machineId = fmod(inValue, 16);
      if (machineId < 0) machineId = 0;
      sequence->setMachineId(machineId);
    }
    else if (currentSeqParam == Sequence::machineTypeConfig){
      double machineType = fmod(inValue, CommandProcessor::countCommands());
      if (machineType < 0) machineType = 0;
      sequence->setMachineType(machineType);
    }
    else if (currentSeqParam == Sequence::probConfig){
      double prob = inValue;
      if (prob < 0) prob = 0;
      if (prob > 1) prob = 1;
      sequence->setTriggerProbability(prob);
    }
    else if (currentSeqParam == Sequence::tpsConfig){
      int tps = static_cast<int>(inValue);
      if (tps < 1) tps = 1;
      if (tps > 16) tps = 16;
      sequence->setTicksPerStep(static_cast<std::size_t>(tps));
      sequence->onZeroSetTicksPerStep(static_cast<std::size_t>(tps));
    }
  }
}

void SequencerEditor::insertNoteAtTickPos(size_t sequence, int channel, int note, int velocity)
{
  // std::vector<std::vector<double>> data(1, std::vector<double>(Step::maxInd));
  std::vector<std::vector<double>> data = sequencer->getStepData(sequence, sequencer->getCurrentStep(sequence));

  data[0][Step::noteInd] = static_cast<double>(note);
  data[0][Step::velInd] = static_cast<double>(velocity);
  // data[0][Step::chanInd] = data[0][Step::chanInd];// keep the channel the seq already has
  data[0][Step::lengthInd] = 2.0; // shortish
  if (data[0][Step::probInd] == 0.0)
  {
    data[0][Step::probInd] = 1.0; // keep the prob unless it is currently zero
  }

  data[0][Step::probInd] = 1.0; // keep the prob

  // align sequence machine id to the incoming channel if supplied
  if (channel >= 0)
  {
    sequencer->getSequence(sequence)->setMachineId(channel % 16);
  }

  sequencer->setStepData(sequence,
                         sequencer->getCurrentStep(sequence),
                         data);
}

/** add one to current step */
void SequencerEditor::nextStep()
{

  currentStep += 1;
  if (currentStep >= sequencer->howManySteps(currentSequence))
  {
    currentStep = sequencer->howManySteps(currentSequence) - 1;
  }
}
/** moves the editor cursor right - so move to next sequence/ next data field if editing step
 */

void SequencerEditor::moveCursorLeft()
{
  switch (editMode)
  {

  case SequencerEditorMode::selectingSeqAndStep:
  {
    if (currentSequence == 0)
      return;

    currentSequence -= 1;
    if (currentSequence < 0)
      currentSequence = 0;
    if (currentStep >= sequencer->howManySteps(currentSequence))
      currentStep = sequencer->howManySteps(currentSequence) - 1;
    break;
  }
  case SequencerEditorMode::editingStep:
  {
    // move left to previous column in the step data
    if (currentStepCol == 0)
      return;
    currentStepCol--;
    if (currentStepCol < 0)
      currentStepCol = 0;
    break;
  }
  case SequencerEditorMode::configuringSequence:
  {
    // increment the value of the currently selected
    // parameter (channel, sequence type,ticks per second)
    // incrementSeqConfigParam();
    if (currentSequence == 0)
      return;
    currentSequence -= 1;
    if (currentSequence < 0)
      currentSequence = 0;
    if (currentStep >= sequencer->howManySteps(currentSequence))
      currentStep = sequencer->howManySteps(currentSequence) - 1;
    break;
  }
  case SequencerEditorMode::machineConfig:
  {
    if (!isSamplerMachineForCurrentSequence())
      break;
    if (samplerEditMode)
    {
      adjustSamplerEditValue(-1);
      break;
    }
    moveSamplerCursor(0, -1);
    break;
  }
  }// end sw
}

void SequencerEditor::moveCursorRight()
{
  switch (editMode)
  {
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
    currentStepCol++;
    // int naxCols = sequencer->getStepDataAt(currentSequence, currentStep, currentStepRow)
    int maxCols = sequencer->howManyStepDataCols(currentSequence, currentStep);
    // sequencer->getStepDataDirect(currentSequence, currentStep)->at(currentStepRow).size();
    if (currentStepCol >= maxCols)
      currentStepCol = maxCols - 1;
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
  case SequencerEditorMode::machineConfig:
  {
    if (!isSamplerMachineForCurrentSequence())
      break;
    if (samplerEditMode)
    {
      adjustSamplerEditValue(1);
      break;
    }
    moveSamplerCursor(0, 1);
    break;
  }
  }
}

void SequencerEditor::moveCursorUp()
{
  switch (editMode)
  {

  case SequencerEditorMode::selectingSeqAndStep:
  {
    if (currentStep == 0)
    {
      return;
    }

    currentStep -= 1;
    if (currentStep < 0)
      currentStep = 0;
    break;
  }
  case SequencerEditorMode::editingStep:
  {
    if (currentStepRow == 0)
    {
      return;
    }

    // cycles which data field we are editing
    // this->editSubMode = SequencerEditor::cycleSubModeLeft(this->editSubMode);
    currentStepRow -= 1;
    if (currentStepRow < 0)
      currentStepRow = 0;

    break;
  }
  case SequencerEditorMode::configuringSequence:
  {
    if (currentSeqParam == 0)
    {
      return;
    }

    // SequencerEditor::nextSequenceType(sequencer, currentSequence);
    currentSeqParam--;
    if (currentSeqParam < 0)
      currentSeqParam = 0;
    break;
  }
  case SequencerEditorMode::machineConfig:
    if (!isSamplerMachineForCurrentSequence())
      break;
    if (samplerEditMode)
    {
      adjustSamplerEditValue(1);
      break;
    }
    moveSamplerCursor(-1, 0);
    break;
  }
}

void SequencerEditor::moveCursorDown()
{
  switch (editMode)
  {
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
    if (currentStepRow >= rowsInStep) // sequencer->howManySteps(currentSequence))
      currentStepRow = rowsInStep - 1;
    break;
  }
  case SequencerEditorMode::configuringSequence:
  {
    // moving down moves to the next parameter for this track
    currentSeqParam++;
    int max = sequencer->getSeqConfigSpecs().size() - 1;
    if (currentSeqParam > max)
    {
      currentSeqParam = max;
    }
    break;
  }
  case SequencerEditorMode::machineConfig:
  {
    if (!isSamplerMachineForCurrentSequence())
      break;
    if (samplerEditMode)
    {
      adjustSamplerEditValue(-1);
      break;
    }
    moveSamplerCursor(1, 0);
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
  {
    // add another row to the data at this step
    std::vector<std::vector<double>> data = sequencer->getStepData(currentSequence, currentStep);
    // decrementStepData(data, sequencer->getSequenceType(currentSequence));
    std::vector<double> newRow(data[0].size(), 0.0);
    newRow[Step::cmdInd] = sequencer->getSequence(currentSequence)->getMachineType();
    data.push_back(newRow);
    writeStepData(data);
    break;
  }
  case SequencerEditorMode::configuringSequence:
    break;
  case SequencerEditorMode::machineConfig:
    if (isSamplerMachineForCurrentSequence())
      samplerAdjustCurrentCell(-1);
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
  {
    // add another row to the data at this step
    std::vector<std::vector<double>> data = sequencer->getStepData(currentSequence, currentStep);
    if (data.size() > 1)
    {
      data.pop_back();
      writeStepData(data); // only shrink if small enough
    }
    // make sure the cursor is in range
    int rowsInStep = data.size();
    if (currentStepRow >= rowsInStep) // sequencer->howManySteps(currentSequence))
      currentStepRow = rowsInStep - 1;
    break;
  }
  case SequencerEditorMode::configuringSequence:
    break;
  case SequencerEditorMode::machineConfig:
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
  case SequencerEditorMode::machineConfig:
    break;
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
  case SequencerEditorMode::machineConfig:
    break;
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
  sequencer->incrementSeqParam(currentSequence, currentSeqParam);
}

/** decrease the value of the seq param relating to the
 * current subMode
 */
void SequencerEditor::decrementSeqConfigParam()
{
  sequencer->decrementSeqParam(currentSequence, currentSeqParam);
}

void SequencerEditor::incrementChannel()
{
  Sequence* sequence = sequencer->getSequence(currentSequence);
  int machineId = static_cast<int>(sequence->getMachineId());
  machineId = (machineId + 1) % 16;
  sequence->setMachineId(machineId);
}
void SequencerEditor::decrementChannel()
{
  Sequence* sequence = sequencer->getSequence(currentSequence);
  int machineId = static_cast<int>(sequence->getMachineId());
  machineId = (machineId - 1);
  if (machineId < 0)
    machineId = 15;
  sequence->setMachineId(machineId);
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

size_t SequencerEditor::getCurrentSequence() const
{
  return currentSequence;
}

size_t SequencerEditor::getCurrentStep() const
{
  return currentStep;
}
/** which data point in a step are we editing */
size_t SequencerEditor::getCurrentStepRow() const
{
  return currentStepRow;
}
/** which data point in a step are we editing */
size_t SequencerEditor::getCurrentStepCol() const
{
  return currentStepCol;
}

size_t SequencerEditor::getCurrentSeqParam() const
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

void SequencerEditor::gotoSequenceConfigPage()
{
  setEditMode(SequencerEditorMode::configuringSequence);
}

void SequencerEditor::gotoMachineConfigPage()
{
  setEditMode(SequencerEditorMode::machineConfig);
  samplerEditMode = false;
  samplerEditType = SamplerCellType::None;
  samplerEditPlayerIndex = -1;
}

/** write the sent data to a sequence - 1D data version */
// void SequencerEditor::writeSequenceData(std::vector<std::vector<double>> data)
// {
// for (int i=0; i<sequencer->howManySteps(currentSequence); ++i)
// {
//     sequencer->setStepData(currentSequence, currentSequence, data[i % data.size()]); // wrap around
// }
// }

void SequencerEditor::setArmedSequence(const size_t sequence)
{
  if (this->armedSequence == sequence)
  {
    // switch it off
    this->armedSequence = Sequencer::notArmed;
  }
  else
  {
    this->armedSequence = sequence;
  }
}
size_t SequencerEditor::getArmedSequence()
{
  return this->armedSequence;
}

void SequencerEditor::unarmSequence()
{
  this->armedSequence = Sequencer::notArmed;
}

bool SequencerEditor::isArmedForLiveMIDI()
{
  if (this->armedSequence == Sequencer::notArmed)
    return false;
  return true;
}

bool SequencerEditor::isSamplerMachineForCurrentSequence() const
{
  if (sequencer == nullptr){
    DBG("isSamplerMachineForCurrentSequence no sequencerrr ");
    return false; 
  }
  
  const auto *sequence = sequencer->getSequence(currentSequence);
  if (sequence == nullptr){
        DBG("isSamplerMachineForCurrentSequence no sequence... ");
    return false; 
  }
  const auto machineType = static_cast<CommandType>(static_cast<std::size_t>(sequence->getMachineType()));
  return machineType == CommandType::Sampler;
}

std::size_t SequencerEditor::getActiveSamplerIndex() const
{
  if (samplerHost == nullptr)
    return 0;
  const auto count = samplerHost->getSamplerCount();
  if (count == 0)
    return 0;
  const auto *sequence = sequencer->getSequence(currentSequence);
  if (sequence == nullptr)
    return 0;
  const int machineId = static_cast<int>(sequence->getMachineId());
  const std::size_t safeId = machineId < 0 ? 0u : static_cast<std::size_t>(machineId);
  return safeId % count;
}

void SequencerEditor::refreshSamplerStateForCurrentSequence()
{
  if (!isSamplerMachineForCurrentSequence() || samplerHost == nullptr)
  {
    samplerPlayers.clear();
    samplerGlowLevels.clear();
    samplerCells.assign(1, std::vector<SamplerCell>(1));
    return;
  }

  const auto samplerState = samplerHost->getSamplerState(getActiveSamplerIndex());
  if (!samplerState.isObject())
  {
    samplerPlayers.clear();
    samplerGlowLevels.clear();
    rebuildSamplerCells();
    return;
  }

  const auto playersVar = samplerState.getProperty("players", juce::var());
  if (!playersVar.isArray())
  {
    samplerPlayers.clear();
    samplerGlowLevels.clear();
    rebuildSamplerCells();
    return;
  }

  const auto *playersArray = playersVar.getArray();
  if (playersArray == nullptr)
  {
    samplerPlayers.clear();
    samplerGlowLevels.clear();
    rebuildSamplerCells();
    return;
  }

  std::vector<SamplerPlayerState> nextPlayers;
  nextPlayers.reserve(static_cast<std::size_t>(playersArray->size()));
  for (const auto &entry : *playersArray)
  {
    auto *playerObj = entry.getDynamicObject();
    if (playerObj == nullptr)
      continue;

    SamplerPlayerState st;
    st.id = static_cast<int>(playerObj->getProperty("id"));
    st.midiLow = static_cast<int>(playerObj->getProperty("midiLow"));
    st.midiHigh = static_cast<int>(playerObj->getProperty("midiHigh"));
    st.gain = static_cast<float>(double(playerObj->getProperty("gain")));
    st.isPlaying = static_cast<bool>(playerObj->getProperty("isPlaying"));
    st.status = playerObj->getProperty("status").toString().toStdString();
    st.fileName = playerObj->getProperty("fileName").toString().toStdString();
    nextPlayers.push_back(st);
  }

  std::vector<float> nextGlow;
  nextGlow.reserve(nextPlayers.size());
  for (const auto &player : nextPlayers)
  {
    float glow = 0.0f;
    for (std::size_t i = 0; i < samplerPlayers.size(); ++i)
    {
      if (samplerPlayers[i].id == player.id && i < samplerGlowLevels.size())
      {
        glow = samplerGlowLevels[i];
        break;
      }
    }
    if (player.isPlaying)
      glow = 1.0f;
    else
      glow *= 0.85f;
    if (glow < 0.02f)
      glow = 0.0f;
    nextGlow.push_back(glow);
  }

  samplerPlayers = std::move(nextPlayers);
  samplerGlowLevels = std::move(nextGlow);
  rebuildSamplerCells();
}

const std::vector<std::vector<SamplerCell>> &SequencerEditor::getSamplerCells() const
{
  return samplerCells;
}

void SequencerEditor::samplerAddPlayer()
{
  if (!isSamplerMachineForCurrentSequence() || samplerHost == nullptr)
    return;
  samplerHost->samplerAddPlayer(getActiveSamplerIndex());
}

void SequencerEditor::samplerRemovePlayer()
{
  if (!isSamplerMachineForCurrentSequence() || samplerHost == nullptr)
    return;
  if (samplerPlayers.empty())
    return;
  std::size_t playerIndex = samplerPlayers.size() - 1;
  if (samplerCursorRow > 0)
  {
    const std::size_t candidate = samplerCursorRow - 1;
    if (candidate < samplerPlayers.size())
      playerIndex = candidate;
  }
  samplerHost->samplerRemovePlayer(getActiveSamplerIndex(), samplerPlayers[playerIndex].id);
}

void SequencerEditor::samplerActivateCurrentCell()
{
  if (!isSamplerMachineForCurrentSequence() || samplerHost == nullptr)
    return;
  if (samplerCells.empty() || samplerCells[0].empty())
    return;
  if (samplerCursorCol >= samplerCells.size() || samplerCursorRow >= samplerCells[samplerCursorCol].size())
    return;

  const auto &cell = samplerCells[samplerCursorCol][samplerCursorRow];
  switch (cell.type)
  {
  case SamplerCellType::None:
    break;
  case SamplerCellType::Add:
    samplerAddPlayer();
    break;
  case SamplerCellType::Load:
    samplerHost->samplerRequestLoad(getActiveSamplerIndex(), cell.playerId);
    break;
  case SamplerCellType::Trigger:
    samplerHost->samplerTrigger(getActiveSamplerIndex(), cell.playerId);
    break;
  case SamplerCellType::Low:
  case SamplerCellType::High:
  case SamplerCellType::Gain:
    samplerEditMode = true;
    samplerEditType = cell.type;
    samplerEditPlayerIndex = cell.playerIndex;
    break;
  case SamplerCellType::Waveform:
    break;
  }
}

void SequencerEditor::samplerAdjustCurrentCell(int direction)
{
  if (!isSamplerMachineForCurrentSequence() || samplerHost == nullptr)
    return;
  if (samplerEditMode)
  {
    adjustSamplerEditValue(direction);
    return;
  }
  if (samplerCells.empty() || samplerCells[0].empty())
    return;
  if (samplerCursorCol >= samplerCells.size() || samplerCursorRow >= samplerCells[samplerCursorCol].size())
    return;

  const auto &cell = samplerCells[samplerCursorCol][samplerCursorRow];
  if (cell.type == SamplerCellType::Low || cell.type == SamplerCellType::High || cell.type == SamplerCellType::Gain)
  {
    samplerEditType = cell.type;
    samplerEditPlayerIndex = cell.playerIndex;
    adjustSamplerEditValue(direction);
  }
}

void SequencerEditor::samplerCancelEdit()
{
  samplerEditMode = false;
  samplerEditType = SamplerCellType::None;
  samplerEditPlayerIndex = -1;
}

bool SequencerEditor::isSamplerEditing() const
{
  return samplerEditMode;
}

void SequencerEditor::rebuildSamplerCells()
{
  const std::size_t rows = samplerPlayers.size() + 1;
  const std::size_t cols = 6;

  if (rows == 0 || cols == 0)
  {
    samplerCells.assign(1, std::vector<SamplerCell>(1));
    return;
  }

  samplerCursorRow = std::min(samplerCursorRow, rows - 1);
  samplerCursorCol = std::min(samplerCursorCol, cols - 1);
  if (samplerCursorRow == 0)
    samplerCursorCol = 0;

  samplerCells.assign(cols, std::vector<SamplerCell>(rows));

  for (std::size_t col = 0; col < cols; ++col)
  {
    for (std::size_t row = 0; row < rows; ++row)
    {
      SamplerCell cell;
      if (row == 0)
      {
        if (col == 0)
        {
          cell.type = SamplerCellType::Add;
          cell.text = "ADD";
        }
        else
        {
          cell.type = SamplerCellType::None;
        }
      }
      else
      {
        const auto &player = samplerPlayers[row - 1];
        cell.playerIndex = static_cast<int>(row - 1);
        cell.playerId = player.id;
        switch (col)
        {
        case 0:
          cell.type = SamplerCellType::Load;
          cell.text = "LOAD";
          break;
        case 1:
          cell.type = SamplerCellType::Trigger;
          cell.text = player.isPlaying ? "PLAY" : "TRIG";
          break;
        case 2:
          cell.type = SamplerCellType::Low;
          cell.text = sanitizeLabel(juce::String(player.midiLow), 4);
          break;
        case 3:
          cell.type = SamplerCellType::High;
          cell.text = sanitizeLabel(juce::String(player.midiHigh), 4);
          break;
        case 4:
          cell.type = SamplerCellType::Gain;
          cell.text = formatGain(player.gain);
          break;
        case 5:
          cell.type = SamplerCellType::Waveform;
          if (!player.fileName.empty())
            cell.text = sanitizeLabel(juce::String(player.fileName), 18);
          else
            cell.text = sanitizeLabel(juce::String(player.status), 18);
          break;
        default:
          cell.type = SamplerCellType::None;
          break;
        }
        cell.isPlaying = player.isPlaying;
      }

      cell.isSelected = (row == samplerCursorRow && col == samplerCursorCol);
      cell.isEditing = (samplerEditMode && cell.isSelected && cell.type == samplerEditType);
      cell.isDisabled = (cell.type == SamplerCellType::None);

      if (row > 0 && row - 1 < samplerGlowLevels.size())
        cell.glow = samplerGlowLevels[row - 1];

      samplerCells[col][row] = std::move(cell);
    }
  }
}

void SequencerEditor::moveSamplerCursor(int deltaRow, int deltaCol)
{
  DBG("moveSamplerCursor > cols " << deltaCol << " rows " << deltaRow);
  if (samplerCells.empty() || samplerCells[0].empty())
    return;

  const int maxRow = static_cast<int>(samplerCells[0].size()) - 1;
  const int maxCol = static_cast<int>(samplerCells.size()) - 1;

  int nextRow = juce::jlimit(0, maxRow, static_cast<int>(samplerCursorRow) + deltaRow);
  int nextCol = juce::jlimit(0, maxCol, static_cast<int>(samplerCursorCol) + deltaCol);

  if (nextRow == 0)
    nextCol = 0;

  samplerCursorRow = static_cast<std::size_t>(nextRow);
  samplerCursorCol = static_cast<std::size_t>(nextCol);
  rebuildSamplerCells();
}

void SequencerEditor::adjustSamplerEditValue(int direction)
{
  if (samplerEditPlayerIndex < 0 || samplerEditPlayerIndex >= static_cast<int>(samplerPlayers.size()))
    return;
  if (!isSamplerMachineForCurrentSequence() || samplerHost == nullptr)
    return;

  const auto player = samplerPlayers[static_cast<std::size_t>(samplerEditPlayerIndex)];
  const auto samplerIndex = getActiveSamplerIndex();

  if (samplerEditType == SamplerCellType::Low)
  {
    const int low = juce::jlimit(0, 127, player.midiLow + direction);
    samplerHost->samplerSetRange(samplerIndex, player.id, low, player.midiHigh);
  }
  else if (samplerEditType == SamplerCellType::High)
  {
    const int high = juce::jlimit(0, 127, player.midiHigh + direction);
    samplerHost->samplerSetRange(samplerIndex, player.id, player.midiLow, high);
  }
  else if (samplerEditType == SamplerCellType::Gain)
  {
    const float gain = juce::jlimit(0.0f, 2.0f, player.gain + direction * 0.05f);
    samplerHost->samplerSetGain(samplerIndex, player.id, gain);
  }
}
