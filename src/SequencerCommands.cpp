// CommandRegistry.cpp
#include "SequencerCommands.h"
#include <stdexcept>
#include <iostream>
#include <assert.h>

// Constructor definitions
Parameter::Parameter(const std::string& name, const std::string& shortName, double min, double max, double step, double defaultValue)
    : name(name), shortName(shortName), min(min), max(max), step(step), defaultValue(defaultValue) {}

Command::Command(const std::string& name, const std::string& shortName, const std::string& description, const std::vector<Parameter>& parameters,
                 std::function<void(std::vector<double>*)> execute)
    : name(name), shortName(shortName), description(description), parameters(parameters), execute(std::move(execute)) {}


// namespaced global vars used in the command registry
// for speed / not passing around an object too much
namespace CommandManager {
    // this is for string based access to commands
    std::unordered_map<std::string, Command> commands;
    // this is for fast double-based access to commands
    // when the double is stored in the Step data
    std::unordered_map<double, Command> commandsDouble;
}

// // Initialize the command registry
void CommandRegistry::initialize() {
//     // CommandRegistry::count ++; 
    Command midiNote{
            "MIDINote", "Midi", "Plays a MIDI note",
            { Parameter("Channel", "C", 0, 16, 1, 0), 
              Parameter("Note", "N", 0, 127, 1, 0), 
              Parameter("Vel", "V", 0, 127, 1, 0), 
              Parameter("Dur", "D", 0, 8, 1, 0)},
            [](std::vector<double>* params) {
                assert(params->size() == 5);// need 5 params as we also get sent the cmd index as a param
                std::cout << "Executing command " << "MIDINote" << std::endl;
                for (double d : *params){
                    std::cout << d << std::endl;
                }
            }
    };
    Command sample{
            "Sample", "Samp", "Plays a Sample",
            { Parameter("Sound", "Bank", 0, 16, 1, 0), 
              Parameter("Note", "N", 0, 127, 1, 0), 
              Parameter("Vel", "V", 0, 127, 1, 0), 
              Parameter("Dur", "D", 0, 8, 1, 0)},
            [](std::vector<double>* params) {
                assert(params->size() == 5);// need 5 params as we also get sent the cmd index as a param
                std::cout << "Executing command " << "sample" << std::endl;
                for (double d : *params){
                    std::cout << d << std::endl;
                }
            }
    };

    CommandManager::commands[midiNote.shortName] = midiNote;
    CommandManager::commandsDouble[0] = midiNote;
    CommandManager::commands[sample.shortName] = sample;
    CommandManager::commandsDouble[1] = sample;
}


Command& CommandRegistry::getCommand(double commandInd) {
    if (CommandManager::commands.size() == 0){
        CommandRegistry::initialize();
    }
    assert(CommandManager::commandsDouble.find(commandInd) != CommandManager::commandsDouble.end());
    
    auto it = CommandManager::commandsDouble.find(commandInd);
    if (it != CommandManager::commandsDouble.end()) {
        return it->second;
    }
    throw std::runtime_error("Command not found: " + std::to_string(commandInd));
}

// // Get a command by name
Command& CommandRegistry::getCommand(const std::string& commandName) {
    if (CommandManager::commands.size() == 0){
        CommandRegistry::initialize();
    }
    auto it = CommandManager::commands.find(commandName);
    if (it != CommandManager::commands.end()) {
        return it->second;
    }
    throw std::runtime_error("Command not found: " + commandName);
}

// // Function to execute a command
// void CommandRegistry::executeCommand(const std::string& commandName, std::vector<double>* params) {
//     // const Command& cmd = getCommand(commandName);
//     // cmd.execute(params);
// }

void CommandRegistry::executeCommand(double cmdInd, std::vector<double>* params)
{
       if (CommandManager::commands.size() == 0){
        CommandRegistry::initialize();
    }
    // assert(myMap.find("banana") != myMap.end());
    // CommandManager::commandsDouble(cmdInd)
    assert(CommandManager::commandsDouble.find(cmdInd) != CommandManager::commandsDouble.end());
    CommandManager::commandsDouble[cmdInd].execute(params);// later: add in other stuff commands need
}


int CommandRegistry::countCommands()
{
    if (CommandManager::commands.size() == 0){
        CommandRegistry::initialize();
    }
    return CommandManager::commands.size();
}
