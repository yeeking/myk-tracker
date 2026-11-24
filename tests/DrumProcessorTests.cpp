#include <JuceHeader.h>

#include "DrumSynthMachine.h"
#include "McpRouter.h"
#include "PortableTrackerState.h"
#include "TrackerControlService.h"
#include "TrackerMainProcessor.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>

namespace
{
void require(bool condition, const char* message)
{
    if (!condition)
    {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

double energy(const juce::AudioBuffer<float>& buffer)
{
    double result = 0.0;
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
            result += std::abs(static_cast<double>(buffer.getSample(channel, sample)));
    return result;
}

std::size_t findSlot(const TrackerMainProcessor& processor, CommandType type, std::size_t stackIndex = 0)
{
    const auto types = processor.getMachineStackTypes(stackIndex);
    const auto found = std::find(types.begin(), types.end(), type);
    return found == types.end() ? types.size() : static_cast<std::size_t>(found - types.begin());
}

std::size_t focusedCount(const TrackerMainProcessor& processor)
{
    const auto focused = processor.getFocusedParameters();
    return static_cast<std::size_t>(std::count_if(focused.begin(), focused.end(),
                                                  [] (const auto& item) { return item.occupied; }));
}

std::size_t focusedSlot(const TrackerMainProcessor& processor, const std::string& key)
{
    const auto focused = processor.getFocusedParameters();
    const auto found = std::find_if(focused.begin(), focused.end(), [&key] (const auto& item)
    {
        return item.occupied && item.focusKey == key;
    });
    return found == focused.end() ? focused.size() : static_cast<std::size_t>(found - focused.begin());
}

juce::var machineControlArgs(int stackId, const juce::String& slotId, const juce::String& controlId,
                              const juce::String& action)
{
    auto value = juce::var(new juce::DynamicObject());
    auto* object = value.getDynamicObject();
    object->setProperty("stackId", stackId);
    object->setProperty("slotId", slotId);
    object->setProperty("controlId", controlId);
    object->setProperty("action", action);
    return value;
}

juce::String fmSlotIdFromMachineControls(const juce::var& state)
{
    const auto controls = state.getProperty("machineControls", juce::var());
    if (!controls.isArray()) return {};
    for (const auto& machine : *controls.getArray())
    {
        const auto grid = machine.getProperty("controls", juce::var());
        if (static_cast<int>(machine.getProperty("stackId", -1)) != 0 || !grid.isArray() || grid.getArray()->size() != 15)
            continue;
        const auto& firstColumn = grid.getArray()->getReference(0);
        if (firstColumn.isArray() && !firstColumn.getArray()->isEmpty()
            && firstColumn.getArray()->getReference(0).getProperty("label", {}).toString() == "PSET")
            return machine.getProperty("slotId", {}).toString();
    }
    return {};
}

void requireFocusedParameterCoverage()
{
    TrackerMainProcessor processor;
    processor.prepareToPlay(48000.0, 128);

    for (std::size_t stack = 0; stack < processor.getMachineStackCount(); ++stack)
        while (findSlot(processor, CommandType::DistortionFx, stack) >= processor.getMachineStackTypes(stack).size())
            processor.addMachineToStack(stack);

    const auto firstDistortion = findSlot(processor, CommandType::DistortionFx, 0);
    require(processor.toggleFocusedMachineParameter(0, firstDistortion, "grid-1-0", 0, 1),
            "DIST drive can be focused");
    require(focusedCount(processor) == 1, "first focus entry is added");
    require(processor.toggleFocusedMachineParameter(0, firstDistortion, "grid-1-0", 0, 1),
            "focusing the same address toggles it off");
    require(focusedCount(processor) == 0, "duplicate focus does not consume another slot");
    require(processor.toggleFocusedMachineParameter(0, firstDistortion, "grid-1-0", 0, 1),
            "DIST drive can be restored to focus");
    require(processor.toggleFocusedMachineParameter(0, firstDistortion, "grid-1-1", 1, 1),
            "DIST tone can be focused");

    require(processor.toggleFocusedStackEnable(0, firstDistortion), "stack-enable can be focused");
    const auto enableFocus = focusedSlot(processor, "stack-enabled");
    require(enableFocus < TrackerMainProcessor::kFocusedParameterCapacity,
            "stack-enable focus entry is addressable");
    const bool wasEnabled = processor.isMachineEnabledInStack(0, firstDistortion);
    require(processor.adjustFocusedParameter(enableFocus, 1, false), "focused stack-enable adjusts");
    require(processor.isMachineEnabledInStack(0, firstDistortion) != wasEnabled,
            "focused stack-enable toggles the source slot");
    require(!processor.getFocusedParameters()[enableFocus].modulatable,
            "stack-enable is direct-only, not a modulation destination");
    require(processor.removeFocusedParameter(enableFocus), "focused stack-enable can be removed");

    for (std::size_t stack = 0; stack < processor.getMachineStackCount(); ++stack)
    {
        const auto distortion = findSlot(processor, CommandType::DistortionFx, stack);
        for (const auto* key : { "grid-1-0", "grid-1-1" })
        {
            const auto existing = focusedSlot(processor, key);
            const bool alreadyFocusedHere = existing < TrackerMainProcessor::kFocusedParameterCapacity
                && processor.getFocusedParameters()[existing].stackIndex == stack;
            if (!alreadyFocusedHere)
                require(processor.toggleFocusedMachineParameter(stack, distortion, key, key[7] - '0', 1),
                        "DIST control fills a Focus slot");
        }
    }
    require(focusedCount(processor) == TrackerMainProcessor::kFocusedParameterCapacity,
            "all sixteen Focus slots are available");
    require(!processor.toggleFocusedMachineParameter(0, firstDistortion, "grid-1-2", 2, 1),
            "Focus refuses a seventeenth entry");
    for (const auto& item : processor.getFocusedParameters())
        require(item.modulatable, "only declared DIST controls are marked modulatable");

    const auto driveFocus = focusedSlot(processor, "grid-1-0");
    UIBox before;
    require(processor.getFocusedParameterCell(driveFocus, before), "focused control resolves its current cell");
    require(processor.adjustFocusedParameter(driveFocus, 1, true), "focused adjustment invokes the machine callback");
    UIBox after;
    require(processor.getFocusedParameterCell(driveFocus, after) && after.valueNorm >= before.valueNorm,
            "focused adjustment updates the direct performance control");

    juce::MemoryBlock state;
    processor.getStateInformation(state);
    TrackerMainProcessor restored;
    restored.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    require(focusedCount(restored) == TrackerMainProcessor::kFocusedParameterCapacity,
            "all Focus entries survive a tracker-state round trip");

    juce::MemoryInputStream input(state, false);
    auto xml = juce::parseXML(input.readEntireStreamAsString());
    require(xml != nullptr, "Focus state serializes as XML");
    auto tree = juce::ValueTree::fromXml(*xml);
    auto json = juce::JSON::fromString(tree.getProperty("json").toString());
    auto entries = json.getProperty("focusedParameters", juce::var());
    require(entries.isArray() && !entries.getArray()->isEmpty(), "Focus state contains persistent entries");
    entries.getArray()->getReference(0).getDynamicObject()->setProperty("machineSlotId", "stale-slot");
    tree.setProperty("json", juce::JSON::toString(json), nullptr);
    juce::MemoryBlock staleState;
    juce::MemoryOutputStream staleOutput(staleState, false);
    tree.createXml()->writeTo(staleOutput);
    TrackerMainProcessor staleRestored;
    staleRestored.setStateInformation(staleState.getData(), static_cast<int>(staleState.getSize()));
    require(focusedCount(staleRestored) == TrackerMainProcessor::kFocusedParameterCapacity - 1,
            "stale Focus slot identities are discarded during restore");

    processor.removeMachineFromStack(0, firstDistortion);
    require(focusedCount(processor) == TrackerMainProcessor::kFocusedParameterCapacity - 2,
            "removing a machine removes its focused controls");
    const auto nextDistortion = findSlot(processor, CommandType::DistortionFx, 1);
    processor.cycleMachineTypeInStack(1, nextDistortion, 1);
    require(focusedCount(processor) == TrackerMainProcessor::kFocusedParameterCapacity - 4,
            "changing a slot type removes focused controls bound to its old type");
    processor.releaseResources();
}

void requireMachineControlMcpCoverage()
{
    TrackerMainProcessor processor;
    processor.prepareToPlay(48000.0, 128);
    while (findSlot(processor, CommandType::FmSynth) >= processor.getMachineStackTypes(0).size())
        processor.addMachineToStack(0);

    auto& control = processor.getControlService();
    const auto machineCommand = [] (juce::var arguments)
    {
        TrackerControlService::Command command;
        command.kind = TrackerControlService::CommandKind::machineControl;
        command.arguments = std::move(arguments);
        return command;
    };
    const auto discovery = control.getState("machine_controls");
    require(discovery.ok, "machine-control discovery succeeds");
    const auto fmSlotId = fmSlotIdFromMachineControls(discovery.data);
    require(fmSlotId.isNotEmpty(), "MCP exposes FM's full logical 15-column control grid");

    auto focus = machineControlArgs(0, fmSlotId, "pset-1-0", "focus");
    auto focused = control.execute(machineCommand(focus));
    require(focused.ok, focused.message.toRawUTF8());
    require(focusedCount(processor) == 1, "MCP focuses FM by stable stack/slot/control identity");
    auto adjust = machineControlArgs(0, fmSlotId, "pset-1-0", "adjust");
    adjust.getDynamicObject()->setProperty("direction", 1);
    require(control.execute(machineCommand(adjust)).ok,
            "MCP adjusts FM without cursor-grid addressing");
    auto unfocus = machineControlArgs(0, fmSlotId, "pset-1-0", "unfocus");
    require(control.execute(machineCommand(unfocus)).ok
                && focusedCount(processor) == 0,
            "MCP unfocuses FM by stable identity");

    const auto controls = discovery.data.getProperty("machineControls", juce::var());
    for (const auto& machine : *controls.getArray())
        if (machine.getProperty("slotId", {}).toString() == "mod-matrix")
            for (const auto& column : *machine.getProperty("controls", juce::var()).getArray())
                for (const auto& cell : *column.getArray())
                    require(!static_cast<bool>(cell.getProperty("focusable", true)),
                            "MOD controls accurately advertise focusable: false");

    McpRouter router(control);
    const auto modernRequest = [] (const juce::String& method, const juce::String& name, const juce::String& params)
    {
        return McpRouter::Request { "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"" + method
                + "\",\"params\":" + params + "}", "2026-07-28", method, name };
    };
    const auto modernTools = router.route(modernRequest("tools/list", "tools/list",
        "{\"_meta\":{\"io.modelcontextprotocol/protocolVersion\":\"2026-07-28\"}}"));
    require(modernTools.status == 200 && modernTools.json.getProperty("result", juce::var()).isObject(),
            "modern MCP handshake discovers tools");
    const auto legacyInitialise = router.route({ "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{\"protocolVersion\":\"2025-11-25\"}}", {}, {}, {} });
    require(legacyInitialise.status == 200 && legacyInitialise.sessionId.isNotEmpty(),
            "legacy MCP initialize handshake remains supported");
    const auto legacyReady = router.route({ "{\"jsonrpc\":\"2.0\",\"method\":\"notifications/initialized\",\"params\":{}}", {}, {}, {} });
    require(legacyReady.status == 202 && !legacyReady.hasBody, "legacy MCP initialized notification succeeds");
    const auto legacyTools = router.route({ "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"tools/list\",\"params\":{} }", {}, {}, {} });
    require(legacyTools.status == 200 && legacyTools.json.getProperty("result", juce::var()).isObject(),
            "legacy MCP handshake discovers tools");
    processor.releaseResources();
}

void requirePermissiveProjectVersionLoading()
{
    require(static_cast<std::size_t>(CommandType::MidiNote) == 0
                && static_cast<std::size_t>(CommandType::Sampler) == 2
                && static_cast<std::size_t>(CommandType::WavetableSynth) == 4
                && static_cast<std::size_t>(CommandType::DistortionFx) == 6
                && static_cast<std::size_t>(CommandType::FilterFx) == 11
                && static_cast<std::size_t>(CommandType::ModulationMatrix) == 15
                && static_cast<std::size_t>(CommandType::DrumSynth) == 16
                && static_cast<std::size_t>(CommandType::FmSynth) == 17,
            "persisted command IDs retain their historical numeric values");

    TrackerMainProcessor source;
    source.setMasterGainDb(-12.0f);
    juce::MemoryBlock stateData;
    source.getStateInformation(stateData);

    TrackerMainProcessor currentRestored;
    currentRestored.setStateInformation(stateData.getData(), static_cast<int>(stateData.getSize()));
    require(std::abs(currentRestored.getMasterGainDb() + 12.0f) < 0.001f,
            "current tracker schema restores normally");

    const auto directory = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile("myk-project-load-test", "", false);
    require(directory.createDirectory().wasOk(), "create tracker loading test directory");
    const auto stateOnlyFile = directory.getChildFile("current-state.myktracker");
    require(stateOnlyFile.replaceWithData(stateData.getData(), stateData.getSize()),
            "write current state-only tracker file");
    TrackerMainProcessor stateOnlyRestored;
    juce::String error;
    require(stateOnlyRestored.loadTrackerStateFile(stateOnlyFile, error)
                && std::abs(stateOnlyRestored.getMasterGainDb() + 12.0f) < 0.001f,
            "current state-only tracker file loads");

    const auto portableFile = directory.getChildFile("current-portable.myktracker");
    require(source.writePortableStateFile(portableFile, error),
            "write current portable tracker file");
    TrackerMainProcessor portableRestored;
    require(portableRestored.loadTrackerStateFile(portableFile, error)
                && std::abs(portableRestored.getMasterGainDb() + 12.0f) < 0.001f,
            "current portable tracker file loads");

    juce::MemoryInputStream input(stateData, false);
    auto xml = juce::parseXML(input.readEntireStreamAsString());
    require(xml != nullptr, "processor state serializes as XML");
    auto stateTree = juce::ValueTree::fromXml(*xml);
    auto state = juce::JSON::fromString(stateTree.getProperty("json").toString());
    require(state.isObject(), "processor state contains tracker JSON");
    state.getDynamicObject()->removeProperty("stateVersion");
    state.getDynamicObject()->setProperty("masterGainDb", -30.0f);
    stateTree.setProperty("json", juce::JSON::toString(state), nullptr);

    juce::MemoryBlock unversionedState;
    juce::MemoryOutputStream output(unversionedState, false);
    stateTree.createXml()->writeTo(output);

    currentRestored.setMasterGainDb(-6.0f);
    currentRestored.setStateInformation(unversionedState.getData(), static_cast<int>(unversionedState.getSize()));
    require(std::abs(currentRestored.getMasterGainDb() + 30.0f) < 0.001f,
            "unversioned tracker state is interpreted as the current layout");

    const auto unversionedFile = directory.getChildFile("unversioned.myktracker");
    require(unversionedFile.replaceWithData(unversionedState.getData(), unversionedState.getSize()),
            "write unversioned tracker fixture");
    currentRestored.setMasterGainDb(-6.0f);
    error.clear();
    require(currentRestored.loadTrackerStateFile(unversionedFile, error)
                && std::abs(currentRestored.getMasterGainDb() + 30.0f) < 0.001f,
            "file loader accepts an unversioned project as the current layout");

    const auto unversionedPortableFile = directory.getChildFile("unversioned-portable.myktracker");
    require(portable_tracker_state::write(unversionedPortableFile, unversionedState, {}, error),
            "write unversioned portable tracker fixture");
    currentRestored.setMasterGainDb(-6.0f);
    require(currentRestored.loadTrackerStateFile(unversionedPortableFile, error)
                && std::abs(currentRestored.getMasterGainDb() + 30.0f) < 0.001f,
            "portable loader accepts an unversioned project as the current layout");

    state.getDynamicObject()->setProperty("stateVersion", 999);
    state.getDynamicObject()->setProperty("masterGainDb", -18.0f);
    stateTree.setProperty("json", juce::JSON::toString(state), nullptr);
    juce::MemoryBlock mismatchedState;
    juce::MemoryOutputStream mismatchedOutput(mismatchedState, false);
    stateTree.createXml()->writeTo(mismatchedOutput);
    currentRestored.setStateInformation(mismatchedState.getData(), static_cast<int>(mismatchedState.getSize()));
    require(std::abs(currentRestored.getMasterGainDb() + 18.0f) < 0.001f,
            "mismatched tracker version is interpreted as the current layout");
    require(directory.deleteRecursively(), "remove tracker loading test directory");
}
}

// Regression: a song-row switch replaces the playback sequencer mid-callback.
// The transport-stop detection at the end of processBlock must re-resolve the
// playback sequencer; using the stale pointer cached at the top of the callback
// read the old, now-stopped sequencer, took that as a transport stop, and fired
// an all-notes-off that swallowed the incoming pattern's downbeat (its first
// note is already queued by then). The user heard the groove "keep time" with
// the downbeat kick missing.
void requireSongSwitchPreservesIncomingDownbeat()
{
    TrackerMainProcessor processor;
    processor.prepareToPlay(48000.0, 256);

    while (findSlot(processor, CommandType::DrumSynth) >= processor.getMachineStackTypes(0).size())
        processor.addMachineToStack(0);
    const auto types = processor.getMachineStackTypes(0);
    const auto drumSlot = findSlot(processor, CommandType::DrumSynth);
    for (std::size_t slot = 0; slot < types.size(); ++slot)
    {
        const bool shouldEnable = slot == drumSlot;
        if (processor.isMachineEnabledInStack(0, slot) != shouldEnable)
            processor.toggleMachineEnabledInStack(0, slot);
    }

    auto* drum = static_cast<DrumSynthMachine*>(processor.getMachine(CommandType::DrumSynth, 0));
    require(drum != nullptr, "DRUM machine is reachable on stack 0");

    // Outgoing set (0): sequence 0 is empty, so it produces no kick.
    processor.setViewedSequenceSetIndex(0);
    if (auto* outgoing = processor.getSequencer()->getSequence(0))
    {
        outgoing->setMachineId(0);
        outgoing->ensureEnoughStepsForLength(4);
        outgoing->setLength(4);
    }
    require(processor.getSequencer()->getSequence(0) != nullptr, "outgoing set has sequence 0");

    // Clone the viewed set into a second sequence set and song row.
    const auto secondRow = processor.addSongRowByCloningViewedSet();
    require(secondRow == 1, "second song row is appended at index 1");

    // Incoming set (1): a single kick on step 0; the rest of the loop is empty.
    processor.setViewedSequenceSetIndex(1);
    if (auto* incoming = processor.getSequencer()->getSequence(0))
    {
        incoming->setMachineId(0);
        incoming->ensureEnoughStepsForLength(4);
        incoming->setLength(4);
        incoming->setStepData(0, {{ static_cast<double>(CommandType::MidiNote), 72.0, 120.0, 1.0 }});
    }
    require(processor.getSequencer()->getSequence(0) != nullptr, "incoming set has sequence 0");

    // Two short rows: row 0 = outgoing set, row 1 = incoming set.
    processor.adjustSongRowBeatCount(0, -12);
    processor.adjustSongRowBeatCount(1, -12);
    require(processor.getSongRowBeatCount(0) == 4 && processor.getSongRowBeatCount(1) == 4,
            "both song rows run four beats");

    processor.setSongPlayMode(SongPlayMode::song);
    processor.setInternalClockEnabled(true);

    const auto baseKick = drum->getTriggerCounts()[0];
    auto* editor = processor.getSequenceEditor();
    require(editor != nullptr, "processor exposes its sequence editor");
    editor->togglePlayback();

    // Drive until the song has looped row 0 -> row 1 -> row 0. The 0 -> 1
    // boundary is the pattern switch; its downbeat kick must survive.
    bool sawRowOne = false;
    bool completedLoop = false;
    juce::AudioBuffer<float> audio(2, 256);
    juce::MidiBuffer midi;
    for (int block = 0; block < 2000 && !completedLoop; ++block)
    {
        audio.clear();
        processor.processBlock(audio, midi);
        if (!processor.isPlaybackRunningSafely())
            continue;
        const auto row = processor.getCurrentPlaybackSongRow();
        if (row == 1)
            sawRowOne = true;
        if (sawRowOne && row == 0)
            completedLoop = true;
    }
    require(sawRowOne && completedLoop, "song advanced row 0 -> row 1 and looped back");

    const auto kickTriggers = drum->getTriggerCounts()[0] - baseKick;
    require(kickTriggers == 1,
            "song-row switch preserves the incoming pattern's downbeat (exactly one kick, not zero)");

    editor->togglePlayback();
    processor.releaseResources();
}

void requireSongPlaybackFollowsEditedSequenceSet()
{
    TrackerMainProcessor processor;
    processor.prepareToPlay(48000.0, 256);

    auto* editor = processor.getSequenceEditor();
    require(editor != nullptr, "processor exposes an editor for Song follow testing");
    if (editor == nullptr)
        return;

    // Give each Song row a recognisably different pattern, while retaining
    // the normal cloned-set Song construction path.
    for (int set = 0; set < 3; ++set)
    {
        processor.setViewedSequenceSetIndex(static_cast<std::size_t>(set));
        auto* sequence = processor.getSequencer()->getSequence(0);
        require(sequence != nullptr, "Song follow fixture sequence exists");
        if (sequence != nullptr)
            sequence->setStepData(0, {{ static_cast<double>(CommandType::MidiNote), 60.0 + set, 100.0, 1.0 }});
        if (set < 2)
            processor.addSongRowByCloningViewedSet();
    }
    for (std::size_t row = 0; row < processor.getSongRowCount(); ++row)
        processor.adjustSongRowBeatCount(row, -15);

    editor->gotoSongPage();
    editor->setSelectedSongCursor(2, 1);
    const auto songCursorRow = editor->getCurrentSongRow();
    const auto songCursorCol = editor->getCurrentSongCol();

    processor.setSongPlayMode(SongPlayMode::song);
    processor.setInternalClockEnabled(true);
    editor->togglePlayback();

    std::array<bool, 3> visited {};
    juce::AudioBuffer<float> audio(2, 256);
    juce::MidiBuffer midi;
    for (int block = 0; block < 2200 && !(visited[0] && visited[1] && visited[2]); ++block)
    {
        audio.clear();
        processor.processBlock(audio, midi);
        // This is the same message-thread snapshot boundary used by MCP and
        // the UI timer; it must consume the pending editor-follow handoff.
        const auto state = processor.getControlService().getState("state");
        require(state.ok, "Song follow state snapshot succeeds during playback");

        if (!processor.isPlaybackRunningSafely())
            continue;

        const auto activeRow = processor.getCurrentPlaybackSongRow();
        require(activeRow < processor.getSongRowCount(), "Song follow playback row remains valid");
        const auto activeSet = processor.getSongRowSequenceSetId(activeRow);
        require(processor.getViewedSequenceSetIndex() == activeSet,
                "Song playback binds the edited set to the audible Song row");
        const auto activeData = processor.getSequencer()->getStepData(0, 0);
        require(!activeData.empty() && activeData.front().size() > Step::noteInd
                    && std::abs(activeData.front()[Step::noteInd] - (60.0 + static_cast<double>(activeSet))) < 0.001,
                "Song playback exposes the active row's pattern data to the editor");
        if (activeRow < visited.size())
            visited[activeRow] = true;
    }
    require(visited[0] && visited[1] && visited[2], "Song follow visits and binds every Song row");
    require(editor->getCurrentSongRow() == songCursorRow && editor->getCurrentSongCol() == songCursorCol,
            "Song playback does not move the Song-page cursor");

    const auto activeRow = processor.getCurrentPlaybackSongRow();
    const auto activeSet = processor.getSongRowSequenceSetId(activeRow);
    const auto otherRow = (activeRow + 1) % processor.getSongRowCount();
    editor->gotoSongPage();
    editor->setSelectedSongCursor(otherRow + 1, 3);
    editor->click();
    require(processor.getViewedSequenceSetIndex() == activeSet,
            "Song-page EDIT cannot browse away from the active Song pattern while playing");

    processor.setSelectedSongRow(2);
    processor.rewindSongTransport();
    bool rewound = false;
    for (int block = 0; block < 700 && !rewound; ++block)
    {
        audio.clear();
        processor.processBlock(audio, midi);
        processor.getControlService().getState("state");
        rewound = processor.getCurrentPlaybackSongRow() == 2
            && processor.getViewedSequenceSetIndex() == processor.getSongRowSequenceSetId(2);
    }
    require(rewound, "playing rewind immediately rebinds the edited Song pattern");

    const auto retainedSet = processor.getViewedSequenceSetIndex();
    editor->togglePlayback();
    processor.getControlService().getState("state");
    require(processor.getViewedSequenceSetIndex() == retainedSet,
            "stopping Song playback retains the last edited pattern");
    processor.releaseResources();
}

void requireSongRowInsertionClonesIndependently()
{
    TrackerMainProcessor processor;

    auto* firstSet = processor.getSequencer();
    require(firstSet != nullptr, "initial song sequence set exists");
    if (firstSet == nullptr)
        return;

    auto* firstSequence = firstSet->getSequence(0);
    require(firstSequence != nullptr, "initial song sequence exists");
    if (firstSequence == nullptr)
        return;
    firstSequence->setStepData(0, {{ static_cast<double>(CommandType::MidiNote), 60.0, 100.0, 2.0 }});
    processor.setSequenceSetBpm(0, 133.0);
    processor.adjustSongRowBeatCount(0, -8);

    const auto secondRow = processor.addSongRowByCloningViewedSet();
    require(secondRow == 1, "song setup appends a second row");
    processor.setViewedSequenceSetIndex(1);
    processor.setSequenceSetBpm(1, 111.0);
    processor.adjustSongRowBeatCount(1, -4);
    if (auto* secondSequence = processor.getSequencer()->getSequence(0))
        secondSequence->setStepData(0, {{ static_cast<double>(CommandType::MidiNote), 67.0, 90.0, 3.0 }});

    const auto thirdRow = processor.addSongRowByCloningViewedSet();
    require(thirdRow == 2, "song setup appends a following row");
    const auto sourceSetId = processor.getSongRowSequenceSetId(1);
    const auto followingSetId = processor.getSongRowSequenceSetId(2);
    const auto sourceBeats = processor.getSongRowBeatCount(1);
    const auto sourceBpm = processor.getSongRowBpm(1);
    const auto sourceData = processor.getSequencer()->getStepData(0, 0);

    const auto insertedRow = processor.insertSongRowByCloningRow(1);
    require(insertedRow == 2, "song clone is inserted immediately below its source");
    require(processor.getSongRowCount() == 4, "insertion adds exactly one song row");
    require(processor.getSongRowSequenceSetId(1) == sourceSetId, "source row remains in place");
    require(processor.getSongRowSequenceSetId(2) != sourceSetId, "inserted row owns an independent sequence set");
    require(processor.getSongRowSequenceSetId(3) == followingSetId, "following song rows shift down once");
    require(processor.getSongRowBeatCount(2) == sourceBeats, "inserted row copies source beat count");
    require(processor.getSongRowBpm(2) == sourceBpm, "inserted row copies source BPM");

    const auto insertedSetId = processor.getSongRowSequenceSetId(2);
    processor.setViewedSequenceSetIndex(insertedSetId);
    require(processor.getSequencer()->getStepData(0, 0) == sourceData,
            "inserted sequence set copies source pattern data");
    processor.getSequencer()->getSequence(0)->setStepData(0,
        {{ static_cast<double>(CommandType::MidiNote), 72.0, 80.0, 1.0 }});
    processor.setViewedSequenceSetIndex(sourceSetId);
    require(processor.getSequencer()->getStepData(0, 0) == sourceData,
            "editing inserted pattern does not modify its source");

    juce::MemoryBlock state;
    processor.getStateInformation(state);
    TrackerMainProcessor restored;
    restored.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    require(restored.getSongRowCount() == 4, "inserted song rows survive a state round trip");
    require(restored.getSongRowBeatCount(2) == sourceBeats && restored.getSongRowBpm(2) == sourceBpm,
            "inserted song row timing survives a state round trip");

    auto* editor = processor.getSequenceEditor();
    require(editor != nullptr, "processor exposes song editor for insertion action");
    if (editor == nullptr)
        return;
    editor->gotoSongPage();
    editor->setSelectedSongCursor(1, 0);
    for (int press = 0; press < 5; ++press)
        editor->moveCursorRight();
    require(editor->getCurrentSongCol() == 5, "song-row cursor reaches moved delete column");
    editor->setSelectedSongCursor(1, 4);
    editor->click();
    require(editor->getCurrentSongRow() == 2 && editor->getCurrentSongCol() == 0,
            "insert action selects the inserted row SET cell");
    require(processor.getSongRowCount() == 5, "song editor insert action creates a row");
    editor->setSelectedSongCursor(1, 5);
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;
    requirePermissiveProjectVersionLoading();
    requireFocusedParameterCoverage();
    requireMachineControlMcpCoverage();
    requireSongRowInsertionClonesIndependently();
    requireSongSwitchPreservesIncomingDownbeat();
    requireSongPlaybackFollowsEditedSequenceSet();

    TrackerMainProcessor processor;
    processor.prepareToPlay(48000.0, 256);

    while (findSlot(processor, CommandType::DrumSynth) >= processor.getMachineStackTypes(0).size())
        processor.addMachineToStack(0);

    const auto types = processor.getMachineStackTypes(0);
    const auto drumSlot = findSlot(processor, CommandType::DrumSynth);
    require(drumSlot < types.size(), "DRUM is available in the machine stack cycle");
    for (std::size_t slot = 0; slot < types.size(); ++slot)
    {
        const bool shouldEnable = slot == drumSlot;
        if (processor.isMachineEnabledInStack(0, slot) != shouldEnable)
            processor.toggleMachineEnabledInStack(0, slot);
    }

    processor.sendTimedMessageToMachine(CommandType::DrumSynth, 0, 72, 120, 1.0, false);
    juce::AudioBuffer<float> directAudio(2, 256);
    juce::MidiBuffer midi;
    double directEnergy = 0.0;
    for (int block = 0; block < 20; ++block)
    {
        directAudio.clear();
        processor.processBlock(directAudio, midi);
        directEnergy += energy(directAudio);
    }
    require(directEnergy > 0.01, "processor stack renders directly triggered DRUM audio");

    auto* editor = processor.getSequenceEditor();
    require(editor != nullptr, "processor exposes its sequence editor");
    editor->gotoMachineConfigPage();
    editor->refreshMachineStateForCurrentSequence();
    editor->setMachineCursor(drumSlot + 2, 0);
    require(editor->enterSelectedMachineDetail(), "DRUM detail opens from its stack row");
    editor->moveCursorRight();
    const auto& drumCells = editor->getMachineCells();
    editor->moveCursorDown();
    editor->moveCursorDown();
    require(drumCells.size() > 3 && drumCells[3].size() > 2
                && drumCells[3][2].isSelected && drumCells[3][2].text == "80",
            "right/down navigation reaches the compact snare LEV value below its enable and MIDI controls");
    editor->incrementAtCursor();
    editor->refreshMachineStateForCurrentSequence();
    require(editor->getMachineCells()[3][2].text == "90",
            "bracket adjustment uses the compact snare level step");

    auto* sequencer = processor.getSequencer();
    require(sequencer != nullptr && sequencer->getSequence(0) != nullptr,
            "processor has a playable default sequence");
    auto* sequence = sequencer->getSequence(0);
    sequence->setMachineId(0);
    sequence->setLength(1);
    sequence->setStepData(0, {{ static_cast<double>(CommandType::MidiNote), 72.0, 120.0, 1.0 }});
    processor.setInternalClockEnabled(true);
    editor->togglePlayback();

    // Transport starts on the next locked beat boundary, rather than in the
    // keyboard handler. Drive enough callback blocks to reach that boundary.
    for (int block = 0; block < 256 && !processor.isPlaybackRunningSafely(); ++block)
    {
        directAudio.clear();
        processor.processBlock(directAudio, midi);
    }
    require(processor.isPlaybackRunningSafely(), "space-equivalent playback toggle starts transport");

    double sequencedEnergy = 0.0;
    for (int block = 0; block < 40; ++block)
    {
        directAudio.clear();
        processor.processBlock(directAudio, midi);
        sequencedEnergy += energy(directAudio);
    }
    require(sequencedEnergy > 0.01, "running sequencer triggers DRUM through the stack");
    editor->togglePlayback();
    require(!processor.isPlaybackRunningSafely(), "space-equivalent playback toggle stops transport");
    processor.releaseResources();

    TrackerMainProcessor fmProcessor;
    fmProcessor.prepareToPlay(48000.0, 64);
    while (findSlot(fmProcessor, CommandType::FmSynth) >= fmProcessor.getMachineStackTypes(0).size())
        fmProcessor.addMachineToStack(0);
    const auto fmTypes = fmProcessor.getMachineStackTypes(0);
    const auto fmSlot = findSlot(fmProcessor, CommandType::FmSynth);
    require(fmSlot < fmTypes.size(), "FM is available in the machine stack cycle");
    for (std::size_t slot = 0; slot < fmTypes.size(); ++slot)
    {
        const bool shouldEnable = slot == fmSlot;
        if (fmProcessor.isMachineEnabledInStack(0, slot) != shouldEnable)
            fmProcessor.toggleMachineEnabledInStack(0, slot);
    }
    fmProcessor.sendTimedMessageToMachine(CommandType::FmSynth, 0, 57, 120, 8.0, false);
    juce::AudioBuffer<float> fmAudio(2, 64);
    double fmEnergy = 0.0;
    for (int block = 0; block < 128; ++block)
    {
        fmAudio.clear();
        fmProcessor.processBlock(fmAudio, midi);
        fmEnergy += energy(fmAudio);
    }
    require(fmEnergy > 0.01, "processor stack renders sample-accurate FM audio");
    require(dynamic_cast<FmSynthMachine*>(fmProcessor.getMachine(CommandType::FmSynth, 0)) != nullptr,
            "processor exposes the persistent FM machine");
    fmProcessor.releaseResources();
    return 0;
}
