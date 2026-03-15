#include "SequencerEditor.h"
#include "MachineInterface.h"
#include "MachineUtilsAbs.h"
#include "Sequencer.h"
#include "SequencerCommands.h"
#include <JuceHeader.h>
#include <algorithm>
#include <cmath> // fmod
#include <limits>
#include <assert.h>

namespace
{
bool isSequencerPlaying(SequencerAbs *sequencer)
{
  if (sequencer == nullptr)
    return false;
  if (auto *impl = dynamic_cast<Sequencer *>(sequencer))
    return impl->isPlaying();
  return false;
}
} // namespace

SequencerEditor::SequencerEditor(SequencerAbs *_sequencer) : sequencer{_sequencer},
                                                         currentSequence{0},
                                                         currentStep{0},
                                                         currentStepRow{0},
                                                         currentStepCol{0},
                                                         currentSeqParam{0},
                                                         armedSequence{SequencerAbs::notArmed}, // default to a value higher than we'll ever have number of sequences (640k is enough, right Bill?)
                                                         editMode{SequencerEditorMode::selectingSeqAndStep},
                                                         editSubMode{SequencerEditorSubMode::editCol1},
                                                         stepIncrement{0.5f},
                                                         octave{6}
{
}

void SequencerEditor::setSequencer(SequencerAbs *_sequencer)
{
  this->sequencer = _sequencer;
}
void SequencerEditor::setMachineHost(MachineHost *host)
{
  machineHost = host;
}
void SequencerEditor::setResetConfirmationHandler(std::function<void()> handler)
{
  resetConfirmationHandler = std::move(handler);
}
SequencerAbs *SequencerEditor::getSequencer()
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
  machineEditMode = false;
  machineEditCol = 0;
  machineEditRow = 0;
  machineCursorRow = 0;
  machineCursorCol = 0;
}

SequencerEditorMode SequencerEditor::getEditMode() const
{
  return this->editMode;
}

SequencerEditorPage SequencerEditor::getCurrentPage() const
{
  switch (editMode)
  {
  case SequencerEditorMode::selectingSeqAndStep:
    return SequencerEditorPage::sequence;
  case SequencerEditorMode::editingStep:
    return SequencerEditorPage::step;
  case SequencerEditorMode::configuringSequence:
    return SequencerEditorPage::sequenceConfig;
  case SequencerEditorMode::machineConfig:
    return SequencerEditorPage::machine;
  case SequencerEditorMode::resetConfirmation:
    return SequencerEditorPage::resetConfirmation;
  }

  return SequencerEditorPage::sequence;
}

SequencerEditorSubMode SequencerEditor::getEditSubMode() const
{
  return this->editSubMode;
}
void SequencerEditor::setEditMode(SequencerEditorMode mode)
{
  this->editMode = mode;
  if (mode != SequencerEditorMode::machineConfig)
    machineEditMode = false;
}

void SequencerEditor::selectPage(SequencerEditorPage page)
{
  switch (page)
  {
  case SequencerEditorPage::sequence:
    gotoSequencePage();
    break;
  case SequencerEditorPage::step:
    gotoStepPage();
    break;
  case SequencerEditorPage::sequenceConfig:
    gotoSequenceConfigPage();
    break;
  case SequencerEditorPage::machine:
    gotoMachineConfigPage();
    break;
  case SequencerEditorPage::resetConfirmation:
    gotoResetConfirmationPage();
    break;
  }
}

bool SequencerEditor::selectPageShortcut(int shortcut)
{
  switch (shortcut)
  {
  case 1:
    selectPage(SequencerEditorPage::sequence);
    return true;
  case 2:
    selectPage(SequencerEditorPage::step);
    return true;
  case 3:
    selectPage(SequencerEditorPage::sequenceConfig);
    return true;
  case 4:
    selectPage(SequencerEditorPage::machine);
    return true;
  default:
    return false;
  }
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
  case SequencerEditorMode::selectingSeqAndStep:
    return;
  case SequencerEditorMode::editingStep: // go to next data item
    this->editSubMode = SequencerEditor::cycleSubModeRight(this->editSubMode);
    return;
  case SequencerEditorMode::configuringSequence:
    this->editSubMode = SequencerEditor::cycleSubModeRight(this->editSubMode);
    return;
  case SequencerEditorMode::machineConfig:
    return;
  case SequencerEditorMode::resetConfirmation:
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
    if (isMachineUiForCurrentSequence())
      machineActivateCurrentCell();
    break;

  case SequencerEditorMode::selectingSeqAndStep:
  {
    // toggle the step on or off
    // toggle all steps in current sequence to off
    const auto length = sequencer->getSequence(currentSequence)->getLength();
    for (std::size_t i = 0; i < length; ++i)
    {
      sequencer->toggleStepActive(currentSequence, i);
    }
    return;
  }
  case SequencerEditorMode::editingStep:
    sequencer->toggleStepActive(currentSequence, currentStep);
    return;
  case SequencerEditorMode::resetConfirmation:
    return;
  }
}

void SequencerEditor::click()
{
  switch (getCurrentPage())
  {
  case SequencerEditorPage::sequence:
    clickOnSequencePage();
    break;
  case SequencerEditorPage::step:
    clickOnStepPage();
    break;
  case SequencerEditorPage::sequenceConfig:
    break;
  case SequencerEditorPage::machine:
    clickOnMachinePage();
    break;
  case SequencerEditorPage::resetConfirmation:
    clickOnResetConfirmationPage();
    break;
  }
}
/** mode dependent reset function. Might reset */
void SequencerEditor::resetAtCursor()
{
  switch (getCurrentPage())
  {
  case SequencerEditorPage::sequence:
    resetOnSequencePage();
    break;
  case SequencerEditorPage::step:
    resetOnStepPage();
    break;
  case SequencerEditorPage::sequenceConfig:
    break;
  case SequencerEditorPage::machine:
    break;
  case SequencerEditorPage::resetConfirmation:
    resetOnResetConfirmationPage();
    break;
  }
}

/**
 *  Go into edit mode for either the sequence or step
 */
void SequencerEditor::enterAtCursor()
{
  switch (getCurrentPage())
  {
  case SequencerEditorPage::sequence:
    gotoStepPage();
    break;
  case SequencerEditorPage::sequenceConfig:
    gotoSequencePage();
    break;
  case SequencerEditorPage::step:
    gotoSequencePage();
    break;
  case SequencerEditorPage::machine:
    machineEditMode = false;
    gotoSequencePage();
    break;
  case SequencerEditorPage::resetConfirmation:
    resetOnResetConfirmationPage();
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
      currentStepCol = static_cast<std::size_t>(column);
    }
    if (editMode == SequencerEditorMode::selectingSeqAndStep)
    {
      currentStepRow = 0;
    }

    std::vector<std::vector<double>> firstStep = sequencer->getStepData(currentSequence, 0);
    // set the vel, len and probability values for the
    // new step data to defaults if they are currently at zero
    const std::size_t cols[] = {Step::velInd, Step::lengthInd, Step::probInd};
    for (std::size_t col : cols)
    {
      if (std::abs(data[currentStepRow][col]) < std::numeric_limits<double>::epsilon())
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
    sequencer->setStepDataAt(currentSequence, currentStep, currentStepRow, static_cast<std::size_t>(column), value);
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
  case SequencerEditorMode::resetConfirmation:
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
  case SequencerEditorMode::resetConfirmation:
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
    if (std::abs(data[dataRow][Step::velInd]) < std::numeric_limits<double>::epsilon())
      data[dataRow][Step::velInd] = 64;
    if (std::abs(data[dataRow][Step::lengthInd]) < std::numeric_limits<double>::epsilon())
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
  if (std::abs(data[0][Step::probInd]) < std::numeric_limits<double>::epsilon())
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
  switch (getCurrentPage())
  {
  case SequencerEditorPage::sequence: moveCursorLeftOnSequencePage(); break;
  case SequencerEditorPage::step: moveCursorLeftOnStepPage(); break;
  case SequencerEditorPage::sequenceConfig: moveCursorLeftOnSequenceConfigPage(); break;
  case SequencerEditorPage::machine: moveCursorLeftOnMachinePage(); break;
  case SequencerEditorPage::resetConfirmation: moveCursorLeftOnResetConfirmationPage(); break;
  }
}

void SequencerEditor::moveCursorRight()
{
  switch (getCurrentPage())
  {
  case SequencerEditorPage::sequence: moveCursorRightOnSequencePage(); break;
  case SequencerEditorPage::step: moveCursorRightOnStepPage(); break;
  case SequencerEditorPage::sequenceConfig: moveCursorRightOnSequenceConfigPage(); break;
  case SequencerEditorPage::machine: moveCursorRightOnMachinePage(); break;
  case SequencerEditorPage::resetConfirmation: moveCursorRightOnResetConfirmationPage(); break;
  }
}

void SequencerEditor::moveCursorUp()
{
  switch (getCurrentPage())
  {
  case SequencerEditorPage::sequence: moveCursorUpOnSequencePage(); break;
  case SequencerEditorPage::step: moveCursorUpOnStepPage(); break;
  case SequencerEditorPage::sequenceConfig: moveCursorUpOnSequenceConfigPage(); break;
  case SequencerEditorPage::machine: moveCursorUpOnMachinePage(); break;
  case SequencerEditorPage::resetConfirmation: moveCursorUpOnResetConfirmationPage(); break;
  }
}

void SequencerEditor::moveCursorDown()
{
  switch (getCurrentPage())
  {
  case SequencerEditorPage::sequence: moveCursorDownOnSequencePage(); break;
  case SequencerEditorPage::step: moveCursorDownOnStepPage(); break;
  case SequencerEditorPage::sequenceConfig: moveCursorDownOnSequenceConfigPage(); break;
  case SequencerEditorPage::machine: moveCursorDownOnMachinePage(); break;
  case SequencerEditorPage::resetConfirmation: moveCursorDownOnResetConfirmationPage(); break;
  }
}

/** increase the value at the cursor */
void SequencerEditor::addRow()
{
  switch (getCurrentPage())
  {
  case SequencerEditorPage::sequence:
    addRowOnSequencePage();
    break;
  case SequencerEditorPage::step:
    addRowOnStepPage();
    break;
  case SequencerEditorPage::sequenceConfig:
    break;
  case SequencerEditorPage::machine:
    addRowOnMachinePage();
    break;
  case SequencerEditorPage::resetConfirmation:
    break;
  }
}
/** decreae the value at the cursor */
void SequencerEditor::removeRow()
{
  switch (getCurrentPage())
  {
  case SequencerEditorPage::sequence:
    removeRowOnSequencePage();
    break;
  case SequencerEditorPage::step:
    removeRowOnStepPage();
    break;
  case SequencerEditorPage::sequenceConfig:
    break;
  case SequencerEditorPage::machine:
    break;
  case SequencerEditorPage::resetConfirmation:
    break;
  }
}

/** increase the value at the current cursor position, e.g. increasing note number */
void SequencerEditor::incrementAtCursor()
{
  switch (getCurrentPage())
  {
  case SequencerEditorPage::sequence:
    break;
  case SequencerEditorPage::step:
    incrementOnStepPage();
    break;
  case SequencerEditorPage::sequenceConfig:
    incrementOnSequenceConfigPage();
    break;
  case SequencerEditorPage::machine:
    incrementOnMachinePage();
    break;
  case SequencerEditorPage::resetConfirmation:
    break;
  }
}
/** decrease the value at the current cursor position, e.g. increasing note number */
void SequencerEditor::decrementAtCursor()
{
  switch (getCurrentPage())
  {
  case SequencerEditorPage::sequence:
    break;
  case SequencerEditorPage::step:
    decrementOnStepPage();
    break;
  case SequencerEditorPage::sequenceConfig:
    decrementOnSequenceConfigPage();
    break;
  case SequencerEditorPage::machine:
    decrementOnMachinePage();
    break;
  case SequencerEditorPage::resetConfirmation:
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
  std::size_t targetIndex{Step::noteInd};
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
  case SequenceType::chordMidi:
  case SequenceType::samplePlayer:
  {
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
  std::size_t targetIndex{Step::noteInd};
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
  case SequenceType::chordMidi:
  case SequenceType::samplePlayer:
  {
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
  std::size_t tps = sequencer->getSequence(currentSequence)->getTicksPerStep();
  if (tps >= 8)
    tps = 1;
  else
    ++tps;
  sequencer->getSequence(currentSequence)->setTicksPerStep(tps);
}
void SequencerEditor::decrementTicksPerStep()
{
  std::size_t tps = sequencer->getSequence(currentSequence)->getTicksPerStep();
  if (tps <= 1)
    tps = 1;
  else
    --tps;
  sequencer->getSequence(currentSequence)->setTicksPerStep(tps);
}

void SequencerEditor::nextSequenceType(SequencerAbs *seqr, unsigned int sequence)
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
  case SequenceType::chordMidi:
  case SequenceType::samplePlayer:
    seqr->setSequenceType(sequence, SequenceType::transposer);
    break;
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

double SequencerEditor::getCurrentOctave() const
{
  return octave; 
}

void SequencerEditor::moveCursorLeftOnSequencePage()
{
  if (currentSequence == 0)
    return;

  --currentSequence;
  if (currentStep >= sequencer->howManySteps(currentSequence))
    currentStep = sequencer->howManySteps(currentSequence) - 1;
}

void SequencerEditor::moveCursorLeftOnStepPage()
{
  if (currentStepCol == 0)
    return;

  --currentStepCol;
}

void SequencerEditor::moveCursorLeftOnSequenceConfigPage()
{
  if (currentSequence == 0)
    return;

  --currentSequence;
  if (currentStep >= sequencer->howManySteps(currentSequence))
    currentStep = sequencer->howManySteps(currentSequence) - 1;
}

void SequencerEditor::moveCursorLeftOnMachinePage()
{
  if (!isMachineUiForCurrentSequence())
    return;

  moveMachineCursor(0, -1);
}

void SequencerEditor::moveCursorLeftOnResetConfirmationPage()
{
  resetConfirmationYesSelected = true;
}

void SequencerEditor::moveCursorRightOnSequencePage()
{
  ++currentSequence;
  if (currentSequence >= sequencer->howManySequences())
    currentSequence = sequencer->howManySequences() - 1;
  if (currentStep >= sequencer->howManySteps(currentSequence))
    currentStep = sequencer->howManySteps(currentSequence) - 1;
}

void SequencerEditor::moveCursorRightOnStepPage()
{
  ++currentStepCol;
  const std::size_t maxCols = sequencer->howManyStepDataCols(currentSequence, currentStep);
  if (maxCols > 0 && currentStepCol >= maxCols)
    currentStepCol = maxCols - 1;
}

void SequencerEditor::moveCursorRightOnSequenceConfigPage()
{
  ++currentSequence;
  if (currentSequence >= sequencer->howManySequences())
    currentSequence = sequencer->howManySequences() - 1;
  if (currentStep >= sequencer->howManySteps(currentSequence))
    currentStep = sequencer->howManySteps(currentSequence) - 1;
}

void SequencerEditor::moveCursorRightOnMachinePage()
{
  if (!isMachineUiForCurrentSequence())
    return;

  moveMachineCursor(0, 1);
}

void SequencerEditor::moveCursorRightOnResetConfirmationPage()
{
  resetConfirmationYesSelected = false;
}

void SequencerEditor::moveCursorUpOnSequencePage()
{
  if (currentStep == 0)
    return;

  --currentStep;
}

void SequencerEditor::moveCursorUpOnStepPage()
{
  if (currentStepRow == 0)
    return;

  --currentStepRow;
}

void SequencerEditor::moveCursorUpOnSequenceConfigPage()
{
  if (currentSeqParam == 0)
    return;

  --currentSeqParam;
}

void SequencerEditor::moveCursorUpOnMachinePage()
{
  if (!isMachineUiForCurrentSequence())
    return;

  moveMachineCursor(-1, 0);
}

void SequencerEditor::moveCursorUpOnResetConfirmationPage()
{
  resetConfirmationYesSelected = true;
}

void SequencerEditor::moveCursorDownOnSequencePage()
{
  ++currentStep;
  if (currentStep >= sequencer->howManySteps(currentSequence))
    currentStep = sequencer->howManySteps(currentSequence) - 1;
}

void SequencerEditor::moveCursorDownOnStepPage()
{
  ++currentStepRow;
  const std::size_t rowsInStep = sequencer->howManyStepDataRows(currentSequence, currentStep);
  if (rowsInStep > 0 && currentStepRow >= rowsInStep)
    currentStepRow = rowsInStep - 1;
}

void SequencerEditor::moveCursorDownOnSequenceConfigPage()
{
  ++currentSeqParam;
  const std::size_t max = sequencer->getSeqConfigSpecs().size();
  if (max == 0)
    currentSeqParam = 0;
  else if (currentSeqParam >= max)
    currentSeqParam = max - 1;
}

void SequencerEditor::moveCursorDownOnMachinePage()
{
  if (!isMachineUiForCurrentSequence())
    return;

  moveMachineCursor(1, 0);
}

void SequencerEditor::moveCursorDownOnResetConfirmationPage()
{
  resetConfirmationYesSelected = false;
}

void SequencerEditor::addRowOnSequencePage()
{
  sequencer->extendSequence(getCurrentSequence());
}

void SequencerEditor::addRowOnStepPage()
{
  std::vector<std::vector<double>> data = sequencer->getStepData(currentSequence, currentStep);
  std::vector<double> newRow(data[0].size(), 0.0);
  newRow[Step::cmdInd] = sequencer->getSequence(currentSequence)->getMachineType();
  data.push_back(newRow);
  writeStepData(data);
}

void SequencerEditor::addRowOnMachinePage()
{
  if (isMachineUiForCurrentSequence())
    machineAdjustCurrentCell(-1);
}

void SequencerEditor::removeRowOnSequencePage()
{
  sequencer->shrinkSequence(getCurrentSequence());
  if (currentStep >= sequencer->howManySteps(currentSequence))
    currentStep = sequencer->howManySteps(currentSequence) - 1;
}

void SequencerEditor::removeRowOnStepPage()
{
  std::vector<std::vector<double>> data = sequencer->getStepData(currentSequence, currentStep);
  if (data.size() > 1)
  {
    data.pop_back();
    writeStepData(data);
  }

  const std::size_t rowsInStep = data.size();
  if (rowsInStep > 0 && currentStepRow >= rowsInStep)
    currentStepRow = rowsInStep - 1;
}

void SequencerEditor::clickOnSequencePage()
{
  const auto length = sequencer->getSequence(currentSequence)->getLength();
  for (std::size_t i = 0; i < length; ++i)
    sequencer->toggleStepActive(currentSequence, i);
}

void SequencerEditor::clickOnStepPage()
{
  sequencer->toggleStepActive(currentSequence, currentStep);
}

void SequencerEditor::clickOnMachinePage()
{
  if (isMachineUiForCurrentSequence())
    machineActivateCurrentCell();
}

void SequencerEditor::clickOnResetConfirmationPage()
{
  if (resetConfirmationYesSelected && resetConfirmationHandler){
    DBG("SequencerEditor is about to call the resetConfirmationHandler  ");
    resetConfirmationHandler();
  }

  gotoSequencePage();
}

void SequencerEditor::resetOnSequencePage()
{
  const auto rowCount = sequencer->howManyStepDataRows(currentSequence, currentStep);
  for (std::size_t row = 0; row < rowCount; ++row)
    sequencer->resetStepRow(currentSequence, currentStep, row);
}

void SequencerEditor::resetOnStepPage()
{
  sequencer->resetStepRow(currentSequence, currentStep, currentStepRow);
}

void SequencerEditor::resetOnResetConfirmationPage()
{
  gotoSequencePage();
}

void SequencerEditor::incrementOnStepPage()
{
  sequencer->incrementStepDataAt(currentSequence, currentStep, currentStepRow, currentStepCol);
}

void SequencerEditor::incrementOnSequenceConfigPage()
{
  sequencer->incrementSeqParam(currentSequence, currentSeqParam);
}

void SequencerEditor::incrementOnMachinePage()
{
  if (isMachineUiForCurrentSequence())
    machineAdjustCurrentCell(1);
}

void SequencerEditor::decrementOnStepPage()
{
  sequencer->decrementStepDataAt(currentSequence, currentStep, currentStepRow, currentStepCol);
}

void SequencerEditor::decrementOnSequenceConfigPage()
{
  sequencer->decrementSeqParam(currentSequence, currentSeqParam);
}

void SequencerEditor::decrementOnMachinePage()
{
  if (isMachineUiForCurrentSequence())
    machineAdjustCurrentCell(-1);
}

/** move the cursor to a specific sequence*/
void SequencerEditor::setCurrentSequence(int seq)
{
  if (seq < 0)
    currentSequence = 0;
  else
    currentSequence = static_cast<std::size_t>(seq);
}
/** move the cursor to a specific step*/
void SequencerEditor::setCurrentStep(int step)
{
  if (step < 0)
    currentStep = 0;
  else
    currentStep = static_cast<std::size_t>(step);
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
  const auto stepCount = sequencer->howManySteps(currentSequence);
  for (std::size_t i = 0; i < stepCount; ++i)
  {
    stepData[0] = data[i % data.size()]; // wrap it around :)
    sequencer->setStepData(currentSequence, currentSequence, stepData);
  }
}

Sequencer* SequencerEditor::getSequencerImpl() const
{
  return dynamic_cast<Sequencer*>(sequencer);
}

void SequencerEditor::requestStringRefresh()
{
  if (auto* impl = getSequencerImpl())
    impl->requestStrUpdate();
}

std::optional<double> SequencerEditor::lookupKeyboardMidiNote(char key) const
{
  const auto noteMap = MachineUtilsAbs::getKeyboardToMidiNotes(0);
  const auto it = noteMap.find(key);
  if (it == noteMap.end())
    return std::nullopt;
  return it->second;
}

void SequencerEditor::previewEnteredNote(double midiNote)
{
  if (isSequencerPlaying(sequencer))
    return;

  const size_t seqIndex = getCurrentSequence();
  const size_t stepIndex = getCurrentStep();
  const size_t rowIndex = getCurrentStepRow();
  if (Sequence* sequence = sequencer->getSequence(seqIndex))
  {
    auto data = sequence->getStepData(stepIndex);
    const size_t safeRow = rowIndex < data.size() ? rowIndex : 0;

    machineLearnNote(static_cast<int>(midiNote));
    auto context = sequence->getReadOnlyContext();
    bool useDefaults = data.empty();
    if (data.empty())
    {
      data.resize(1);
      data[0].assign(Step::maxInd + 1, 0.0);
    }
    if (data[safeRow].size() < Step::maxInd + 1)
      data[safeRow].resize(Step::maxInd + 1, 0.0);

    if (!useDefaults && std::abs(data[safeRow][Step::noteInd]) < std::numeric_limits<double>::epsilon())
      useDefaults = true;

    if (useDefaults)
    {
      data[safeRow][Step::cmdInd] = context.machineType;
      const Command& cmd = CommandProcessor::getCommand(context.machineType);
      for (std::size_t i = 0; i < cmd.parameters.size() && i < Step::maxInd; ++i)
        data[safeRow][i + 1] = cmd.parameters[i].defaultValue;
    }

    data[safeRow][Step::noteInd] = midiNote;
    data[safeRow][Step::probInd] = 1.0;
    context.triggerProbability = 1.0;
    CommandProcessor::executeCommand(data[safeRow][Step::cmdInd], &data[safeRow], &context);
  }
}

void SequencerEditor::clampStepCursorToCurrentStep()
{
  const std::size_t maxRows = sequencer->howManyStepDataRows(currentSequence, currentStep);
  if (maxRows > 0 && currentStepRow >= maxRows)
    currentStepRow = maxRows - 1;

  const std::size_t maxCols = sequencer->howManyStepDataCols(currentSequence, currentStep);
  if (maxCols > 0 && currentStepCol >= maxCols)
    currentStepCol = maxCols - 1;
}

void SequencerEditor::gotoSequenceConfigPage()
{
  setEditMode(SequencerEditorMode::configuringSequence);
}

void SequencerEditor::gotoMachineConfigPage()
{
  setEditMode(SequencerEditorMode::machineConfig);
  machineEditMode = false;
}

void SequencerEditor::gotoSequencePage()
{
  setEditMode(SequencerEditorMode::selectingSeqAndStep);
}

void SequencerEditor::gotoStepPage()
{
  setEditMode(SequencerEditorMode::editingStep);
  clampStepCursorToCurrentStep();
}

void SequencerEditor::gotoResetConfirmationPage()
{
  setEditMode(SequencerEditorMode::resetConfirmation);
  resetConfirmationYesSelected = true;
}

bool SequencerEditor::isResetConfirmationYesSelected() const
{
  return resetConfirmationYesSelected;
}

void SequencerEditor::togglePlayback()
{
  if (auto* impl = getSequencerImpl())
  {
    CommandProcessor::sendAllNotesOff();
    if (impl->isPlaying())
      impl->stop();
    else
    {
      impl->rewindAtNextZero();
      impl->play();
    }
  }
}

void SequencerEditor::rewindTransport()
{
  if (auto* impl = getSequencerImpl())
  {
    CommandProcessor::sendAllNotesOff();
    impl->rewindAtNextZero();
  }
}

void SequencerEditor::toggleArmCurrentSequence()
{
  setArmedSequence(getCurrentSequence());
}

void SequencerEditor::toggleMuteCurrentSequence()
{
  if (auto* impl = getSequencerImpl())
    impl->toggleSequenceMute(getCurrentSequence());
}

bool SequencerEditor::handleNoteKey(char key)
{
  if (getCurrentPage() == SequencerEditorPage::resetConfirmation)
    return false;

  const auto midiNote = lookupKeyboardMidiNote(key);
  if (!midiNote.has_value())
    return false;

  const double note = midiNote.value() + (12 * getCurrentOctave());
  previewEnteredNote(note);
  enterStepData(midiNote.value(), Step::noteInd);
  return true;
}

void SequencerEditor::requestTrackerReset()
{
  gotoResetConfirmationPage();
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
    this->armedSequence = SequencerAbs::notArmed;
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
  this->armedSequence = SequencerAbs::notArmed;
}

bool SequencerEditor::isArmedForLiveMIDI()
{
  if (this->armedSequence == SequencerAbs::notArmed)
    return false;
  return true;
}

bool SequencerEditor::isMachineUiForCurrentSequence() const
{
  if (sequencer == nullptr)
    return false;
  const auto* sequence = sequencer->getSequence(currentSequence);
  if (sequence == nullptr)
    return false;
  const auto machineType = static_cast<CommandType>(static_cast<std::size_t>(sequence->getMachineType()));
  return machineType == CommandType::Sampler || machineType == CommandType::Arpeggiator;
}

std::size_t SequencerEditor::getActiveMachineIndex(CommandType type) const
{
  if (machineHost == nullptr)
    return 0;
  const auto count = machineHost->getMachineCount(type);
  if (count == 0)
    return 0;
  const auto* sequence = sequencer->getSequence(currentSequence);
  if (sequence == nullptr)
    return 0;
  const int machineId = static_cast<int>(sequence->getMachineId());
  const std::size_t safeId = machineId < 0 ? 0u : static_cast<std::size_t>(machineId);
  return safeId % count;
}

MachineInterface* SequencerEditor::getActiveMachine(CommandType type) const
{
  if (machineHost == nullptr)
    return nullptr;
  return machineHost->getMachine(type, getActiveMachineIndex(type));
}

void SequencerEditor::refreshMachineStateForCurrentSequence()
{
  if (!isMachineUiForCurrentSequence() || machineHost == nullptr)
  {
    machineCells.assign(1, std::vector<UIBox>(1));
    return;
  }
  const auto* sequence = sequencer->getSequence(currentSequence);
  if (sequence == nullptr)
  {
    machineCells.assign(1, std::vector<UIBox>(1));
    return;
  }

  const auto machineType = static_cast<CommandType>(static_cast<std::size_t>(sequence->getMachineType()));
  auto* machine = getActiveMachine(machineType);
  if (machine == nullptr)
  {
    machineCells.assign(1, std::vector<UIBox>(1));
    return;
  }

  MachineUiContext context;
  context.disableLearning = isSequencerPlaying(sequencer);
  machineCells = machine->getUIBoxes(context);
  rebuildMachineCells();
}

const std::vector<std::vector<UIBox>>& SequencerEditor::getMachineCells() const
{
  return machineCells;
}

void SequencerEditor::machineAddEntry()
{
  if (!isMachineUiForCurrentSequence())
    return;
  const auto* sequence = sequencer->getSequence(currentSequence);
  if (sequence == nullptr)
    return;
  const auto machineType = static_cast<CommandType>(static_cast<std::size_t>(sequence->getMachineType()));
  if (auto* machine = getActiveMachine(machineType))
    machine->addEntry();
}

void SequencerEditor::machineRemoveEntry()
{
  if (!isMachineUiForCurrentSequence())
    return;
  if (machineCursorRow == 0)
    return;
  const auto* sequence = sequencer->getSequence(currentSequence);
  if (sequence == nullptr)
    return;
  const auto machineType = static_cast<CommandType>(static_cast<std::size_t>(sequence->getMachineType()));
  if (auto* machine = getActiveMachine(machineType))
    machine->removeEntry(static_cast<int>(machineCursorRow - 1));
}

void SequencerEditor::machineActivateCurrentCell()
{
  if (!isMachineUiForCurrentSequence())
    return;
  if (machineCells.empty() || machineCells[0].empty())
    return;
  if (machineCursorCol >= machineCells.size() || machineCursorRow >= machineCells[machineCursorCol].size())
    return;

  const auto& cell = machineCells[machineCursorCol][machineCursorRow];
  if (cell.onActivate)
    cell.onActivate();
}

void SequencerEditor::machineLearnNote(int midiNote)
{
  if (!isMachineUiForCurrentSequence())
    return;
  const auto* sequence = sequencer->getSequence(currentSequence);
  if (sequence == nullptr)
    return;
  const auto machineType = static_cast<CommandType>(static_cast<std::size_t>(sequence->getMachineType()));
  if (auto* machine = getActiveMachine(machineType))
    machine->applyLearnedNote(midiNote);
}

void SequencerEditor::machineAdjustCurrentCell(int direction)
{
  if (!isMachineUiForCurrentSequence())
    return;
  if (machineCells.empty() || machineCells[0].empty())
    return;
  if (machineCursorCol >= machineCells.size() || machineCursorRow >= machineCells[machineCursorCol].size())
    return;

  const auto& cell = machineCells[machineCursorCol][machineCursorRow];
  if (cell.onAdjust)
  {
    machineEditCol = machineCursorCol;
    machineEditRow = machineCursorRow;
    cell.onAdjust(direction);
  }
}

void SequencerEditor::rebuildMachineCells()
{
  if (machineCells.empty() || machineCells[0].empty())
  {
    machineCells.assign(1, std::vector<UIBox>(1));
    return;
  }

  const std::size_t rows = machineCells[0].size();
  const std::size_t cols = machineCells.size();
  machineCursorRow = std::min(machineCursorRow, rows - 1);
  machineCursorCol = std::min(machineCursorCol, cols - 1);
  machineEditRow = std::min(machineEditRow, rows - 1);
  machineEditCol = std::min(machineEditCol, cols - 1);

  for (std::size_t col = 0; col < cols; ++col)
  {
    for (std::size_t row = 0; row < rows; ++row)
    {
      auto& cell = machineCells[col][row];
      cell.isSelected = (row == machineCursorRow && col == machineCursorCol);
      cell.isEditing = (machineEditMode && cell.isSelected && row == machineEditRow && col == machineEditCol);
      if (cell.kind == UIBox::Kind::None)
        cell.isDisabled = true;
    }
  }
}

void SequencerEditor::moveMachineCursor(int deltaRow, int deltaCol)
{
  if (machineCells.empty() || machineCells[0].empty())
    return;

  const int maxRow = static_cast<int>(machineCells[0].size()) - 1;
  const int maxCol = static_cast<int>(machineCells.size()) - 1;

  const int nextRow = juce::jlimit(0, maxRow, static_cast<int>(machineCursorRow) + deltaRow);
  const int nextCol = juce::jlimit(0, maxCol, static_cast<int>(machineCursorCol) + deltaCol);

  machineCursorRow = static_cast<std::size_t>(nextRow);
  machineCursorCol = static_cast<std::size_t>(nextCol);
  rebuildMachineCells();
}
