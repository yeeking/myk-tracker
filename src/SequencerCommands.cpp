// CommandRegistry.cpp
#include "SequencerCommands.h"
#include <stdexcept>
#include <iostream>

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
            "MIDINote", "M", "Plays a MIDI note",
            { Parameter("Channel", "C", 0, 16, 1, 0), 
              Parameter("Note", "N", 0, 127, 1, 0), 
              Parameter("Vel", "V", 0, 127, 1, 0), 
              Parameter("Dur", "D", 0, 8, 1, 0)},
            [](std::vector<double>* params) {
                if (params->size() < 2) {
                    std::cerr << "Add command requires exactly two parameters." << std::endl;
                    return;
                }
                double result = (*params)[0] + (*params)[1];
                std::cout << "Result of Add: " << result << std::endl;
            }
    };

    CommandManager::commands["MIDINote"] = midiNote;
    CommandManager::commandsDouble[0] = midiNote;

}


Command CommandRegistry::getCommand(double commandInd) {
    if (CommandManager::commands.size() == 0){
        CommandRegistry::initialize();
    }
    auto it = CommandManager::commandsDouble.find(commandInd);
    if (it != CommandManager::commandsDouble.end()) {
        return it->second;
    }
    throw std::runtime_error("Command not found: " + std::to_string(commandInd));
}

// // Get a command by name
 Command CommandRegistry::getCommand(const std::string& commandName) {
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
void CommandRegistry::executeCommand(const std::string& commandName, std::vector<double>* params) {
    // const Command& cmd = getCommand(commandName);
    // cmd.execute(params);
}

int CommandRegistry::countCommands()
{
    return CommandManager::commands.size();
}
