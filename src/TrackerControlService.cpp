#include "TrackerControlService.h"

#include "TrackerMainProcessor.h"
#include "Sequencer.h"
#include "SequencerEditor.h"
#include "SuperSamplerProcessor.h"
#include "standalone/TrackerStandaloneHost.h"

#include <algorithm>

namespace
{
constexpr std::size_t kMaxCompactTrackSteps = 128;

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
        { "sampler", CommandType::Sampler },
        { "wavetable_synth", CommandType::WavetableSynth },
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

juce::String machineControlLocalId(const std::vector<std::vector<UIBox>>& cells,
                                   std::size_t column, std::size_t row)
{
    const auto labelFor = [] (const UIBox& cell)
    {
        // Labels have no executable action.  Value text is deliberately not
        // used here: a control ID must survive a value change.
        if (cell.onActivate || cell.onAdjust || cell.onInsert || cell.onPreview || cell.onReset)
            return juce::String();
        return juce::String(cell.text).toLowerCase().retainCharacters("abcdefghijklmnopqrstuvwxyz0123456789");
    };

    juce::String label;
    for (std::size_t candidate = column; candidate-- > 0 && label.isEmpty();)
        if (row < cells[candidate].size()) label = labelFor(cells[candidate][row]);
    for (std::size_t candidate = row; candidate-- > 0 && label.isEmpty();)
        if (column < cells.size() && candidate < cells[column].size()) label = labelFor(cells[column][candidate]);
    if (label.isEmpty())
        label = juce::String(cells[column][row].text).toLowerCase().retainCharacters("abcdefghijklmnopqrstuvwxyz0123456789");
    if (label.isEmpty()) label = "cell";
    return label + "-" + juce::String(static_cast<int>(column)) + "-" + juce::String(static_cast<int>(row));
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
    if (command.kind == CommandKind::getScreenshot)
        return getScreenshot();
    return onMessageThread([this, command] { return executeNow(command); });
}

TrackerControlService::Result TrackerControlService::getScreenshot()
{
    auto request = std::make_shared<TrackerMainProcessor::UiScreenshotRequest>();
    request->completed = std::make_shared<juce::WaitableEvent>();
    request->image = std::make_shared<juce::Image>();
    request->error = std::make_shared<juce::String>();

    auto* manager = juce::MessageManager::getInstanceWithoutCreating();
    if (manager == nullptr)
        return fail("message_thread_unavailable", "The tracker message thread is unavailable");

    auto requestError = std::make_shared<juce::String>();
    const bool queued = manager->callAsync([this, request, requestError]
    {
        *requestError = processor.requestUiScreenshot(std::move(request));
    });
    if (!queued)
        return fail("message_thread_unavailable", "The tracker did not accept the screenshot request");
    if (!request->completed->wait(3000))
        return fail("screenshot_timeout", "The tracker UI did not render a screenshot in time");
    if (!request->error->isEmpty())
        return fail(*request->error, "The tracker UI screenshot failed");
    if (!request->image->isValid())
        return fail("screenshot_empty", "The tracker UI screenshot contained no image data");

    juce::MemoryBlock pngData;
    juce::MemoryOutputStream pngStream(pngData, false);
    if (!juce::PNGImageFormat().writeImageToStream(*request->image, pngStream))
        return fail("screenshot_failed", "The tracker UI screenshot could not be encoded as PNG");

    auto detail = makeObject();
    auto* object = detail.getDynamicObject();
    object->setProperty("format", "png");
    object->setProperty("mimeType", "image/png");
    object->setProperty("width", request->image->getWidth());
    object->setProperty("height", request->image->getHeight());
    object->setProperty("bytes", static_cast<juce::int64>(pngData.getSize()));
    object->setProperty("data", juce::Base64::toBase64(pngData.getData(), pngData.getSize()));
    return success(detail);
}

TrackerControlService::Result TrackerControlService::getState(const juce::String& scope)
{
    return onMessageThread([this, scope] { return getStateNow(scope, true); });
}

TrackerControlService::TrackerViewSnapshot TrackerControlService::getViewSnapshot(bool includeSerializedMachineCells)
{
    const auto result = onMessageThread([this, includeSerializedMachineCells] { return getStateNow("view", includeSerializedMachineCells); });
    return { result.contentRevision, result.viewRevision, result.data };
}

TrackerControlService::TrackerViewSnapshot TrackerControlService::getViewSnapshotForPage()
{
    const auto result = onMessageThread([this] { return getStateNow("view", false, true); });
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

juce::var TrackerControlService::serialiseMachineCells(std::size_t stackIndex, const juce::String& slotId,
                                                        const std::vector<std::vector<UIBox>>& cells) const
{
    juce::Array<juce::var> columns;
    for (std::size_t col = 0; col < cells.size(); ++col)
    {
        juce::Array<juce::var> rows;
        for (std::size_t row = 0; row < cells[col].size(); ++row)
        {
            const auto& cell = cells[col][row];
            auto item = makeObject();
            auto* object = item.getDynamicObject();
            const auto controlName = machineControlLocalId(cells, col, row);
            object->setProperty("id", "stack/" + juce::String(static_cast<int>(stackIndex)) + "/slot/" + slotId + "/control/" + controlName);
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

juce::var TrackerControlService::makeMachineCellsNow()
{
    auto& editor = processor.seqEditor;
    editor.refreshMachineStateForCurrentSequence();

    std::size_t stackIndex = 0;
    if (auto* viewedSequencer = processor.getSequencer(); viewedSequencer != nullptr)
    {
        const auto sequenceIndex = editor.getCurrentSequence();
        if (sequenceIndex < viewedSequencer->howManySequences())
            stackIndex = static_cast<std::size_t>(juce::jmax(0, static_cast<int>(viewedSequencer->getSequence(sequenceIndex)->getMachineId())));
    }
    if (stackIndex >= processor.machineStacks.size())
        stackIndex = 0;

    juce::String slotId = "stack-controls";
    if (const auto selectedSlot = editor.getFocusedMachineDetailSlot())
    {
        if (const auto* stack = processor.getMachineStack(stackIndex); stack != nullptr && *selectedSlot < stack->slots.size())
            slotId = stack->slots[*selectedSlot].id;
    }
    return serialiseMachineCells(stackIndex, slotId, editor.getMachineCells());
}

juce::var TrackerControlService::makeMachineControlsNow()
{
    juce::Array<juce::var> controls;
    for (std::size_t stackIndex = 0; stackIndex < processor.machineStacks.size(); ++stackIndex)
    {
        auto& stack = processor.machineStacks[stackIndex];
        for (const auto& slot : stack.slots)
        {
            auto* machine = processor.getMachineForStackType(stack, slot.type);
            if (machine == nullptr)
                continue;
            MachineUiContext context;
            const auto cells = machine->getUIBoxes(context);
            auto item = makeObject();
            auto* object = item.getDynamicObject();
            object->setProperty("stackId", static_cast<int>(stackIndex));
            object->setProperty("slotId", juce::String(slot.id));
            object->setProperty("controls", serialiseMachineCells(stackIndex, slot.id, cells));
            controls.add(item);
        }
    }
    auto result = makeObject();
    result.getDynamicObject()->setProperty("machineControls", controls);
    return result;
}

juce::var TrackerControlService::makeUiNow()
{
    auto ui = processor.getUiState();
    if (sequenceSelection.active && ui.getDynamicObject() != nullptr)
    {
        auto selection = makeObject();
        selection.getDynamicObject()->setProperty("sequence", static_cast<int>(sequenceSelection.sequence));
        selection.getDynamicObject()->setProperty("startStep", static_cast<int>(std::min(sequenceSelection.anchorStep, sequenceSelection.cursorStep)));
        selection.getDynamicObject()->setProperty("endStep", static_cast<int>(std::max(sequenceSelection.anchorStep, sequenceSelection.cursorStep)));
        ui.getDynamicObject()->setProperty("sequenceSelection", selection);
    }
    return ui;
}

juce::var TrackerControlService::makeUiForPageNow()
{
    auto ui = processor.getUiStateForCurrentPage();
    if (sequenceSelection.active && ui.getDynamicObject() != nullptr)
    {
        auto selection = makeObject();
        selection.getDynamicObject()->setProperty("sequence", static_cast<int>(sequenceSelection.sequence));
        selection.getDynamicObject()->setProperty("startStep", static_cast<int>(std::min(sequenceSelection.anchorStep, sequenceSelection.cursorStep)));
        selection.getDynamicObject()->setProperty("endStep", static_cast<int>(std::max(sequenceSelection.anchorStep, sequenceSelection.cursorStep)));
        ui.getDynamicObject()->setProperty("sequenceSelection", selection);
    }
    return ui;
}

juce::var TrackerControlService::makeViewNow(bool includeSerializedMachineCells, bool pageOnly)
{
    auto view = makeObject();
    auto* object = view.getDynamicObject();
    const auto ui = pageOnly ? makeUiForPageNow() : makeUiNow();
    object->setProperty("ui", ui);
    if (includeSerializedMachineCells)
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
    object->setProperty("ui", makeUiNow());
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
    object->setProperty("machineControls", makeMachineControlsNow().getProperty("machineControls", {}));
    return state;
}

TrackerControlService::Result TrackerControlService::getStateNow(const juce::String& scope, bool includeSerializedMachineCells, bool pageOnly)
{
    synchroniseAsyncCompletions();
    return processor.withAudioThreadExclusive([&]() -> Result
    {
        if (scope == "view") return success(makeViewNow(includeSerializedMachineCells, pageOnly));
        if (scope == "machine_controls") return success(makeMachineControlsNow());
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
            case CommandKind::getTrackNotes:
            {
                const int setId = intArg(args, "setId");
                const int sequenceId = intArg(args, "sequenceId");
                if (setId < 0 || sequenceId < 0)
                    return fail("invalid_argument", "setId and sequenceId must be non-negative");
                const auto stepCount = processor.getStepCountForSequenceSet(static_cast<std::size_t>(setId), static_cast<std::size_t>(sequenceId));
                if (stepCount == 0)
                    return fail("not_found", "Sequence set or track does not exist");

                juce::Array<juce::var> notes;
                for (std::size_t step = 0; step < stepCount; ++step)
                {
                    std::vector<std::vector<double>> values;
                    if (!processor.getStepValuesForSequenceSet(static_cast<std::size_t>(setId), static_cast<std::size_t>(sequenceId), step, values))
                        return fail("not_found", "Track changed while being read");
                    juce::Array<juce::var> entry;
                    entry.add(static_cast<int>(step));
                    for (const auto& row : values)
                        if (row.size() > Step::noteInd && row[Step::noteInd] > 0.0)
                            entry.add(row[Step::noteInd]);
                    if (entry.size() > 1)
                        notes.add(entry);
                }
                auto detail = makeObject();
                detail.getDynamicObject()->setProperty("n", notes);
                return success(detail);
            }
            case CommandKind::getStepValues:
            {
                const int setId = intArg(args, "setId");
                const int sequenceId = intArg(args, "sequenceId");
                const int stepId = intArg(args, "stepId");
                if (setId < 0 || sequenceId < 0 || stepId < 0)
                    return fail("invalid_argument", "setId, sequenceId, and stepId must be non-negative");
                std::vector<std::vector<double>> values;
                if (!processor.getStepValuesForSequenceSet(static_cast<std::size_t>(setId), static_cast<std::size_t>(sequenceId), static_cast<std::size_t>(stepId), values))
                    return fail("not_found", "Step does not exist");
                juce::Array<juce::var> rows;
                for (const auto& row : values)
                {
                    juce::Array<juce::var> encoded;
                    for (const auto value : row) encoded.add(value);
                    rows.add(encoded);
                }
                auto detail = makeObject();
                detail.getDynamicObject()->setProperty("v", rows);
                return success(detail);
            }
            case CommandKind::setTrackNotes:
            {
                const int setIndex = intArg(args, "setId", static_cast<int>(processor.getViewedSequenceSetIndex()));
                const int sequenceId = intArg(args, "sequenceId");
                const auto notesVar = args.getProperty("notes", juce::var());
                if (setIndex < 0 || sequenceId < 0 || !notesVar.isArray())
                    return fail("invalid_argument", "setId, sequenceId, and numeric notes array are required");
                selectAddress(setIndex, sequenceId, 0, true);
                auto* sequencer = processor.getSequencer();
                if (sequencer == nullptr || static_cast<std::size_t>(sequenceId) >= sequencer->howManySequences())
                    return fail("not_found", "Sequence does not exist");
                auto stepCount = sequencer->howManySteps(static_cast<std::size_t>(sequenceId));
                const auto& noteSteps = *notesVar.getArray();
                if (static_cast<std::size_t>(noteSteps.size()) > kMaxCompactTrackSteps)
                    return fail("invalid_argument", "notes is limited to 128 track steps");
                const double velocity = juce::jlimit(0.0, 127.0, numberArg(args, "velocity", 100.0));
                const double duration = juce::jlimit(1.0, 65535.0, numberArg(args, "durationTicks", 1.0));

                // Validate the complete compact payload before mutating any
                // step, so a rejected chord cannot leave a partial pattern.
                for (const auto& requested : noteSteps)
                {
                    if (requested.isVoid() || requested.isUndefined()) continue;
                    if (requested.isArray())
                    {
                        for (const auto& noteValue : *requested.getArray())
                        {
                            const auto note = static_cast<double>(noteValue);
                            if (note <= 0.0 || note > 127.0)
                                return fail("invalid_argument", "notes must be MIDI values 1 through 127; use 0 or null for a rest");
                        }
                    }
                    else
                    {
                        const auto note = static_cast<double>(requested);
                        if (note < 0.0 || note > 127.0)
                            return fail("invalid_argument", "notes must be MIDI values 1 through 127; use 0 or null for a rest");
                    }
                }

                // A compact note list is allowed to extend a pattern, but the
                // fixed upper bound prevents an external client from creating
                // unbounded sequences.  Resize only after its whole payload
                // has validated, so rejected requests leave song content intact.
                if (static_cast<std::size_t>(noteSteps.size()) > stepCount)
                {
                    auto* sequence = sequencer->getSequence(static_cast<std::size_t>(sequenceId));
                    sequence->ensureEnoughStepsForLength(static_cast<std::size_t>(noteSteps.size()));
                    sequence->setLength(static_cast<std::size_t>(noteSteps.size()));
                    stepCount = sequencer->howManySteps(static_cast<std::size_t>(sequenceId));
                }

                for (std::size_t step = 0; step < stepCount; ++step)
                {
                    auto data = sequencer->getStepData(static_cast<std::size_t>(sequenceId), step);
                    for (auto& row : data)
                    {
                        if (row.size() < Step::maxInd + 1) row.resize(Step::maxInd + 1, 0.0);
                        row[Step::noteInd] = 0.0;
                    }

                    const juce::var requested = step < static_cast<std::size_t>(noteSteps.size())
                        ? noteSteps.getReference(static_cast<int>(step)) : juce::var();
                    juce::Array<juce::var> chord;
                    if (requested.isArray()) chord = *requested.getArray();
                    else if (!requested.isVoid() && !requested.isUndefined() && static_cast<double>(requested) > 0.0) chord.add(requested);

                    for (int rowIndex = 0; rowIndex < chord.size(); ++rowIndex)
                    {
                        const auto note = static_cast<double>(chord.getReference(rowIndex));
                        while (data.size() <= static_cast<std::size_t>(rowIndex)) data.emplace_back(Step::maxInd + 1, 0.0);
                        auto& row = data[static_cast<std::size_t>(rowIndex)];
                        if (row.size() < Step::maxInd + 1) row.resize(Step::maxInd + 1, 0.0);
                        row[Step::noteInd] = note;
                        row[Step::velInd] = velocity;
                        row[Step::lengthInd] = duration;
                    }
                    sequencer->setStepData(static_cast<std::size_t>(sequenceId), step, std::move(data));
                }
                contentChanged = true;
                auto detail = makeObject();
                detail.getDynamicObject()->setProperty("c", static_cast<int>(noteSteps.size()));
                changed(contentChanged, viewChanged);
                return success(detail);
            }
            case CommandKind::setTrackLengths:
            {
                const int setIndex = intArg(args, "setId", static_cast<int>(processor.getViewedSequenceSetIndex()));
                const int sequenceId = intArg(args, "sequenceId");
                const auto lengthsVar = args.getProperty("lengths", juce::var());
                if (setIndex < 0 || sequenceId < 0 || !lengthsVar.isArray())
                    return fail("invalid_argument", "setId, sequenceId, and lengths array are required");
                selectAddress(setIndex, sequenceId, 0, true);
                auto* sequencer = processor.getSequencer();
                if (sequencer == nullptr || static_cast<std::size_t>(sequenceId) >= sequencer->howManySequences())
                    return fail("not_found", "Sequence does not exist");
                const auto stepCount = sequencer->howManySteps(static_cast<std::size_t>(sequenceId));
                const auto& lengths = *lengthsVar.getArray();
                if (static_cast<std::size_t>(lengths.size()) != stepCount)
                    return fail("invalid_argument", "lengths must contain exactly one value for every track step");
                for (const auto& lengthValue : lengths)
                {
                    const auto duration = static_cast<double>(lengthValue);
                    if (duration < 1.0 || duration > 65535.0)
                        return fail("invalid_argument", "lengths must be between 1 and 65535 ticks");
                }
                for (std::size_t step = 0; step < stepCount; ++step)
                {
                    const auto duration = static_cast<double>(lengths.getReference(static_cast<int>(step)));
                    auto data = sequencer->getStepData(static_cast<std::size_t>(sequenceId), step);
                    for (auto& row : data)
                        if (row.size() > Step::noteInd && row[Step::noteInd] > 0.0)
                        {
                            if (row.size() < Step::maxInd + 1) row.resize(Step::maxInd + 1, 0.0);
                            row[Step::lengthInd] = duration;
                        }
                    sequencer->setStepData(static_cast<std::size_t>(sequenceId), step, std::move(data));
                }
                contentChanged = true;
                changed(contentChanged, viewChanged);
                return success();
            }
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
                        if (*type != CommandType::MidiNote && *type != CommandType::Log
                            && *type != CommandType::Sampler && *type != CommandType::WavetableSynth)
                            return fail("invalid_argument", "Step commands support midi, log, sampler, or wavetable_synth");
                        event[Step::cmdInd] = static_cast<double>(*type);
                    }
                    if (hasArg(args, "note")) event[Step::noteInd] = juce::jlimit(0.0, 127.0, numberArg(args, "note"));
                    if (hasArg(args, "velocity")) event[Step::velInd] = juce::jlimit(0.0, 127.0, numberArg(args, "velocity"));
                    if (hasArg(args, "durationTicks")) event[Step::lengthInd] = juce::jlimit(0.0, 65535.0, numberArg(args, "durationTicks"));
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
                const int targetHeadCount = intArg(args, "headCount", static_cast<int>(sequence->getReadHeadCount()));
                const int requestedHeadValue = intArg(args, "headIndex", 0);
                if (targetHeadCount < 1 || targetHeadCount > 3 || requestedHeadValue < 0 || requestedHeadValue >= targetHeadCount)
                    return fail("invalid_argument", "headIndex is outside the configured head count");
                const auto requestedHead = static_cast<std::size_t>(requestedHeadValue);
                auto head = requestedHead < sequence->getReadHeadCount()
                    ? sequence->getReadHeadConfig(requestedHead) : SequenceReadHeadConfig{};
                if (hasArg(args, "ticksPerStep"))
                {
                    const int value = intArg(args, "ticksPerStep");
                    if (value < 1 || value > 16) return fail("invalid_argument", "ticksPerStep must be between 1 and 16");
                    head.ticksPerStep = static_cast<std::size_t>(value);
                }
                if (hasArg(args, "mode"))
                {
                    SequenceReadMode parsed{};
                    if (!Sequence::parseReadMode(stringArg(args, "mode").toStdString(), parsed))
                        return fail("invalid_argument", "mode must be linear, random, or rand_chord");
                    head.mode = parsed;
                }
                if (hasArg(args, "polyphony"))
                {
                    const int value = intArg(args, "polyphony");
                    if (value < 1 || value > 5) return fail("invalid_argument", "polyphony must be between 1 and 5");
                    head.polyphony = static_cast<std::size_t>(value);
                }
                if (hasArg(args, "rhythm")) head.rhythm = stringArg(args, "rhythm").toStdString();
                if (std::find(Sequence::getRhythmPresets().begin(), Sequence::getRhythmPresets().end(), head.rhythm) == Sequence::getRhythmPresets().end())
                    return fail("invalid_argument", "rhythm must be a non-zero binary pattern of one to four bits");
                if (hasArg(args, "headProbability"))
                {
                    const double value = numberArg(args, "headProbability");
                    if (value < 0.0 || value > 1.0) return fail("invalid_argument", "headProbability must be between 0 and 1");
                    head.probability = value;
                }
                const int length = intArg(args, "length", static_cast<int>(sequence->getLength()));
                if (length < 1 || length > 128) return fail("invalid_argument", "length must be between 1 and 128");
                const int stackId = intArg(args, "machineStackId", static_cast<int>(sequence->getMachineId()));
                if (stackId < 0 || stackId >= 16) return fail("invalid_argument", "machineStackId must be between 0 and 15");
                const auto oldStackId = static_cast<std::size_t>(juce::jlimit(0, 15, static_cast<int>(sequence->getMachineId())));

                const bool structuralChange = hasArg(args, "headCount") || hasArg(args, "ticksPerStep")
                    || hasArg(args, "mode") || hasArg(args, "rhythm") || hasArg(args, "machineStackId")
                    || hasArg(args, "length");
                sequence->ensureEnoughStepsForLength(static_cast<std::size_t>(length));
                sequence->setLength(static_cast<std::size_t>(length));
                if (sequence->getReadHeadCount() != static_cast<std::size_t>(targetHeadCount))
                    sequence->setReadHeadCount(static_cast<std::size_t>(targetHeadCount));
                if (!sequence->setReadHeadConfig(requestedHead, head))
                    return fail("invalid_argument", "rhythm must be a non-zero binary pattern of one to four bits");
                sequence->setMachineId(stackId);
                if (structuralChange)
                {
                    processor.allNotesOffForStack(oldStackId);
                    if (oldStackId != static_cast<std::size_t>(stackId))
                        processor.allNotesOffForStack(static_cast<std::size_t>(stackId));
                }
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
                if (stack >= processor.getMachineStackCount())
                    return fail("not_found", "Machine stack does not exist");
                if (action == "add") processor.addMachineToStack(stack);
                else if (action == "remove") processor.removeMachineFromStack(stack, slot);
                else if (action == "move") processor.moveMachineInStack(stack, slot, intArg(args, "direction"));
                else if (action == "cycle_type") processor.cycleMachineTypeInStack(stack, slot, intArg(args, "direction", 1));
                else if (action == "toggle") processor.toggleMachineEnabledInStack(stack, slot);
                else if (action == "set_enabled")
                {
                    auto* stackState = processor.getMachineStack(stack);
                    if (stackState == nullptr || slot >= stackState->slots.size())
                        return fail("not_found", "Machine slot does not exist");
                    if (!hasArg(args, "enabled"))
                        return fail("invalid_argument", "set_enabled requires enabled=true or enabled=false");
                    if (stackState->slots[slot].enabled != boolArg(args, "enabled"))
                        processor.toggleMachineEnabledInStack(stack, slot);
                }
                else if (action == "set_gain") processor.setStackGainDb(stack, static_cast<float>(numberArg(args, "gainDb")));
                else if (action == "set_muted")
                {
                    if (!hasArg(args, "muted")) return fail("invalid_argument", "set_muted requires muted");
                    processor.setStackMuted(stack, boolArg(args, "muted"));
                }
                else if (action == "set_solo")
                {
                    if (!hasArg(args, "solo")) return fail("invalid_argument", "set_solo requires solo");
                    processor.setStackSolo(stack, boolArg(args, "solo"));
                }
                else if (action == "set_send")
                {
                    auto* stackState = processor.getMachineStack(stack);
                    if (stackState == nullptr || slot >= stackState->slots.size())
                        return fail("not_found", "Machine slot does not exist");
                    stackState->slots[slot].sendLevelDb = juce::jlimit(-60.0f, 12.0f, static_cast<float>(numberArg(args, "gainDb")));
                }
                else return fail("invalid_argument", "Unknown machine stack action");
                contentChanged = true;
                break;
            }
            case CommandKind::machineControl:
            {
                // A fully addressed control intentionally bypasses SequencerEditor.
                // The GUI is free to move its cursor while this callback runs: the
                // target is the persisted slot identity and a machine-local cell ID.
                auto requestedStack = intArg(args, "stackId", -1);
                auto requestedSlot = stringArg(args, "slotId");
                auto requestedControl = stringArg(args, "controlId");
                if (requestedControl.startsWith("stack/"))
                {
                    const auto address = juce::StringArray::fromTokens(requestedControl, "/", "");
                    if (address.size() != 6 || address[0] != "stack" || address[2] != "slot" || address[4] != "control")
                        return fail("invalid_argument", "controlId must be stack/{stackId}/slot/{slotId}/control/{controlId}");
                    const int addressedStack = address[1].getIntValue();
                    if (requestedStack >= 0 && requestedStack != addressedStack)
                        return fail("invalid_argument", "stackId does not match controlId");
                    if (requestedSlot.isNotEmpty() && requestedSlot != address[3])
                        return fail("invalid_argument", "slotId does not match controlId");
                    requestedStack = addressedStack;
                    requestedSlot = address[3];
                    requestedControl = address[5];
                }

                if (requestedStack >= 0 || requestedSlot.isNotEmpty())
                {
                    if (requestedStack < 0 || requestedSlot.isEmpty() || requestedControl.isEmpty()
                        || static_cast<std::size_t>(requestedStack) >= processor.machineStacks.size())
                        return fail("invalid_argument", "Direct machine control requires stackId, slotId, and controlId");

                    auto& stack = processor.machineStacks[static_cast<std::size_t>(requestedStack)];
                    const auto slotIt = std::find_if(stack.slots.begin(), stack.slots.end(), [&requestedSlot] (const auto& slot)
                    {
                        return slot.id == requestedSlot.toStdString();
                    });
                    if (slotIt == stack.slots.end()) return fail("not_found", "Machine slot does not exist in stack");
                    auto* machine = processor.getMachineForStackType(stack, slotIt->type);
                    if (machine == nullptr) return fail("unsupported", "The addressed stack slot has no direct machine controls");

                    MachineUiContext context;
                    auto cells = machine->getUIBoxes(context);
                    UIBox* target = nullptr;
                    int targetColumn = -1;
                    int targetRow = -1;
                    for (std::size_t column = 0; column < cells.size() && target == nullptr; ++column)
                        for (std::size_t row = 0; row < cells[column].size(); ++row)
                        {
                            const auto id = machineControlLocalId(cells, column, row);
                            if (id == requestedControl)
                            {
                                target = &cells[column][row];
                                targetColumn = static_cast<int>(column);
                                targetRow = static_cast<int>(row);
                                break;
                            }
                        }
                    if (target == nullptr) return fail("not_found", "Machine control does not exist in the addressed slot");
                    if (target->isDisabled) return fail("unsupported", "Machine control is currently disabled");

                    const auto action = stringArg(args, "action", "activate");
                    if (action == "activate" && target->onActivate) target->onActivate();
                    else if (action == "adjust" && target->onAdjust) target->onAdjust(intArg(args, "direction", 1));
                    else if ((action == "insert" || action == "set") && target->onInsert) target->onInsert(numberArg(args, "value"));
                    else if (action == "preview" && target->onPreview) target->onPreview();
                    else if (action == "reset" && target->onReset) target->onReset();
                    else if (action == "text" && machine->handleTextInput(stringArg(args, "text").isNotEmpty() ? stringArg(args, "text")[0] : '\0')) {}
                    else if (action == "backspace" && (machine->clearCell(targetRow, targetColumn) || machine->handleTextBackspace())) {}
                    else return fail("unsupported", "The addressed machine control does not support this action");
                    contentChanged = true;
                    break;
                }

                // Compatibility mode for older MCP clients.  This remains
                // cursor-driven because a grid row/column is inherently UI state.
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
                    if (ctrl && (keyCode == 'c' || keyCode == 'C'))
                    {
                        auto* sequencer = processor.getSequencer();
                        const auto sequenceIndex = editor.getCurrentSequence();
                        if (sequencer == nullptr || sequenceIndex >= sequencer->howManySequences())
                            return fail("not_found", "Current sequence does not exist");
                        auto* source = sequencer->getSequence(sequenceIndex);
                        if (source == nullptr) return fail("not_found", "Current sequence does not exist");

                        const bool copySelection = sequenceSelection.active && sequenceSelection.sequence == sequenceIndex;
                        const auto firstStep = copySelection ? std::min(sequenceSelection.anchorStep, sequenceSelection.cursorStep) : 0u;
                        const auto lastStep = copySelection ? std::max(sequenceSelection.anchorStep, sequenceSelection.cursorStep) : source->getLength() - 1u;
                        sequenceClipboard.steps.clear();
                        sequenceClipboard.wholeSequenceLength = copySelection ? 0u : source->getLength();
                        sequenceClipboard.steps.reserve(lastStep - firstStep + 1u);
                        for (std::size_t step = firstStep; step <= lastStep; ++step)
                            sequenceClipboard.steps.push_back(source->getStepData(step));
                        sequenceClipboard.hasSteps = true;
                        sequenceClipboard.pasteAtCursor = copySelection;
                        handled = true;
                    }
                    else if (ctrl && (keyCode == 'v' || keyCode == 'V'))
                    {
                        if (!sequenceClipboard.hasSteps)
                            return fail("clipboard_empty", "Copy a sequence with Ctrl+C before pasting");
                        auto* sequencer = processor.getSequencer();
                        const auto sequenceIndex = editor.getCurrentSequence();
                        if (sequencer == nullptr || sequenceIndex >= sequencer->howManySequences())
                            return fail("not_found", "Current sequence does not exist");
                        auto* destination = sequencer->getSequence(sequenceIndex);
                        if (destination == nullptr) return fail("not_found", "Current sequence does not exist");

                        // Whole-sequence clips replace from step zero.  A selected
                        // range is pasted at the destination cursor; both forms
                        // keep the destination's machine routing and timing.
                        const auto firstStep = sequenceClipboard.pasteAtCursor ? editor.getCurrentStep() : 0u;
                        const auto requiredLength = firstStep + sequenceClipboard.steps.size();
                        destination->ensureEnoughStepsForLength(requiredLength);
                        if (sequenceClipboard.pasteAtCursor)
                        {
                            if (requiredLength > destination->getLength()) destination->setLength(requiredLength);
                        }
                        else
                        {
                            destination->ensureEnoughStepsForLength(sequenceClipboard.wholeSequenceLength);
                            destination->setLength(sequenceClipboard.wholeSequenceLength);
                        }
                        for (std::size_t step = 0; step < sequenceClipboard.steps.size(); ++step)
                            destination->setStepData(firstStep + step, sequenceClipboard.steps[step]);
                        sequenceSelection.active = false;
                        sequencer->requestStrUpdate();
                        contentChanged = true;
                        handled = true;
                    }
                    else if (ctrl && (keyCode == '=' || keyCode == '-'))
                    {
                        auto* sequencer = processor.getSequencer();
                        const auto sequenceIndex = editor.getCurrentSequence();
                        if (sequencer == nullptr || sequenceIndex >= sequencer->howManySequences())
                            return fail("not_found", "Current sequence does not exist");
                        auto* sequence = sequencer->getSequence(sequenceIndex);
                        if (sequence == nullptr) return fail("not_found", "Current sequence does not exist");

                        const auto length = sequence->getLength();
                        if (length > 1)
                        {
                            struct StepState { std::vector<std::vector<double>> data; bool active = true; };
                            std::vector<StepState> original;
                            original.reserve(length);
                            for (std::size_t step = 0; step < length; ++step)
                                original.push_back({ sequence->getStepData(step), sequence->isStepActive(step) });

                            for (std::size_t step = 0; step < length; ++step)
                            {
                                // Ctrl+= shifts downward: the last step wraps to
                                // the first position. Ctrl+- is the inverse.
                                const auto source = keyCode == '=' ? (step + length - 1u) % length : (step + 1u) % length;
                                sequence->setStepData(step, original[source].data);
                                if (sequence->isStepActive(step) != original[source].active)
                                    sequence->toggleActive(step);
                            }
                            sequencer->requestStrUpdate();
                            contentChanged = true;
                        }
                        handled = true;
                    }
                    else if (!ctrl && shift && editor.getCurrentPage() == SequencerEditorPage::sequence
                             && (keyCode == juce::KeyPress::upKey || keyCode == juce::KeyPress::downKey))
                    {
                        const auto sequenceIndex = editor.getCurrentSequence();
                        if (!sequenceSelection.active || sequenceSelection.sequence != sequenceIndex)
                        {
                            sequenceSelection.active = true;
                            sequenceSelection.sequence = sequenceIndex;
                            sequenceSelection.anchorStep = editor.getCurrentStep();
                        }
                        if (keyCode == juce::KeyPress::upKey) editor.moveCursorUp();
                        else editor.moveCursorDown();
                        sequenceSelection.cursorStep = editor.getCurrentStep();
                        viewChanged = true;
                        handled = true;
                    }
                    else if (shift && key == 'c') { processor.setInternalClockEnabled(!processor.isInternalClockEnabled()); contentChanged = true; handled = true; }
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
                    else if (keyCode >= '1' && keyCode <= '7') { handled = editor.selectPageShortcut(keyCode - '0'); viewChanged = handled; }
                    else if (editor.handleChordKey(key) || editor.handleNoteKey(key)) { contentChanged = true; handled = true; }
                    else if (keyCode == juce::KeyPress::backspaceKey) { handled = editor.machineHandleTextBackspace(); if (!handled) { editor.resetAtCursor(); handled = true; } contentChanged = handled; }
                    else if (keyCode == juce::KeyPress::escapeKey) { handled = editor.dismissCurrentTransientUi(); viewChanged = handled; }
                    else if (keyCode == juce::KeyPress::returnKey) { editor.click(); contentChanged = true; handled = true; }
                    else if (keyCode == juce::KeyPress::upKey) { sequenceSelection.active = false; editor.moveCursorUp(); viewChanged = true; handled = true; }
                    else if (keyCode == juce::KeyPress::downKey) { sequenceSelection.active = false; editor.moveCursorDown(); viewChanged = true; handled = true; }
                    else if (keyCode == juce::KeyPress::leftKey) { sequenceSelection.active = false; editor.moveCursorLeft(); viewChanged = true; handled = true; }
                    else if (keyCode == juce::KeyPress::rightKey) { sequenceSelection.active = false; editor.moveCursorRight(); viewChanged = true; handled = true; }
                    else if (key == 'q') { editor.toggleMuteCurrentSequence(); contentChanged = true; handled = true; }
                    else if (key == 'e') { editor.toggleArmCurrentSequence(); viewChanged = true; handled = true; }
                    else if (key == 'r') { editor.rewindTransport(); handled = true; }
                    else if (key == '\t')
                    {
                        if (editor.isEditingMachineDetail())
                            handled = editor.cycleMachineDetailNext();
                        else if (editor.getCurrentPage() == SequencerEditorPage::step)
                        {
                            const auto* sequencer = processor.getSequencer();
                            const auto count = sequencer != nullptr ? sequencer->howManySteps(editor.getCurrentSequence()) : 0u;
                            if (count > 0 && editor.getCurrentStep() + 1u >= count)
                                editor.setCurrentStep(0);
                            else
                                editor.nextStep();
                            handled = count > 0;
                        }
                        else
                        {
                            editor.nextStep();
                            handled = true;
                        }
                        viewChanged = handled;
                    }
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
                contentChanged = action != "up" && action != "down" && action != "left" && action != "right"
                    && action != "page" && action != "next_step";
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
            case CommandKind::getScreenshot:
                return fail("unsupported", "Screenshots must use the dedicated capture path");
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
    object->setProperty("commandKinds", "get_track_notes,get_step_values,set_track_notes,set_track_lengths,transport,set_step,edit_sequence,edit_song,edit_machine_stack,machine_control,load_sample,ui_action,application,get_screenshot");
    object->setProperty("resources", "state,view,song,capabilities,sequence-set,machine-stack");
    return result;
}
