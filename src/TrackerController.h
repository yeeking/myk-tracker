#pragma once 

#include "Sequencer.h"
#include "SequencerEditor.h"
#include "ClockAbs.h"
#include "MachineUtilsAbs.h"

/** This class provides high level control over the tracker. Includes 'sequencer-level' things  */
class TrackerController{
  public:
    /** Creates a high-level controller for the shared tracker state. */
    TrackerController( Sequencer* _sequencer, ClockAbs* _clockAbs, SequencerEditor* _seqEditor);
/** Returns tracker controls and status information as a grid of strings. */
    std::vector<std::vector<std::string>> getControlPanelAsGridOfStrings();
/** Stops tracker playback. */
    void stopPlaying();
    /** Starts tracker playback. */
    void startPlaying();
    /** Sets the global tempo in beats per minute. */
    void setBPM(unsigned int bpm);
    /** Increments the global tempo by one BPM. */
    void incrementBPM();
    /** Decrements the global tempo by one BPM. */
    void decrementBPM();
    
    /** Loads tracker state from disk. */
    void loadTrack(const std::string& fname);
    /** Saves tracker state to disk. */
    void saveTrack(const std::string& fname);
  private:
    /** Owned sequencer model controlled by this wrapper. */
    Sequencer* sequencer; 
    /** Shared transport clock used for tempo control. */
    ClockAbs* clock;
    /** Shared editor state that reflects controller actions. */
    SequencerEditor* seqEditor;
    // SequencerEditor* seqEditor; 
};
