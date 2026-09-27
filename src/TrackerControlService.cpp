#include "TrackerControlService.h"

#include "TrackerMainProcessor.h"
#include "Sequencer.h"
#include "SequencerEditor.h"
#include "SuperSamplerProcessor.h"
#include "standalone/TrackerStandaloneHost.h"

#include <algorithm>

namespace
{
juce::String stringArg(const juce::var& args, const char* name, const juce::String& fallback = {})
{
    return args.isObject() ? args.getProperty(name, fallback).toString() : fallback;
}

int intArg(const juce::var& args, const char* name, int fallback = 0)
{
    return args.isObject() ? static_cast<int>(args.getProperty(name, fallback)) : fallback;
}

double numberArg(const juce::var& args, const char* name, double fallback = 0.0)
{
    return args.isObject() ? static_cast<double>(args.getProperty(name, fallback)) : fallback;
}

bool boolArg(const juce::var& args, const char* name, bool fallback = false)
{
    return args.isObject() ? static_cast<bool>(args.getProperty(name, fallback)) : fallback;
}

bool hasArg(const juce::var& args, const char* name)
{
    return args.isObject() && args.getDynamicObject()->hasProperty(name);
}

std::optional<CommandType> commandTypeFromName(const juce::String& name)
{
    static const std::pair<const char*, CommandType> types[] = {
        { "midi", CommandType::MidiNote }, { "midinote", CommandType::MidiNote },
        { "sampler", CommandType::Sampler }, { "arpeggiator", CommandType::Arpeggiator },
        { "wavetable_synth", CommandType::WavetableSynth }, { "poly_arpeggiator", CommandType::PolyArpeggiator },
        { "distortion", CommandType::DistortionFx }, { "delay", CommandType::DelayFx },
        { "channel_strip", CommandType::ChannelStripFx }, { "aux_send_1", CommandType::AuxSend1Fx },
        { "aux_send_2", CommandType::AuxSend2Fx }, { "log", CommandType::Log }
    };
    const auto lower = name.toLowerCase();
    for (const auto& [label, type] : types)
        if (lower == label)
            return type;
    return std::nullopt;
}

juce::var makeObject()
{
    return juce::var(new juce::DynamicObject());
}
} // namespace

TrackerControlService::TrackerControlService(TrackerMainProcessor& processorIn)
    : processor(processorIn)
{
}

TrackerControlService::Result TrackerControlService::fail(const juce::String& code, const juce::String& message) const
{
    Result result;
    result.ok = false;
    result.code = code;
    result.message = message;
    result.contentRevision = contentRevision;
    result.viewRevision = viewRevision;
    return result;
}

TrackerControlService::Result TrackerControlService::success(juce::var data) const
{
    Result result;
    result.contentRevision = contentRevision;
    result.viewRevision = viewRevision;
    result.data = std::move(data);
    return result;
}

void TrackerControlService::changed(bool content, bool view)
{
    if (content) ++contentRevision;
    if (view) ++viewRevision;
}

void TrackerControlService::synchroniseAsyncCompletions()
{
    const auto completed = asyncState->completedSinceRevision.exchange(0);
    contentRevision += completed;
}

bool TrackerControlService::revisionsMatch(const Command& command, Result& failure) const
{
    if (command.expectedContentRevision && *command.expectedContentRevision != contentRevision)
    {
        failure = fail("stale_content", "The tracker content has changed; refresh state and retry");
        return false;
    }
    if (command.expectedViewRevision && *command.expectedViewRevision != viewRevision)
    {
        failure = fail("stale_view", "The tracker view has changed; refresh state and retry");
        return false;
    }
    return true;
}

TrackerControlService::Result TrackerControlService::execute(const Command& command)
{
    return onMessageThread([this, command] { return executeNow(command); });
}

TrackerControlService::Result TrackerControlService::getState(const juce::String& scope)
{
    return onMessageThread([this, scope] { return getStateNow(scope); });
}

TrackerControlService::TrackerViewSnapshot TrackerControlService::getViewSnapshot()
{
    const auto result = getState("view");
    return { result.contentRevision, result.viewRevision, result.data };
}

TrackerControlService::Result TrackerControlService::readResource(const juce::String& uri)
{
    return onMessageThread([this, uri] { return readResourceNow(uri); });
}

juce::var TrackerControlService::serialiseGrid(const std::vector<std::vector<std::string>>& grid) const
{
    juce::Array<juce::var> columns;
    for (const auto& column : grid)
    {
        juce::Array<juce::var> rows;
        for (const auto& value : column)
            rows.add(juce::String(value));
        columns.add(rows);
    }
    return columns;
}

juce::var TrackerControlService::makeMachineCellsNow()
{
    auto& editor = processor.seqEditor;
    editor.refreshMachineStateForCurrentSequence();
    const auto& cells = editor.getMachineCells();
    const auto ui = processor.getUiState();
    const auto stackIndex = static_cast<std::size_t>(juce::jmax(0, static_cast<int>(ui.getProperty("machineId", 0))));
    juce::String slotId = "stack-controls";
    if (const auto selectedSlot = editor.getFocusedMachineDetailSlot())
    {
        if (const auto* stack = processor.getMachineStack(stackIndex); stack != nullptr && *selectedSlot < stack->slots.size())
            slotId = stack->slots[*selectedSlot].id;
    }
    juce::Array<juce::var> columns;
    for (std::size_t col = 0; col < cells.size(); ++col)
    {
        juce::Array<juce::var> rows;
        for (std::size_t row = 0; row < cells[col].size(); ++row)
        {
            const auto& cell = cells[col][row];
            auto item = makeObject();
            auto* object = item.getDynamicObject();
            auto controlName = juce::String(cell.text).toLowerCase().retainCharacters("abcdefghijklmnopqrstuvwxyz0123456789");
            if (controlName.isEmpty()) controlName = "cell";
            object->setProperty("id", "stack/" + juce::String(static_cast<int>(stackIndex)) + "/slot/" + slotId + "/control/" + controlName + "-" + juce::String((int)col) + "-" + juce::String((int)row));
            object->setProperty("label", juce::String(cell.text));
            object->setProperty("valueType", cell.onInsert != nullptr ? "number" : "action");
            object->setProperty("displayKind", static_cast<int>(cell.kind));
            object->setProperty("disabled", cell.isDisabled);
            object->setProperty("selected", cell.isSelected);
            object->setProperty("active", cell.isActive);
            object->setProperty("editable", cell.onInsert != nullptr || cell.onAdjust != nullptr);
            object->setProperty("canActivate", cell.onActivate != nullptr);
            object->setProperty("canAdjust", cell.onAdjust != nullptr);
            object->setProperty("canReset", cell.onReset != nullptr);
            object->setProperty("canPreview", cell.onPreview != nullptr);
            juce::Array<juce::var> actions;
            if (cell.onActivate) actions.add("activate");
            if (cell.onAdjust) actions.add("adjust");
            if (cell.onInsert) actions.add("insert");
            if (cell.onPreview) actions.add("preview");
            if (cell.onReset) actions.add("reset");
            object->setProperty("supportedActions", actions);
            rows.add(item);
        }
        columns.add(rows);
    }
    return columns;
}

juce::var TrackerControlService::makeViewNow()
{
    auto view = makeObject();
    auto* object = view.getDynamicObject();
    const auto ui = processor.getUiState();
    object->setProperty("ui", ui);
    object->setProperty("machineCells", makeMachineCellsNow());
    object->setProperty("contentRevision", static_cast<juce::int64>(contentRevision));
    object->setProperty("viewRevision", static_cast<juce::int64>(viewRevision));
    processor.sendCurrentCellValueOverOscIfChanged();
    return view;
}

juce::var TrackerControlService::makeStateNow()
{
    auto state = makeObject();
    auto* object = state.getDynamicObject();
    object->setProperty("schemaVersion", 1);
    object->setProperty("contentRevision", static_cast<juce::int64>(contentRevision));
    object->setProperty("viewRevision", static_cast<juce::int64>(viewRevision));
    object->setProperty("ui", processor.getUiState());
    object->setProperty("document", processor.serializeSequencerState());
    object->setProperty("internalClock", processor.isInternalClockEnabled());
    object->setProperty("hostClockActive", processor.isHostClockActive());
    juce::Array<juce::var> loads;
    {
        std::lock_guard<std::mutex> lock(asyncState->mutex);
        for (const auto& [_, load] : asyncState->loads)
        {
            auto item = makeObject();
            item.getDynamicObject()->setProperty("loadId", load.id);
            item.getDynamicObject()->setProperty("status", load.status);
            item.getDynamicObject()->setProperty("path", load.path);
            item.getDynamicObject()->setProperty("error", load.error);
            item.getDynamicObject()->setProperty("stackId", load.stackId);
            item.getDynamicObject()->setProperty("playerId", load.playerId);
            item.getDynamicObject()->setProperty("startNote", load.startNote);
            item.getDynamicObject()->setProperty("endNote", load.endNote);
            loads.add(item);
        }
    }
    object->setProperty("sampleLoads", loads);
    return state;
}

TrackerControlService::Result TrackerControlService::getStateNow(const juce::String& scope)
{
    synchroniseAsyncCompletions();
    return processor.withAudioThreadExclusive([&]() -> Result
    {
        if (scope == "view") return success(makeViewNow());
        if (scope == "capabilities") return success(capabilities());
        return success(makeStateNow());
    });
}

TrackerControlService::Result TrackerControlService::readResourceNow(const juce::String& uri)
{
    synchroniseAsyncCompletions();
    return processor.withAudioThreadExclusive([&]() -> Result
    {
        if (uri == "myktracker://state") return success(makeStateNow());
        if (uri == "myktracker://view") return success(makeViewNow());
        if (uri == "myktracker://capabilities") return success(capabilities());
        const auto document = processor.serializeSequencerState();
        if (uri == "myktracker://song")
        {
            auto song = makeObject();
            auto* out = song.getDynamicObject();
            out->setProperty("songRows", document.getProperty("songRows", juce::var()));
            out->setProperty("songPlayMode", document.getProperty("songPlayMode", "sequence"));
            out->setProperty("viewedSequenceSetIndex", document.getProperty("viewedSequenceSetIndex", 0));
            return success(song);
        }
        const auto prefix = juce::String("myktracker://sequence-set/");
        if (uri.startsWith(prefix))
        {
            const int index = uri.fromFirstOccurrenceOf(prefix, false, false).getIntValue();
            const auto sets = document.getProperty("sequenceSets", juce::var());
            if (!sets.isArray() || index < 0 || index >= sets.getArray()->size())
                return fail("not_found", "Sequence set does not exist");
            return success(sets.getArray()->getReference(index));
        }
        const auto stepPrefix = juce::String("myktracker://step/");
        if (uri.startsWith(stepPrefix))
        {
            const auto parts = juce::StringArray::fromTokens(uri.fromFirstOccurrenceOf(stepPrefix, false, false), "/", "");
            if (parts.size() != 3) return fail("not_found", "Step resource address is invalid");
            const int setIndex = parts[0].getIntValue();
            const int sequenceIndex = parts[1].getIntValue();
            const int stepIndex = parts[2].getIntValue();
            const auto sets = document.getProperty("sequenceSets", juce::var());
            if (!sets.isArray() || setIndex < 0 || setIndex >= sets.getArray()->size()) return fail("not_found", "Sequence set does not exist");
            const auto sequences = sets.getArray()->getReference(setIndex).getProperty("sequences", juce::var());
            if (!sequences.isArray() || sequenceIndex < 0 || sequenceIndex >= sequences.getArray()->size()) return fail("not_found", "Sequence does not exist");
            const auto steps = sequences.getArray()->getReference(sequenceIndex).getProperty("steps", juce::var());
            if (!steps.isArray() || stepIndex < 0 || stepIndex >= steps.getArray()->size()) return fail("not_found", "Step does not exist");
            return success(steps.getArray()->getReference(stepIndex));
        }
        const auto stackPrefix = juce::String("myktracker://machine-stack/");
        if (uri.startsWith(stackPrefix))
        {
            const int index = uri.fromFirstOccurrenceOf(stackPrefix, false, false).getIntValue();
            const auto stacks = document.getProperty("machineStacks", juce::var());
            if (!stacks.isArray() || index < 0 || index >= stacks.getArray()->size())
                return fail("not_found", "Machine stack does not exist");
            return success(stacks.getArray()->getReference(index));
        }
        return fail("not_found", "Unknown tracker resource");
    });
}

TrackerControlService::Result TrackerControlService::executeNow(const Command& command)
{
    synchroniseAsyncCompletions();
    if (command.kind == CommandKind::getState)
        return getStateNow(stringArg(command.arguments, "scope", "state"));

    Result revisionFailure;
    if (!revisionsMatch(command, revisionFailure))
        return revisionFailure;

    return processor.withAudioThreadExclusive([&]() -> Result
    {
        auto& editor = processor.seqEditor;
        const auto& args = command.arguments;
        bool contentChanged = false;
        bool viewChanged = false;

        auto selectAddress = [&](int setIndex, int sequence, int step, bool stepPage)
        {
            if (setIndex >= 0) processor.setViewedSequenceSetIndex(static_cast<std::size_t>(setIndex));
            editor.setCurrentSequence(sequence);
            editor.setCurrentStep(step);
            if (stepPage) editor.gotoStepPage(); else editor.gotoSequencePage();
            viewChanged = true;
        };

        switch (command.kind)
        {
            case CommandKind::getState:
                return getStateNow(stringArg(args, "scope", "state"));
            case CommandKind::transport:
            {
                const auto action = stringArg(args, "action", "toggle");
                if (hasArg(args, "bpm"))
                {
                    const auto bpm = numberArg(args, "bpm");
                    if (bpm <= 0.0) return fail("invalid_argument", "bpm must be positive");
                    processor.setBPM(bpm);
                    contentChanged = true;
                }
                if (hasArg(args, "internalClock")) { processor.setInternalClockEnabled(boolArg(args, "internalClock")); contentChanged = true; }
                if (hasArg(args, "playMode"))
                    processor.setSongPlayMode(stringArg(args, "playMode") == "song" ? SongPlayMode::song : SongPlayMode::sequence);
                auto* sequencer = processor.getSequencer();
                if (action == "rewind") processor.rewindSongTransport();
                else if (action == "play" && sequencer != nullptr && !sequencer->isPlaying()) processor.toggleSongPlayback();
                else if (action == "stop" && sequencer != nullptr && sequencer->isPlaying()) processor.toggleSongPlayback();
                else if (action == "toggle") processor.toggleSongPlayback();
                break;
            }
            case CommandKind::setStep:
            {
                const int setIndex = intArg(args, "setId", static_cast<int>(processor.getViewedSequenceSetIndex()));
                const int sequence = intArg(args, "sequenceId");
                const int step = intArg(args, "stepId");
                const int row = intArg(args, "row", 0);
                if (sequence < 0 || step < 0 || row < 0) return fail("invalid_argument", "Step address must be non-negative");
                selectAddress(setIndex, sequence, step, true);
                auto* sequencer = processor.getSequencer();
                if (sequencer == nullptr || static_cast<std::size_t>(sequence) >= sequencer->howManySequences()
                    || static_cast<std::size_t>(step) >= sequencer->howManySteps(static_cast<std::size_t>(sequence)))
                    return fail("not_found", "Step does not exist");
                auto data = sequencer->getStepData(static_cast<std::size_t>(sequence), static_cast<std::size_t>(step));
                while (data.size() <= static_cast<std::size_t>(row)) data.emplace_back(Step::maxInd + 1, 0.0);
                auto& event = data[static_cast<std::size_t>(row)];
                if (event.size() < Step::maxInd + 1) event.resize(Step::maxInd + 1, 0.0);
                const auto action = stringArg(args, "action", "set");
                if (action == "clear") std::fill(event.begin(), event.end(), 0.0);
                else if (action == "toggle_active") sequencer->toggleStepActive(static_cast<std::size_t>(sequence), static_cast<std::size_t>(step));
                else
                {
                    if (hasArg(args, "command"))
                    {
                        const auto type = commandTypeFromName(stringArg(args, "command"));
                        if (!type) return fail("invalid_argument", "Unknown command type");
                        event[Step::cmdInd] = static_cast<double>(*type);
                    }
                    if (hasArg(args, "note")) event[Step::noteInd] = juce::jlimit(0.0, 127.0, numberArg(args, "note"));
                    if (hasArg(args, "velocity")) event[Step::velInd] = juce::jlimit(0.0, 127.0, numberArg(args, "velocity"));
                    if (hasArg(args, "durationTicks")) event[Step::lengthInd] = juce::jlimit(0.0, 65535.0, numberArg(args, "durationTicks"));
                    if (hasArg(args, "probability")) event[Step::probInd] = juce::jlimit(0.0, 1.0, numberArg(args, "probability"));
                }
                sequencer->setStepData(static_cast<std::size_t>(sequence), static_cast<std::size_t>(step), std::move(data));
                editor.setStepCursor(static_cast<std::size_t>(row), 0);
                contentChanged = true;
                break;
            }
            case CommandKind::editSequence:
            {
                const int setIndex = intArg(args, "setId", static_cast<int>(processor.getViewedSequenceSetIndex()));
                const int sequenceIndex = intArg(args, "sequenceId");
                selectAddress(setIndex, sequenceIndex, intArg(args, "stepId", 0), false);
                auto* sequencer = processor.getSequencer();
                if (sequencer == nullptr || sequenceIndex < 0 || static_cast<std::size_t>(sequenceIndex) >= sequencer->howManySequences())
                    return fail("not_found", "Sequence does not exist");
                auto* sequence = sequencer->getSequence(static_cast<std::size_t>(sequenceIndex));
                if (hasArg(args, "length")) sequence->setLength(static_cast<std::size_t>(juce::jmax(1, intArg(args, "length"))));
                if (hasArg(args, "ticksPerStep")) sequence->setTicksPerStep(static_cast<std::size_t>(juce::jlimit(1, 16, intArg(args, "ticksPerStep"))));
                if (hasArg(args, "triggerProbability")) sequence->setTriggerProbability(juce::jlimit(0.0, 1.0, numberArg(args, "triggerProbability")));
                if (hasArg(args, "machineStackId")) sequence->setMachineId(juce::jmax(0, intArg(args, "machineStackId")));
                if (hasArg(args, "muted") && sequence->isMuted() != boolArg(args, "muted")) sequencer->toggleSequenceMute(static_cast<std::size_t>(sequenceIndex));
                if (hasArg(args, "armed")) { editor.setArmedSequence(static_cast<std::size_t>(sequenceIndex)); viewChanged = true; }
                contentChanged = true;
                break;
            }
            case CommandKind::editSong:
            {
                editor.gotoSongPage(); viewChanged = true;
                const auto action = stringArg(args, "action");
                if (action == "add") processor.addSongRowByCloningViewedSet();
                else if (action == "remove") processor.removeSongRow(static_cast<std::size_t>(juce::jmax(0, intArg(args, "row"))));
                else if (action == "set_mode") processor.setSongPlayMode(stringArg(args, "mode") == "song" ? SongPlayMode::song : SongPlayMode::sequence);
                else if (action == "select") processor.setSelectedSongRow(static_cast<std::size_t>(juce::jmax(0, intArg(args, "row"))));
                else return fail("invalid_argument", "Unknown song action");
                contentChanged = true;
                break;
            }
            case CommandKind::editMachineStack:
            {
                const auto stack = static_cast<std::size_t>(juce::jmax(0, intArg(args, "stackId")));
                const auto slot = static_cast<std::size_t>(juce::jmax(0, intArg(args, "slotId")));
                const auto action = stringArg(args, "action");
                editor.gotoMachineConfigPage(); editor.refreshMachineStateForCurrentSequence(); viewChanged = true;
                if (action == "add") processor.addMachineToStack(stack);
                else if (action == "remove") processor.removeMachineFromStack(stack, slot);
                else if (action == "move") processor.moveMachineInStack(stack, slot, intArg(args, "direction"));
                else if (action == "cycle_type") processor.cycleMachineTypeInStack(stack, slot, intArg(args, "direction", 1));
                else if (action == "toggle") processor.toggleMachineEnabledInStack(stack, slot);
                else if (action == "set_gain") processor.setStackGainDb(stack, static_cast<float>(numberArg(args, "gainDb")));
                else return fail("invalid_argument", "Unknown machine stack action");
                contentChanged = true;
                break;
            }
            case CommandKind::machineControl:
            {
                editor.gotoMachineConfigPage(); editor.refreshMachineStateForCurrentSequence();
                int row = intArg(args, "row");
                int column = intArg(args, "column");
                const auto controlId = stringArg(args, "controlId");
                if (controlId.isNotEmpty())
                {
                    const auto cells = makeMachineCellsNow();
                    bool found = false;
                    if (cells.isArray())
                    {
                        for (int col = 0; col < cells.getArray()->size() && !found; ++col)
                        {
                            const auto& columnCells = cells.getArray()->getReference(col);
                            if (!columnCells.isArray()) continue;
                            for (int candidateRow = 0; candidateRow < columnCells.getArray()->size(); ++candidateRow)
                                if (columnCells.getArray()->getReference(candidateRow).getProperty("id", {}).toString() == controlId)
                                {
                                    column = col;
                                    row = candidateRow;
                                    found = true;
                                    break;
                                }
                        }
                    }
                    if (!found) return fail("not_found", "Machine control does not exist");
                }
                editor.setMachineCursor(static_cast<std::size_t>(juce::jmax(0, row)), static_cast<std::size_t>(juce::jmax(0, column)));
                const auto action = stringArg(args, "action", "activate");
                if (action == "activate") editor.machineActivateCurrentCell();
                else if (action == "adjust") editor.machineAdjustCurrentCell(intArg(args, "direction", 1));
                else if (action == "insert") { if (!editor.machineInsertCurrentCell(numberArg(args, "value"))) return fail("unsupported", "Cell does not accept numeric input"); }
                else if (action == "preview") { if (!editor.machinePreviewCurrentCell()) return fail("unsupported", "Cell cannot be previewed"); }
                else if (action == "reset") { if (!editor.machineResetCurrentCell()) return fail("unsupported", "Cell cannot be reset"); }
                else if (action == "text") { if (!editor.machineHandleTextInput(stringArg(args, "text").isNotEmpty() ? stringArg(args, "text")[0] : '\0')) return fail("unsupported", "Cell does not accept text input"); }
                else if (action == "backspace") { if (!editor.machineHandleTextBackspace()) return fail("unsupported", "Cell cannot be cleared"); }
                else return fail("invalid_argument", "Unknown machine control action");
                contentChanged = true; viewChanged = true;
                break;
            }
            case CommandKind::loadSample:
            {
                const auto operation = stringArg(args, "action", "load");
                if (operation == "status")
                {
                    const auto loadId = stringArg(args, "loadId");
                    std::lock_guard<std::mutex> lock(asyncState->mutex);
                    const auto found = asyncState->loads.find(loadId.toStdString());
                    if (found == asyncState->loads.end()) return fail("not_found", "Sample load does not exist");
                    auto detail = makeObject();
                    detail.getDynamicObject()->setProperty("loadId", found->second.id);
                    detail.getDynamicObject()->setProperty("status", found->second.status);
                    detail.getDynamicObject()->setProperty("path", found->second.path);
                    detail.getDynamicObject()->setProperty("error", found->second.error);
                    detail.getDynamicObject()->setProperty("stackId", found->second.stackId);
                    detail.getDynamicObject()->setProperty("playerId", found->second.playerId);
                    detail.getDynamicObject()->setProperty("startNote", found->second.startNote);
                    detail.getDynamicObject()->setProperty("endNote", found->second.endNote);
                    return success(detail);
                }
                if (operation != "load") return fail("invalid_argument", "Unknown sample load action");
                const auto stack = static_cast<std::size_t>(juce::jmax(0, intArg(args, "stackId")));
                const auto filename = stringArg(args, "filename", stringArg(args, "path"));
                const juce::File file(filename);
                if (!file.existsAsFile() || !file.hasReadAccess()) return fail("not_found", "Sample file does not exist or cannot be read");
                juce::AudioFormatManager formats;
                formats.registerBasicFormats();
                if (formats.createReaderFor(file) == nullptr) return fail("unsupported", "Sample file is not a supported audio format");
                auto* sampler = dynamic_cast<SuperSamplerProcessor*>(processor.getMachine(CommandType::Sampler, stack));
                if (sampler == nullptr) return fail("not_found", "Sampler stack does not exist");

                const bool hasRange = hasArg(args, "startNote") || hasArg(args, "endNote");
                int playerId = intArg(args, "playerId", -1);
                int startNote = 0;
                int endNote = 127;
                bool createdPlayer = false;
                if (hasRange)
                {
                    if (!hasArg(args, "startNote") || !hasArg(args, "endNote"))
                        return fail("invalid_argument", "startNote and endNote must be supplied together");
                    startNote = intArg(args, "startNote");
                    endNote = intArg(args, "endNote");
                    if (startNote < 0 || startNote > 127 || endNote < 0 || endNote > 127 || startNote > endNote)
                        return fail("invalid_argument", "Sample note range must be within MIDI notes 0 through 127, with startNote <= endNote");
                    playerId = sampler->addSamplePlayerForControl(startNote, endNote);
                    createdPlayer = true;
                }
                else if (playerId < 0)
                {
                    return fail("invalid_argument", "filename, stackId, startNote, and endNote are required to create a playable sample mapping");
                }

                const auto loadId = "load-" + juce::String(static_cast<juce::int64>(asyncState->nextId.fetch_add(1)));
                {
                    std::lock_guard<std::mutex> lock(asyncState->mutex);
                    asyncState->loads[loadId.toStdString()] = { loadId, "loading", file.getFullPathName(), {}, static_cast<int>(stack), playerId, startNote, endNote };
                }
                std::weak_ptr<AsyncState> weakState = asyncState;
                sampler->loadSampleFromControl(playerId, file, [weakState, loadId, sampler, playerId, createdPlayer] (bool ok, juce::String error)
                {
                    if (!ok && createdPlayer)
                        sampler->removeSamplePlayer(playerId);
                    if (const auto state = weakState.lock())
                    {
                        std::lock_guard<std::mutex> lock(state->mutex);
                        const auto found = state->loads.find(loadId.toStdString());
                        if (found != state->loads.end())
                        {
                            found->second.status = ok ? "succeeded" : "failed";
                            found->second.error = error;
                            if (ok) state->completedSinceRevision.fetch_add(1);
                        }
                    }
                });
                auto detail = makeObject();
                detail.getDynamicObject()->setProperty("loadId", loadId);
                detail.getDynamicObject()->setProperty("status", "loading");
                detail.getDynamicObject()->setProperty("stackId", static_cast<int>(stack));
                detail.getDynamicObject()->setProperty("playerId", playerId);
                detail.getDynamicObject()->setProperty("startNote", startNote);
                detail.getDynamicObject()->setProperty("endNote", endNote);
                return success(detail);
                break;
            }
            case CommandKind::uiAction:
            {
                const auto action = stringArg(args, "action");
                if (action == "key")
                {
                    const int keyCode = intArg(args, "keyCode");
                    const auto rawText = stringArg(args, "text");
                    const char key = rawText.isNotEmpty() ? static_cast<char>(std::tolower(static_cast<unsigned char>(rawText[0]))) : '\0';
                    const bool ctrl = boolArg(args, "ctrl");
                    const bool shift = boolArg(args, "shift");
                    bool handled = false;
                    const bool machineCapturesKeyboard = editor.machineWantsExclusiveKeyboardInput() && !ctrl;
                    if (shift && key == 'c') { processor.setInternalClockEnabled(!processor.isInternalClockEnabled()); contentChanged = true; handled = true; }
                    else if (ctrl && (keyCode == 'r' || keyCode == 'R')) { editor.requestTrackerReset(); viewChanged = true; handled = true; }
                    else if (ctrl && (keyCode == 'q' || keyCode == 'Q')) { editor.requestApplicationQuit(); viewChanged = true; handled = true; }
                    // File browsers and text-entry machine pages get first use of
                    // text/backspace, but navigation keys intentionally fall
                    // through to the common cursor handling below.
                    else if (machineCapturesKeyboard && keyCode == juce::KeyPress::backspaceKey)
                    {
                        handled = editor.machineHandleTextBackspace();
                        contentChanged = handled;
                    }
                    else if (machineCapturesKeyboard && key >= 32 && key <= 126)
                    {
                        handled = editor.machineHandleTextInput(key);
                        contentChanged = handled;
                    }
                    else if (keyCode == juce::KeyPress::spaceKey) { editor.togglePlayback(); handled = true; }
                    else if (keyCode == '5') { handled = editor.enterMachineDetailFromAnywhere(); viewChanged = handled; }
                    else if (keyCode >= '1' && keyCode <= '6') { handled = editor.selectPageShortcut(keyCode - '0'); viewChanged = handled; }
                    else if (editor.handleChordKey(key) || editor.handleNoteKey(key)) { contentChanged = true; handled = true; }
                    else if (keyCode == juce::KeyPress::backspaceKey) { handled = editor.machineHandleTextBackspace(); if (!handled) { editor.resetAtCursor(); handled = true; } contentChanged = handled; }
                    else if (keyCode == juce::KeyPress::escapeKey) { handled = editor.dismissCurrentTransientUi(); viewChanged = handled; }
                    else if (keyCode == juce::KeyPress::returnKey) { editor.click(); contentChanged = true; handled = true; }
                    else if (keyCode == juce::KeyPress::upKey) { editor.moveCursorUp(); viewChanged = true; handled = true; }
                    else if (keyCode == juce::KeyPress::downKey) { editor.moveCursorDown(); viewChanged = true; handled = true; }
                    else if (keyCode == juce::KeyPress::leftKey) { editor.moveCursorLeft(); viewChanged = true; handled = true; }
                    else if (keyCode == juce::KeyPress::rightKey) { editor.moveCursorRight(); viewChanged = true; handled = true; }
                    else if (key == 'q') { editor.toggleMuteCurrentSequence(); contentChanged = true; handled = true; }
                    else if (key == 'e') { editor.toggleArmCurrentSequence(); viewChanged = true; handled = true; }
                    else if (key == 'r') { editor.rewindTransport(); handled = true; }
                    else if (key == '\t') { handled = editor.isEditingMachineDetail() ? editor.cycleMachineDetailNext() : (editor.nextStep(), true); viewChanged = handled; }
                    else if (key == '-') { editor.removeRow(); contentChanged = true; handled = true; }
                    else if (key == '=') { editor.addRow(); contentChanged = true; handled = true; }
                    else if (key == '_') { processor.setBPM(juce::jmax(1.0, processor.getBPM() - 1.0)); contentChanged = true; handled = true; }
                    else if (key == '+') { processor.setBPM(processor.getBPM() + 1.0); contentChanged = true; handled = true; }
                    else if (key == '[') { editor.decrementAtCursor(); contentChanged = true; handled = true; }
                    else if (key == ']') { editor.incrementAtCursor(); contentChanged = true; handled = true; }
                    else if (key == ',') { editor.decrementOctave(); viewChanged = true; handled = true; }
                    else if (key == '.') { editor.incrementOctave(); viewChanged = true; handled = true; }
                    if (!handled) return fail("unsupported", "Unmapped keyboard action");
                    viewChanged = viewChanged || !contentChanged;
                    break;
                }
                if (action == "up") editor.moveCursorUp();
                else if (action == "down") editor.moveCursorDown();
                else if (action == "left") editor.moveCursorLeft();
                else if (action == "right") editor.moveCursorRight();
                else if (action == "activate") editor.click();
                else if (action == "increment") editor.incrementAtCursor();
                else if (action == "decrement") editor.decrementAtCursor();
                else if (action == "add_row") editor.addRow();
                else if (action == "remove_row") editor.removeRow();
                else if (action == "reset") editor.resetAtCursor();
                else if (action == "next_step") editor.nextStep();
                else if (action == "mute") editor.toggleMuteCurrentSequence();
                else if (action == "arm") editor.toggleArmCurrentSequence();
                else if (action == "note") editor.enterDataAtCursor(numberArg(args, "value"));
                else if (action == "page") editor.selectPageShortcut(intArg(args, "value"));
                else return fail("invalid_argument", "Unknown UI action");
                contentChanged = action != "up" && action != "down" && action != "left" && action != "right";
                viewChanged = true;
                break;
            }
            case CommandKind::application:
            {
                const auto action = stringArg(args, "action");
                if ((action == "reset" || action == "quit") && !boolArg(args, "confirm")) return fail("confirmation_required", "reset and quit require confirm=true");
                if (action == "reset") { processor.recreateSequencersAndMachines(); contentChanged = true; viewChanged = true; }
                // Defer beyond the current message-loop turn so an MCP caller
                // receives its acknowledgement before standalone shutdown.
                else if (action == "quit") juce::Timer::callAfterDelay(50, [] { if (auto* app = juce::JUCEApplicationBase::getInstance()) app->systemRequestedQuit(); });
                else if (action == "audio_settings")
                {
                    if (auto* holder = tracker::standalone::StandalonePluginHolder::getInstance()) holder->showAudioSettingsDialog();
                    else return fail("unsupported", "Audio settings are only available in the standalone application");
                }
                else return fail("invalid_argument", "Unknown application action");
                break;
            }
            default: return fail("unsupported", "Unsupported command");
        }
        changed(contentChanged, viewChanged);
        return success(makeViewNow());
    });
}

juce::var TrackerControlService::capabilities() const
{
    auto result = makeObject();
    auto* object = result.getDynamicObject();
    object->setProperty("protocolVersion", "2026-07-28");
    object->setProperty("commandKinds", "transport,set_step,edit_sequence,edit_song,edit_machine_stack,machine_control,load_sample,ui_action,application");
    object->setProperty("resources", "state,view,song,capabilities,sequence-set,machine-stack");
    return result;
}
