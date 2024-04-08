#include "SequencerCommands.h"

SequencerCommands::SequencerCommands()
{
// initialise the command map

// example of the callback used before for a midi note player
//    [&midiUtils, &clock](std::vector<double>* data){
//         double channel = data->at(Step::channelInd);
//         double offTick = clock.getCurrentTick() + data->at(Step::lengthInd);
//         // make the length quantised by steps
//         double noteVolocity = data->at(Step::velInd);
//         double noteOne = data->at(Step::note1Ind);
//         midiUtils.playSingleNote(channel, noteOne, noteVolocity, offTick);        
//     }
this->functionMap.insert({"midi", [](VecDle data){
            // play a midi note
        }});
        
this->functionMap.insert({"osc", [](VecDle data){
            // send an osc message
        }});
this->functionMap.insert({"samp", [](VecDle data){
            // play an internal sample
        }});
this->functionMap.insert({"shuff", [](VecDle data){
            // shuffle another track
        }});
}