#include <JuceHeader.h>

#include "ClockAbs.h"
#include "MachineUtilsAbs.h"
#include "Sequencer.h"
#include "SequencerCommands.h"
#include "SequencerEditor.h"

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
    void toggleAuxSendForStack(unsigned short machineId, bool isAux1) override
    { auxToggles.emplace_back(machineId, isAux1); }
    void setFilterCutoffHzForStack(unsigned short machineId, double cutoffHz) override
    { cutoffs.emplace_back(machineId, cutoffHz); }
    std::vector<unsigned short> notes;
    std::vector<std::pair<unsigned short, bool>> auxToggles;
    std::vector<std::pair<unsigned short, double>> cutoffs;
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
    require(sequence.getReadHeadConfig(0).octaves == 1, "default head has one octave layer");

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

    Sequence controlSequence(nullptr, 8, 5);
    controlSequence.setControlMode(true);
    controlSequence.setStepData(0, {{ static_cast<double>(CommandType::ToggleAux1), 0.0, 0.0, 0.0 }});
    controlSequence.setStepData(1, {{ static_cast<double>(CommandType::ToggleAux2), 0.0, 0.0, 0.0 }});
    controlSequence.setStepData(2, {{ static_cast<double>(CommandType::FilterCutoff), 1234.0, 0.0, 0.0 }});
    controlSequence.setStepData(3, {{ static_cast<double>(CommandType::FilterCutoff), 0.0, 0.0, 0.0 }});
    require(controlSequence.stepToStringFlat(0) == "TAUX1", "aux [1] toggle displays in the sequence grid");
    require(controlSequence.stepToStringFlat(1) == "TAUX2", "aux [2] toggle displays in the sequence grid");
    require(controlSequence.stepToStringFlat(2) == "COFF", "filter cutoff displays in the sequence grid");
    require(controlSequence.stepToStringFlat(3) == "----", "zero filter cutoff is hidden in the sequence grid");

    auto controlHead = controlSequence.getReadHeadConfig(0);
    controlHead.ticksPerStep = 1;
    controlHead.rhythm = "1";
    controlSequence.setReadHeadConfig(0, controlHead);
    machines.auxToggles.clear();
    machines.cutoffs.clear();
    controlSequence.resetReadHeads(true);
    controlSequence.tick(true);
    controlSequence.tick(true);
    controlSequence.tick(true);
    require(machines.auxToggles.size() == 2, "linear control sequence triggers both aux toggles");
    if (machines.auxToggles.size() == 2)
    {
        require(machines.auxToggles[0] == std::make_pair<unsigned short, bool>(5, true),
                "TAUX1 targets the sequence's stack and aux [1]");
        require(machines.auxToggles[1] == std::make_pair<unsigned short, bool>(5, false),
                "TAUX2 targets the sequence's stack and aux [2]");
    }
    require(machines.cutoffs.size() == 1, "linear control sequence triggers only the nonzero cutoff");
    if (!machines.cutoffs.empty())
        require(machines.cutoffs[0] == std::make_pair<unsigned short, double>(5, 1234.0),
                "COFF passes its value field to the filter cutoff command");

    Sequencer editorSequencer(1, 4);
    SequencerEditor editor(&editorSequencer);
    require(!editor.handleControlKey('z'), "control keys are inactive in note mode");
    require(editor.handleNoteKey('z'), "note keys remain active in note mode");
    auto noteEntry = editorSequencer.getStepData(0, 0);
    require(!noteEntry.empty()
        && noteEntry[0][Step::cmdInd] == static_cast<double>(CommandType::MidiNote)
        && noteEntry[0][Step::noteInd] > 0.0,
            "note mode still enters piano notes");

    editorSequencer.setSequenceControlMode(0, true);
    require(editor.handleControlKey('z'), "z enters TAUX1 in control mode");
    auto aux1Entry = editorSequencer.getStepData(0, 0);
    require(!aux1Entry.empty()
        && aux1Entry[0][Step::cmdInd] == static_cast<double>(CommandType::ToggleAux1)
        && aux1Entry[0][Step::noteInd] == 0.0,
            "TAUX1 stores a zero-value control row");

    editor.setCurrentStep(1);
    require(editor.handleControlKey('x'), "x enters TAUX2 in control mode");
    auto aux2Entry = editorSequencer.getStepData(0, 1);
    require(!aux2Entry.empty()
        && aux2Entry[0][Step::cmdInd] == static_cast<double>(CommandType::ToggleAux2),
            "TAUX2 is stored on the focused step");

    editor.setCurrentStep(2);
    require(editor.handleControlKey('c'), "c enters COFF in control mode");
    auto cutoffEntry = editorSequencer.getStepData(0, 2);
    require(!cutoffEntry.empty()
        && cutoffEntry[0][Step::cmdInd] == static_cast<double>(CommandType::FilterCutoff)
        && cutoffEntry[0][Step::noteInd] == 2000.0,
            "COFF starts at its command default");

    const auto suppressedCutoff = cutoffEntry;
    require(editor.handleNoteKey('s'), "piano keys are swallowed in control mode");
    require(editorSequencer.getStepData(0, 2) == suppressedCutoff,
            "swallowed piano keys leave the control row unchanged");

    editor.gotoStepPage();
    editor.setCurrentStep(3);
    const auto emptyStep = editorSequencer.getStepData(0, 3);
    require(editor.handleChordKey('q'), "chord shortcuts are swallowed in control mode");
    require(editorSequencer.getStepData(0, 3) == emptyStep,
            "swallowed chord shortcuts leave the step unchanged");

    editor.setCurrentStep(2);
    editor.setStepCursor(0, Step::noteInd);
    const auto octaveBefore = editor.getCurrentOctave();
    editor.incrementAtCursor();
    auto incrementedCutoff = editorSequencer.getStepData(0, 2);
    require(!incrementedCutoff.empty() && incrementedCutoff[0][Step::noteInd] == 2050.0,
            "COFF values increment by the command step");
    require(editor.getCurrentOctave() == octaveBefore,
            "control command values do not sync the note octave");

    Sequencer insertSequencer(3, 4);
    for (std::size_t seqIndex = 0; seqIndex < 3; ++seqIndex)
        insertSequencer.getSequence(seqIndex)->setMachineId(10.0 + static_cast<double>(seqIndex));
    require(insertSequencer.insertSequence(1, 8, 7.0, true),
            "sequence insert succeeds below the maximum sequence count");
    require(insertSequencer.howManySequences() == 4, "inserting a sequence increases the sequence count");
    require(insertSequencer.getSequence(1)->getMachineId() == 7.0,
            "inserted sequence uses the supplied machine stack");
    require(insertSequencer.getSequence(1)->isControlMode(), "inserted sequence can start in control mode");
    require(insertSequencer.getSequence(1)->getLength() == 8, "inserted sequence uses the supplied length");
    require(insertSequencer.getSequence(2)->getMachineId() == 11.0,
            "inserting a sequence shifts later sequences right");
    require(insertSequencer.eraseSequence(1), "sequence erase removes the targeted sequence");
    require(insertSequencer.howManySequences() == 3, "erasing a sequence decreases the sequence count");
    require(insertSequencer.getSequence(1)->getMachineId() == 11.0,
            "erasing a sequence shifts later sequences left");

    Sequencer maxSequencer(127, 1);
    require(maxSequencer.insertSequence(0, 1, 0.0, false), "the 128th sequence can be inserted");
    require(maxSequencer.howManySequences() == Sequencer::maxSequences,
            "the sequencer accepts up to 128 sequences");
    require(!maxSequencer.insertSequence(Sequencer::maxSequences, 1, 0.0, false),
            "sequence insert rejects a count beyond 128");

    Sequencer singleSequencer(1, 1);
    require(!singleSequencer.eraseSequence(0), "the last remaining sequence cannot be erased");

    Sequencer actionSequencer(4, 4);
    for (std::size_t seqIndex = 0; seqIndex < 4; ++seqIndex)
        actionSequencer.getSequence(seqIndex)->setMachineId(10.0 + static_cast<double>(seqIndex));
    SequencerEditor actionEditor(&actionSequencer);
    actionEditor.setCurrentSequence(1);
    actionEditor.setCurrentStep(2);
    actionEditor.setArmedSequence(3);

    require(actionEditor.insertSequenceToRight(), "the editor can insert a sequence to the right");
    require(actionSequencer.howManySequences() == 5, "editor insert adds one sequence");
    require(actionEditor.getCurrentSequence() == 1, "inserting to the right keeps the current sequence");
    require(actionSequencer.getSequence(2)->getMachineId() == 11.0,
            "editor insert copies the current sequence's machine stack");
    require(actionSequencer.getSequence(2)->isControlMode(),
            "editor insert creates a control-mode sequence");
    require(actionSequencer.getSequence(2)->getLength() == 8,
            "editor insert creates an eight-step sequence");
    require(actionSequencer.getSequence(3)->getMachineId() == 12.0,
            "editor insert shifts later sequences right");
    require(actionEditor.getArmedSequence() == 4,
            "editor insert shifts a later armed sequence right");

    actionEditor.gotoStepPage();
    const auto preHelpStep = actionSequencer.getStepData(1, 2);
    actionEditor.toggleHelpPage();
    require(actionEditor.getCurrentPage() == SequencerEditorPage::help,
            "shift+help enters the help page");
    actionEditor.moveCursorRight();
    actionEditor.moveCursorDown();
    require(actionEditor.getCurrentSequence() == 1 && actionEditor.getCurrentStep() == 2,
            "cursor movement is inert on the help page");
    require(actionEditor.handleNoteKey('z'), "piano keys are swallowed on the help page");
    require(actionEditor.handleChordKey('q'), "chord keys are swallowed on the help page");
    require(actionEditor.handleControlKey('z'), "control keys are swallowed on the help page");
    require(actionSequencer.getStepData(1, 2) == preHelpStep,
            "help-page input leaves step data unchanged");
    actionEditor.toggleHelpPage();
    require(actionEditor.getCurrentPage() == SequencerEditorPage::step,
            "leaving help restores the previous page");

    actionEditor.toggleHelpPage();
    actionEditor.selectPageShortcut(2);
    require(actionEditor.getCurrentPage() == SequencerEditorPage::sequence,
            "page shortcuts leave the help page");

    actionEditor.toggleHelpPage();
    actionEditor.resetAtCursor();
    require(actionEditor.getCurrentPage() == SequencerEditorPage::sequence,
            "backspace leaves the help page to the previous page");

    actionEditor.setCurrentSequence(1);
    actionEditor.setArmedSequence(1);
    require(actionEditor.getArmedSequence() == 1, "the current sequence can be armed before delete");
    require(actionEditor.deleteCurrentSequence(), "the editor can delete the current sequence");
    require(actionSequencer.howManySequences() == 4, "editor delete removes one sequence");
    require(actionSequencer.getSequence(1)->getMachineId() == 11.0,
            "editor delete shifts later sequences left");
    require(actionEditor.getCurrentSequence() == 1,
            "editor delete keeps the cursor on the replacement sequence");
    require(actionEditor.getArmedSequence() == SequencerAbs::notArmed,
            "editor delete unarms a deleted sequence");

    actionEditor.setCurrentSequence(1);
    actionEditor.setArmedSequence(2);
    require(actionEditor.deleteCurrentSequence(), "the editor can delete a sequence with a later armed sequence");
    require(actionSequencer.howManySequences() == 3, "a second editor delete removes one sequence");
    require(actionEditor.getArmedSequence() == 1,
            "editor delete shifts a later armed sequence left");

    Sequencer keyboardSequencer(1, 4);
    keyboardSequencer.stop();
    putNote(*keyboardSequencer.getSequence(0), 0, 60.0);
    SequencerEditor keyboardEditor(&keyboardSequencer);
    machines.notes.clear();
    require(keyboardEditor.handleChordKey('q'), "sequence-page chord key is handled");
    const auto sequenceChord = keyboardSequencer.getStepData(0, 0);
    require(sequenceChord.size() == 3 && sequenceChord[0][Step::noteInd] == 60.0
                && sequenceChord[1][Step::noteInd] == 64.0 && sequenceChord[2][Step::noteInd] == 67.0,
            "sequence-page chord key writes the expected major chord");
    require(machines.notes.size() == 3, "sequence-page chord auditions while stopped");

    keyboardEditor.gotoMixerPage();
    const auto beforePreview = keyboardSequencer.getStepData(0, 0);
    machines.notes.clear();
    require(keyboardEditor.handleNoteKey('z'), "mixer-page piano key is handled");
    require(keyboardEditor.handleChordKey('q'), "mixer-page chord key is handled");
    require(keyboardSequencer.getStepData(0, 0) == beforePreview,
            "mixer-page musical keys do not mutate sequence data");
    require(machines.notes.size() == 4, "mixer-page note and chord audition while stopped");
    keyboardSequencer.play();
    machines.notes.clear();
    require(keyboardEditor.handleNoteKey('x'), "playing mixer-page piano key is swallowed");
    require(machines.notes.empty(), "preview remains suppressed while playing");

    Sequencer navigationSequencer(1, 4);
    SequencerEditor navigationEditor(&navigationSequencer);
    putNote(*navigationSequencer.getSequence(0), 1, 65.0);
    navigationSequencer.toggleStepActive(0, 1);
    navigationSequencer.setStepData(0, 3,
        {{ static_cast<double>(CommandType::FilterCutoff), 1000.0, 0.0, 0.0 }});
    navigationEditor.setCurrentStep(0);
    require(navigationEditor.nextNonEmptyStep() && navigationEditor.getCurrentStep() == 1,
            "next populated step includes inactive note data");
    require(navigationEditor.nextNonEmptyStep() && navigationEditor.getCurrentStep() == 3,
            "next populated step includes control commands");
    navigationSequencer.resetSequence(0);
    navigationEditor.setCurrentStep(2);
    require(navigationEditor.nextNonEmptyStep() && navigationEditor.getCurrentStep() == 2,
            "next populated step leaves the cursor when every step is empty");

    Sequence octaveSequence(nullptr, 1, 0);
    octaveSequence.setStepData(0, {
        { static_cast<double>(CommandType::MidiNote), 60.0, 100.0, 1.0 },
        { static_cast<double>(CommandType::MidiNote), 64.0, 100.0, 1.0 }
    });
    auto octaveHead = octaveSequence.getReadHeadConfig(0);
    octaveHead.ticksPerStep = 1;
    octaveHead.octaves = 3;
    octaveSequence.setReadHeadConfig(0, octaveHead);
    machines.notes.clear();
    octaveSequence.resetReadHeads(true);
    for (int tick = 0; tick < 12; ++tick) octaveSequence.tick(true);
    require(machines.notes.size() == 24, "octave head triggers every row in each step");
    for (std::size_t index = 0; index < machines.notes.size(); index += 2)
    {
        const auto offset = static_cast<int>(machines.notes[index]) - 60;
        require((offset == 0 || offset == 12 || offset == 24)
                    && static_cast<int>(machines.notes[index + 1]) - 64 == offset,
                "one random octave layer is shared by every note row in a step");
    }
    octaveHead.octaves = 0;
    octaveSequence.setReadHeadConfig(0, octaveHead);
    require(octaveSequence.getReadHeadConfig(0).octaves == 1, "octave range clamps to one");
    octaveHead.octaves = 99;
    octaveSequence.setReadHeadConfig(0, octaveHead);
    require(octaveSequence.getReadHeadConfig(0).octaves == Sequence::maxOctaves, "octave range clamps to four");

    Sequence clampedOctaveSequence(nullptr, 1, 0);
    clampedOctaveSequence.setStepData(0,
        {{ static_cast<double>(CommandType::MidiNote), 127.0, 100.0, 1.0 }});
    auto clampedHead = clampedOctaveSequence.getReadHeadConfig(0);
    clampedHead.ticksPerStep = 1;
    clampedHead.octaves = 4;
    clampedOctaveSequence.setReadHeadConfig(0, clampedHead);
    machines.notes.clear();
    clampedOctaveSequence.resetReadHeads(true);
    for (int tick = 0; tick < 4; ++tick) clampedOctaveSequence.tick(true);
    require(std::all_of(machines.notes.begin(), machines.notes.end(), [] (unsigned short note) { return note == 127; }),
            "octave playback clamps high MIDI notes to 127");

    Sequence octaveControlSequence(nullptr, 1, 0);
    octaveControlSequence.setControlMode(true);
    octaveControlSequence.setStepData(0,
        {{ static_cast<double>(CommandType::FilterCutoff), 1234.0, 0.0, 0.0 }});
    auto controlOctaveHead = octaveControlSequence.getReadHeadConfig(0);
    controlOctaveHead.ticksPerStep = 1;
    controlOctaveHead.octaves = 4;
    octaveControlSequence.setReadHeadConfig(0, controlOctaveHead);
    machines.cutoffs.clear();
    octaveControlSequence.resetReadHeads(true);
    octaveControlSequence.tick(true);
    require(machines.cutoffs == std::vector<std::pair<unsigned short, double>>{{0, 1234.0}},
            "octave playback does not transpose control commands");

    Sequence implodeSequence(nullptr, 8, 0);
    for (std::size_t step = 0; step < 7; ++step)
        putNote(implodeSequence, step, 66.0 - static_cast<double>(step));
    implodeSequence.implodeNotes();
    require(implodeSequence.getLength() == 2, "seven notes implode into two chord steps");
    const auto firstChord = implodeSequence.getStepData(0);
    const auto secondChord = implodeSequence.getStepData(1);
    require(firstChord.size() == 4 && secondChord.size() == 3,
            "implode distributes seven notes as four and three");
    require(firstChord[0][Step::noteInd] == 60.0 && firstChord[1][Step::noteInd] == 62.0
                && firstChord[2][Step::noteInd] == 64.0 && firstChord[3][Step::noteInd] == 66.0
                && secondChord[0][Step::noteInd] == 61.0 && secondChord[1][Step::noteInd] == 63.0
                && secondChord[2][Step::noteInd] == 65.0,
            "implode stable-sorts then distributes notes round-robin");

    for (const auto& expected : std::vector<std::pair<std::size_t, std::size_t>>{{6, 2}, {8, 2}, {9, 3}, {12, 4}})
    {
        Sequence sizedImplode(nullptr, expected.first, 0);
        for (std::size_t step = 0; step < expected.first; ++step)
            putNote(sizedImplode, step, 48.0 + static_cast<double>(step));
        sizedImplode.implodeNotes();
        require(sizedImplode.getLength() == expected.second,
                "implode creates the expected number of chord steps");
    }

    Sequence emptyImplode(nullptr, 4, 0);
    emptyImplode.implodeNotes();
    require(emptyImplode.getLength() == 1 && !emptyImplode.stepHasTriggerableEvent(0),
            "imploding an empty sequence leaves one blank step");

    return 0;
}
