#include "SequencerEditor.h"
#include "MachineInterface.h"
#include "Sequencer.h"
#include "SequencerCommands.h"

#include <JuceHeader.h>
#include <algorithm>
#include <cmath>

namespace
{
bool showsStackSendLevel(CommandType type)
{
  return machineTraits(type).isAudioEffect;
}

std::string getStackMachineLabel(CommandType type)
{
  return machineTraits(type).shortLabel;
}

std::string formatStackLevelDb(float value)
{
  return juce::String(value, 0).toStdString();
}

// Stack send/return levels live in -60..+12 dB; map that onto [0,1] for the
// horizontal parameter fill bar.
float normalizeStackLevelDb(float db)
{
  constexpr float kMinDb = -60.0f;
  constexpr float kMaxDb = 12.0f;
  return juce::jlimit(0.0f, 1.0f, (db - kMinDb) / (kMaxDb - kMinDb));
}

// A machine cell is a cursor "stop" only when it is visible, enabled and has
// at least one interaction (adjust/insert/activate). Label, disabled and
// empty cells are passed over by cursor movement.
bool isMachineCellLandable(const UIBox& cell)
{
  return cell.kind != UIBox::Kind::None
      && !cell.isDisabled
      && (cell.onAdjust != nullptr || cell.onInsert != nullptr || cell.onActivate != nullptr);
}

UIBox* machineCellAt(std::vector<std::vector<UIBox>>& cells,
                     std::size_t row, std::size_t column)
{
  if (column >= cells.size() || row >= cells[column].size())
    return nullptr;
  return &cells[column][row];
}
}

bool SequencerEditor::isMachineUiForCurrentSequence() const
{
  return sequencer != nullptr && sequencer->getSequence(currentSequence) != nullptr;
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

std::vector<std::vector<UIBox>> SequencerEditor::buildMachineStackCells(std::size_t stackIndex)
{
  std::vector<std::vector<UIBox>> boxes(7);

  UIBox addCell;
  addCell.kind = UIBox::Kind::TrackerCell;
  addCell.text = "ADD";
  addCell.onActivate = [this, stackIndex]()
  {
    if (machineHost != nullptr)
      machineHost->addMachineToStack(stackIndex);
  };
  boxes[0].push_back(std::move(addCell));
  for (std::size_t col = 1; col < boxes.size(); ++col)
  {
    boxes[col].push_back(UIBox{});
    boxes[col][0].kind = UIBox::Kind::None;
    boxes[col][0].isDisabled = true;
  }

  if (machineHost == nullptr)
    return boxes;

  UIBox modCell;
  modCell.kind = UIBox::Kind::TrackerCell;
  modCell.text = "MOD";
  boxes[0].push_back(std::move(modCell));
  UIBox modEnabled;
  modEnabled.kind = UIBox::Kind::TrackerCell;
  modEnabled.text = machineHost->isModulationEnabledInStack(stackIndex) ? "ON" : "OFF";
  modEnabled.onActivate = [this, stackIndex]() { if (machineHost != nullptr) machineHost->toggleModulationEnabledInStack(stackIndex); };
  boxes[1].push_back(std::move(modEnabled));
  for (std::size_t col = 2; col < boxes.size(); ++col)
  {
    UIBox blank; blank.kind = UIBox::Kind::None; blank.isDisabled = true;
    boxes[col].push_back(std::move(blank));
  }

  auto stackTypes = machineHost->getMachineStackTypes(stackIndex);
  if (stackTypes.empty())
    stackTypes.push_back(CommandType::MidiNote);
  for (std::size_t slotIndex = 0; slotIndex < stackTypes.size(); ++slotIndex)
  {
    UIBox machineCell;
    machineCell.kind = UIBox::Kind::TrackerCell;
    machineCell.text = getStackMachineLabel(stackTypes[slotIndex]);
    machineCell.onAdjust = [this, stackIndex, slotIndex](int direction)
    {
      if (machineHost != nullptr)
        machineHost->cycleMachineTypeInStack(stackIndex, slotIndex, direction);
    };
    boxes[0].push_back(std::move(machineCell));

    UIBox enabledCell;
    enabledCell.kind = UIBox::Kind::TrackerCell;
    enabledCell.text = machineHost->isMachineEnabledInStack(stackIndex, slotIndex) ? "ON" : "OFF";
    enabledCell.onActivate = [this, stackIndex, slotIndex]()
    {
      if (machineHost != nullptr)
        machineHost->toggleMachineEnabledInStack(stackIndex, slotIndex);
    };
    boxes[1].push_back(std::move(enabledCell));

    UIBox sendCell;
    sendCell.kind = UIBox::Kind::TrackerCell;
    const bool hasSend = showsStackSendLevel(stackTypes[slotIndex]);
    sendCell.text = hasSend ? "S" + formatStackLevelDb(machineHost->getMachineSendLevelDbInStack(stackIndex, slotIndex)) : "";
    sendCell.isDisabled = !hasSend;
    if (hasSend)
    {
      sendCell.onAdjust = [this, stackIndex, slotIndex](int direction)
      {
        if (machineHost != nullptr)
          machineHost->adjustMachineSendLevelDbInStack(stackIndex, slotIndex, direction);
      };
      sendCell.hasValueScale = true;
      sendCell.valueNorm = normalizeStackLevelDb(machineHost->getMachineSendLevelDbInStack(stackIndex, slotIndex));
    }
    boxes[2].push_back(std::move(sendCell));

    UIBox returnCell;
    returnCell.kind = UIBox::Kind::TrackerCell;
    const bool hasReturn = machineHost->machineHasReturnLevelInStack(stackIndex, slotIndex);
    returnCell.text = hasReturn ? "R" + formatStackLevelDb(machineHost->getMachineReturnLevelDbInStack(stackIndex, slotIndex)) : "";
    returnCell.isDisabled = !hasReturn;
    if (hasReturn)
    {
      returnCell.onAdjust = [this, stackIndex, slotIndex](int direction)
      {
        if (machineHost != nullptr)
          machineHost->adjustMachineReturnLevelDbInStack(stackIndex, slotIndex, direction);
      };
      returnCell.hasValueScale = true;
      returnCell.valueNorm = normalizeStackLevelDb(machineHost->getMachineReturnLevelDbInStack(stackIndex, slotIndex));
    }
    boxes[3].push_back(std::move(returnCell));

    UIBox upCell;
    upCell.kind = UIBox::Kind::TrackerCell;
    upCell.text = slotIndex == 0 ? "" : "UP";
    upCell.isDisabled = slotIndex == 0;
    if (slotIndex > 0)
    {
      upCell.onActivate = [this, stackIndex, slotIndex]()
      {
        if (machineHost != nullptr)
          machineHost->moveMachineInStack(stackIndex, slotIndex, -1);
      };
    }
    boxes[4].push_back(std::move(upCell));

    UIBox downCell;
    downCell.kind = UIBox::Kind::TrackerCell;
    downCell.text = slotIndex + 1 >= stackTypes.size() ? "" : "DOWN";
    downCell.isDisabled = slotIndex + 1 >= stackTypes.size();
    if (slotIndex + 1 < stackTypes.size())
    {
      downCell.onActivate = [this, stackIndex, slotIndex]()
      {
        if (machineHost != nullptr)
          machineHost->moveMachineInStack(stackIndex, slotIndex, 1);
      };
    }
    boxes[5].push_back(std::move(downCell));

    UIBox deleteCell;
    deleteCell.kind = UIBox::Kind::TrackerCell;
    deleteCell.text = "DEL";
    deleteCell.onActivate = [this, stackIndex, slotIndex]()
    {
      if (machineHost != nullptr)
        machineHost->removeMachineFromStack(stackIndex, slotIndex);
    };
    boxes[6].push_back(std::move(deleteCell));
  }

  // Scope band: two dark rows reserved for the live oscilloscope trace that
  // the UI renders over the grid. Disabled (and callback-free) so the editor
  // cursor can never land on them (see isMachineCellLandable).
  for (int scopeRow = 0; scopeRow < 2; ++scopeRow)
  {
    for (std::size_t col = 0; col < boxes.size(); ++col)
    {
      UIBox scopeCell;
      scopeCell.kind = UIBox::Kind::TrackerCell;
      scopeCell.isDisabled = true;
      scopeCell.useCustomFillColour = true;
      scopeCell.customFillArgb = 0xFF0A0F14;
      if (col == 0 && scopeRow == 0)
        scopeCell.text = "OUT";
      boxes[col].push_back(std::move(scopeCell));
    }
  }

  return boxes;
}

std::vector<std::vector<UIBox>> buildMidiOutDetailCells(MachineHost* machineHost, std::size_t stackIndex)
{
  std::vector<std::vector<UIBox>> boxes(2, std::vector<UIBox>(1));

  boxes[0][0].kind = UIBox::Kind::TrackerCell;
  boxes[0][0].text = "CHAN";

  boxes[1][0].kind = UIBox::Kind::TrackerCell;
  const int midiChannel = machineHost != nullptr ? machineHost->getStackMidiOutputChannel(stackIndex) : 1;
  boxes[1][0].text = juce::String(midiChannel).paddedLeft('0', 2).toStdString();
  boxes[1][0].onAdjust = [machineHost, stackIndex](int direction)
  {
    if (machineHost != nullptr)
      machineHost->adjustStackMidiOutputChannel(stackIndex, direction);
  };

  return boxes;
}

std::optional<CommandType> SequencerEditor::getSelectedStackMachineType() const
{
  if (machineHost == nullptr)
    return std::nullopt;
  if (machineSelectedStackSlot == kModulationMatrixStackEntry)
    return CommandType::ModulationMatrix;
  const auto stackTypes = machineHost->getMachineStackTypes(getActiveMachineIndex(CommandType::Sampler));
  if (machineSelectedStackSlot >= stackTypes.size())
    return std::nullopt;
  return stackTypes[machineSelectedStackSlot];
}

void SequencerEditor::leaveMachineDetail()
{
  machineStackDetailMode = false;
  machineEditMode = false;
}

void SequencerEditor::refreshMachineStateForCurrentSequence()
{
  machineStateDirty = false;
  const std::size_t previousRows = (machineCells.empty() || machineCells[0].empty()) ? 0 : machineCells[0].size();
  // In the stack table the last two rows are the scope band, so the row the
  // cursor follows when rows are added is the last slot row (or the ADD row
  // when the stack has no slots yet).
  const std::size_t previousControlRow = (!machineStackDetailMode && previousRows >= 3)
    ? previousRows - 3
    : (previousRows > 0 ? previousRows - 1 : 0);
  const bool cursorWasOnControlRow = previousRows > 0 && machineCursorRow == previousControlRow;
  const bool editWasOnControlRow = previousRows > 0 && machineEditRow == previousControlRow;

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

  const std::size_t stackIndex = getActiveMachineIndex(CommandType::Sampler);
  if (!machineStackDetailMode)
  {
    machineCells = buildMachineStackCells(stackIndex);
  }
  else
  {
    const auto selectedType = getSelectedStackMachineType();
    if (!selectedType.has_value())
    {
      leaveMachineDetail();
      machineCells = buildMachineStackCells(stackIndex);
    }
    else if (selectedType.value() == CommandType::MidiNote)
    {
      machineCells = buildMidiOutDetailCells(machineHost, stackIndex);
    }
    else if (auto* machine = getActiveMachine(selectedType.value()))
    {
      MachineUiContext context;
      if (auto* implementation = dynamic_cast<Sequencer*>(sequencer))
        context.disableLearning = implementation->isPlaying();
      machineCells = machine->getUIBoxes(context);
      const int preferredRow = machine->consumePreferredCursorRow(machineCells);
      if (preferredRow >= 0 && !machineCells.empty())
      {
        machineCursorRow = static_cast<std::size_t>(juce::jlimit(0,
            static_cast<int>(machineCells[0].size()) - 1,
            preferredRow));
        machineCursorCol = 0;
        machineEditRow = machineCursorRow;
        machineEditCol = machineCursorCol;
      }
    }
    else
    {
      leaveMachineDetail();
      machineCells = buildMachineStackCells(stackIndex);
    }
  }
  const std::size_t newRows = (machineCells.empty() || machineCells[0].empty()) ? 0 : machineCells[0].size();
  if (cursorWasOnControlRow && newRows > previousRows)
    machineCursorRow += (newRows - previousRows);
  if (editWasOnControlRow && newRows > previousRows)
    machineEditRow += (newRows - previousRows);
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
  if (!machineStackDetailMode)
  {
    if (machineHost != nullptr)
      machineHost->addMachineToStack(getActiveMachineIndex(CommandType::Sampler));
  }
  else if (const auto selectedType = getSelectedStackMachineType(); selectedType.has_value())
    if (auto* machine = getActiveMachine(selectedType.value()))
      machine->addEntry();
  requestMachineStateRefresh();
}

void SequencerEditor::machineRemoveEntry()
{
  if (!isMachineUiForCurrentSequence())
    return;
  if (!machineStackDetailMode)
  {
    if (machineCursorRow > 1 && machineHost != nullptr)
      machineHost->removeMachineFromStack(getActiveMachineIndex(CommandType::Sampler), machineCursorRow - 2);
  }
  else if (const auto selectedType = getSelectedStackMachineType(); selectedType.has_value())
    if (auto* machine = getActiveMachine(selectedType.value()))
      machine->removeEntry(static_cast<int>(machineCursorRow - 1));
  requestMachineStateRefresh();
}

void SequencerEditor::machineActivateCurrentCell()
{
  if (!isMachineUiForCurrentSequence())
    return;
  const auto* cell = machineCellAt(machineCells, machineCursorRow, machineCursorCol);
  if (cell == nullptr)
    return;
  if (cell->onActivate)
  {
    cell->onActivate();
    requestMachineStateRefresh();
  }
}

bool SequencerEditor::machineInsertCurrentCell(double value)
{
  if (!isMachineUiForCurrentSequence())
    return false;
  auto* cell = machineCellAt(machineCells, machineCursorRow, machineCursorCol);
  if (cell == nullptr || !cell->onInsert)
    return false;

  machineEditCol = machineCursorCol;
  machineEditRow = machineCursorRow;
  cell->onInsert(value);
  requestMachineStateRefresh();
  return true;
}

bool SequencerEditor::machinePreviewCurrentCell()
{
  if (!isMachineUiForCurrentSequence())
    return false;
  const auto* cell = machineCellAt(machineCells, machineCursorRow, machineCursorCol);
  if (cell == nullptr || !cell->onPreview)
    return false;
  cell->onPreview();
  requestMachineStateRefresh();
  return true;
}

bool SequencerEditor::machineResetCurrentCell()
{
  if (!isMachineUiForCurrentSequence())
    return false;
  const auto* cell = machineCellAt(machineCells, machineCursorRow, machineCursorCol);
  if (cell == nullptr || !cell->onReset)
    return false;
  cell->onReset();
  requestMachineStateRefresh();
  return true;
}

bool SequencerEditor::machineHandleTextInput(char character)
{
  if (!isMachineUiForCurrentSequence() || !machineStackDetailMode)
    return false;

  const auto selectedType = getSelectedStackMachineType();
  if (!selectedType.has_value())
    return false;

  if (auto* machine = getActiveMachine(selectedType.value()))
  {
    if (machine->handleTextInput(character))
    {
      requestMachineStateRefresh();
      if (!machineCells.empty() && !machineCells[0].empty())
      {
        machineCursorRow = std::min<std::size_t>(1, machineCells[0].size() - 1);
        machineCursorCol = 0;
        machineEditRow = machineCursorRow;
        machineEditCol = machineCursorCol;
        rebuildMachineCells();
      }
      return true;
    }
  }

  return false;
}

bool SequencerEditor::machineHandleTextBackspace()
{
  if (!isMachineUiForCurrentSequence() || !machineStackDetailMode)
    return false;

  const auto selectedType = getSelectedStackMachineType();
  if (!selectedType.has_value())
    return false;

  if (auto* machine = getActiveMachine(selectedType.value()))
  {
    const int row = static_cast<int>(machineCursorRow);
    const int col = static_cast<int>(machineCursorCol);

    if (machine->clearCell(row, col) || machine->handleTextBackspace())
    {
      requestMachineStateRefresh();
      rebuildMachineCells();
      machineEditRow = machineCursorRow;
      machineEditCol = machineCursorCol;
      return true;
    }
  }

  return false;
}

bool SequencerEditor::machineShiftNoteCurrentCell(int semitones)
{
  if (!isMachineUiForCurrentSequence() || !machineStackDetailMode)
    return false;

  const auto selectedType = getSelectedStackMachineType();
  if (!selectedType.has_value())
    return false;

  if (auto* machine = getActiveMachine(selectedType.value()))
  {
    const int row = static_cast<int>(machineCursorRow);
    const int col = static_cast<int>(machineCursorCol);
    if (machine->shiftNoteAtCell(row, col, semitones))
    {
      requestMachineStateRefresh();
      rebuildMachineCells();
      machineEditRow = machineCursorRow;
      machineEditCol = machineCursorCol;
      return true;
    }
  }

  return false;
}

bool SequencerEditor::dismissCurrentTransientUi()
{
  if (!isMachineUiForCurrentSequence())
    return false;
  if (!machineStackDetailMode)
    return false;
  const auto selectedType = getSelectedStackMachineType();
  if (selectedType.has_value())
  {
    if (auto* machine = getActiveMachine(selectedType.value()))
      if (machine->dismissTransientUi())
      {
        requestMachineStateRefresh();
        return true;
      }
  }

  return false;
}

void SequencerEditor::machineAdjustCurrentCell(int direction)
{
  if (!isMachineUiForCurrentSequence())
    return;
  const auto* cell = machineCellAt(machineCells, machineCursorRow, machineCursorCol);
  if (cell == nullptr || !cell->onAdjust)
    return;
  machineEditCol = machineCursorCol;
  machineEditRow = machineCursorRow;
  cell->onAdjust(direction);
  requestMachineStateRefresh();
}

bool SequencerEditor::enterSelectedMachineDetail()
{
  if (getCurrentPage() != SequencerEditorPage::machine || machineStackDetailMode)
    return false;
  if (machineHost == nullptr)
    return false;

  const auto stackTypes = machineHost->getMachineStackTypes(getActiveMachineIndex(CommandType::Sampler));
  if (stackTypes.empty())
    return false;

  if (machineCursorRow == 1)
    machineSelectedStackSlot = kModulationMatrixStackEntry;
  else
  {
    const std::size_t slotIndex = machineCursorRow == 0 ? 0 : machineCursorRow - 2;
    if (slotIndex >= stackTypes.size())
      return false;
    machineSelectedStackSlot = slotIndex;
  }
  machineStackDetailMode = true;
  machineCursorRow = 0;
  machineCursorCol = 0;
  machineEditRow = 0;
  machineEditCol = 0;
  refreshMachineStateForCurrentSequence();
  return true;
}

bool SequencerEditor::enterMachineDetailFromAnywhere()
{
  if (machineHost == nullptr)
    return false;

  if (getCurrentPage() != SequencerEditorPage::machine)
  {
    gotoMachineConfigPage();
    machineCursorRow = 0;
    machineCursorCol = 0;
    machineEditRow = 0;
    machineEditCol = 0;
    refreshMachineStateForCurrentSequence();
  }

  if (machineStackDetailMode)
    return true;

  const auto stackTypes = machineHost->getMachineStackTypes(getActiveMachineIndex(CommandType::Sampler));
  if (stackTypes.empty())
    return false;

  if (machineCursorRow == 1)
    machineSelectedStackSlot = kModulationMatrixStackEntry;
  else
  {
    const std::size_t slotIndex = machineCursorRow == 0 ? 0 : machineCursorRow - 2;
    machineSelectedStackSlot = std::min(slotIndex, stackTypes.size() - 1);
  }
  machineStackDetailMode = true;
  machineCursorRow = 0;
  machineCursorCol = 0;
  machineEditRow = 0;
  machineEditCol = 0;
  refreshMachineStateForCurrentSequence();
  return true;
}

bool SequencerEditor::isEditingMachineDetail() const
{
  return machineStackDetailMode;
}

std::optional<CommandType> SequencerEditor::getFocusedMachineDetailType() const
{
  if (!machineStackDetailMode)
    return std::nullopt;
  return getSelectedStackMachineType();
}

std::optional<std::size_t> SequencerEditor::getFocusedMachineDetailSlot() const
{
  if (!machineStackDetailMode)
    return std::nullopt;
  if (machineSelectedStackSlot == kModulationMatrixStackEntry)
    return std::nullopt;
  return machineSelectedStackSlot;
}

bool SequencerEditor::machineWantsExclusiveKeyboardInput() const
{
  if (!machineStackDetailMode)
    return false;

  const auto selectedType = getSelectedStackMachineType();
  if (!selectedType.has_value())
    return false;

  if (auto* machine = getActiveMachine(selectedType.value()))
    return machine->wantsExclusiveKeyboardInput();

  return false;
}

bool SequencerEditor::cycleMachineDetailNext()
{
  if (!machineStackDetailMode || machineHost == nullptr)
    return false;
  const auto stackTypes = machineHost->getMachineStackTypes(getActiveMachineIndex(CommandType::Sampler));
  if (stackTypes.empty())
    return false;
  machineSelectedStackSlot = machineSelectedStackSlot == kModulationMatrixStackEntry ? 0 :
      (machineSelectedStackSlot + 1 >= stackTypes.size() ? kModulationMatrixStackEntry : machineSelectedStackSlot + 1);
  refreshMachineStateForCurrentSequence();
  return true;
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

  snapMachineCursorToLandable();

  for (std::size_t col = 0; col < cols; ++col)
  {
    for (std::size_t row = 0; row < machineCells[col].size(); ++row)
    {
      auto& cell = machineCells[col][row];
      cell.isSelected = (row == machineCursorRow && col == machineCursorCol);
      cell.isEditing = (machineEditMode && cell.isSelected && row == machineEditRow && col == machineEditCol);
      if (cell.kind == UIBox::Kind::None)
        cell.isDisabled = true;
    }
  }
}

void SequencerEditor::snapMachineCursorToLandable()
{
  if (machineCells.empty() || machineCells[0].empty())
    return;
  const std::size_t cols = machineCells.size();
  if (machineCursorCol >= cols || machineCursorRow >= machineCells[machineCursorCol].size())
    return;
  if (isMachineCellLandable(machineCells[machineCursorCol][machineCursorRow]))
    return;

  // Pick the nearest cell (by row distance, then column distance) that the
  // cursor is allowed to rest on.
  std::size_t bestRow = machineCursorRow;
  std::size_t bestCol = machineCursorCol;
  bool bestFound = false;
  for (std::size_t col = 0; col < cols; ++col)
  {
    for (std::size_t row = 0; row < machineCells[col].size(); ++row)
    {
      if (!isMachineCellLandable(machineCells[col][row]))
        continue;
      if (bestFound)
      {
        const std::size_t rowDiff = row > machineCursorRow ? row - machineCursorRow : machineCursorRow - row;
        const std::size_t colDiff = col > machineCursorCol ? col - machineCursorCol : machineCursorCol - col;
        const std::size_t bestRowDiff = bestRow > machineCursorRow ? bestRow - machineCursorRow : machineCursorRow - bestRow;
        const std::size_t bestColDiff = bestCol > machineCursorCol ? bestCol - machineCursorCol : machineCursorCol - bestCol;
        if (rowDiff > bestRowDiff || (rowDiff == bestRowDiff && colDiff >= bestColDiff))
          continue;
      }
      bestRow = row;
      bestCol = col;
      bestFound = true;
    }
  }

  if (bestFound && (bestRow != machineCursorRow || bestCol != machineCursorCol))
  {
    machineCursorRow = bestRow;
    machineCursorCol = bestCol;
    machineEditRow = bestRow;
    machineEditCol = bestCol;
  }
}

void SequencerEditor::moveMachineCursor(int deltaRow, int deltaCol)
{
  if (machineCells.empty() || machineCells[0].empty())
    return;

  const int maxCol = static_cast<int>(machineCells.size()) - 1;

  std::size_t nextRow = machineCursorRow;
  std::size_t nextCol = machineCursorCol;

  if (deltaRow != 0)
  {
    // Step through rows in the requested direction, skipping cells the
    // cursor is not allowed to rest on. If the target row has no usable cell
    // in this column (for example MOD's ON/OFF column above the ADD row), use
    // the closest usable cell on that row instead.
    const std::size_t colRows = machineCursorCol < machineCells.size()
        ? machineCells[machineCursorCol].size()
        : 0;
    for (int row = static_cast<int>(machineCursorRow) + deltaRow;
         row >= 0 && static_cast<std::size_t>(row) < colRows;
         row += deltaRow)
    {
      if (isMachineCellLandable(machineCells[machineCursorCol][static_cast<std::size_t>(row)]))
      {
        nextRow = static_cast<std::size_t>(row);
        break;
      }

      std::size_t nearestColumn = machineCursorCol;
      bool foundOnRow = false;
      for (std::size_t column = 0; column < machineCells.size(); ++column)
      {
        if (static_cast<std::size_t>(row) >= machineCells[column].size()
            || !isMachineCellLandable(machineCells[column][static_cast<std::size_t>(row)]))
          continue;
        if (!foundOnRow
            || (column > machineCursorCol ? column - machineCursorCol : machineCursorCol - column)
                < (nearestColumn > machineCursorCol ? nearestColumn - machineCursorCol : machineCursorCol - nearestColumn))
        {
          nearestColumn = column;
          foundOnRow = true;
        }
      }
      if (foundOnRow)
      {
        nextRow = static_cast<std::size_t>(row);
        nextCol = nearestColumn;
        break;
      }
    }
  }
  else if (deltaCol != 0)
  {
    for (int col = static_cast<int>(machineCursorCol) + deltaCol;
         col >= 0 && col <= maxCol;
         col += deltaCol)
    {
      if (machineCursorRow >= machineCells[static_cast<std::size_t>(col)].size())
        continue;
      if (isMachineCellLandable(machineCells[static_cast<std::size_t>(col)][machineCursorRow]))
      {
        nextCol = static_cast<std::size_t>(col);
        break;
      }
    }
  }

  machineCursorRow = nextRow;
  machineCursorCol = nextCol;
  rebuildMachineCells();

  if (machineStackDetailMode)
  {
    const auto selectedType = getSelectedStackMachineType();
    if (selectedType.has_value())
      if (auto* machine = getActiveMachine(selectedType.value()))
        machine->onCursorMoved(static_cast<int>(machineCursorRow), static_cast<int>(machineCursorCol));
  }
}

void SequencerEditor::dismissMachineTransientUiIfNeeded()
{
  const auto previousMode = editMode;
  if (previousMode == SequencerEditorMode::machineConfig)
  {
    dismissCurrentTransientUi();
    leaveMachineDetail();
  }
}
