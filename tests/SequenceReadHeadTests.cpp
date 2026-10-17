#include <JuceHeader.h>

#include "ClockAbs.h"
#include "MachineUtilsAbs.h"
#include "MixerState.h"
#include "Sequencer.h"
#include "SequencerCommands.h"
#include "SequencerEditor.h"
#include "SequenceScopeUtils.h"
#include "TrackerFileBrowser.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <random>
#include <set>

// These fixtures intentionally compare exactly representable, discrete tracker
// fields (MIDI notes, command IDs, and integer-valued settings) stored as double.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wfloat-equal"

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
    void beginHeldNote(CommandType, unsigned short, unsigned short note,
                       unsigned short) override { notes.push_back(note); heldNotes.push_back(note); }
    void endHeldNote(CommandType, unsigned short, unsigned short note) override
    { releasedNotes.push_back(note); }
    std::string describeStepNote(CommandType, unsigned short, unsigned short note) const override
    { return std::to_string(note); }
    void sendQueuedMessages(long) override {}
    void toggleAuxSendForStack(unsigned short machineId, bool isAux1) override
    { auxToggles.emplace_back(machineId, isAux1); }
    void setFilterCutoffHzForStack(unsigned short machineId, double cutoffHz) override
    { cutoffs.emplace_back(machineId, cutoffHz); }
    std::vector<unsigned short> notes;
    std::vector<unsigned short> heldNotes;
    std::vector<unsigned short> releasedNotes;
    std::vector<std::pair<unsigned short, bool>> auxToggles;
    std::vector<std::pair<unsigned short, double>> cutoffs;
};

struct TestMachineHost final : MachineHost
{
    std::size_t getMachineCount(CommandType) const override { return 0; }
    MachineInterface* getMachine(CommandType, std::size_t) override { return nullptr; }
    const MachineInterface* getMachine(CommandType, std::size_t) const override { return nullptr; }
    std::size_t getMachineStackCount() const override { return muted.size(); }
    std::vector<CommandType> getMachineStackTypes(std::size_t) const override { return {}; }
    void addMachineToStack(std::size_t) override {}
    void removeMachineFromStack(std::size_t, std::size_t) override {}
    void cycleMachineTypeInStack(std::size_t, std::size_t, int) override {}
    void moveMachineInStack(std::size_t, std::size_t, int) override {}
    bool isMachineEnabledInStack(std::size_t, std::size_t) const override { return false; }
    void toggleMachineEnabledInStack(std::size_t, std::size_t) override {}
    float getMachineSendLevelDbInStack(std::size_t, std::size_t) const override { return 0.0f; }
    void adjustMachineSendLevelDbInStack(std::size_t, std::size_t, int) override {}
    bool machineHasReturnLevelInStack(std::size_t, std::size_t) const override { return false; }
    float getMachineReturnLevelDbInStack(std::size_t, std::size_t) const override { return 0.0f; }
    void adjustMachineReturnLevelDbInStack(std::size_t, std::size_t, int) override {}
    float getStackMeterLevel(std::size_t stack) const override { return meters[stack]; }
    float getStackGainDb(std::size_t stack) const override { return gains[stack]; }
    void setStackGainDb(std::size_t stack, float gainDb) override { gains[stack] = gainDb; }
    float getStackPan(std::size_t stack) const override { return stackPans[stack]; }
    void setStackPan(std::size_t stack, float pan) override { stackPans[stack] = MixerState::clampPan(pan); }
    float getMasterMeterLevel(std::size_t channel) const override { return masterMeters[channel]; }
    float getMasterGainDb() const override { return masterGain; }
    void setMasterGainDb(float gainDb) override { masterGain = MixerState::clampMasterGainDb(gainDb); }
    bool isMasterMuted() const override { return masterMuted; }
    void setMasterMuted(bool value) override { masterMuted = value; }
    bool isMasterRecording() const override { return masterRecording; }
    bool toggleMasterRecording() override { masterRecording = !masterRecording; return masterRecording; }
    float getAuxReturnMeterLevel(std::size_t aux, std::size_t channel) const override { return auxMeters[aux][channel]; }
    float getAuxReturnGainDb(std::size_t aux) const override { return auxGains[aux]; }
    void setAuxReturnGainDb(std::size_t aux, float gainDb) override { auxGains[aux] = MixerState::clampMasterGainDb(gainDb); }
    float getAuxReturnPan(std::size_t aux) const override { return auxPans[aux]; }
    void setAuxReturnPan(std::size_t aux, float pan) override { auxPans[aux] = MixerState::clampPan(pan); }
    bool isAuxReturnMuted(std::size_t aux) const override { return auxMuted[aux]; }
    void setAuxReturnMuted(std::size_t aux, bool value) override { auxMuted[aux] = value; }
    bool isStackMuted(std::size_t stack) const override { return muted[stack]; }
    bool isStackSolo(std::size_t stack) const override { return solo[stack]; }
    void setStackMuted(std::size_t stack, bool value) override { muted[stack] = value; }
    void setStackSolo(std::size_t stack, bool value) override { solo[stack] = value; }
    void silenceStack(std::size_t stack) override { silencedStacks.push_back(stack); }
    int getStackMidiOutputChannel(std::size_t) const override { return 1; }
    void adjustStackMidiOutputChannel(std::size_t, int) override {}

    std::vector<bool> muted { false, false };
    std::vector<bool> solo { false, false };
    std::vector<float> meters { 0.0f, 0.0f };
    std::vector<float> gains { 0.0f, 0.0f };
    std::vector<float> stackPans { 0.0f, 0.0f };
    std::array<float, 2> masterMeters { 0.0f, 0.0f };
    std::array<std::array<float, 2>, 2> auxMeters {};
    float masterGain = 0.0f;
    std::array<float, 2> auxGains { 0.0f, 0.0f };
    std::array<float, 2> auxPans { 0.0f, 0.0f };
    bool masterRecording = false;
    bool masterMuted = false;
    std::array<bool, 2> auxMuted { false, false };
    std::vector<std::size_t> silencedStacks;
};

struct TestAudioConfigurationHost final : AudioConfigurationHost
{
    AudioConfigurationSnapshot getAudioConfigurationSnapshot() override { return snapshot; }
    void refreshAudioDevices() override { ++refreshes; }
    void adjustAudioOutput(int direction) override { outputAdjust += direction; }
    void adjustAudioInput(int direction) override { inputAdjust += direction; }
    void adjustAudioSampleRate(int direction) override { rateAdjust += direction; }
    void adjustAudioBufferSize(int direction) override { bufferAdjust += direction; }
    void applyAudioConfigurationAsync() override { ++applies; }
    void revertAudioConfiguration() override { ++reverts; }
    void resetAudioPerformanceStats() override { ++resets; }

    AudioConfigurationSnapshot snapshot {
        true, false, true,
        { "", "Input" }, { "Output" },
        "", "Output", "", "Output",
        48000.0, 48000.0, 256, 256,
        128, 128, 24, 0.2, 0.4, 0, "ALSA", "OK"
    };
    int refreshes = 0, outputAdjust = 0, inputAdjust = 0, rateAdjust = 0, bufferAdjust = 0;
    int applies = 0, reverts = 0, resets = 0;
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
    const auto browserRoot = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile("myk-tracker-browser-test", "", false);
    require(browserRoot.createDirectory().wasOk(), "create temporary file-browser directory");
    const auto childDirectory = browserRoot.getChildFile("Alpha");
    require(childDirectory.createDirectory().wasOk(), "create browser child directory");
    require(browserRoot.getChildFile("zeta.myktracker").replaceWithText("state"), "create state file");
    require(browserRoot.getChildFile("audio.wav").replaceWithText("audio"), "create filtered file");
    require(browserRoot.getChildFile(".hidden.myktracker").replaceWithText("hidden"), "create hidden file");
    TrackerFileBrowser fileBrowser;
    fileBrowser.open(browserRoot);
    const auto stateEntries = fileBrowser.getEntries([] (const juce::File& file) { return file.hasFileExtension("myktracker"); });
    require(stateEntries.size() == 2 && stateEntries[0].isDirectory
                && stateEntries[0].file.getFileName() == "Alpha"
                && stateEntries[1].file.getFileName() == "zeta.myktracker",
            "browser sorts folders first and filters hidden/non-state files");
    fileBrowser.appendSearch('z');
    require(fileBrowser.getEntries([] (const juce::File& file) { return file.hasFileExtension("myktracker"); }).size() == 1,
            "browser search filters entries");
    fileBrowser.clearSearch();
    require(fileBrowser.browseInto(childDirectory), "browser enters child directory");
    fileBrowser.browseUp();
    require(fileBrowser.consumePreferredFocusName() == "Alpha", "browser preserves upward-navigation focus name");
    browserRoot.deleteRecursively();

    const auto uniqueScopeStacks = sequence_scope::collectUniqueStackIndices(
        { 3, 1, 3, -1, 16, 1, 0 }, 16);
    require(uniqueScopeStacks == std::vector<std::size_t>({ 3, 1, 0 }),
            "scope routing keeps valid stack IDs in first-seen order without duplicates");

    std::vector<float> silentScope(1024, 0.0f);
    std::vector<float> scopePreview;
    sequence_scope::reducePeakPreserving(silentScope, scopePreview);
    require(scopePreview.size() == sequence_scope::kPreviewPointCount
                && std::all_of(scopePreview.begin(), scopePreview.end(), [] (float value) { return std::abs(value) < 0.000001f; }),
            "silent scope reduces to a centred 96-point preview");

    std::vector<float> transientScope(1024, 0.0f);
    transientScope[123] = 0.91f;
    transientScope[130] = -0.84f;
    sequence_scope::reducePeakPreserving(transientScope, scopePreview);
    require(std::find(scopePreview.begin(), scopePreview.end(), 0.91f) != scopePreview.end()
                && std::find(scopePreview.begin(), scopePreview.end(), -0.84f) != scopePreview.end(),
            "scope reduction preserves brief positive and negative peaks");

    std::vector<float> chronologicalScope(96, 0.0f);
    for (std::size_t pair = 0; pair < 48; ++pair)
    {
        chronologicalScope[pair * 2] = 0.7f;
        chronologicalScope[pair * 2 + 1] = -0.6f;
    }
    sequence_scope::reducePeakPreserving(chronologicalScope, scopePreview);
    require(std::abs(scopePreview[0] - 0.7f) < 0.000001f
                && std::abs(scopePreview[1] + 0.6f) < 0.000001f,
            "scope reduction retains the source order of each bucket extrema");

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
    keyboardEditor.releaseKeyboardAuditionKey('q');
    require(machines.releasedNotes.size() == 3,
            "releasing an auditioned chord key sends one note-off per chord note");

    keyboardEditor.gotoMixerPage();
    const auto beforePreview = keyboardSequencer.getStepData(0, 0);
    machines.notes.clear();
    require(keyboardEditor.handleNoteKey('z'), "mixer-page piano key is handled");
    require(keyboardEditor.handleChordKey('q'), "mixer-page chord key is handled");
    require(keyboardSequencer.getStepData(0, 0) == beforePreview,
            "mixer-page musical keys do not mutate sequence data");
    require(machines.notes.size() == 4, "mixer-page note and chord audition while stopped");
    require(machines.notes[1] == machines.notes[0]
                && machines.notes[2] == machines.notes[0] + 4
                && machines.notes[3] == machines.notes[0] + 7,
            "preview-only chord root follows the last auditioned piano key");
    const auto auditionCount = machines.notes.size();
    require(keyboardEditor.handleNoteKey('z'), "repeated held piano key is handled");
    require(machines.notes.size() == auditionCount,
            "repeated piano-key press does not retrigger an existing held audition");
    keyboardEditor.releaseKeyboardAuditionKey('z');
    keyboardEditor.releaseKeyboardAuditionKey('q');

    keyboardEditor.gotoSequencePage();
    keyboardEditor.setCurrentStep(1);
    keyboardSequencer.setStepData(0, 1,
        {{ static_cast<double>(CommandType::MidiNote), 62.0, 100.0, 1.0 },
         { static_cast<double>(CommandType::MidiNote), 65.0, 100.0, 1.0 }});
    const auto beforeStepTrigger = keyboardSequencer.getStepData(0, 1);
    machines.notes.clear();
    require(keyboardEditor.triggerCurrentStep(), "Return-style Sequence-page step trigger is handled");
    require(machines.notes == std::vector<unsigned short>({62, 65}),
            "Sequence-page step trigger auditions every stored chord row");
    require(keyboardSequencer.getStepData(0, 1) == beforeStepTrigger,
            "Sequence-page step trigger does not edit step data");

    keyboardSequencer.toggleStepActive(0, 1);
    keyboardEditor.gotoStepPage();
    machines.notes.clear();
    require(keyboardEditor.triggerCurrentStep(), "Return-style Step-page step trigger is handled");
    require(machines.notes == std::vector<unsigned short>({62, 65}),
            "Step-page trigger auditions stored data even when the step is inactive");
    require(!keyboardSequencer.isStepActive(0, 1),
            "Step-page trigger does not change the step active state");

    keyboardSequencer.play();
    machines.notes.clear();
    require(keyboardEditor.triggerCurrentStep(), "playing Step-page trigger is swallowed");
    require(machines.notes.empty(), "step audition remains suppressed while playing");
    keyboardEditor.gotoMixerPage();
    require(keyboardEditor.handleNoteKey('x'), "playing mixer-page piano key is swallowed");
    require(machines.notes.empty(), "preview remains suppressed while playing");

    Sequencer tpsSequencer(1, 4);
    SequencerEditor tpsEditor(&tpsSequencer);
    TestMachineHost tpsHost;
    tpsEditor.setMachineHost(&tpsHost);
    tpsEditor.gotoSequenceConfigPage();
    tpsEditor.moveCursorDown();
    tpsEditor.moveCursorDown();
    tpsEditor.moveCursorDown();
    require(tpsEditor.getCurrentSeqParam() == Sequence::tpsConfig,
            "TPS row is reachable on the sequence configuration page");
    tpsSequencer.play();
    tpsEditor.incrementAtCursor();
    require(tpsSequencer.getSequence(0)->getReadHeadConfig(0).ticksPerStep == 5,
            "TPS increment updates the selected read head");
    require(tpsSequencer.getSequence(0)->isWaitingForQuarterBeatResync(),
            "TPS increment keeps the existing quarter-beat restart behaviour");
    tpsEditor.decrementAtCursor();
    require(tpsSequencer.getSequence(0)->getReadHeadConfig(0).ticksPerStep == 4,
            "TPS decrement updates the selected read head");
    require(tpsHost.silencedStacks.empty(),
            "TPS edits preserve active stack voices and pending note-offs");
    tpsEditor.moveCursorUp();
    tpsEditor.moveCursorUp();
    require(tpsEditor.getCurrentSeqParam() == Sequence::headCountConfig,
            "HEADS row is reachable after TPS");
    tpsEditor.incrementAtCursor();
    require(tpsHost.silencedStacks.empty()
                && tpsSequencer.getSequence(0)->isWaitingForQuarterBeatResync(),
            "HEADS edits retain locked resync without silencing active voices");
    tpsEditor.moveCursorDown();
    tpsEditor.moveCursorDown();
    tpsEditor.moveCursorDown();
    require(tpsEditor.getCurrentSeqParam() == Sequence::modeConfig,
            "MODE row is reachable after HEADS");
    tpsEditor.incrementAtCursor();
    require(tpsHost.silencedStacks.empty(), "MODE edits do not silence active voices");
    tpsEditor.moveCursorDown();
    tpsEditor.moveCursorDown();
    tpsEditor.moveCursorDown();
    require(tpsEditor.getCurrentSeqParam() == Sequence::rhythmConfig,
            "RHY row is reachable after MODE");
    tpsEditor.incrementAtCursor();
    require(tpsHost.silencedStacks.empty(), "RHY edits do not silence active voices");

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
    std::mt19937 expectedOctaveRandom { 0x4f435431u };
    std::uniform_int_distribution<int> expectedOctaveLayer(0, 2);
    bool foundIndependentlyTransposedChord = false;
    for (std::size_t index = 0; index < machines.notes.size(); index += 2)
    {
        const int firstOffset = 12 * expectedOctaveLayer(expectedOctaveRandom);
        const int secondOffset = 12 * expectedOctaveLayer(expectedOctaveRandom);
        require(machines.notes[index] == static_cast<unsigned short>(60 + firstOffset)
                    && machines.notes[index + 1] == static_cast<unsigned short>(64 + secondOffset),
                "each note row receives its own deterministic octave RNG draw");
        foundIndependentlyTransposedChord = foundIndependentlyTransposedChord || firstOffset != secondOffset;
    }
    require(foundIndependentlyTransposedChord,
            "octave-expanded chords contain independently transposed note rows");
    octaveHead.octaves = 1;
    octaveSequence.setReadHeadConfig(0, octaveHead);
    machines.notes.clear();
    octaveSequence.resetReadHeads(true);
    octaveSequence.tick(true);
    require(machines.notes == std::vector<unsigned short>({60, 64}),
            "OCT:1 preserves untransposed chord playback");
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

    Sequence controlThenNoteSequence(nullptr, 2, 0);
    controlThenNoteSequence.setControlMode(true);
    controlThenNoteSequence.setStepData(0,
        {{ static_cast<double>(CommandType::FilterCutoff), 1234.0, 0.0, 0.0 }});
    controlThenNoteSequence.setStepData(1,
        {{ static_cast<double>(CommandType::MidiNote), 60.0, 100.0, 1.0 }});
    auto controlThenNoteHead = controlThenNoteSequence.getReadHeadConfig(0);
    controlThenNoteHead.ticksPerStep = 1;
    controlThenNoteHead.octaves = 3;
    controlThenNoteSequence.setReadHeadConfig(0, controlThenNoteHead);
    machines.notes.clear();
    controlThenNoteSequence.resetReadHeads(true);
    controlThenNoteSequence.tick(true);
    controlThenNoteSequence.tick(true);
    std::mt19937 firstOctaveRandom { 0x4f435431u };
    std::uniform_int_distribution<int> firstOctaveLayer(0, 2);
    require(machines.notes == std::vector<unsigned short>({ static_cast<unsigned short>(60 + 12 * firstOctaveLayer(firstOctaveRandom)) }),
            "control commands do not consume octave RNG draws");

    Sequencer transposeSequencer(1, 3);
    SequencerEditor transposeEditor(&transposeSequencer);
    transposeEditor.gotoSequencePage();
    transposeSequencer.setStepData(0, 0, {
        { static_cast<double>(CommandType::MidiNote), 60.0, 100.0, 1.0 },
        { static_cast<double>(CommandType::WavetableSynth), 72.0, 100.0, 1.0 },
        { static_cast<double>(CommandType::FilterCutoff), 1234.0, 0.0, 0.0 }
    });
    transposeSequencer.setStepData(0, 1,
        {{ static_cast<double>(CommandType::Sampler), 120.0, 100.0, 1.0 }});
    transposeSequencer.toggleStepActive(0, 1);
    transposeSequencer.setStepData(0, 2, {
        { static_cast<double>(CommandType::MidiNote), 5.0, 100.0, 1.0 },
        { static_cast<double>(CommandType::Sampler), 13.0, 100.0, 1.0 }
    });
    const auto noteEntryOctave = transposeEditor.getCurrentOctave();
    transposeEditor.transposeCurrentSequence(-12);
    const auto transposedFirstStep = transposeSequencer.getStepData(0, 0);
    require(transposedFirstStep[0][Step::noteInd] == 48.0
                && transposedFirstStep[1][Step::noteInd] == 60.0
                && transposedFirstStep[2][Step::noteInd] == 1234.0,
            "sequence transpose shifts every playable chord row but not control values");
    require(transposeSequencer.getStepData(0, 1)[0][Step::noteInd] == 108.0,
            "sequence transpose includes inactive steps");
    require(transposeSequencer.getStepData(0, 2)[0][Step::noteInd] == 5.0
                && transposeSequencer.getStepData(0, 2)[1][Step::noteInd] == 1.0,
            "sequence transpose preserves too-low notes and never creates MIDI zero");
    require(transposeEditor.getCurrentOctave() == noteEntryOctave,
            "sequence transpose does not alter the note-entry octave");
    transposeSequencer.setStepData(0, 2,
        {{ static_cast<double>(CommandType::MidiNote), 123.0, 100.0, 1.0 }});
    transposeEditor.transposeCurrentSequence(12);
    require(transposeSequencer.getStepData(0, 0)[0][Step::noteInd] == 60.0
                && transposeSequencer.getStepData(0, 0)[1][Step::noteInd] == 72.0,
            "sequence transpose restores unclamped notes when shifted back up");
    require(transposeSequencer.getStepData(0, 2)[0][Step::noteInd] == 123.0,
            "sequence transpose preserves notes that would exceed MIDI 127");

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

    const auto ownSolo = MixerState::getSequencePresentation(true, false, false, false);
    require(ownSolo.stackAudible && ownSolo.soloOutline,
            "sequence solo remains visible when no stack override is active");
    const auto stackMuted = MixerState::getSequencePresentation(true, true, false, false);
    require(!stackMuted.stackAudible && !stackMuted.soloOutline,
            "stack mute overrides sequence solo presentation");
    const auto nonSoloedStack = MixerState::getSequencePresentation(true, false, false, true);
    require(!nonSoloedStack.stackAudible && !nonSoloedStack.soloOutline,
            "global stack solo hides sequences routed to non-soloed stacks");
    const auto mutedSoloedStack = MixerState::getSequencePresentation(false, true, true, true);
    require(!mutedSoloedStack.stackAudible && mutedSoloedStack.soloOutline,
            "a muted soloed stack stays silent but keeps its yellow outline");

    require(MixerState::clampMasterGainDb(-100.0f) == -48.0f
                && MixerState::clampMasterGainDb(100.0f) == 6.0f
                && MixerState::clampMasterGainDb(std::numeric_limits<float>::infinity()) == 0.0f,
            "master gain clamps to the mixer range");
    require(std::abs(MixerState::masterGainToLinear(0.0f) - 1.0f) < 0.0001f,
            "zero dB master gain is unity");
    require(std::abs(MixerState::masterGainToLinear(6.0f) - 1.995262f) < 0.0001f
                && std::abs(MixerState::masterGainToLinear(-6.0f) - 0.501187f) < 0.0001f,
            "positive and negative master gain use decibel scaling");

    Sequencer mixerSequencer(1, 4);
    SequencerEditor mixerEditor(&mixerSequencer);
    TestMachineHost mixerHost;
    mixerEditor.setMachineHost(&mixerHost);
    mixerEditor.gotoMixerPage();
    require(mixerEditor.isMixerMasterSelected(), "mixer opens with the master column selected");
    mixerEditor.moveCursorDown();
    mixerEditor.incrementAtCursor();
    require(mixerHost.masterGain == 1.0f, "master gain row adjusts by one decibel");
    mixerEditor.moveCursorDown();
    mixerEditor.cycleAtCursor();
    require(mixerHost.masterRecording, "activating the master R cell starts recording");
    mixerEditor.cycleAtCursor();
    require(!mixerHost.masterRecording, "activating the master R cell again stops recording");
    mixerEditor.moveCursorDown();
    mixerEditor.decrementAtCursor();
    require(mixerHost.masterGain == 0.0f, "master fader rows adjust master gain");
    mixerEditor.toggleMuteCurrentMixerStack();
    mixerEditor.toggleSoloCurrentMixerStack();
    require(mixerHost.masterMuted && !mixerHost.muted[0] && !mixerHost.solo[0],
            "mixer mute toggles master while solo does nothing on master");
    mixerEditor.moveCursorRight();
    require(mixerEditor.getSelectedMixerAux() == std::optional<std::size_t>(0),
            "first aux return follows the master column");
    mixerEditor.moveCursorUp();
    mixerEditor.moveCursorUp();
    mixerEditor.incrementAtCursor();
    require(mixerHost.auxGains[0] == 1.0f, "aux gain row adjusts by one decibel");
    mixerEditor.moveCursorDown();
    mixerEditor.moveCursorDown();
    mixerEditor.incrementAtCursor();
    require(std::abs(mixerHost.auxPans[0] - 0.05f) < 0.0001f,
            "aux PAN row adjusts in five-percent steps");
    mixerEditor.toggleMuteCurrentMixerStack();
    mixerEditor.toggleSoloCurrentMixerStack();
    require(mixerHost.auxMuted[0] && !mixerHost.solo[0],
            "mixer mute toggles aux while solo does nothing on aux returns");
    mixerEditor.moveCursorRight();
    require(mixerEditor.getSelectedMixerAux() == std::optional<std::size_t>(1),
            "second aux return follows the first");
    mixerEditor.moveCursorRight();
    require(mixerEditor.getSelectedMixerStack() == std::optional<std::size_t>(0),
            "first stack retains stack id zero after master and aux insertions");
    mixerEditor.incrementAtCursor();
    require(std::abs(mixerHost.stackPans[0] - 0.05f) < 0.0001f,
            "stack PAN row adjusts in five-percent steps");
    mixerEditor.toggleMuteCurrentMixerStack();
    mixerEditor.toggleSoloCurrentMixerStack();
    require(mixerHost.muted[0] && mixerHost.solo[0],
            "mixer shortcuts edit the selected stack's canonical mute and solo state");

    Sequencer sequenceMuteSequencer(1, 4);
    SequencerEditor sequenceMuteEditor(&sequenceMuteSequencer);
    TestMachineHost sequenceMuteHost;
    sequenceMuteEditor.setMachineHost(&sequenceMuteHost);
    auto* routedSequence = sequenceMuteSequencer.getSequence(0);
    routedSequence->setMachineId(1.0);

    sequenceMuteEditor.toggleMuteCurrentSequence();
    require(routedSequence->isMuted() && !sequenceMuteHost.muted[1],
            "sequence mute still mutes the sequence when its routed stack is audible");
    sequenceMuteHost.muted[1] = true;
    sequenceMuteEditor.toggleMuteCurrentSequence();
    require(!routedSequence->isMuted() && !sequenceMuteHost.muted[1],
            "unmuting a sequence clears both sequence and routed-stack mute states");

    sequenceMuteHost.muted[1] = true;
    sequenceMuteEditor.toggleMuteCurrentSequence();
    require(!routedSequence->isMuted() && !sequenceMuteHost.muted[1],
            "unmuting an effectively muted sequence clears a routed-stack-only mute");

    Sequencer audioConfigSequencer(1, 4);
    SequencerEditor audioConfigEditor(&audioConfigSequencer);
    TestAudioConfigurationHost audioConfigHost;
    audioConfigEditor.setAudioConfigurationHost(&audioConfigHost);
    require(audioConfigEditor.selectPageShortcut(0)
                && audioConfigEditor.getCurrentPage() == SequencerEditorPage::audioConfig,
            "zero selects the audio configuration page");
    require(audioConfigHost.refreshes == 1, "entering audio configuration refreshes device names");
    audioConfigEditor.moveCursorDown();
    audioConfigEditor.moveCursorRight();
    audioConfigEditor.incrementAtCursor();
    require(audioConfigHost.outputAdjust == 1, "audio output selector supports bracket adjustment");
    audioConfigEditor.moveCursorLeft();
    for (int i = 0; i < 4; ++i) audioConfigEditor.moveCursorDown();
    audioConfigEditor.click();
    require(audioConfigHost.applies == 1, "audio configuration apply activates through the grid");
    const auto& audioCells = audioConfigEditor.getAudioConfigCells();
    require(audioCells.size() == 4 && audioCells[3][4].text == "5.33MS",
            "audio configuration reports current block duration");
    audioConfigHost.snapshot.canApply = false;
    audioConfigEditor.click();
    require(audioConfigHost.applies == 1, "disabled audio apply does not activate");

    return 0;
}

#pragma GCC diagnostic pop
