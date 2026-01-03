#pragma once 

#include <string>
#include <unordered_map>
#include <vector>
#include <tuple>
#include <functional>
#include "ClockAbs.h"
#include "MachineUtilsAbs.h"

/** Define the structure for a parameter 
 * parameters are used as arguments to Commands but also as a handy wrapper 
 * for configuring things
*/
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

struct SequenceReadOnly {
    double triggerProbability;
    double machineType;
    double machineId;
};

/** Commands are the main things that are executed by the sequencer when triggering a step 
 * 
*/
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
    std::function<void(std::vector<double>*, const SequenceReadOnly*)> execute;
    Command(){}
    Command(const std::string& name, const std::string& shortName, const std::string& description, const std::vector<Parameter>& parameters,
            int noteEditGoesToParam, int numberEditGoesToParam, int lengthEditGoesToParam,
            std::function<void(std::vector<double>*, const SequenceReadOnly*)> execute);
};

// Stable identifiers for command slots in CommandProcessor::commandsDouble.
enum class CommandType : std::size_t {
    MidiNote = 0,
    Log = 1,
};



/** Static class to manage objects and data relating to running of commands e.g. 
 * the MachineUtilsAbs object which allows commands to send MIDI 
 * and the ClockAbs object which allows commands to know about time  */ 
class CommandProcessor {
public:
    static void assignMasterClock(ClockAbs* masterClock);
    static void assignMachineUtils(MachineUtilsAbs* _machineUtils);
    static void sendAllNotesOff();
    static void sendQueuedMIDI(long tick);

    static Command& getCommand(double commandInd);
    static Command& getCommand(const std::string& commandName);
    // static void executeCommand(const std::string& commandName, std::vector<double>* params);
    static void executeCommand(double cmdInd, std::vector<double>* params, const SequenceReadOnly* sequenceContext);
    static int countCommands();
private: 
/** populates the commands variable */
    static void initialiseCommands();
};
