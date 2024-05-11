#pragma once 

#include <string>
#include <unordered_map>
#include <vector>
#include <tuple>
#include <functional>
#include "SimpleClock.h"

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
    /** which column in a step's data vector does this param read from?*/
    int stepCol;
    /** how many decimal places to display on the UI?*/
    int decPlaces;
    Parameter();
    Parameter(const std::string& name, const std::string& shortName, double min, double max, double step, double defaultValue, int stepCol, int dps=0);
};

// Define the structure for a command
struct Command {
    std::string name;
    std::string shortName;
    std::string description;
    std::vector<Parameter> parameters;
    
    /** when user provides 'note input' during editing, which param to send it to? */
    int noteEditGoesToParam;
    /** when user sends number input during editing of this command, which param to send it to?*/
    int numberEditGoesToParam;
    /** when user sends length input during editing, which param to send it to? */
    int lengthEditGoesToParam;
    std::function<void(std::vector<double>*)> execute;
    Command(){}
    Command(const std::string& name, const std::string& shortName, const std::string& description, const std::vector<Parameter>& parameters,
            int noteEditGoesToParam, int numberEditGoesToParam, int lengthEditGoesToParam, std::function<void(std::vector<double>*)> execute);
};



// Static class to manage commands
class CommandProcessor {
public:
    static void assignMasterClock(SimpleClock* masterClock);
    static void initialiseMIDI();
    static void sendAllNotesOff();
    static void sendQueuedMIDI(long tick);

    static Command& getCommand(double commandInd);
    static Command& getCommand(const std::string& commandName);
    // static void executeCommand(const std::string& commandName, std::vector<double>* params);
    static void executeCommand(double cmdInd, std::vector<double>* params);
    static int countCommands();
private: 
/** populates the commands variable */
    static void initialiseCommands();
};

