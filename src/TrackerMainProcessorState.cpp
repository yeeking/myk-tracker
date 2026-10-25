#include "TrackerMainProcessor.h"
#include "PortableTrackerState.h"
#include "MixerState.h"

#include <algorithm>
#include <cmath>
#include <iterator>

namespace
{
constexpr int kTrackerStateVersion = 1;

bool decodeCurrentTrackerState(const void* data, std::size_t size,
                               juce::ValueTree& tree, juce::var& state,
                               juce::String* error = nullptr)
{
    juce::MemoryInputStream input(data, size, false);
    auto xml = juce::parseXML(input.readEntireStreamAsString());
    if (xml == nullptr || !(tree = juce::ValueTree::fromXml(*xml)).isValid())
    {
        if (error != nullptr) *error = "INVALID TRACKER STATE";
        return false;
    }

    const auto json = tree.getProperty("json").toString();
    state = json.isNotEmpty() ? juce::JSON::fromString(json) : juce::var();
    if (!state.isObject())
    {
        if (error != nullptr) *error = "INVALID TRACKER STATE";
        return false;
    }
    return true;
}

juce::String encodeMachineState(MachineInterface* machine)
{
    if (machine == nullptr)
        return juce::String();
    juce::MemoryBlock state;
    machine->getStateInformation(state);
    return juce::Base64::toBase64(state.getData(), state.getSize());
}

void decodeMachineState(const juce::var& encodedVar, MachineInterface* machine)
{
    if (machine == nullptr)
        return;
    const auto encoded = encodedVar.toString();
    if (encoded.isEmpty())
        return;
    juce::MemoryBlock state;
    juce::MemoryOutputStream stream(state, false);
    if (!juce::Base64::convertFromBase64(stream, encoded) || state.getSize() == 0)
        return;
    machine->setStateInformation(state.getData(), static_cast<int>(state.getSize()));
}
}

void TrackerMainProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    withAudioThreadExclusive([&]() { writeTrackerStateUnlocked(destData); });
}

void TrackerMainProcessor::writeTrackerStateUnlocked(juce::MemoryBlock& destData)
{
    const auto stateVar = serializeSequencerState();
    const auto json = juce::JSON::toString(stateVar);
    apvts.state.setProperty("json", json, nullptr);

    destData.reset();
    juce::MemoryOutputStream stream(destData, false);
    if (auto xml = apvts.copyState().createXml())
        xml->writeTo(stream);
}

void TrackerMainProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    withAudioThreadExclusive([&]()
    {
        juce::ValueTree tree;
        juce::var state;
        if (!decodeCurrentTrackerState(data, static_cast<std::size_t>(juce::jmax(0, sizeInBytes)), tree, state))
            return;
        apvts.replaceState(tree);
        restoreSequencerState(state);
    });
}

bool TrackerMainProcessor::canWritePortableState()
{
    return withAudioThreadExclusive([&]()
    {
        const auto* playbackSequencer = getPlaybackSequencerInternal();
        return (playbackSequencer == nullptr || !playbackSequencer->isPlaying()) && !isMasterRecording();
    });
}

bool TrackerMainProcessor::isPlaybackRunningSafely()
{
    return withAudioThreadExclusive([&]()
    {
        const auto* playbackSequencer = getPlaybackSequencerInternal();
        return playbackSequencer != nullptr && playbackSequencer->isPlaying();
    });
}

bool TrackerMainProcessor::writePortableStateFile(const juce::File& destination, juce::String& error)
{
    juce::MemoryBlock state;
    std::vector<PortableSampleAsset> samples;
    return capturePortableState(state, samples, error) && portable_tracker_state::write(destination, state, samples, error);
}

bool TrackerMainProcessor::capturePortableState(juce::MemoryBlock& state,
                                                std::vector<PortableSampleAsset>& samples,
                                                juce::String& error)
{
    const bool captured = withAudioThreadExclusive([&]()
    {
        const auto* playbackSequencer = getPlaybackSequencerInternal();
        if ((playbackSequencer != nullptr && playbackSequencer->isPlaying()) || isMasterRecording())
            return false;
        writeTrackerStateUnlocked(state);
        return true;
    });
    if (!captured)
    {
        error = "STOP PLAYBACK/RECORDING";
        return false;
    }
    if (state.getSize() == 0) { error = "EMPTY TRACKER STATE"; return false; }
    samples.clear();
    for (std::size_t stackIndex = 0; stackIndex < machineStacks.size(); ++stackIndex)
    {
        auto stackSamples = machineStacks[stackIndex].sampler->exportPortableSamples(static_cast<int>(stackIndex));
        samples.insert(samples.end(), std::make_move_iterator(stackSamples.begin()), std::make_move_iterator(stackSamples.end()));
    }
    return true;
}

bool TrackerMainProcessor::loadTrackerStateFile(const juce::File& source, juce::String& error)
{
    const auto validState = [&error](const juce::MemoryBlock& data)
    {
        juce::ValueTree tree;
        juce::var state;
        return decodeCurrentTrackerState(data.getData(), data.getSize(), tree, state, &error);
    };
    if (!source.existsAsFile()) { error = "FILE NOT FOUND"; return false; }
    if (!portable_tracker_state::isPortableFile(source))
    {
        juce::MemoryBlock state;
        if (!source.loadFileAsData(state) || state.getSize() == 0) { error = "COULD NOT LOAD " + source.getFileName(); return false; }
        if (!validState(state)) return false;
        setStateInformation(state.getData(), static_cast<int>(state.getSize()));
        return true;
    }
    PortableTrackerDocument document;
    if (!portable_tracker_state::read(source, document, error)) return false;
    if (!validState(document.state)) return false;
    setStateInformation(document.state.getData(), static_cast<int>(document.state.getSize()));
    for (const auto& sample : document.samples)
        if (sample.stackId >= 0 && static_cast<std::size_t>(sample.stackId) < machineStacks.size())
            machineStacks[static_cast<std::size_t>(sample.stackId)].sampler->restorePortableSample(sample);
    return true;
}

juce::var TrackerMainProcessor::stringGridToVar(const std::vector<std::vector<std::string>>& grid)
{
    juce::Array<juce::var> cols;
    for (const auto& col : grid)
    {
        juce::Array<juce::var> rows;
        for (const auto& cell : col)
            rows.add(juce::String(cell));
        cols.add(rows);
    }
    return cols;
}
juce::var TrackerMainProcessor::numberGridToVar(const std::vector<std::vector<double>>& grid)
{
    juce::Array<juce::var> cols;
    for (const auto& col : grid)
    {
        juce::Array<juce::var> rows;
        for (const auto& cell : col)
            rows.add(cell);
        cols.add(rows);
    }
    return cols;
}

juce::String TrackerMainProcessor::getUiModeString() const
{
    juce::String modeStr = "sequence";
    switch (seqEditor.getEditMode())
    {
        case SequencerEditorMode::arrangingSong:
            modeStr = "song";
            break;
        case SequencerEditorMode::selectingSeqAndStep:
            modeStr = "sequence";
            break;
        case SequencerEditorMode::editingStep:
            modeStr = "step";
            break;
        case SequencerEditorMode::configuringSequence:
            modeStr = "config";
            break;
        case SequencerEditorMode::machineConfig:
            modeStr = "machine";
            break;
        case SequencerEditorMode::mixer:
            modeStr = "mixer";
            break;
        case SequencerEditorMode::audioConfig:
            modeStr = "audio";
            break;
        case SequencerEditorMode::help:
            modeStr = "help";
            break;
        case SequencerEditorMode::resetConfirmation:
            modeStr = "reset";
            break;
    }
    return modeStr;
}

void TrackerMainProcessor::fillCommonUiState(juce::DynamicObject::Ptr state)
{
    auto* viewedSequencer = getViewedSequencerInternal();
    auto* playbackSequencer = getPlaybackSequencerInternal();
    state->setProperty("bpm", getBPM());
    state->setProperty("isPlaying", playbackSequencer != nullptr && playbackSequencer->isPlaying());
    state->setProperty("mode", getUiModeString());

    state->setProperty("currentSequence", static_cast<int>(seqEditor.getCurrentSequence()));
    state->setProperty("currentStep", static_cast<int>(seqEditor.getCurrentStep()));
    state->setProperty("currentStepRow", static_cast<int>(seqEditor.getCurrentStepRow()));
    state->setProperty("currentStepCol", static_cast<int>(seqEditor.getCurrentStepCol()));
    state->setProperty("armedSequence", static_cast<int>(seqEditor.getArmedSequence()));
    state->setProperty("sequenceEntryArmed", seqEditor.isSequenceEntryArmed());
    state->setProperty("chordHudLabel", juce::String(seqEditor.getChordHudLabel()));
    state->setProperty("chordHudVariation", static_cast<int>(seqEditor.getChordHudVariation()));
    state->setProperty("chordHudVariationCount", static_cast<int>(seqEditor.getChordHudVariationCount()));
    state->setProperty("chordHudExpiryMs", seqEditor.getChordHudExpiryMs());
    state->setProperty("currentSeqParam", static_cast<int>(seqEditor.getCurrentSeqParam()));
    state->setProperty("mixerColumn", static_cast<int>(seqEditor.getMixerColumn()));
    const auto selectedMixerStack = seqEditor.getSelectedMixerStack();
    const auto selectedMixerAux = seqEditor.getSelectedMixerAux();
    state->setProperty("mixerStack", selectedMixerStack ? static_cast<int>(*selectedMixerStack) : -1);
    state->setProperty("mixerAux", selectedMixerAux ? static_cast<int>(*selectedMixerAux) : -1);
    state->setProperty("mixerMasterSelected", seqEditor.isMixerMasterSelected());
    state->setProperty("mixerRow", static_cast<int>(seqEditor.getMixerRow()));
    state->setProperty("masterRecording", isMasterRecording());
    state->setProperty("masterMeterLeft", masterMeterLevels[0]);
    state->setProperty("masterMeterRight", masterMeterLevels[1]);
    state->setProperty("selectedHead", static_cast<int>(seqEditor.getCurrentConfigHead()));

    juce::Array<juce::var> playHeads;
    juce::Array<juce::var> sequenceSoloStates;
    juce::Array<juce::var> sequenceEffectiveSoloStates;
    juce::Array<juce::var> sequenceStackAudibleStates;
    juce::Array<juce::var> sequenceMachineIds;
    juce::Array<juce::var> sequenceNoteLengthModes;
    juce::Array<juce::var> sequenceLengths;
    const bool anyStackSolo = std::any_of(machineStacks.begin(), machineStacks.end(),
                                          [] (const auto& stack) { return stack.solo; });
    if (viewedSequencer != nullptr)
    {
        for (std::size_t col = 0; col < viewedSequencer->howManySequences(); ++col)
        {
            const auto* sequence = viewedSequencer->getSequence(col);
            const bool sequenceSolo = sequence->isSolo();
            sequenceSoloStates.add(sequenceSolo);
            sequenceMachineIds.add(static_cast<int>(sequence->getMachineId()));
            sequenceLengths.add(static_cast<int>(sequence->getLength()));
            const auto noteLengthMode = sequence->getNoteLengthMode();
            sequenceNoteLengthModes.add(noteLengthMode == SequenceReadOnly::NoteLengthMode::half ? 1
                : noteLengthMode == SequenceReadOnly::NoteLengthMode::quarter ? 2 : 0);
            const auto stackIndex = static_cast<std::size_t>(juce::jmax(0, static_cast<int>(sequence->getMachineId())));
            const auto* stack = getMachineStack(stackIndex);
            const auto presentation = MixerState::getSequencePresentation(
                sequenceSolo,
                stack != nullptr && stack->muted,
                stack != nullptr && stack->solo,
                anyStackSolo);
            sequenceEffectiveSoloStates.add(presentation.soloOutline);
            sequenceStackAudibleStates.add(presentation.stackAudible);
            const auto heads = sequence->getReadHeadSnapshots();
            for (std::size_t headIndex = 0; headIndex < heads.size(); ++headIndex)
                for (const auto position : heads[headIndex].positions)
                {
                    juce::DynamicObject::Ptr ph = new juce::DynamicObject();
                    ph->setProperty("sequence", static_cast<int>(col));
                    ph->setProperty("head", static_cast<int>(headIndex));
                    ph->setProperty("step", static_cast<int>(position));
                    playHeads.add(juce::var(ph));
                }
        }
    }
    state->setProperty("playHeads", playHeads);
    state->setProperty("sequenceSoloStates", sequenceSoloStates);
    state->setProperty("sequenceEffectiveSoloStates", sequenceEffectiveSoloStates);
    state->setProperty("sequenceStackAudibleStates", sequenceStackAudibleStates);
    state->setProperty("sequenceMachineIds", sequenceMachineIds);
    state->setProperty("sequenceNoteLengthModes", sequenceNoteLengthModes);
    state->setProperty("sequenceLengths", sequenceLengths);
}

juce::var TrackerMainProcessor::getUiState()
{
    auto* viewedSequencer = getViewedSequencerInternal();
    if (viewedSequencer == nullptr)
        return {};

    juce::DynamicObject::Ptr state = new juce::DynamicObject();
    fillCommonUiState(state.get());
    state->setProperty("sequenceGrid", stringGridToVar(viewedSequencer->getSequenceAsGridOfStrings()));
    state->setProperty("stepGrid", stringGridToVar(viewedSequencer->getStepAsGridOfStrings(seqEditor.getCurrentSequence(), seqEditor.getCurrentStep())));
    state->setProperty("sequenceConfigs", stringGridToVar(viewedSequencer->getSequenceConfigsAsGridOfStrings(seqEditor.getCurrentConfigHead())));
    state->setProperty("stepData", numberGridToVar(viewedSequencer->getStepData(seqEditor.getCurrentSequence(), seqEditor.getCurrentStep())));

    Sequence* currentSequence = viewedSequencer->getSequence(seqEditor.getCurrentSequence());
    state->setProperty("machineId", currentSequence->getMachineId());
    state->setProperty("machineType", currentSequence->getMachineType());
    juce::Array<juce::var> readHeads;
    for (const auto& head : currentSequence->getReadHeadSnapshots())
    {
        juce::DynamicObject::Ptr item = new juce::DynamicObject();
        item->setProperty("ticksPerStep", static_cast<int>(head.config.ticksPerStep));
        item->setProperty("mode", Sequence::readModeName(head.config.mode));
        item->setProperty("polyphony", static_cast<int>(head.config.polyphony));
        item->setProperty("octaves", static_cast<int>(head.config.octaves));
        item->setProperty("rhythm", juce::String(head.config.rhythm));
        item->setProperty("probability", head.config.probability);
        juce::Array<juce::var> positions;
        for (auto position : head.positions) positions.add(static_cast<int>(position));
        item->setProperty("positions", positions);
        readHeads.add(item.get());
    }
    state->setProperty("readHeads", readHeads);

    return state.get();
}

juce::var TrackerMainProcessor::getUiStateForCurrentPage()
{
    auto* viewedSequencer = getViewedSequencerInternal();
    if (viewedSequencer == nullptr)
        return {};

    juce::DynamicObject::Ptr state = new juce::DynamicObject();
    fillCommonUiState(state.get());
    switch (seqEditor.getEditMode())
    {
        case SequencerEditorMode::selectingSeqAndStep:
        {
            state->setProperty("sequenceGrid", stringGridToVar(viewedSequencer->getSequenceAsGridOfStrings()));
            juce::Array<juce::var> controlModes;
            for (std::size_t sequenceIndex = 0; sequenceIndex < viewedSequencer->howManySequences(); ++sequenceIndex)
            {
                auto* sequence = viewedSequencer->getSequence(sequenceIndex);
                controlModes.add(sequence != nullptr && sequence->isControlMode());
            }
            state->setProperty("sequenceControlModes", controlModes);
            break;
        }
        case SequencerEditorMode::editingStep:
            state->setProperty("stepGrid", stringGridToVar(viewedSequencer->getStepAsGridOfStrings(seqEditor.getCurrentSequence(), seqEditor.getCurrentStep())));
            break;
        case SequencerEditorMode::configuringSequence:
            state->setProperty("sequenceConfigs", stringGridToVar(viewedSequencer->getSequenceConfigsAsGridOfStrings(seqEditor.getCurrentConfigHead())));
            break;
        case SequencerEditorMode::arrangingSong:
        case SequencerEditorMode::machineConfig:
        case SequencerEditorMode::mixer:
            break;
        case SequencerEditorMode::audioConfig:
        {
            const auto& cells = seqEditor.getAudioConfigCells();
            std::vector<std::vector<std::string>> grid(cells.size());
            for (std::size_t column = 0; column < cells.size(); ++column)
                for (const auto& cell : cells[column]) grid[column].push_back(cell.text);
            state->setProperty("audioConfigGrid", stringGridToVar(grid));
            break;
        }
        case SequencerEditorMode::help:
        case SequencerEditorMode::resetConfirmation:
            break;
    }
    return state.get();
}

juce::var TrackerMainProcessor::serializeSingleSequencer(const Sequencer& sequencerToSave) const
{
    juce::DynamicObject::Ptr seqRoot = new juce::DynamicObject();
    juce::Array<juce::var> sequencesVar;
    const auto seqCount = sequencerToSave.howManySequences();
    for (std::size_t seqIndex = 0; seqIndex < seqCount; ++seqIndex)
    {
        juce::DynamicObject::Ptr seqObj = new juce::DynamicObject();
        Sequence* seq = const_cast<Sequencer&>(sequencerToSave).getSequence(seqIndex);
        const auto length = seq->getLength();
        seqObj->setProperty("length", static_cast<int>(length));
        seqObj->setProperty("type", static_cast<int>(seq->getType()));
        seqObj->setProperty("muted", seq->isMuted());
        seqObj->setProperty("solo", seq->isSolo());
        seqObj->setProperty("controlMode", seq->isControlMode());
        const auto noteLengthMode = seq->getNoteLengthMode();
        seqObj->setProperty("noteLengthMode", noteLengthMode == SequenceReadOnly::NoteLengthMode::half ? "half"
            : noteLengthMode == SequenceReadOnly::NoteLengthMode::quarter ? "quarter" : "off");
        seqObj->setProperty("machineId", seq->getMachineId());
        seqObj->setProperty("machineType", seq->getMachineType());
        juce::Array<juce::var> readHeads;
        for (std::size_t headIndex = 0; headIndex < seq->getReadHeadCount(); ++headIndex)
        {
            const auto& head = seq->getReadHeadConfig(headIndex);
            juce::DynamicObject::Ptr headObj = new juce::DynamicObject();
            headObj->setProperty("ticksPerStep", static_cast<int>(head.ticksPerStep));
            headObj->setProperty("mode", Sequence::readModeName(head.mode));
            headObj->setProperty("polyphony", static_cast<int>(head.polyphony));
            headObj->setProperty("octaves", static_cast<int>(head.octaves));
            headObj->setProperty("rhythm", juce::String(head.rhythm));
            headObj->setProperty("probability", head.probability);
            readHeads.add(headObj.get());
        }
        seqObj->setProperty("readHeads", readHeads);

        juce::Array<juce::var> stepsVar;
        for (std::size_t step = 0; step < length; ++step)
        {
            juce::DynamicObject::Ptr stepObj = new juce::DynamicObject();
            stepObj->setProperty("active", sequencerToSave.isStepActive(seqIndex, step));
            juce::Array<juce::var> rows;
            const auto data = const_cast<Sequencer&>(sequencerToSave).getStepData(seqIndex, step);
            for (const auto& row : data)
            {
                juce::Array<juce::var> cols;
                for (auto val : row)
                    cols.add(val);
                rows.add(cols);
            }
            stepObj->setProperty("data", rows);
            stepsVar.add(stepObj.get());
        }

        seqObj->setProperty("steps", stepsVar);
        sequencesVar.add(seqObj.get());
    }

    seqRoot->setProperty("sequences", sequencesVar);
    return juce::var(seqRoot.get());
}

void TrackerMainProcessor::restoreSingleSequencer(Sequencer& target, const juce::var& seqVar)
{
    if (!seqVar.isObject())
        return;

    const auto seqArrayVar = seqVar.getProperty("sequences", juce::var());
    if (!seqArrayVar.isArray())
        return;

    const auto& seqArray = *seqArrayVar.getArray();
    const auto seqCount = std::min(static_cast<std::size_t>(seqArray.size()), target.howManySequences());
    for (std::size_t i = 0; i < seqCount; ++i)
    {
        const auto& seqObj = seqArray[static_cast<int>(i)];
        if (!seqObj.isObject())
            continue;

        Sequence* seq = target.getSequence(i);
        const int length = juce::jmax(1, static_cast<int>(seqObj.getProperty("length", static_cast<int>(seq->getLength()))));
        seq->ensureEnoughStepsForLength(static_cast<std::size_t>(length));
        seq->setLength(static_cast<std::size_t>(length));
        const auto lengthMode = seqObj.getProperty("noteLengthMode", "off").toString();
        seq->setNoteLengthMode(lengthMode == "half" ? SequenceReadOnly::NoteLengthMode::half
            : lengthMode == "quarter" ? SequenceReadOnly::NoteLengthMode::quarter
            : SequenceReadOnly::NoteLengthMode::off);

        const auto typeInt = static_cast<int>(seqObj.getProperty("type", static_cast<int>(seq->getType())));
        seq->setType(static_cast<SequenceType>(typeInt));

        const auto headsVar = seqObj.getProperty("readHeads", juce::var());
        if (headsVar.isArray() && !headsVar.getArray()->isEmpty())
        {
            seq->setReadHeadCount(static_cast<std::size_t>(juce::jlimit(1, 3, headsVar.getArray()->size())));
            for (std::size_t headIndex = 0; headIndex < seq->getReadHeadCount(); ++headIndex)
            {
                const auto& headVar = headsVar.getArray()->getReference(static_cast<int>(headIndex));
                auto config = seq->getReadHeadConfig(headIndex);
                config.ticksPerStep = static_cast<std::size_t>(static_cast<int>(headVar.getProperty("ticksPerStep", 4)));
                SequenceReadMode mode{};
                if (Sequence::parseReadMode(headVar.getProperty("mode", "linear").toString().toStdString(), mode)) config.mode = mode;
                config.polyphony = static_cast<std::size_t>(static_cast<int>(headVar.getProperty("polyphony", 3)));
                config.octaves = static_cast<std::size_t>(static_cast<int>(headVar.getProperty("octaves", 1)));
                config.rhythm = headVar.getProperty("rhythm", "1").toString().toStdString();
                config.probability = static_cast<double>(headVar.getProperty("probability", 1.0));
                seq->setReadHeadConfig(headIndex, config);
            }
        }

        double machineId = static_cast<double>(seqObj.getProperty("machineId", seq->getMachineId()));
        if (machineId >= static_cast<double>(kMachineStackCount))
            machineId = std::fmod(machineId, static_cast<double>(kMachineStackCount));
        int machineType = static_cast<int>(seqObj.getProperty("machineType", seq->getMachineType()));
        if (machineType < 0
            || !isValidCommandTypeValue(static_cast<std::size_t>(machineType))
            || !machineTraits(static_cast<CommandType>(machineType)).isStepCommandType)
            machineType = static_cast<int>(CommandType::MidiNote);

        const auto stepsVar = seqObj.getProperty("steps", juce::var());
        if (stepsVar.isArray())
        {
            const auto& stepsArray = *stepsVar.getArray();
            const auto stepsToLoad = std::min(static_cast<std::size_t>(stepsArray.size()), static_cast<std::size_t>(length));
            for (std::size_t step = 0; step < stepsToLoad; ++step)
            {
                const auto& stepVar = stepsArray[static_cast<int>(step)];
                if (!stepVar.isObject())
                    continue;

                const auto dataVar = stepVar.getProperty("data", juce::var());
                if (dataVar.isArray())
                {
                    std::vector<std::vector<double>> data;
                    for (const auto& rowVar : *dataVar.getArray())
                    {
                        std::vector<double> row;
                        if (rowVar.isArray())
                        {
                            for (const auto& val : *rowVar.getArray())
                                row.push_back(static_cast<double>(val));
                        }
                        if (row.size() == Step::maxInd + 1)
                        {
                            const auto commandType = static_cast<int>(row.front());
                            if (commandType < 0
                                || !isValidCommandTypeValue(static_cast<std::size_t>(commandType))
                                || !machineTraits(static_cast<CommandType>(commandType)).isStepCommandType)
                                continue;
                            data.push_back(std::move(row));
                        }
                    }
                    if (!data.empty())
                        target.setStepData(i, step, data);
                }

                const bool active = static_cast<bool>(stepVar.getProperty("active", true));
                if (target.isStepActive(i, step) != active)
                    target.toggleStepActive(i, step);
            }
        }

        seq->setMachineId(machineId);
        seq->setMachineType(machineType);

        const bool mutedTarget = static_cast<bool>(seqObj.getProperty("muted", false));
        if (seq->isMuted() != mutedTarget)
            target.toggleSequenceMute(i);

        const bool soloTarget = static_cast<bool>(seqObj.getProperty("solo", false));
        if (seq->isSolo() != soloTarget)
            target.setSequenceSolo(i, soloTarget);

        const bool controlModeTarget = static_cast<bool>(seqObj.getProperty("controlMode", false));
        if (seq->isControlMode() != controlModeTarget)
            target.setSequenceControlMode(i, controlModeTarget);
    }
}

juce::var TrackerMainProcessor::serializeSequencerState()
{
    juce::DynamicObject::Ptr root = new juce::DynamicObject();
    root->setProperty("stateVersion", kTrackerStateVersion);
    // Keep file saves and host/plugin saves identical.  The arrangement and
    // machine data below are not useful without the transport they were made
    // against, so persist the tracker-owned clock settings alongside them.
    root->setProperty("bpm", getBPM());
    root->setProperty("internalClockEnabled", isInternalClockEnabled());
    root->setProperty("masterGainDb", masterGainDb);
    root->setProperty("masterMuted", masterMuted);
    juce::Array<juce::var> sequenceSetStates;
    for (const auto& sequenceSet : sequenceSets)
        if (sequenceSet != nullptr)
            sequenceSetStates.add(serializeSingleSequencer(*sequenceSet));
    root->setProperty("sequenceSets", sequenceSetStates);
    juce::Array<juce::var> sequenceSetBpmsVar;
    for (std::size_t setIndex = 0; setIndex < sequenceSets.size(); ++setIndex)
        sequenceSetBpmsVar.add(setIndex < sequenceSetBpms.size() ? sequenceSetBpms[setIndex] : getBPM());
    root->setProperty("sequenceSetBpms", sequenceSetBpmsVar);

    juce::Array<juce::var> machineEnableStateSets;
    for (std::size_t setIndex = 0; setIndex < sequenceSets.size(); ++setIndex)
    {
        const auto state = setIndex < sequenceSetMachineEnableStates.size()
            ? sequenceSetMachineEnableStates[setIndex]
            : captureMachineEnableState();
        juce::DynamicObject::Ptr setObj = new juce::DynamicObject();
        juce::Array<juce::var> stacks;
        for (const auto& savedStack : state.stacks)
        {
            juce::Array<juce::var> slots;
            for (const auto& savedSlot : savedStack)
            {
                juce::DynamicObject::Ptr slotObj = new juce::DynamicObject();
                slotObj->setProperty("id", juce::String(savedSlot.id));
                slotObj->setProperty("type", static_cast<int>(savedSlot.type));
                slotObj->setProperty("enabled", savedSlot.enabled);
                slots.add(slotObj.get());
            }
            stacks.add(slots);
        }
        setObj->setProperty("stacks", stacks);
        juce::Array<juce::var> modulation;
        for (bool enabled : state.modulationEnabled) modulation.add(enabled);
        setObj->setProperty("modulationEnabled", modulation);
        machineEnableStateSets.add(setObj.get());
    }
    root->setProperty("sequenceSetMachineEnableStates", machineEnableStateSets);
    root->setProperty("viewedSequenceSetIndex", static_cast<int>(viewedSequenceSetIndex));
    root->setProperty("activePlaybackSequenceSetIndex", static_cast<int>(activePlaybackSequenceSetIndex));
    root->setProperty("selectedSongRow", static_cast<int>(selectedSongRow));
    root->setProperty("currentSongRow", static_cast<int>(currentSongRow));
    root->setProperty("currentSongRowBeatCounter", currentSongRowBeatCounter);
    root->setProperty("songPlayMode", songPlayMode == SongPlayMode::song ? "song" : "sequence");
    root->setProperty("currentSequence", static_cast<int>(seqEditor.getCurrentSequence()));
    root->setProperty("currentStep", static_cast<int>(seqEditor.getCurrentStep()));
    root->setProperty("currentStepRow", static_cast<int>(seqEditor.getCurrentStepRow()));
    root->setProperty("currentStepCol", static_cast<int>(seqEditor.getCurrentStepCol()));
    root->setProperty("currentSongRowCursor", static_cast<int>(seqEditor.getCurrentSongRow()));
    root->setProperty("currentSongColCursor", static_cast<int>(seqEditor.getCurrentSongCol()));

    root->setProperty("mode", getUiModeString());

    juce::Array<juce::var> songRowsVar;
    for (const auto& row : songRows)
    {
        juce::DynamicObject::Ptr rowObj = new juce::DynamicObject();
        rowObj->setProperty("sequenceSetId", static_cast<int>(row.sequenceSetId));
        rowObj->setProperty("beatCount", row.beatCount);
        songRowsVar.add(rowObj.get());
    }
    root->setProperty("songRows", songRowsVar);

    juce::Array<juce::var> stackStates;
    for (auto& stack : machineStacks)
    {
        juce::DynamicObject::Ptr stackObj = new juce::DynamicObject();
        juce::Array<juce::var> slots;
        for (const auto& slot : stack.slots)
        {
            juce::DynamicObject::Ptr slotObj = new juce::DynamicObject();
            slotObj->setProperty("id", juce::String(slot.id));
            slotObj->setProperty("type", static_cast<int>(slot.type));
            slotObj->setProperty("enabled", slot.enabled);
            slotObj->setProperty("sendLevelDb", slot.sendLevelDb);
            slotObj->setProperty("returnLevelDb", slot.returnLevelDb);
            slots.add(slotObj.get());
        }
        stackObj->setProperty("slots", slots);
        stackObj->setProperty("midiOutputChannel", stack.midiOutputChannel);
        stackObj->setProperty("gainDb", stack.gainDb);
        stackObj->setProperty("pan", stack.pan);
        stackObj->setProperty("muted", stack.muted);
        stackObj->setProperty("solo", stack.solo);

        for (const auto& [key, machine] : stackMachineStateEntries(stack))
            stackObj->setProperty(key, encodeMachineState(machine));
        stackStates.add(stackObj.get());
    }
    root->setProperty("machineStacks", stackStates);

    juce::DynamicObject::Ptr auxObj = new juce::DynamicObject();
    auxObj->setProperty("aux1", encodeMachineState(auxBus1.machine.get()));
    auxObj->setProperty("aux2", encodeMachineState(auxBus2.machine.get()));
    auxObj->setProperty("aux1GainDb", auxBus1.gainDb);
    auxObj->setProperty("aux1Pan", auxBus1.pan);
    auxObj->setProperty("aux1Muted", auxBus1.muted);
    auxObj->setProperty("aux2GainDb", auxBus2.gainDb);
    auxObj->setProperty("aux2Pan", auxBus2.pan);
    auxObj->setProperty("aux2Muted", auxBus2.muted);
    root->setProperty("sharedAuxBuses", auxObj.get());

    return root.get();
}

bool TrackerMainProcessor::getStepValuesForSequenceSet(std::size_t setIndex,
                                                        std::size_t sequenceIndex,
                                                        std::size_t stepIndex,
                                                        std::vector<std::vector<double>>& values) const
{
    values.clear();
    if (setIndex >= sequenceSets.size() || sequenceSets[setIndex] == nullptr)
        return false;
    auto* sequencer = sequenceSets[setIndex].get();
    if (sequenceIndex >= sequencer->howManySequences()
        || stepIndex >= sequencer->howManySteps(sequenceIndex))
        return false;
    values = sequencer->getStepData(sequenceIndex, stepIndex);
    return true;
}

std::size_t TrackerMainProcessor::getStepCountForSequenceSet(std::size_t setIndex,
                                                              std::size_t sequenceIndex) const
{
    if (setIndex >= sequenceSets.size() || sequenceSets[setIndex] == nullptr)
        return 0;
    const auto* sequencer = sequenceSets[setIndex].get();
    return sequenceIndex < sequencer->howManySequences() ? sequencer->howManySteps(sequenceIndex) : 0;
}

void TrackerMainProcessor::restoreSequencerState(const juce::var& stateVar)
{
    if (!stateVar.isObject())
        return;

    // This is a live-performance safety state, deliberately not part of a
    // tracker document. Any state-load attempt starts with Sequence note entry off.
    seqEditor.setSequenceEntryArmed(false);
    seqEditor.resetChordEntryState();

    const auto restoredBpm = static_cast<double>(stateVar.getProperty("bpm", getBPM()));
    if (restoredBpm > 0.0)
        setBPM(restoredBpm);
    setInternalClockEnabled(static_cast<bool>(stateVar.getProperty("internalClockEnabled", isInternalClockEnabled())));
    setMasterGainDb(static_cast<float>(stateVar.getProperty("masterGainDb", 0.0f)));
    setMasterMuted(static_cast<bool>(stateVar.getProperty("masterMuted", false)));
    masterMeterLevels.fill(0.0f);
    auxBus1.meterLevels.fill(0.0f);
    auxBus2.meterLevels.fill(0.0f);
    setAuxReturnGainDb(0, 0.0f);
    setAuxReturnGainDb(1, 0.0f);
    setAuxReturnMuted(0, false);
    setAuxReturnMuted(1, false);

    resetSongState();

    const auto sequenceSetsVar = stateVar.getProperty("sequenceSets", juce::var());
    if (sequenceSetsVar.isArray() && !sequenceSetsVar.getArray()->isEmpty())
    {
        const auto savedSequenceCount = [](const juce::var& setVar) -> std::size_t
        {
            if (!setVar.isObject())
                return 16;
            const auto sequencesVar = setVar.getProperty("sequences", juce::var());
            if (!sequencesVar.isArray())
                return 16;
            return static_cast<std::size_t>(juce::jlimit(
                1,
                static_cast<int>(Sequencer::maxSequences),
                sequencesVar.getArray()->size()));
        };

        sequenceSets.clear();
        for (const auto& sequenceSetVar : *sequenceSetsVar.getArray())
        {
            auto sequenceSet = createDefaultSequenceSet(savedSequenceCount(sequenceSetVar));
            restoreSingleSequencer(*sequenceSet, sequenceSetVar);
            sequenceSets.push_back(std::move(sequenceSet));
        }
    }

    sequenceSetBpms.assign(sequenceSets.size(), juce::jlimit(1.0, 999.0, restoredBpm));
    const auto sequenceSetBpmsVar = stateVar.getProperty("sequenceSetBpms", juce::var());
    if (sequenceSetBpmsVar.isArray())
    {
        const auto restoredCount = std::min(sequenceSetBpms.size(), static_cast<std::size_t>(sequenceSetBpmsVar.getArray()->size()));
        for (std::size_t setIndex = 0; setIndex < restoredCount; ++setIndex)
        {
            const double restoredSetBpm = static_cast<double>((*sequenceSetBpmsVar.getArray())[static_cast<int>(setIndex)]);
            if (std::isfinite(restoredSetBpm))
                sequenceSetBpms[setIndex] = juce::jlimit(1.0, 999.0, restoredSetBpm);
        }
    }

    sequenceSetMachineEnableStates.clear();
    const auto machineEnableStateSetsVar = stateVar.getProperty("sequenceSetMachineEnableStates", juce::var());
    if (machineEnableStateSetsVar.isArray())
    {
        const auto savedSetCount = std::min(static_cast<std::size_t>(machineEnableStateSetsVar.getArray()->size()), sequenceSets.size());
        sequenceSetMachineEnableStates.resize(savedSetCount);
        for (std::size_t setIndex = 0; setIndex < savedSetCount; ++setIndex)
        {
            const auto& setVar = (*machineEnableStateSetsVar.getArray())[static_cast<int>(setIndex)];
            if (!setVar.isObject())
                continue;
            const auto stacksVar = setVar.getProperty("stacks", juce::var());
            const auto modulationVar = setVar.getProperty("modulationEnabled", juce::var());
            if (modulationVar.isArray())
                for (const auto& enabled : *modulationVar.getArray())
                    sequenceSetMachineEnableStates[setIndex].modulationEnabled.push_back(static_cast<bool>(enabled));
            if (!stacksVar.isArray())
                continue;
            auto& restoredState = sequenceSetMachineEnableStates[setIndex];
            restoredState.stacks.resize(static_cast<std::size_t>(stacksVar.getArray()->size()));
            for (int stackIndex = 0; stackIndex < stacksVar.getArray()->size(); ++stackIndex)
            {
                const auto& slotsVar = (*stacksVar.getArray())[stackIndex];
                if (!slotsVar.isArray())
                    continue;
                auto& restoredSlots = restoredState.stacks[static_cast<std::size_t>(stackIndex)];
                restoredSlots.reserve(static_cast<std::size_t>(slotsVar.getArray()->size()));
                for (const auto& slotVar : *slotsVar.getArray())
                {
                    if (!slotVar.isObject())
                        continue;
                    SequenceSetMachineEnableState::Slot slot;
                    slot.id = slotVar.getProperty("id", juce::String()).toString().toStdString();
                    const int typeValue = static_cast<int>(slotVar.getProperty("type", -1));
                    if (typeValue < 0 || !isValidCommandTypeValue(static_cast<std::size_t>(typeValue)))
                        continue;
                    slot.type = static_cast<CommandType>(typeValue);
                    if (!machineTraits(slot.type).isStackRoutable)
                        continue;
                    slot.enabled = static_cast<bool>(slotVar.getProperty("enabled", true));
                    if (!slot.id.empty())
                        restoredSlots.push_back(std::move(slot));
                }
            }
        }
    }
    sequenceSetMachineEnableStates.resize(sequenceSets.size());

    const auto songRowsVar = stateVar.getProperty("songRows", juce::var());
    songRows.clear();
    if (songRowsVar.isArray())
    {
        for (const auto& rowVar : *songRowsVar.getArray())
        {
            if (!rowVar.isObject())
                continue;
            SongRow row;
            row.sequenceSetId = static_cast<std::size_t>(juce::jmax(0, static_cast<int>(rowVar.getProperty("sequenceSetId", 0))));
            row.beatCount = juce::jmax(1, static_cast<int>(rowVar.getProperty("beatCount", 16)));
            if (!sequenceSets.empty())
                row.sequenceSetId = std::min(row.sequenceSetId, sequenceSets.size() - 1);
            songRows.push_back(row);
        }
    }
    if (songRows.empty())
        songRows.push_back({ 0, 16 });

    viewedSequenceSetIndex = static_cast<std::size_t>(juce::jmax(0, static_cast<int>(stateVar.getProperty("viewedSequenceSetIndex", 0))));
    activePlaybackSequenceSetIndex = static_cast<std::size_t>(juce::jmax(0, static_cast<int>(stateVar.getProperty("activePlaybackSequenceSetIndex", static_cast<int>(viewedSequenceSetIndex)))));
    if (!sequenceSets.empty())
    {
        viewedSequenceSetIndex = std::min(viewedSequenceSetIndex, sequenceSets.size() - 1);
        activePlaybackSequenceSetIndex = std::min(activePlaybackSequenceSetIndex, sequenceSets.size() - 1);
    }

    selectedSongRow = static_cast<std::size_t>(juce::jmax(0, static_cast<int>(stateVar.getProperty("selectedSongRow", 0))));
    currentSongRow = static_cast<std::size_t>(juce::jmax(0, static_cast<int>(stateVar.getProperty("currentSongRow", static_cast<int>(selectedSongRow)))));
    if (!songRows.empty())
    {
        selectedSongRow = std::min(selectedSongRow, songRows.size() - 1);
        currentSongRow = std::min(currentSongRow, songRows.size() - 1);
    }
    currentSongRowBeatCounter = juce::jmax(1, static_cast<int>(stateVar.getProperty("currentSongRowBeatCounter", songRows.empty() ? 16 : songRows[currentSongRow].beatCount)));
    songPlayMode = stateVar.getProperty("songPlayMode", "sequence").toString().equalsIgnoreCase("song")
        ? SongPlayMode::song
        : SongPlayMode::sequence;
    pendingPlaybackSequenceSetIndex.reset();

    bindViewedSequenceSetToEditor();
    auto* viewedSequencer = getViewedSequencerInternal();
    if (viewedSequencer == nullptr)
        return;

    int seq = static_cast<int>(stateVar.getProperty("currentSequence", static_cast<int>(seqEditor.getCurrentSequence())));
    const int maxSeq = static_cast<int>(std::max<std::size_t>(1, viewedSequencer->howManySequences())) - 1;
    seq = juce::jlimit(0, juce::jmax(0, maxSeq), seq);
    seqEditor.setCurrentSequence(seq);

    int step = static_cast<int>(stateVar.getProperty("currentStep", static_cast<int>(seqEditor.getCurrentStep())));
    const int maxStep = static_cast<int>(std::max<std::size_t>(1, viewedSequencer->howManySteps(static_cast<std::size_t>(seq)))) - 1;
    step = juce::jlimit(0, juce::jmax(0, maxStep), step);
    seqEditor.setCurrentStep(step);

    int stepRow = static_cast<int>(stateVar.getProperty("currentStepRow", static_cast<int>(seqEditor.getCurrentStepRow())));
    stepRow = juce::jmax(0, stepRow);
    // seqEditor.setCurrentStepRow(stepRow);

    int stepCol = static_cast<int>(stateVar.getProperty("currentStepCol", static_cast<int>(seqEditor.getCurrentStepCol())));
    stepCol = juce::jmax(0, stepCol);
    // seqEditor.setCurrentStepCol(stepCol);

    seqEditor.setSelectedSongCursor(
        static_cast<std::size_t>(juce::jmax(0, static_cast<int>(stateVar.getProperty("currentSongRowCursor", static_cast<int>(selectedSongRow))))),
        static_cast<std::size_t>(juce::jmax(0, static_cast<int>(stateVar.getProperty("currentSongColCursor", 0)))));

    juce::String mode = stateVar.getProperty("mode", "sequence").toString().toLowerCase();
    if (mode == "song")
        seqEditor.setEditMode(SequencerEditorMode::arrangingSong);
    else if (mode == "step")
        seqEditor.setEditMode(SequencerEditorMode::editingStep);
    else if (mode == "config")
        seqEditor.setEditMode(SequencerEditorMode::configuringSequence);
    else if (mode == "machine")
        seqEditor.setEditMode(SequencerEditorMode::machineConfig);
    else if (mode == "audio")
        seqEditor.setEditMode(SequencerEditorMode::audioConfig);
    else
        seqEditor.setEditMode(SequencerEditorMode::selectingSeqAndStep);

    const auto machineStacksVar = stateVar.getProperty("machineStacks", juce::var());
    if (machineStacksVar.isArray())
    {
        const auto& stackArray = *machineStacksVar.getArray();
        const auto stackCount = std::min(static_cast<std::size_t>(stackArray.size()), machineStacks.size());
        for (std::size_t i = 0; i < stackCount; ++i)
        {
            if (!stackArray[static_cast<int>(i)].isObject())
                continue;
            auto& stack = machineStacks[i];
            stack.slots.clear();
            const auto slotsVar = stackArray[static_cast<int>(i)].getProperty("slots", juce::var());
            if (slotsVar.isArray())
            {
                for (const auto& slotVar : *slotsVar.getArray())
                {
                    if (!slotVar.isObject())
                        continue;
                    const auto typeValue = static_cast<int>(slotVar.getProperty("type", -1));
                    if (typeValue < 0 || !isValidCommandTypeValue(static_cast<std::size_t>(typeValue)))
                        continue;
                    const auto type = static_cast<CommandType>(typeValue);
                    if (!machineTraits(type).isStackRoutable)
                        continue;
                    auto slot = makeDefaultSlotState(type);
                    slot.id = slotVar.getProperty("id", juce::String(slot.id)).toString().toStdString();
                    slot.enabled = static_cast<bool>(slotVar.getProperty("enabled", slot.enabled));
                    slot.sendLevelDb = juce::jlimit(-60.0f, 12.0f, static_cast<float>(slotVar.getProperty("sendLevelDb", slot.sendLevelDb)));
                    slot.returnLevelDb = juce::jlimit(-60.0f, 12.0f, static_cast<float>(slotVar.getProperty("returnLevelDb", slot.returnLevelDb)));
                    if (!slotSupportsReturnLevel(type))
                        slot.returnLevelDb = 0.0f;
                    stack.slots.push_back(slot);
                }
            }
            if (stack.slots.empty())
                stack.slots.push_back(makeDefaultSlotState(CommandType::MidiNote));
            stack.midiOutputChannel = juce::jlimit(1, 16,
                static_cast<int>(stackArray[static_cast<int>(i)].getProperty("midiOutputChannel", stack.midiOutputChannel)));
            stack.gainDb = juce::jlimit(-48.0f, 6.0f,
                                        static_cast<float>(stackArray[static_cast<int>(i)].getProperty("gainDb", stack.gainDb)));
            setStackPan(i, static_cast<float>(stackArray[static_cast<int>(i)].getProperty("pan", 0.0f)));
            stack.meterLevel = 0.0f;

            // Reset parameters before applying the document so a missing or
            // malformed blob cannot retain settings from the previous song.
            if (stack.drumSynth != nullptr)
                stack.drumSynth->resetParametersToDefaults();
            for (const auto& [key, machine] : stackMachineStateEntries(stack))
                decodeMachineState(stackArray[static_cast<int>(i)].getProperty(key, juce::var()), machine);
            stack.muted = static_cast<bool>(stackArray[static_cast<int>(i)].getProperty("muted", false));
            stack.solo = static_cast<bool>(stackArray[static_cast<int>(i)].getProperty("solo", false));
            refreshStackProcessingState(stack);
        }
    }

    if (const auto sharedAuxVar = stateVar.getProperty("sharedAuxBuses", juce::var()); sharedAuxVar.isObject())
    {
        decodeMachineState(sharedAuxVar.getProperty("aux1", juce::var()), auxBus1.machine.get());
        decodeMachineState(sharedAuxVar.getProperty("aux2", juce::var()), auxBus2.machine.get());
        setAuxReturnGainDb(0, static_cast<float>(sharedAuxVar.getProperty("aux1GainDb", 0.0f)));
        setAuxReturnPan(0, static_cast<float>(sharedAuxVar.getProperty("aux1Pan", 0.0f)));
        setAuxReturnMuted(0, static_cast<bool>(sharedAuxVar.getProperty("aux1Muted", false)));
        setAuxReturnGainDb(1, static_cast<float>(sharedAuxVar.getProperty("aux2GainDb", 0.0f)));
        setAuxReturnPan(1, static_cast<float>(sharedAuxVar.getProperty("aux2Pan", 0.0f)));
        setAuxReturnMuted(1, static_cast<bool>(sharedAuxVar.getProperty("aux2Muted", false)));
    }

    normaliseAllMachineEnableStates();
    if (viewedSequenceSetIndex < sequenceSetBpms.size())
        setBPM(sequenceSetBpms[viewedSequenceSetIndex]);
    applyMachineEnableStateForSequenceSet(viewedSequenceSetIndex);

    viewedSequencer->updateSeqStringGrid();

    sendChangeMessage();
}
