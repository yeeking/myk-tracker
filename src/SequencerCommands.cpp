// CommandRegistry.cpp
#include "SequencerCommands.h"
#include <stdexcept>
#include <iostream>
#include <assert.h>
#include "MidiUtils.h"
#include "Sequencer.h"

// Constructor definitions
Parameter::Parameter(const std::string& name, const std::string& shortName, double min, double max, double step, double defaultValue)
    : name(name), shortName(shortName), min(min), max(max), step(step), defaultValue(defaultValue) {}

Command::Command(const std::string& name, const std::string& shortName, const std::string& description, const std::vector<Parameter>& parameters,
                 int noteEditGoesToParam, int numberEditGoesToParam, int lengthEditGoesToParam,
                 std::function<void(std::vector<double>*)> execute)
    : name(name), shortName(shortName), description(description), parameters(parameters), 
    noteEditGoesToParam{noteEditGoesToParam}, numberEditGoesToParam{numberEditGoesToParam}, lengthEditGoesToParam{lengthEditGoesToParam}, execute(std::move(execute)) {}


// namespaced global vars used in the command processing lambdas
// for speed / avoiding passing around objects too much
// since they are placed in this CPP file, they are not visible
// elsewhere. So the CommandProcessor class provides static functions that call on this data
// its like a lazy man's singleton 
namespace CommandData {
    // this is for string based access to commands
    std::unordered_map<std::string, Command> commands;
    // this is for fast double-based access to commands
    // when the double is stored in the Step data
    std::unordered_map<double, Command> commandsDouble;
    MidiUtils midiUtils;
    // the clock is externally created but 
    // we need access to it for 
    // some of the lanbdas
    SimpleClock* masterClock = nullptr;
}


void CommandProcessor::assignMasterClock(SimpleClock* masterClock)
{
    CommandData::masterClock = masterClock;
}

// // Initialize the command registry
void CommandProcessor::initialiseCommands() {
    assert(CommandData::masterClock != nullptr);
    // get a MIDI link going
    Command midiNote{
            "MIDINote", "Midi", "Plays a MIDI note",
            { Parameter("Channel", "C", 0, 16, 1, 0), 
              Parameter("Note", "N", 0, 127, 1, 0), 
              Parameter("Vel", "V", 0, 127, 1, 0), 
              Parameter("Dur", "D", 0, 8, 1, 0)},
            Step::noteInd, // int noteEditGoesToParam;
            Step::velInd, // int numberEditGoesToParam;
            Step::lengthInd, // int lengthEditGoesToParam;  
            [](std::vector<double>* params) {
                assert(params->size() == 5);// need 5 params as we also get sent the cmd index as a param
                if ((*params)[Step::noteInd] > 0) {// there is a valid note
                    double now = CommandData::masterClock->getCurrentTick();
                    CommandData::midiUtils.playSingleNote((int) (*params)[Step::chanInd],(int) (*params)[Step::noteInd], (int) (*params)[Step::velInd], 
                    (long) ((*params)[Step::lengthInd]+now));
                }
                
            }
    };
    Command sample{
            "Sample", "Samp", "Plays a Sample",
            { Parameter("Sound", "Bank", 0, 16, 1, 0), 
              Parameter("Note", "N", 0, 127, 1, 0), 
              Parameter("Vel", "V", 0, 127, 1, 0), 
              Parameter("Dur", "D", 0, 8, 1, 0)},
            Step::noteInd, // int noteEditGoesToParam;
            Step::velInd, // int numberEditGoesToParam;
            Step::lengthInd, // int lengthEditGoesToParam;  
            [](std::vector<double>* params) {
                assert(params->size() == 5);// need 5 params as we also get sent the cmd index as a param
                std::cout << "Executing command " << "sample" << std::endl;
                for (double d : *params){
                    std::cout << d << std::endl;
                }
            }
    };

    CommandData::commands[midiNote.shortName] = midiNote;
    CommandData::commandsDouble[0] = midiNote;
    CommandData::commands[sample.shortName] = sample;
    CommandData::commandsDouble[1] = sample;
}


Command& CommandProcessor::getCommand(double commandInd) {
    if (CommandData::commands.size() == 0){
        CommandProcessor::initialiseCommands();
    }
    assert(CommandData::commandsDouble.find(commandInd) != CommandData::commandsDouble.end());
    
    auto it = CommandData::commandsDouble.find(commandInd);
    if (it != CommandData::commandsDouble.end()) {
        return it->second;
    }
    throw std::runtime_error("Command not found: " + std::to_string(commandInd));
}

// // Get a command by name
Command& CommandProcessor::getCommand(const std::string& commandName) {
    if (CommandData::commands.size() == 0){
        CommandProcessor::initialiseCommands();
    }
    auto it = CommandData::commands.find(commandName);
    if (it != CommandData::commands.end()) {
        return it->second;
    }
    throw std::runtime_error("Command not found: " + commandName);
}


void CommandProcessor::executeCommand(double cmdInd, std::vector<double>* params)
{
    if (CommandData::commands.size() == 0){
        CommandProcessor::initialiseCommands();
    }
    assert(CommandData::commandsDouble.find(cmdInd) != CommandData::commandsDouble.end());
    CommandData::commandsDouble[cmdInd].execute(params);// later: add in other stuff commands need
}


int CommandProcessor::countCommands()
{
    if (CommandData::commands.size() == 0){
        CommandProcessor::initialiseCommands();
    }
    return CommandData::commands.size();
}

void CommandProcessor::initialiseMIDI()
{
    CommandData::midiUtils.interactiveInitMidi();
}

void CommandProcessor::sendAllNotesOff()
{
    CommandData::midiUtils.allNotesOff();
}
void CommandProcessor::sendQueuedMIDI(long tick)
{
    CommandData::midiUtils.sendQueuedMessages(tick);
}