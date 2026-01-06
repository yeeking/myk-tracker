#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "UIBox.h"

// todo: remove this and just include the juce header instead 
namespace juce
{
class var;
}

class Sequence;
enum class SequenceType;
struct Parameter;

// Interface for routing sampler actions from the editor to the host processor.
class SamplerHost
{
public:
  virtual ~SamplerHost() = default;
  virtual std::size_t getSamplerCount() const = 0;
  virtual juce::var getSamplerState(std::size_t samplerIndex) const = 0;
  virtual void samplerAddPlayer(std::size_t samplerIndex) = 0;
  virtual void samplerRemovePlayer(std::size_t samplerIndex, int playerId) = 0;
  virtual void samplerRequestLoad(std::size_t samplerIndex, int playerId) = 0;
  virtual void samplerTrigger(std::size_t samplerIndex, int playerId) = 0;
  virtual void samplerSetRange(std::size_t samplerIndex, int playerId, int low, int high) = 0;
  virtual void samplerSetGain(std::size_t samplerIndex, int playerId, float gain) = 0;
};

// Abstract interface for editor-facing sequencer access.
class SequencerAbs{
  public:
    virtual ~SequencerAbs() = default;
    /** armed (MIDI recording) channel will be set to this if nothing is armed */
    const static std::size_t notArmed{4096};

    virtual std::size_t howManySequences() const = 0;
    virtual std::size_t howManySteps(std::size_t sequence) const = 0;
    virtual std::size_t getCurrentStep(std::size_t sequence) const = 0;
    virtual SequenceType getSequenceType(std::size_t sequence) const = 0;
    virtual Sequence* getSequence(std::size_t sequence) = 0;
    virtual void setSequenceType(std::size_t sequence, SequenceType type) = 0;
    virtual void setStepData(std::size_t sequence, std::size_t step, std::vector<std::vector<double>> data) = 0;
    virtual std::vector<std::vector<double>> getStepData(std::size_t sequence, std::size_t step) = 0;
    virtual double getStepDataAt(std::size_t seq, std::size_t step, std::size_t row, std::size_t col) = 0;
    virtual void setStepDataAt(std::size_t sequence, std::size_t step, std::size_t row, std::size_t col, double value) = 0;
    virtual void resetStepRow(std::size_t sequence, std::size_t step, std::size_t row) = 0;
    virtual std::size_t howManyStepDataRows(std::size_t seq, std::size_t step) = 0;
    virtual std::size_t howManyStepDataCols(std::size_t seq, std::size_t step) = 0;
    virtual void setStepDataToDefault(std::size_t sequence, std::size_t step, std::size_t row, std::size_t col) = 0;
    virtual void extendSequence(std::size_t sequence) = 0;
    virtual void shrinkSequence(std::size_t sequence) = 0;
    virtual void incrementStepDataAt(std::size_t sequence, std::size_t step, std::size_t row, std::size_t col) = 0;
    virtual void decrementStepDataAt(std::size_t sequence, std::size_t step, std::size_t row, std::size_t col) = 0;
    virtual std::vector<Parameter>& getSeqConfigSpecs() = 0;
    virtual void incrementSeqParam(std::size_t seq, std::size_t paramIndex) = 0;
    virtual void decrementSeqParam(std::size_t seq, std::size_t paramIndex) = 0;
    virtual void toggleStepActive(std::size_t sequence, std::size_t step) = 0;
};

/**
 * Top level modes that dictate the main UI output
 */
enum class SequencerEditorMode
{
  selectingSeqAndStep,
  // settingSeqLength,// deprecate as set length happens in selectedSeqAndStep mode now
  configuringSequence,
  editingStep,
  machineConfig
};

/**
 * Dictates which item in a sub page we are editing
 **/
enum class SequencerEditorSubMode
{
  editCol1,
  editCol2,
  editCol3
};

/** Represents an editor for a sequencer, which allows stateful edit operations to be applied
 * to sequences. For example, select sequemce, select step, enter data
 * Used to build editing interfaces for a sequencer
 */
// State machine for sequencer editing and sampler configuration.
class SequencerEditor
{
public:
  SequencerEditor(SequencerAbs *_sequencer);
  void setSequencer(SequencerAbs *sequencer);
  void setSamplerHost(SamplerHost *host);
  SequencerAbs *getSequencer();
  /** resets editor, e.g. when changing sequence*/
  void resetCursor();
  /** returns current major edit mode which is a SequencerEditorMode*/
  SequencerEditorMode getEditMode() const;
  /** returns current minor edit mode which is a SequencerEditorSubMode*/
  SequencerEditorSubMode getEditSubMode() const;
  /** directly set major edit mode */
  void setEditMode(SequencerEditorMode mode);
    // -> enterMidiNoteData
    // -> enterNumberData
    // -> enterLengthData 

  void enterStepData(double value, int column, bool applyOctave = true);

  /** cycle through the edit modes in the sequence:
   * settingSeqLength (start mode)
   * selectingSeqAndStep
   * editingStep
   */
  void cycleEditMode();
  /** depending on the mode, whoops bad coupling again!
      cycles the condition of the thing under the cursor */
  void cycleAtCursor();
  /** mode dependent reset function. Might reset */
  void resetAtCursor();
  /**  Go into edit mode for either the sequence or step*/
  void enterAtCursor();
  /** Tell the editor the user entered note data. The incoming note
   *  value is assumed to be in the range 0-127 */
  void enterDataAtCursor(double value);
  /** whack a note into the sent sequence at its current step  */
  void insertNoteAtTickPos(size_t sequence, int channel, int note, int velocity);      

  /** moves the editor cursor up.
   * If in selectingSeqAndStep mode, steps through the sequenbces, wrapping at the top
   * if in editingStep mode, edits the*/
  void moveCursorUp();
  void moveCursorDown();
  void moveCursorLeft();
  void moveCursorRight();
  /** add one to current step */
  void nextStep();
  /** Increase the octave offset applied when entering notes  */
  void incrementOctave();
  /** decrease the octave offset applied when entering notes  */
  void decrementOctave();
  /**add a row: might add a new step to current track or add a new row to a step's data */
  void addRow();
  /**remove a row: might remove a new step to current track or remove a new row to a step's data */
  void removeRow();
  /** increase the value at the current cursor position, e.g. increasing note number */
  void incrementAtCursor();
  /** decrease the value at the current cursor position, e.g. increasing note number */
  void decrementAtCursor();
  /** enter sequence configuration page */
  void gotoSequenceConfigPage();
  /** enter machine configuration page */
  void gotoMachineConfigPage();

  void refreshSamplerStateForCurrentSequence();
  const std::vector<std::vector<UIBox>> &getSamplerCells() const;
  void samplerAddPlayer();
  void samplerRemovePlayer();
  void samplerActivateCurrentCell();
  void samplerAdjustCurrentCell(int direction);
  void samplerCancelEdit();
  bool isSamplerEditing() const;
  
  static SequencerEditorSubMode cycleSubModeLeft(SequencerEditorSubMode subMode);
  static SequencerEditorSubMode cycleSubModeRight(SequencerEditorSubMode subMode);

  /** decreas the sent step's data
   * based on current edit mode and edit sub mode
   */
  void decrementStepData(std::vector<std::vector<double>> &data, SequenceType seqType);

  /** increase the sent step's data
   * based on current edit mode and edit sub mode
   */
  void incrementStepData(std::vector<std::vector<double>> &data, SequenceType seqType);
  /** increase the value of the seq param relating to the
   * current subMode
   */
  void incrementSeqConfigParam();

  /** decrease the value of the seq param relating to the
   * current subMode
   */
  void decrementSeqConfigParam();

  void incrementChannel();
  void decrementChannel();
  void incrementTicksPerStep();
  void decrementTicksPerStep();
  static void nextSequenceType(SequencerAbs *seqr, unsigned int sequence);
  /** returns the index of the sequence that the editor is currently focused on*/
  size_t getCurrentSequence() const;
  /** returns the index of the step that the editor is currently focused on*/
  size_t getCurrentStep() const;
  /** which data point in a step are we editing */
  size_t getCurrentStepRow() const;
  /** which data point in a step are we editing */
  size_t getCurrentStepCol() const;
  /** which seq param index are we editing? */
  size_t getCurrentSeqParam() const; 
  /** returns the current edit octave */
  double getCurrentOctave() const;
  /** move the cursor to a specific sequence*/
  void setCurrentSequence(int seq);
  /** move the cursor to a specific step*/
  void setCurrentStep(int step);
  /** write the sent data to the current step and sequence */
  void writeStepData(std::vector<std::vector<double>> data);
  /** write the sent data to the sequence at 'currentSequence' - 1D data version for simple one value per step -style sequences*/
  void writeSequenceData(std::vector<std::vector<double>> data);
  /** write the sent data to a sequence - 1D data version */
  // void writeSequenceData(std::vector<std::vector<double>> data);
  /** arm a sequence for live midi recording */
  void setArmedSequence(const size_t sequence);
  /** un-arm a sequence  */
  void unarmSequence();
  /** which sequence is armed?  */
  size_t getArmedSequence();
  /** return true if you have armed a sequence  */
  bool isArmedForLiveMIDI();
  
private:
  struct SamplerPlayerState
  {
    int id = 0;
    int midiLow = 36;
    int midiHigh = 60;
    float gain = 1.0f;
    bool isPlaying = false;
    float vuDb = -60.0f;
    std::string status;
    std::string fileName;
  };

  bool isSamplerMachineForCurrentSequence() const;
  std::size_t getActiveSamplerIndex() const;
  void rebuildSamplerCells();
  void moveSamplerCursor(int deltaRow, int deltaCol);
  void adjustSamplerEditValue(int direction);

  SequencerAbs *sequencer;
  SamplerHost *samplerHost = nullptr;
  /** which sequence*/
  size_t currentSequence;
  /** which step */
  size_t currentStep;
  /** which row in a step */
  size_t currentStepRow;
  /** which data point inside a step*/
  size_t currentStepCol;
  /** which sequence param are you editing?*/
  size_t currentSeqParam; 
  /** one sequence can be armed for live MIDI recording */
  size_t armedSequence;

  SequencerEditorMode editMode;
  SequencerEditorSubMode editSubMode;
  double stepIncrement;
  double octave;
  std::vector<SamplerPlayerState> samplerPlayers;
  std::vector<float> samplerGlowLevels;
  std::vector<std::vector<UIBox>> samplerCells;
  std::size_t samplerCursorRow = 0;
  std::size_t samplerCursorCol = 0;
  bool samplerEditMode = false;
  std::size_t samplerEditCol = 0;
  std::size_t samplerEditRow = 0;
};
