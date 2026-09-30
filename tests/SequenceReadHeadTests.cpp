#include <JuceHeader.h>

#include "ClockAbs.h"
#include "MachineUtilsAbs.h"
#include "Sequencer.h"
#include "SequencerCommands.h"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <set>

namespace
{
struct TestClock final : ClockAbs
{
    void setBPM(double value) override { bpm = value; }
    double getBPM() override { return bpm; }
    double bpm = 120.0;
};

struct TestMachines final : MachineUtilsAbs
{
    void allNotesOff() override {}
    void sendMessageToMachine(CommandType, unsigned short, unsigned short note,
                              unsigned short, unsigned short) override { notes.push_back(note); }
    std::string describeStepNote(CommandType, unsigned short, unsigned short note) const override
    { return std::to_string(note); }
    void sendQueuedMessages(long) override {}
    std::vector<unsigned short> notes;
};

void require(bool condition, const char* message)
{
    if (!condition)
    {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

void putNote(Sequence& sequence, std::size_t step, double note)
{
    sequence.setStepData(step, {{ static_cast<double>(CommandType::MidiNote), note, 100.0, 1.0 }});
}
}

int main()
{
    TestClock clock;
    TestMachines machines;
    CommandProcessor::assignMasterClock(&clock);
    CommandProcessor::assignMachineUtils(&machines);
    (void) CommandProcessor::getCommand(static_cast<double>(CommandType::MidiNote));

    require(Sequence::getRhythmPresets().size() == 26, "all nonzero one-to-four-bit rhythms exist");
    require(Sequence::getRhythmPresets().front() == "1", "rhythms start with the one-bit trigger");

    Sequence sequence(nullptr, 8, 0);
    for (std::size_t step = 0; step < 8; ++step) putNote(sequence, step, 60.0 + step);
    require(sequence.getReadHeadCount() == 1, "default sequence has one head");
    require(sequence.getReadHeadConfig(0).mode == SequenceReadMode::linear, "default head is linear");

    sequence.resetReadHeads(true);
    sequence.tick(true);
    require(machines.notes == std::vector<unsigned short>{60}, "linear head triggers the first step");
    for (int tick = 0; tick < 4; ++tick) sequence.tick(true);
    require(machines.notes.back() == 61, "linear TPS advances at the configured interval");

    auto head = sequence.getReadHeadConfig(0);
    head.ticksPerStep = 1;
    head.rhythm = "10";
    sequence.setReadHeadConfig(0, head);
    machines.notes.clear();
    sequence.resetReadHeads(true);
    sequence.tick(true);
    sequence.tick(true);
    sequence.tick(true);
    require(machines.notes == std::vector<unsigned short>({60, 61}), "rhythm rests hold the linear position");

    for (const auto& rhythm : Sequence::getRhythmPresets())
    {
        head = sequence.getReadHeadConfig(0);
        head.ticksPerStep = 1;
        head.mode = SequenceReadMode::linear;
        head.rhythm = rhythm;
        require(sequence.setReadHeadConfig(0, head), "every advertised rhythm is accepted");
        machines.notes.clear();
        sequence.resetReadHeads(true);
        for (std::size_t tick = 0; tick < rhythm.size(); ++tick) sequence.tick(true);
        require(machines.notes.size() == static_cast<std::size_t>(std::count(rhythm.begin(), rhythm.end(), '1')),
                "each rhythm emits exactly once per set bit");
    }

    head = sequence.getReadHeadConfig(0);
    head.mode = SequenceReadMode::randomChord;
    head.polyphony = 3;
    head.rhythm = "1";
    sequence.setReadHeadConfig(0, head);
    machines.notes.clear();
    sequence.resetReadHeads(true);
    sequence.tick(true);
    require(machines.notes.size() == 3, "random chord respects polyphony");
    require(std::set<unsigned short>(machines.notes.begin(), machines.notes.end()).size() == 3,
            "random chord selects distinct steps");

    head.probability = 0.0;
    sequence.setReadHeadConfig(0, head);
    const auto before = sequence.getReadHeadSnapshots()[0].positions;
    machines.notes.clear();
    sequence.tick(true);
    const auto after = sequence.getReadHeadSnapshots()[0].positions;
    require(machines.notes.empty(), "zero head probability suppresses the complete chord");
    require(before != after, "probability misses still select the next position");

    sequence.setReadHeadCount(3);
    require(sequence.getReadHeadCount() == 3, "head count grows to three");
    sequence.setReadHeadCount(1);
    sequence.setReadHeadCount(2);
    require(sequence.getReadHeadConfig(1).ticksPerStep == 4, "regrown heads use fresh defaults");

    return 0;
}
