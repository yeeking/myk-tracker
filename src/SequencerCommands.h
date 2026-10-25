#pragma once

#include <array>
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
    Parameter(const std::string& _name, const std::string& _shortName, double _min, double _max, double _step, double _defaultValue, int _stepCol, int _dps=0);
};

struct SequenceReadOnly {
    double machineType;
    double machineId;
    bool controlMode = false;
    /** Per-trigger playback offset; stored sequence rows remain unchanged. */
    int noteTransposeSemitones = 0;
    /** Live performance gate.  It deliberately does not alter stored rows. */
    enum class NoteLengthMode : unsigned char { off, half, quarter } noteLengthMode = NoteLengthMode::off;
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
    Command(const std::string& _name, const std::string& _shortName, const std::string& _description, const std::vector<Parameter>& _parameters,
            int _noteEditGoesToParam, int _numberEditGoesToParam, int _lengthEditGoesToParam,
            std::function<void(std::vector<double>*, const SequenceReadOnly*)> _execute);
};

// Identifiers for command slots in CommandProcessor::commandsDouble. Project
// state is guarded by an exact schema version, so obsolete command IDs are not
// retained in this enum.
enum class CommandType : std::size_t {
    MidiNote = 0,
    Log = 1,
    Sampler = 2,
    WavetableSynth = 4,
    DistortionFx = 6,
    DelayFx = 7,
    ChannelStripFx = 8,
    AuxSend1Fx = 9,
    AuxSend2Fx = 10,
    FilterFx = 11,
    ToggleAux1 = 12,
    ToggleAux2 = 13,
    FilterCutoff = 14,
    /** Fixed per-stack modulation block; never a serial slot or step command. */
    ModulationMatrix = 15,
    /** Six-part internal drum synthesizer. */
    DrumSynth = 16,
};

/** Canonical command range. */
inline constexpr CommandType kLastCommandType = CommandType::DrumSynth;
inline constexpr std::size_t kCommandTypeCount = static_cast<std::size_t>(kLastCommandType) + 1;

constexpr bool isValidCommandTypeValue(std::size_t value)
{
    switch (static_cast<CommandType>(value))
    {
        case CommandType::MidiNote:
        case CommandType::Log:
        case CommandType::Sampler:
        case CommandType::WavetableSynth:
        case CommandType::DistortionFx:
        case CommandType::DelayFx:
        case CommandType::ChannelStripFx:
        case CommandType::AuxSend1Fx:
        case CommandType::AuxSend2Fx:
        case CommandType::FilterFx:
        case CommandType::ToggleAux1:
        case CommandType::ToggleAux2:
        case CommandType::FilterCutoff:
        case CommandType::ModulationMatrix:
        case CommandType::DrumSynth:
            return true;
    }
    return false;
}

/** Shared behaviour table for machine types so the processor, editor, UI and
    control service all agree on labels and routing. */
struct MachineTypeTraits {
    /** Short uppercase cell label shown in the stack grid (e.g. "WAVE"). */
    const char* shortLabel = "MACH";
    /** Long name shown in detail views (e.g. "wavetable synth"). */
    const char* longName = "";
    /** True for audio-only effect machines in the stack audio path. */
    bool isAudioEffect = false;
    /** True for machine types a sequence step row may store. */
    bool isStepCommandType = false;
    /** True for machine types that route notes through a stack. */
    bool isStackRoutable = false;
    /** Position in kSlotCycleTypes, or -1 when the type cannot be cycled in. */
    int slotCycleIndex = -1;
    /** True for sequence control commands that are not stack-routable machines. */
    bool isControlCommand = false;
};

constexpr MachineTypeTraits machineTraits(CommandType type) {
    switch (type) {
        case CommandType::MidiNote: return { "MIDI", "midi", false, true, true, 0 };
        case CommandType::Log: return { "LOG", "log", false, true, false, -1 };
        case CommandType::Sampler: return { "SAMPLER", "sampler", false, true, true, 2 };
        case CommandType::WavetableSynth: return { "WAVE", "wavetable synth", false, true, true, 1 };
        case CommandType::DrumSynth: return { "DRUM", "drum synth", false, true, true, 3 };
        case CommandType::DistortionFx: return { "DIST", "distortion", true, false, true, 4 };
        case CommandType::DelayFx: return { "DELAY", "delay", true, false, true, 5 };
        case CommandType::ChannelStripFx: return { "CHSTR", "channel strip", true, false, true, 6 };
        case CommandType::AuxSend1Fx: return { "AUX1", "shared aux [1] reverb", true, false, true, 7 };
        case CommandType::AuxSend2Fx: return { "AUX2", "shared aux [2] reverb", true, false, true, 8 };
        case CommandType::FilterFx: return { "FILTER", "filter", true, false, true, 9 };
        case CommandType::ToggleAux1: return { "TAUX1", "toggle aux [1] send", false, true, false, -1, true };
        case CommandType::ToggleAux2: return { "TAUX2", "toggle aux [2] send", false, true, false, -1, true };
        case CommandType::FilterCutoff: return { "COFF", "filter cutoff", false, true, false, -1, true };
        case CommandType::ModulationMatrix: return { "MOD", "modulation matrix", false, false, false, -1 };
    }
    return {};
}

/** Slot types a stack can hold, in the order the slot-type cycle walks. */
constexpr std::array<CommandType, 10> kSlotCycleTypes = {
    CommandType::MidiNote,
    CommandType::WavetableSynth,
    CommandType::Sampler,
    CommandType::DrumSynth,
    CommandType::DistortionFx,
    CommandType::DelayFx,
    CommandType::ChannelStripFx,
    CommandType::AuxSend1Fx,
    CommandType::AuxSend2Fx,
    CommandType::FilterFx
};



/** Static class to manage objects and data relating to running of commands e.g. 
 * the MachineUtilsAbs object which allows commands to send MIDI 
 * and the ClockAbs object which allows commands to know about time  */ 
// Static registry/executor for sequencer commands and machine routing.
class CommandProcessor {
public:
    static void assignMasterClock(ClockAbs* masterClock);
    static void assignMachineUtils(MachineUtilsAbs* _machineUtils);
    static void sendAllNotesOff();
    static void sendQueuedMIDI(long tick);
    static void beginHeldNote(CommandType machineType, unsigned short machineId,
                              unsigned short note, unsigned short velocity);
    static void endHeldNote(CommandType machineType, unsigned short machineId,
                            unsigned short note);
    static std::string describeStepNote(const SequenceReadOnly* sequenceContext, double noteValue);

    static Command& getCommand(double commandInd);
    static Command& getCommand(const std::string& commandName);
    // static void executeCommand(const std::string& commandName, std::vector<double>* params);
    static void executeCommand(double cmdInd, std::vector<double>* params, const SequenceReadOnly* sequenceContext);
    static int countCommands();
private: 
/** populates the commands variable */
    static void initialiseCommands();
};
