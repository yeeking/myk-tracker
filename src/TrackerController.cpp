#include "TrackerController.h"

TrackerController::TrackerController( Sequencer* _sequencer, 
                                        ClockAbs* _clockAbs, 
                                        SequencerEditor* _seqEditor)
                                        : sequencer{_sequencer},
                                        clock{_clockAbs}, 
                                        seqEditor{_seqEditor}
{

}

std::vector<std::vector<std::string>> TrackerController::getControlPanelAsGridOfStrings()
{
    std::string cursorStatus = 
        std::to_string(seqEditor->getCurrentSequence()) + ":" 
        + std::to_string(seqEditor->getCurrentStep()) + "["
        + std::to_string(sequencer->howManySteps(seqEditor->getCurrentSequence())) + "]";
    
    // add the info about the current if editing a step 
    if (seqEditor->getEditMode() == SequencerEditorMode::editingStep){
        int rowsInStep = sequencer->howManyStepDataRows(seqEditor->getCurrentSequence(), seqEditor->getCurrentStep());
        cursorStatus += ":" + std::to_string(seqEditor->getCurrentStepRow()) + "[" + std::to_string(rowsInStep) + "]";
    }

    std::string viewMode;
    switch(seqEditor->getEditMode()){
        case SequencerEditorMode::configuringSequence:
            viewMode = "Conf";
            break;
        case SequencerEditorMode::editingStep:
            viewMode = "Step";
            break;
        case SequencerEditorMode::selectingSeqAndStep:
            viewMode = "Seq";
            break;
        default:
            break; 
             
    }
    std::string playMode; 
    

    std::vector<std::vector<std::string>> buttons = {{cursorStatus}, {playMode}, {viewMode}};
    
    return buttons; 
}

void TrackerController::stopPlaying()
{

}
void TrackerController::startPlaying()
{

}
void TrackerController::setBPM(unsigned int bpm)
{

}

void TrackerController::loadTrack(const std::string& fname)
{

}
void TrackerController::saveTrack(const std::string& fname)
{

}