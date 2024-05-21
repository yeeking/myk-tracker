// CommandRegistry.cpp
#include "SequencerCommands.h"
#include <stdexcept>
#include <iostream>
#include <assert.h>
#include <random>
#include "MidiUtilsAbs.h"
#include "Sequencer.h"

// Constructor definitions
Parameter::Parameter(){}
Parameter::Parameter(const std::string& name, const std::string& shortName, double min, double max, double step, double defaultValue, int stepCol, int dps)
    : name(name), shortName(shortName), min(min), max(max), step(step), defaultValue(defaultValue), stepCol{stepCol}, decPlaces{dps} {}

Command::Command(const std::string& name, const std::string& shortName, const std::string& description, const std::vector<Parameter>& parameters,
                 int noteEditGoesToParam, int numberEditGoesToParam, int lengthEditGoesToParam,
                 std::function<void(std::vector<double>*)> execute)
    : name(name), shortName(shortName), description(description), parameters(parameters), 
    noteEditGoesToParam{noteEditGoesToParam}, numberEditGoesToParam{numberEditGoesToParam}, lengthEditGoesToParam{lengthEditGoesToParam}, execute(std::move(execute)) {}


/** handy wrapper for generating random numbers */
class RandomNumberGenerator {
private:
    static std::mt19937 gen; // Mersenne Twister random number generator
    static std::uniform_real_distribution<> dis; // Uniform real distribution between 0 and 1

public:
    // Static function to get a random number between 0 and 1
    static double getRandomNumber() {
        return dis(gen);
    }

    // Initialize the random number generator and the distribution
    static void initialize() {
        std::random_device rd; // Non-deterministic random device for seeding
        gen = std::mt19937(rd()); // Seed the generator
        dis = std::uniform_real_distribution<>(0.0, 1.0); // Define the range
    }
};
// Definition of static members
std::mt19937 RandomNumberGenerator::gen;
std::uniform_real_distribution<> RandomNumberGenerator::dis;

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
    MidiUtilsAbs* midiUtils = nullptr;
    // the clock is externally created but 
    // we need access to it for 
    // some of the lambdas
    ClockAbs* masterClock = nullptr;
}


void CommandProcessor::assignMasterClock(ClockAbs* masterClock)
{
    CommandData::masterClock = masterClock;
}


/** get the commands ready and verify they have the data they need
*/
void CommandProcessor::initialiseCommands() {

    assert(CommandData::masterClock != nullptr);
    assert(CommandData::midiUtils != nullptr);
    
    RandomNumberGenerator::initialize();
    // get a MIDI link going
    Command midiNote{
            "MIDINote", "Midi", "Plays a MIDI note",
              // long, short, min, max, step,default
            { Parameter("Channel", "C", 0, 16, 1, 0, Step::chanInd), 
              Parameter("Note", "N", 0, 127, 1, 32, Step::noteInd), 
              Parameter("Vel", "V", 0, 127, 4, 64, Step::velInd), 
              Parameter("Dur", "D", 0, 8, 1, 1, Step::lengthInd),
              Parameter("Prob", "%", 0, 1, 0.1, 1.0, Step::probInd, 2)},
              
            Step::noteInd, // int noteEditGoesToParam;
            Step::velInd, // int numberEditGoesToParam;
            Step::lengthInd, // int lengthEditGoesToParam;  
            [](std::vector<double>* stepData) {
                assert(stepData->size() == Step::maxInd + 1);// need +1 params as we also get sent the cmd index as a param
                if ((*stepData)[Step::noteInd] > 0) {// there is a valid note
                    double random_number = RandomNumberGenerator::getRandomNumber();
                    if (random_number < (*stepData)[Step::probInd]){ 
                        double now = CommandData::masterClock->getCurrentTick();
                        // std::cout << "command data " << (*stepData)[Step::noteInd] << std::endl;
                        CommandData::midiUtils->playSingleNote((int) (*stepData)[Step::chanInd],(int) (*stepData)[Step::noteInd], (int) (*stepData)[Step::velInd], 
                        (long) ((*stepData)[Step::lengthInd]+now));
                    }
                }
                
            }
    };
    // Command sample{
    //         "Sample", "Samp", "Plays a Sample",
    //         { Parameter("Sound", "Bank", 0, 16, 1, 0, Step::chanInd), 
    //           Parameter("Note", "N", 0, 127, 1, 0, Step::noteInd), 
    //           Parameter("Vel", "V", 0, 127, 1, 0, Step::velInd), 
    //           Parameter("Dur", "D", 0, 8, 1, 0, Step::lengthInd),
    //           Parameter("Prob", "%", 0, 1, 0.1, 1, Step::probInd)},

    //         Step::noteInd, // int noteEditGoesToParam;
    //         Step::velInd, // int numberEditGoesToParam;
    //         Step::lengthInd, // int lengthEditGoesToParam;  
    //         [](std::vector<double>* params) {
    //             assert(params->size() == Step::maxInd + 1);// need +1 params as we also get sent the cmd index as a param
    //             std::cout << "Executing command " << "sample" << std::endl;
    //             for (double d : *params){
    //                 std::cout << d << std::endl;
    //             }
    //         }
    // };

    CommandData::commands[midiNote.shortName] = midiNote;
    CommandData::commandsDouble[0] = midiNote;
    // CommandData::commands[sample.shortName] = sample;
    // CommandData::commandsDouble[1] = sample;
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

void CommandProcessor::assignMidiUtils(MidiUtilsAbs* _midiUtils)
{
    CommandData::midiUtils = _midiUtils; 
}

void CommandProcessor::sendAllNotesOff()
{
    CommandData::midiUtils->allNotesOff();
}
void CommandProcessor::sendQueuedMIDI(long tick)
{
    CommandData::midiUtils->sendQueuedMessages(tick);
}

