#pragma once 

#include <string>
#include <unordered_map>
#include <vector>
#include <tuple>
#include <functional>

// Define the structure for a parameter of a command
struct Parameter {
    std::string name;
    std::string shortName;
    /** highest value for this parameter */
    double min;
    /** lowest value for this parameter */
    double max;
    /** increment step size for this parameter */
    double step;
    /** default value for this parameter */
    double defaultValue;

    Parameter(const std::string& name, const std::string& shortName, double min, double max, double step, double defaultValue);
};

// Define the structure for a command
struct Command {
    std::string name;
    std::string shortName;
    std::string description;
    std::vector<Parameter> parameters;
    std::function<void(std::vector<double>*)> execute;
    Command(){}
    Command(const std::string& name, const std::string& shortName, const std::string& description, const std::vector<Parameter>& parameters,
            std::function<void(std::vector<double>*)> execute);
};



// Static class to manage commands
class CommandRegistry {
public:
    static Command& getCommand(double commandInd);
    static Command& getCommand(const std::string& commandName);
    // static void executeCommand(const std::string& commandName, std::vector<double>* params);
    static void executeCommand(double cmdInd, std::vector<double>* params);
    static int countCommands();
private: 
/** populates the commands variable */
    static void initialize();
};

