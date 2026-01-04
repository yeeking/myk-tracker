#pragma once

#include "Sequencer.h"

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
class SequencerEditor
{
public:
  SequencerEditor(Sequencer *sequencer);
  void setSequencer(Sequencer *sequencer);
  Sequencer *getSequencer();
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
  static void nextSequenceType(Sequencer *seqr, unsigned int sequence);
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
  Sequencer *sequencer;
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
};
