#include "TrackerController.h"
#include <JuceHeader.h>

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
        std::size_t rowsInStep = sequencer->howManyStepDataRows(seqEditor->getCurrentSequence(),
                                                                seqEditor->getCurrentStep());
        cursorStatus += ":" + std::to_string(seqEditor->getCurrentStepRow()) + "[" + std::to_string(rowsInStep) + "]";
    }

    std::string viewMode;
    switch(seqEditor->getEditMode()){
        case SequencerEditorMode::configuringSequence:
            viewMode = "Conf";
            break;
        case SequencerEditorMode::machineConfig:
            viewMode = "Mach";
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
    std::string playMode = (clock->getCurrentTick() % 8) > 3 ? "+" : "-"; 
    std::string bpm = "@" + std::to_string(static_cast<int>(clock->getBPM()));
    if (sequencer->isPlaying()){
        playMode = "> " + playMode + bpm;
    }
    else{
        playMode = "|| " + playMode + bpm; 
    }

    std::vector<std::vector<std::string>> buttons = {{cursorStatus}, {playMode}, {viewMode}};
    
    return buttons; 
}

void TrackerController::stopPlaying()
{
    sequencer->stop();
}
void TrackerController::startPlaying()
{
    sequencer->play();
}
void TrackerController::setBPM(unsigned int bpm)
{
    clock->setBPM(bpm);    
}
void TrackerController::loadTrack(const std::string& fname)
{
    juce::ignoreUnused(fname);

}
void TrackerController::saveTrack(const std::string& fname)
{
    juce::ignoreUnused(fname);

}

void TrackerController::incrementBPM()
{
    double bpm = clock->getBPM();
    bpm ++;
    clock->setBPM(bpm);
}
void TrackerController::decrementBPM()
{
    double bpm = clock->getBPM();
    bpm --;
    if (bpm <= 0) bpm = 1;
    clock->setBPM(bpm);

}
