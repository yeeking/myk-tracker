#pragma once 

#include "Sequencer.h"
#include "SequencerEditor.h"
#include "ClockAbs.h"
#include "MachineUtilsAbs.h"

/** This class provides high level control over the tracker. Includes 'sequencer-level' things  */
class TrackerController{
  public:
    TrackerController( Sequencer* _sequencer, ClockAbs* _clockAbs, SequencerEditor* _seqEditor);
/** return the tracker controls and status info as a grid of strings */
    std::vector<std::vector<std::string>> getControlPanelAsGridOfStrings();
/** stops playback */
    void stopPlaying();
    void startPlaying();
    /** directly set BPM */
    void setBPM(unsigned int bpm);
    /** add 1 to bpm*/
    void incrementBPM();
    /** subtract one from bpm */
    void decrementBPM();
    
    void loadTrack(const std::string& fname);
    void saveTrack(const std::string& fname);
  private:
    /** used to get i*/
    Sequencer* sequencer; 
    ClockAbs* clock;
    SequencerEditor* seqEditor;
    // SequencerEditor* seqEditor; 
};
