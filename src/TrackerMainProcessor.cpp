
/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#include "TrackerMainProcessor.h"
#include "TrackerControlService.h"
#include "TrackerMainUI.h"
#include "MixerState.h"
#include <algorithm>
#include <cmath>

namespace
{
constexpr int oscListenPort = 9001;
constexpr int lcdDisplayPort = 9000;
constexpr const char* lcdDisplayHost = "127.0.0.1";
constexpr const char* cellValueAddress = "/cell";
constexpr const char* zoomInAddress = "/zoom_in";
constexpr const char* zoomOutAddress = "/zoom_out";
constexpr const char* incrementAddress = "/increment";
constexpr const char* decrementAddress = "/decrement";
constexpr std::size_t kPanTableSize = 2049;
struct PanGains { float left = 0.70710678f, right = 0.70710678f; };
const std::array<PanGains, kPanTableSize>& equalPowerPanTable()
{
    static const std::array<PanGains, kPanTableSize> table = []
    {
        std::array<PanGains, kPanTableSize> values {};
        for (std::size_t i = 0; i < values.size(); ++i)
        {
            const float fraction = static_cast<float>(i) / static_cast<float>(values.size() - 1);
            const float angle = fraction * juce::MathConstants<float>::halfPi;
            values[i] = { std::cos(angle), std::sin(angle) };
        }
        return values;
    }();
    return table;
}
PanGains equalPowerPan(float pan) noexcept
{
    const float position = (MixerState::clampPan(pan) + 1.0f) * 0.5f * static_cast<float>(kPanTableSize - 1);
    const int index = juce::jlimit(0, static_cast<int>(kPanTableSize - 2), static_cast<int>(position));
    const float fraction = position - static_cast<float>(index);
    const auto& table = equalPowerPanTable();
    return { table[static_cast<std::size_t>(index)].left + (table[static_cast<std::size_t>(index + 1)].left - table[static_cast<std::size_t>(index)].left) * fraction,
             table[static_cast<std::size_t>(index)].right + (table[static_cast<std::size_t>(index + 1)].right - table[static_cast<std::size_t>(index)].right) * fraction };
}
float advancePan(float& current, float target, int& remaining) noexcept
{
    if (remaining > 0)
    {
        current += (target - current) / static_cast<float>(remaining--);
        if (remaining == 0) current = target;
    }
    return current;
}
std::string formatMidiNoteLabel(unsigned short note)
{
    const std::size_t noteIndex = static_cast<std::size_t>(note % 12);
    const std::size_t octave = static_cast<std::size_t>(note / 12);
    const char nchar = MachineUtilsAbs::getIntToNoteMap()[static_cast<int>(noteIndex)];
    std::string disp;
    disp.push_back(nchar);
    disp += "-" + std::to_string(octave) + " ";
    return disp;
}

double getSecondsPerTickFromBpm(double bpm)
{
    return bpm > 0.0 ? (60.0 / bpm) / 8.0 : (60.0 / 120.0) / 8.0;
}

bool isAudioEffectType(CommandType type)
{
    return machineTraits(type).isAudioEffect;
}

float measureBufferRms(const juce::AudioBuffer<float>& buffer)
{
    if (buffer.getNumChannels() == 0 || buffer.getNumSamples() == 0)
        return 0.0f;

    double sumSquares = 0.0;
    std::size_t sampleCount = 0;
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
    {
        const auto* samples = buffer.getReadPointer(channel);
        for (int sampleIndex = 0; sampleIndex < buffer.getNumSamples(); ++sampleIndex)
        {
            const double sample = static_cast<double>(samples[sampleIndex]);
            sumSquares += sample * sample;
        }
        sampleCount += static_cast<std::size_t>(buffer.getNumSamples());
    }


    if (sampleCount == 0)
        return 0.0f;

    return static_cast<float>(std::sqrt(sumSquares / static_cast<double>(sampleCount)));
}

float measureChannelRms(const juce::AudioBuffer<float>& buffer, int channel)
{
    if (channel < 0 || channel >= buffer.getNumChannels() || buffer.getNumSamples() == 0)
        return 0.0f;
    return buffer.getRMSLevel(channel, 0, buffer.getNumSamples());
}

float linearToMeterNormalised(float linearLevel)
{
    if (linearLevel <= 0.0f)
        return 0.0f;

    const float db = juce::Decibels::gainToDecibels(linearLevel, -48.0f);
    return juce::jlimit(0.0f, 1.0f, (db + 48.0f) / 48.0f);
}

}

void TrackerMainProcessor::enqueueMachineMidi(juce::MidiBuffer& targetBuffer,
                                              unsigned short channel,
                                              unsigned short outNote,
                                              unsigned short outVelocity,
                                              double outDurTicks)
{
    const int onSample = elapsedSamples;
    const int offsetSamples = juce::jmax(1, static_cast<int>(std::lround(samplesPerTick * outDurTicks))) % maxHorizon;
    const int offSample = onSample + offsetSamples;
    lastQdOnAt = onSample;

    targetBuffer.addEvent(MidiMessage::noteOn((int)channel, (int)outNote, (uint8)outVelocity), onSample);
    targetBuffer.addEvent(MidiMessage::noteOff((int)channel, (int)outNote, (uint8)outVelocity), offSample);
    outstandingNoteOffs++;
}

std::array<std::pair<const char*, MachineInterface*>, 8> TrackerMainProcessor::stackMachineStateEntries(MachineStack& stack)
{
    std::array<std::pair<const char*, MachineInterface*>, 8> entries;
    entries[0] = { "sampler", stack.sampler.get() };
    entries[1] = { "wavetableSynth", stack.wavetableSynth.get() };
    entries[2] = { "distortionFx", stack.distortionFx.get() };
    entries[3] = { "delayFx", stack.delayFx.get() };
    entries[4] = { "channelStripFx", stack.channelStripFx.get() };
    entries[5] = { "filterFx", stack.filterFx.get() };
    entries[6] = { "modulationMatrix", stack.modulationMatrix.get() };
    entries[7] = { "drumSynth", stack.drumSynth.get() };
    return entries;
}

TrackerMainProcessor::MachineStack::SlotState TrackerMainProcessor::makeDefaultSlotState(CommandType type)
{
    static std::atomic<std::uint64_t> nextSlotId { 1 };
    MachineStack::SlotState slot;
    slot.id = "slot-" + std::to_string(nextSlotId.fetch_add(1));
    slot.type = type;
    slot.enabled = type != CommandType::MidiNote;
    slot.sendLevelDb = 0.0f;
    slot.returnLevelDb = 0.0f;
    if (isAuxSendType(type))
        slot.returnLevelDb = 0.0f;
    return slot;
}

bool TrackerMainProcessor::isAuxSendType(CommandType type)
{
    return type == CommandType::AuxSend1Fx || type == CommandType::AuxSend2Fx;
}

bool TrackerMainProcessor::slotSupportsReturnLevel(CommandType type)
{
    return isAudioEffectType(type) && !isAuxSendType(type);
}

bool TrackerMainProcessor::slotAllowsDuplicate(CommandType)
{
    return false;
}

void TrackerMainProcessor::refreshStackProcessingState(MachineStack& stack)
{
    bool hasAudioPath = false;
    bool samplerActive = false;
    bool wavetableActive = false;
    bool drumActive = false;
    bool filterActive = false;

    for (const auto& slot : stack.slots)
    {
        if (!slot.enabled)
            continue;

        if (slot.type == CommandType::Sampler)
            samplerActive = true;
        else if (slot.type == CommandType::WavetableSynth)
            wavetableActive = true;
        else if (slot.type == CommandType::DrumSynth)
            drumActive = true;
        else if (slot.type == CommandType::FilterFx)
            filterActive = true;

        if (slot.type == CommandType::Sampler
            || slot.type == CommandType::WavetableSynth
            || slot.type == CommandType::DrumSynth
            || isAudioEffectType(slot.type))
            hasAudioPath = true;
    }

    stack.audioProcessingActive = hasAudioPath;
    stack.samplerProcessingActive = samplerActive;
    stack.wavetableProcessingActive = wavetableActive;
    stack.drumProcessingActive = drumActive;
    stack.filterProcessingActive = filterActive;
    unsigned int destinations = 0;
    for (const auto& slot : stack.slots)
    {
        if (slot.type == CommandType::WavetableSynth) destinations |= 1u;
        if (slot.type == CommandType::FilterFx) destinations |= 2u;
        if (slot.type == CommandType::Sampler) destinations |= 4u;
        if (slot.type == CommandType::DelayFx) destinations |= 8u;
    }
    if (stack.modulationMatrix != nullptr)
        stack.modulationMatrix->setInstalledDestinations(destinations);
}

void TrackerMainProcessor::refreshAllStackProcessingStates()
{
    for (auto& stack : machineStacks)
        refreshStackProcessingState(stack);
}

bool TrackerMainProcessor::isStackAssigned(std::size_t stackIndex)
{
    auto* playbackSequencer = getPlaybackSequencerInternal();
    if (playbackSequencer == nullptr)
        return false;

    for (std::size_t seq = 0; seq < playbackSequencer->howManySequences(); ++seq)
    {
        const auto* sequence = playbackSequencer->getSequence(seq);
        if (sequence == nullptr)
            continue;

        if (sequence->isPlaybackMuted())
            continue;

        if (static_cast<std::size_t>(sequence->getMachineId()) == stackIndex)
            return true;
    }

    return false;
}

// Real-time path: dispatch on the type table instead of dynamic_cast.
AudioEffectMachine* TrackerMainProcessor::getAudioEffectForStackType(MachineStack& stack, CommandType type)
{
    if (!isAudioEffectType(type))
        return nullptr;
    return static_cast<AudioEffectMachine*> (getMachineForStackType(stack, type));
}

const AudioEffectMachine* TrackerMainProcessor::getAudioEffectForStackType(const MachineStack& stack, CommandType type) const
{
    if (!isAudioEffectType(type))
        return nullptr;
    return static_cast<const AudioEffectMachine*> (getMachineForStackType(stack, type));
}

std::unique_ptr<Sequencer> TrackerMainProcessor::createDefaultSequenceSet(std::size_t sequenceCount, std::size_t sequenceLength)
{
    sequenceCount = std::max<std::size_t>(1, std::min<std::size_t>(Sequencer::maxSequences, sequenceCount));
    sequenceLength = std::max<std::size_t>(1, sequenceLength);
    auto sequencer = std::make_unique<Sequencer>(sequenceCount, sequenceLength);
    sequencer->stop();
    for (std::size_t seqIndex = 0; seqIndex < sequencer->howManySequences(); ++seqIndex)
        if (auto* sequence = sequencer->getSequence(seqIndex))
            sequence->setMachineId(static_cast<double>(seqIndex));
    sequencer->requestStrUpdate();
    return sequencer;
}

void TrackerMainProcessor::resetSongState()
{
    sequenceSets.clear();
    sequenceSets.push_back(createDefaultSequenceSet());
    sequenceSetBpms.assign(1, 120.0);
    setBPM(120.0);
    sequenceSetMachineEnableStates.clear();
    songRows.clear();
    songRows.push_back({ 0, 16 });
    songPlayMode = SongPlayMode::sequence;
    viewedSequenceSetIndex = 0;
    activePlaybackSequenceSetIndex = 0;
    pendingPlaybackSequenceSetIndex.reset();
    selectedSongRow = 0;
    currentSongRow = 0;
    currentSongRowBeatCounter = 0;
    pendingTransportQuarterBeatReset = false;
    pendingTransportStartOnQuarterBeat = false;
    quarterBeatTicksAccumulator = 0;
    resetClockTicks();
    setCurrentQuarterBeat(0);
}

TrackerMainProcessor::SequenceSetMachineEnableState TrackerMainProcessor::captureMachineEnableState() const
{
    SequenceSetMachineEnableState state;
    state.stacks.resize(machineStacks.size());
    state.modulationEnabled.resize(machineStacks.size(), false);
    for (std::size_t stackIndex = 0; stackIndex < machineStacks.size(); ++stackIndex)
    {
        const auto& stack = machineStacks[stackIndex];
        state.modulationEnabled[stackIndex] = stack.modulationEnabled;
        auto& storedSlots = state.stacks[stackIndex];
        storedSlots.reserve(stack.slots.size());
        for (const auto& slot : stack.slots)
            storedSlots.push_back({ slot.id, slot.type, slot.enabled });
    }
    return state;
}

void TrackerMainProcessor::captureMachineEnableStateForSequenceSet(std::size_t setIndex)
{
    if (setIndex >= sequenceSets.size())
        return;
    if (sequenceSetMachineEnableStates.size() < sequenceSets.size())
        sequenceSetMachineEnableStates.resize(sequenceSets.size());
    sequenceSetMachineEnableStates[setIndex] = captureMachineEnableState();
}

void TrackerMainProcessor::normaliseMachineEnableState(SequenceSetMachineEnableState& state) const
{
    auto previous = std::move(state.stacks);
    auto previousModulation = std::move(state.modulationEnabled);
    state.stacks.clear();
    state.stacks.resize(machineStacks.size());
    state.modulationEnabled.resize(machineStacks.size(), false);
    for (std::size_t stackIndex = 0; stackIndex < machineStacks.size(); ++stackIndex)
    {
        const auto& stack = machineStacks[stackIndex];
        state.modulationEnabled[stackIndex] = stackIndex < previousModulation.size()
            ? previousModulation[stackIndex] : false;
        const std::vector<SequenceSetMachineEnableState::Slot>* previousSlots =
            stackIndex < previous.size() ? &previous[stackIndex] : nullptr;
        auto& normalisedSlots = state.stacks[stackIndex];
        normalisedSlots.reserve(stack.slots.size());
        for (const auto& slot : stack.slots)
        {
            bool enabled = slot.enabled;
            if (previousSlots != nullptr)
            {
                const auto found = std::find_if(previousSlots->begin(), previousSlots->end(),
                    [&slot](const SequenceSetMachineEnableState::Slot& savedSlot)
                    {
                        return savedSlot.id == slot.id && savedSlot.type == slot.type;
                    });
                if (found != previousSlots->end())
                    enabled = found->enabled;
            }
            normalisedSlots.push_back({ slot.id, slot.type, enabled });
        }
    }
}

void TrackerMainProcessor::normaliseAllMachineEnableStates()
{
    sequenceSetMachineEnableStates.resize(sequenceSets.size());
    for (auto& state : sequenceSetMachineEnableStates)
        normaliseMachineEnableState(state);
}

void TrackerMainProcessor::applyMachineEnableStateForSequenceSet(std::size_t setIndex)
{
    if (setIndex >= sequenceSetMachineEnableStates.size())
        return;

    const auto& state = sequenceSetMachineEnableStates[setIndex];
    for (std::size_t stackIndex = 0; stackIndex < machineStacks.size(); ++stackIndex)
    {
        auto& stack = machineStacks[stackIndex];
        stack.modulationEnabled = stackIndex < state.modulationEnabled.size()
            ? state.modulationEnabled[stackIndex] : false;
        if (stackIndex < state.stacks.size())
        {
            const auto& savedSlots = state.stacks[stackIndex];
            for (auto& slot : stack.slots)
            {
                const auto found = std::find_if(savedSlots.begin(), savedSlots.end(),
                    [&slot](const SequenceSetMachineEnableState::Slot& savedSlot)
                    {
                        return savedSlot.id == slot.id && savedSlot.type == slot.type;
                    });
                if (found != savedSlots.end())
                    slot.enabled = found->enabled;
            }
        }
        refreshStackProcessingState(stack);
    }
    updateClockedMachineActivity();
}

bool TrackerMainProcessor::isSongPlaybackRunning() const
{
    const auto* playbackSequencer = getPlaybackSequencerInternal();
    return songPlayMode == SongPlayMode::song
        && playbackSequencer != nullptr
        && playbackSequencer->isPlaying();
}

std::size_t TrackerMainProcessor::getMachineEnableStateEditingSetIndex() const
{
    return isSongPlaybackRunning() ? activePlaybackSequenceSetIndex : viewedSequenceSetIndex;
}

Sequencer* TrackerMainProcessor::getViewedSequencerInternal()
{
    if (sequenceSets.empty())
        return nullptr;
    viewedSequenceSetIndex = std::min(viewedSequenceSetIndex, sequenceSets.size() - 1);
    return sequenceSets[viewedSequenceSetIndex].get();
}

const Sequencer* TrackerMainProcessor::getViewedSequencerInternal() const
{
    if (sequenceSets.empty())
        return nullptr;
    const auto safeIndex = std::min(viewedSequenceSetIndex, sequenceSets.size() - 1);
    return sequenceSets[safeIndex].get();
}

Sequencer* TrackerMainProcessor::getPlaybackSequencerInternal()
{
    if (sequenceSets.empty())
        return nullptr;
    activePlaybackSequenceSetIndex = std::min(activePlaybackSequenceSetIndex, sequenceSets.size() - 1);
    return sequenceSets[activePlaybackSequenceSetIndex].get();
}

const Sequencer* TrackerMainProcessor::getPlaybackSequencerInternal() const
{
    if (sequenceSets.empty())
        return nullptr;
    const auto safeIndex = std::min(activePlaybackSequenceSetIndex, sequenceSets.size() - 1);
    return sequenceSets[safeIndex].get();
}

void TrackerMainProcessor::bindViewedSequenceSetToEditor()
{
    if (auto* viewedSequencer = getViewedSequencerInternal())
    {
        seqEditor.setSequencer(viewedSequencer);
        trackerController = TrackerController{viewedSequencer, this, &seqEditor};

        const auto maxSeq = juce::jmax<int>(0, static_cast<int>(viewedSequencer->howManySequences()) - 1);
        seqEditor.setCurrentSequence(juce::jlimit(0, maxSeq, static_cast<int>(seqEditor.getCurrentSequence())));
        const auto maxStep = juce::jmax<int>(0, static_cast<int>(viewedSequencer->howManySteps(seqEditor.getCurrentSequence())) - 1);
        seqEditor.setCurrentStep(juce::jlimit(0, maxStep, static_cast<int>(seqEditor.getCurrentStep())));
        viewedSequencer->requestStrUpdate();
    }
}

void TrackerMainProcessor::schedulePlaybackSequenceSetSwitch(std::size_t index)
{
    if (sequenceSets.empty())
        return;
    const auto safeIndex = std::min(index, sequenceSets.size() - 1);
    pendingPlaybackSequenceSetIndex = safeIndex;
    primeSequenceSetForTransportStart(safeIndex);
}

void TrackerMainProcessor::primeSequenceSetForTransportStart(std::size_t index)
{
    if (sequenceSets.empty())
        return;
    const auto safeIndex = std::min(index, sequenceSets.size() - 1);
    if (safeIndex == activePlaybackSequenceSetIndex)
        return;
    if (auto* pendingSequencer = sequenceSets[safeIndex].get())
    {
        pendingSequencer->stop();
        pendingSequencer->resetForTransportStart();
    }
}

void TrackerMainProcessor::switchPlaybackSequenceSetImmediately(std::size_t index, bool rewindNow)
{
    if (sequenceSets.empty())
        return;

    auto* previousPlaybackSequencer = getPlaybackSequencerInternal();
    const bool wasPlaying = previousPlaybackSequencer != nullptr && previousPlaybackSequencer->isPlaying();
    const auto targetIndex = std::min(index, sequenceSets.size() - 1);
    if (targetIndex < sequenceSetBpms.size())
        setBPM(sequenceSetBpms[targetIndex]);
    const bool sameSequenceSet = previousPlaybackSequencer != nullptr && targetIndex == activePlaybackSequenceSetIndex;
    if (sameSequenceSet)
    {
        pendingPlaybackSequenceSetIndex.reset();
        applyMachineEnableStateForSequenceSet(targetIndex);
        if (songPlayMode == SongPlayMode::sequence)
            setViewedSequenceSetIndex(activePlaybackSequenceSetIndex);

        if (rewindNow && previousPlaybackSequencer != nullptr)
        {
            previousPlaybackSequencer->resetForTransportStart();
        }
        return;
    }

    activePlaybackSequenceSetIndex = targetIndex;
    pendingPlaybackSequenceSetIndex.reset();
    applyMachineEnableStateForSequenceSet(targetIndex);
    if (previousPlaybackSequencer != nullptr)
        previousPlaybackSequencer->stop();
    if (songPlayMode == SongPlayMode::sequence)
        setViewedSequenceSetIndex(activePlaybackSequenceSetIndex);

    if (auto* playbackSequencer = getPlaybackSequencerInternal())
    {
        if (rewindNow)
            playbackSequencer->resetForTransportStart();
        if (wasPlaying)
            playbackSequencer->play();
    }
}

void TrackerMainProcessor::applyPendingSequenceSetSwitchForCurrentQuarterBeat()
{
    if (!pendingPlaybackSequenceSetIndex.has_value() || ((getCurrentQuarterBeat() - 1) % 4) != 0)
        return;

    switchPlaybackSequenceSetImmediately(*pendingPlaybackSequenceSetIndex, true);
}

bool TrackerMainProcessor::isPlaybackSequencerAtBoundary() const
{
    auto* playbackSequencer = getPlaybackSequencerInternal();
    if (playbackSequencer == nullptr)
        return false;

    if (playbackSequencer->howManySequences() == 0)
        return false;

    for (std::size_t seqIndex = 0; seqIndex < playbackSequencer->howManySequences(); ++seqIndex)
    {
        if (playbackSequencer->getCurrentStep(seqIndex) != 0)
            return false;
    }
    return true;
}

void TrackerMainProcessor::handleSongBeatBoundary()
{
    auto* playbackSequencer = getPlaybackSequencerInternal();
    if (playbackSequencer == nullptr || !playbackSequencer->isPlaying() || songRows.empty())
        return;

    if (songPlayMode == SongPlayMode::sequence)
        return;

    if (currentSongRow >= songRows.size())
        currentSongRow = 0;

    if (currentSongRowBeatCounter <= 0)
    {
        currentSongRowBeatCounter = juce::jmax(1, songRows[currentSongRow].beatCount);
        primeSequenceSetForTransportStart(songRows[(currentSongRow + 1) % songRows.size()].sequenceSetId);
    }

    if (currentSongRowBeatCounter <= 0)
        return;

    --currentSongRowBeatCounter;

    if (currentSongRowBeatCounter > 0)
    {
        if (currentSongRowBeatCounter == 1 && songRows.size() > 1)
            primeSequenceSetForTransportStart(songRows[(currentSongRow + 1) % songRows.size()].sequenceSetId);
        return;
    }

    currentSongRow = (currentSongRow + 1) % songRows.size();
    selectedSongRow = currentSongRow;
    currentSongRowBeatCounter = juce::jmax(1, songRows[currentSongRow].beatCount);
    schedulePlaybackSequenceSetSwitch(songRows[currentSongRow].sequenceSetId);
    applyPendingSequenceSetSwitchForCurrentQuarterBeat();

    if (songRows.size() > 1)
        primeSequenceSetForTransportStart(songRows[(currentSongRow + 1) % songRows.size()].sequenceSetId);
}

bool TrackerMainProcessor::stackContainsType(std::size_t stackIndex, CommandType machineType) const
{
    if (const auto* stack = getMachineStack(stackIndex))
        for (const auto& slot : stack->slots)
            if (slot.type == machineType && slot.enabled)
                return true;
    return false;
}

std::optional<std::size_t> TrackerMainProcessor::findMachineInStack(std::size_t stackIndex, CommandType type) const
{
    if (const auto* stack = getMachineStack(stackIndex))
    {
        for (std::size_t i = 0; i < stack->slots.size(); ++i)
            if (stack->slots[i].type == type)
                return i;
    }
    return std::nullopt;
}

void TrackerMainProcessor::allNotesOffForStack(std::size_t stackIndex)
{
    if (auto* stack = getMachineStack(stackIndex))
    {
        stack->forEachMachine([] (MachineInterface& machine)
        {
            machine.allNotesOff();
        });
        samplerEventsToSend.push_back({ stackIndex, MidiMessage::allNotesOff(1), elapsedSamples });
        midiToSend.addEvent(MidiMessage::allNotesOff(getStackMidiOutputChannel(stackIndex)), elapsedSamples);
    }
}

void TrackerMainProcessor::enqueueStackSamplerMidi(std::size_t stackIndex,
                                                   unsigned short outNote,
                                                   unsigned short outVelocity,
                                                   double outDurTicks, bool gateAtDuration)
{
    const int offsetSamples = juce::jmax(1, static_cast<int>(std::lround(samplesPerTick * outDurTicks))) % maxHorizon;
    const int fadeSamples = juce::jmax(1, static_cast<int>(std::lround(getSampleRate() * 0.003)));
    const int offSample = elapsedSamples + juce::jmax(1, offsetSamples - fadeSamples);
    samplerEventsToSend.push_back({ stackIndex, MidiMessage::noteOn(1, static_cast<int>(outNote), static_cast<uint8>(outVelocity)), elapsedSamples });
    if (gateAtDuration)
        samplerEventsToSend.push_back({ stackIndex, MidiMessage::noteOff(1, static_cast<int>(outNote), static_cast<uint8>(outVelocity)), offSample });
}

void TrackerMainProcessor::enqueueStackNote(NoteEventQueue& queue,
                                             std::size_t stackIndex,
                                             unsigned short note,
                                             unsigned short velocity,
                                             double durInTicks,
                                             bool isNoteOff)
{
    queue.pending.push_back({ stackIndex, note, velocity, durInTicks, isNoteOff, elapsedSamples });
}

void TrackerMainProcessor::distributeNoteEvents(NoteEventQueue& queue,
                                                int blockStartSample,
                                                int blockEndSample,
                                                std::vector<MachineScheduledNote> MachineStack::* blockNotesMember)
{
    queue.scratch.clear();
    if (queue.scratch.capacity() < queue.pending.size())
        queue.scratch.reserve(queue.pending.size());
    for (const auto& event : queue.pending)
    {
        bool inThisBlock = false;
        int sampleOffset = 0;
        if (blockEndSample < blockStartSample)
        {
            if (event.samplePosition >= blockStartSample || event.samplePosition < blockEndSample)
            {
                inThisBlock = true;
                sampleOffset = event.samplePosition - blockStartSample;
            }
        }
        else if (event.samplePosition >= blockStartSample && event.samplePosition < blockEndSample)
        {
            inThisBlock = true;
            sampleOffset = event.samplePosition - blockStartSample;
        }

        if (inThisBlock)
        {
            const std::size_t safeStackIndex = event.stackIndex < machineStacks.size() ? event.stackIndex : 0u;
            (machineStacks[safeStackIndex].*blockNotesMember).push_back(
                { sampleOffset, event.note, event.velocity, event.durationTicks, event.isNoteOff });
        }
        else
        {
            queue.scratch.push_back(event);
        }
    }
    queue.pending.swap(queue.scratch);

    // the machines walk these with a cursor, so keep them sorted by sample offset
    for (auto& stack : machineStacks)
    {
        auto& notes = stack.*blockNotesMember;
        // Stable insertion sort needs no scratch allocation and is linear
        // for the normally already ordered event stream.
        for (std::size_t i = 1; i < notes.size(); ++i)
        {
            const auto note = notes[i];
            std::size_t position = i;
            while (position > 0 && note.sampleOffset < notes[position - 1].sampleOffset)
            {
                notes[position] = notes[position - 1];
                --position;
            }
            notes[position] = note;
        }
    }
}

void TrackerMainProcessor::dispatchNoteThroughStack(std::size_t stackIndex,
                                                    unsigned short note,
                                                    unsigned short velocity,
                                                    double durInTicks,
                                                    std::size_t startSlotIndex,
                                                    bool held,
                                                    bool gateSampler)
{
    auto* stack = getMachineStack(stackIndex);
    if (stack == nullptr || !isStackAudible(stackIndex))
        return;

    // The modulation block observes every note which reaches this stack,
    // independent of the terminal selected by the serial chain.
    enqueueStackNote(modulationEvents, stackIndex, note, velocity, durInTicks);

    bool anyTerminalTriggered = false;
    bool hasExplicitTerminalRoute = false;

    for (std::size_t slotIndex = startSlotIndex; slotIndex < stack->slots.size(); ++slotIndex)
    {
        const auto& slot = stack->slots[slotIndex];
        const auto slotType = slot.type;

        if (slotType == CommandType::MidiNote
            || slotType == CommandType::Sampler
            || slotType == CommandType::WavetableSynth
            || slotType == CommandType::DrumSynth)
        {
            hasExplicitTerminalRoute = true;
        }

        if (!slot.enabled)
            continue;

        switch (slotType)
        {
            case CommandType::MidiNote:
                if (held)
                    midiToSend.addEvent(MidiMessage::noteOn(getStackMidiOutputChannel(stackIndex), static_cast<int>(note), static_cast<uint8>(velocity)), elapsedSamples);
                else
                    enqueueMachineMidi(midiToSend,
                                       static_cast<unsigned short>(getStackMidiOutputChannel(stackIndex)),
                                       note,
                                       velocity,
                                       durInTicks);
                anyTerminalTriggered = true;
                break;
            case CommandType::Sampler:
                if (held)
                    samplerEventsToSend.push_back({ stackIndex, MidiMessage::noteOn(1, static_cast<int>(note), static_cast<uint8>(velocity)), elapsedSamples });
                else
                    enqueueStackSamplerMidi(stackIndex, note, velocity, durInTicks, gateSampler);
                anyTerminalTriggered = true;
                break;
            case CommandType::WavetableSynth:
                // Notes are queued and applied sample-accurately in the synth's processBlock.
                enqueueStackNote(wavetableEvents, stackIndex, note, velocity, held ? 0 : durInTicks);
                anyTerminalTriggered = true;
                break;
            case CommandType::DrumSynth:
                enqueueStackNote(drumEvents, stackIndex, note, velocity, held ? 0 : durInTicks);
                anyTerminalTriggered = true;
                break;
            case CommandType::DistortionFx:
            case CommandType::DelayFx:
            case CommandType::ChannelStripFx:
            case CommandType::AuxSend1Fx:
            case CommandType::AuxSend2Fx:
            case CommandType::FilterFx:
                // The filter retriggers its cutoff envelope from any note that
                // reaches it; the actual audio is processed on the audio path.
                enqueueStackNote(filterEvents, stackIndex, note, velocity, durInTicks);
                break;
            case CommandType::Log:
            case CommandType::ToggleAux1:
            case CommandType::ToggleAux2:
            case CommandType::FilterCutoff:
            case CommandType::ModulationMatrix:
                break;
        }
    }

    if (!anyTerminalTriggered && !hasExplicitTerminalRoute)
    {
        if (held)
            midiToSend.addEvent(MidiMessage::noteOn(getStackMidiOutputChannel(stackIndex), static_cast<int>(note), static_cast<uint8>(velocity)), elapsedSamples);
        else
            enqueueMachineMidi(midiToSend,
                               static_cast<unsigned short>(getStackMidiOutputChannel(stackIndex)),
                               note,
                               velocity,
                               durInTicks);
    }
}

void TrackerMainProcessor::configureClockListeners()
{
    removeClockListeners();
    for (std::size_t i = 0; i < machineStacks.size(); ++i)
    {
        auto* stack = getMachineStack(i);
        if (stack == nullptr)
            continue;

        if (stack->delayFx != nullptr)
            ClockAbs::addListener(*stack->delayFx);
    }
}

void TrackerMainProcessor::removeClockListeners()
{
    clearListeners();
}

void TrackerMainProcessor::updateClockedMachineActivity()
{
    // Sequence-owned read heads require no machine clock listeners.
}

bool TrackerMainProcessor::emitQuarterBeatTickIfNeeded()
{
    const int nextQuarterBeat = getCurrentQuarterBeat() <= 0
        ? 1
        : ((getCurrentQuarterBeat() % 16) + 1);
    const bool isBeatStart = ((nextQuarterBeat - 1) % 4) == 0;
    const bool isQuarterNoteStart = ((nextQuarterBeat - 1) % 8) == 0;
    const bool shouldResetOnThisBoundary = pendingTransportQuarterBeatReset && isBeatStart;

    setCurrentQuarterBeat(nextQuarterBeat);
    if (shouldResetOnThisBoundary)
    {
        resetClockTicks();
        applyPendingSequenceSetSwitchForCurrentQuarterBeat();
        if (auto* playbackSequencer = getPlaybackSequencerInternal())
            playbackSequencer->resetForTransportStart();
        notifyClockReset();
        for (auto& stack : machineStacks)
            if (stack.modulationMatrix != nullptr) stack.modulationMatrix->resetClockDividers();
        pendingTransportQuarterBeatReset = false;
        if (pendingTransportStartOnQuarterBeat)
        {
            if (auto* playbackSequencer = getPlaybackSequencerInternal())
                playbackSequencer->play();
            pendingTransportStartOnQuarterBeat = false;
        }
    }
    else if (((getCurrentQuarterBeat() - 1) % 4) == 0)
    {
        handleSongBeatBoundary();
        applyPendingSequenceSetSwitchForCurrentQuarterBeat();
    }

    updateClockedMachineActivity();
    notifyClockTick(getCurrentQuarterBeat(), isQuarterNoteStart);
    return isQuarterNoteStart;
}

void TrackerMainProcessor::emitClockedMachineEvent(std::size_t stackIndex, CommandType machineType, const MachineNoteEvent& event)
{
    const auto slotIndex = findMachineInStack(stackIndex, machineType);
    if (!slotIndex.has_value())
        return;

    dispatchNoteThroughStack(stackIndex,
                             event.note,
                             event.velocity,
                             event.durationTicks,
                             *slotIndex + 1);
}

void TrackerMainProcessor::processPlaybackTickBoundary()
{
    advanceClockTick();
    if (modulationClockTickCount < modulationClockTicks.size())
        modulationClockTicks[modulationClockTickCount++] = { currentTickSampleOffset, getCurrentTick() };
    const bool isQuarterNoteBoundary = emitQuarterBeatTickIfNeeded();

    if (auto* playbackSequencer = getPlaybackSequencerInternal())
        playbackSequencer->tick(isQuarterNoteBoundary);
}

//==============================================================================
TrackerMainProcessor::TrackerMainProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
     : AudioProcessor (BusesProperties()
                     #if ! JucePlugin_IsMidiEffect
                      #if ! JucePlugin_IsSynth
                       .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                      #endif
                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                     #endif
                       ),
                       seqEditor{nullptr},
                       trackerController{nullptr, this, &seqEditor},
                       elapsedSamples{0}, maxHorizon{44100 * 3600},
                       samplesPerTick{44100/(120/60)/8}, bpm{120.0},
                       outstandingNoteOffs{0},
                       apvts(*this, nullptr, "params", createParameterLayout())
#endif
{
    
    CommandProcessor::assignMasterClock(this);
    CommandProcessor::assignMachineUtils(this);
    resetSongState();
    bindViewedSequenceSetToEditor();
    initialiseMachines();
    seqEditor.setMachineHost(this);
    seqEditor.setSongHost(this);
    seqEditor.setResetConfirmationHandler([this]()
    {
        recreateSequencersAndMachines();
    });
    seqEditor.setQuitConfirmationHandler([]()
    {
        if (auto* app = juce::JUCEApplicationBase::getInstance())
            app->systemRequestedQuit();
    });

    initialiseOsc();
    controlService = std::make_unique<TrackerControlService>(*this);
}

TrackerMainProcessor::~TrackerMainProcessor()
{
    stopMasterRecording();
    removeClockListeners();
    oscReceiver.removeListener(this);
    oscReceiver.disconnect();
}


//==============================================================================
const juce::String TrackerMainProcessor::getName() const
{
    return JucePlugin_Name;
}

bool TrackerMainProcessor::acceptsMidi() const
{
   #if JucePlugin_WantsMidiInput
    return true;
   #else
    return false;
   #endif
}

bool TrackerMainProcessor::producesMidi() const
{
   #if JucePlugin_ProducesMidiOutput
    return true;
   #else
    return false;
   #endif
}

bool TrackerMainProcessor::isMidiEffect() const
{
   #if JucePlugin_IsMidiEffect
    return true;
   #else
    return false;
   #endif
}

double TrackerMainProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int TrackerMainProcessor::getNumPrograms()
{
    return 1;   // NB: some hosts don't cope very well if you tell them there are 0 programs,
                // so this should be at least 1, even if you're not really implementing programs.
}

int TrackerMainProcessor::getCurrentProgram()
{
    return 0;
}

void TrackerMainProcessor::setCurrentProgram (int index)
{
    juce::ignoreUnused(index);
}

const juce::String TrackerMainProcessor::getProgramName (int index)
{
    juce::ignoreUnused(index);
    return {};
}

void TrackerMainProcessor::changeProgramName (int index, const juce::String& newName)
{
    juce::ignoreUnused(index, newName);
}

void TrackerMainProcessor::initialiseOsc()
{
    oscReceiver.addListener(this);

    oscReceiverReady = oscReceiver.connect(oscListenPort);
    if (!oscReceiverReady)
    {
        DBG("OSC receiver failed to listen on port " << oscListenPort);
    }
    else
    {
        DBG("OSC receiver listening on port " << oscListenPort);
    }

    oscSenderReady = oscSender.connect(lcdDisplayHost, lcdDisplayPort);
    if (!oscSenderReady)
    {
        DBG("OSC sender failed to connect to " << lcdDisplayHost << ":" << lcdDisplayPort);
    }
    else
    {
        DBG("OSC sender connected to " << lcdDisplayHost << ":" << lcdDisplayPort);
    }
}

void TrackerMainProcessor::initialiseMachines()
{
    removeClockListeners();
    machineStacks.clear();
    machineStacks.resize(kMachineStackCount);
    auxBus1.id = 1;
    auxBus2.id = 2;
    auxBus1.machine = std::make_unique<AuxReverbMachine>(juce::Reverb::Parameters{ 0.72f, 0.35f, 0.28f, 0.0f, 1.0f, 0.0f });
    auxBus2.machine = std::make_unique<AuxReverbMachine>(juce::Reverb::Parameters{ 0.42f, 0.55f, 0.22f, 0.0f, 0.75f, 0.0f });
    auxBus1.gainDb = auxBus2.gainDb = 0.0f;
    auxBus1.gainLinear = auxBus2.gainLinear = 1.0f;
    auxBus1.pan = auxBus2.pan = 0.0f;
    auxBus1.panCurrent = auxBus1.panTarget = auxBus2.panCurrent = auxBus2.panTarget = 0.0f;
    auxBus1.muted = auxBus2.muted = false;
    auxBus1.meterLevels.fill(0.0f);
    auxBus2.meterLevels.fill(0.0f);
    setMasterGainDb(0.0f);
    masterMuted = false;
    masterMeterLevels.fill(0.0f);
    auxBus1.meterLevels.fill(0.0f);
    auxBus2.meterLevels.fill(0.0f);
    for (auto& stack : machineStacks)
    {
        stack.sampler = std::make_unique<SuperSamplerProcessor>();
        stack.wavetableSynth = std::make_unique<WavetableSynthMachine>();
        stack.drumSynth = std::make_unique<DrumSynthMachine>();
        stack.distortionFx = std::make_unique<WaveshaperDistortionMachine>();
        stack.delayFx = std::make_unique<DelayFxMachine>();
        stack.channelStripFx = std::make_unique<ChannelStripMachine>();
        stack.filterFx = std::make_unique<FilterFxMachine>();
        stack.modulationMatrix = std::make_unique<ModulationMatrixMachine>();
        stack.modulationEnabled = false;
        // A fresh tracker should be immediately audible.  MIDI output remains
        // available as a stack type, but every new/reset stack starts with an
        // enabled internal synth instead of a silent external-MIDI route.
        stack.slots = { makeDefaultSlotState(CommandType::WavetableSynth) };
        stack.samplerMidiBuffer.clear();
        stack.wavetableBlockNotes.clear();
        stack.drumBlockNotes.clear();
        stack.filterBlockNotes.clear();
        stack.modulationBlockNotes.clear();
        stack.midiOutputChannel = 1;
        stack.audioProcessingActive = false;
        stack.samplerProcessingActive = false;
        stack.wavetableProcessingActive = false;
        stack.drumProcessingActive = false;
        stack.filterProcessingActive = false;
        stack.muted = false;
        stack.solo = false;
        stack.gainDb = 0.0f;
        stack.pan = stack.panCurrent = stack.panTarget = stack.panSmoothed = 0.0f;
        stack.meterLevel = 0.0f;
    }
    samplerEventsToSend.clear();
    scratchFutureSamplerEvents.clear();
    wavetableEvents.clear();
    drumEvents.clear();
    filterEvents.clear();
    modulationEvents.clear();

    const double secondsPerTick = getSecondsPerTickFromBpm(getBPM());
    for (auto& stack : machineStacks)
        stack.forEachMachine([&secondsPerTick] (MachineInterface& machine)
        {
            machine.setSecondsPerTick(secondsPerTick);
        });
    refreshAllStackProcessingStates();
    normaliseAllMachineEnableStates();
    applyMachineEnableStateForSequenceSet(viewedSequenceSetIndex);
    configureClockListeners();
    updateClockedMachineActivity();
}

void TrackerMainProcessor::recreateSequencersAndMachines()
{
    suspendProcessing(true);
    const double activeSampleRate = getSampleRate();
    const int activeBlockSize = getBlockSize();
    CommandProcessor::sendAllNotesOff();
    if (auto* playbackSequencer = getPlaybackSequencerInternal())
        playbackSequencer->stop();
    clearPendingEvents();
    resetSongState();
    bindViewedSequenceSetToEditor();
    seqEditor.resetCursor();
    seqEditor.gotoSongPage();
    seqEditor.setMachineHost(this);
    seqEditor.setSongHost(this);
    seqEditor.setResetConfirmationHandler([this]()
    {
        recreateSequencersAndMachines();
    });
    initialiseMachines();
    // Reset can run while the standalone device or plug-in host remains
    // prepared.  The freshly-created machines therefore need the same DSP
    // lifecycle call as the instances they replaced before audio resumes.
    // Without this, Delay owns no delay buffer and Filter owns unprepared JUCE
    // DSP storage until the host happens to call prepareToPlay again.
    if (activeSampleRate > 0.0 && activeBlockSize > 0)
        prepareToPlay(activeSampleRate, activeBlockSize);
    if (auto* viewedSequencer = getViewedSequencerInternal())
        viewedSequencer->requestStrUpdate();
    suspendProcessing(false);
}

juce::String TrackerMainProcessor::formatOscMessage(const juce::OSCMessage& message)
{
    juce::StringArray parts;
    parts.add(message.getAddressPattern().toString());

    juce::StringArray argStrings;
    for (auto arg : message)
    {
        if (arg.isInt32())
            argStrings.add(juce::String(arg.getInt32()));
        else if (arg.isFloat32())
            argStrings.add(juce::String(arg.getFloat32()));
        else if (arg.isString())
            argStrings.add(arg.getString());
        else if (arg.isBlob())
            argStrings.add("<blob>");
        else if (arg.isColour())
            argStrings.add("<colour>");
        else
            argStrings.add("<arg>");
    }

    if (!argStrings.isEmpty())
        parts.add("(" + argStrings.joinIntoString(", ") + ")");

    return parts.joinIntoString(" ");
}

void TrackerMainProcessor::oscMessageReceived(const juce::OSCMessage& message)
{
    if (!oscReceiverReady)
        return;
    DBG("OSC in: " << formatOscMessage(message));
    handleIncomingOscControlMessage(message);
}

void TrackerMainProcessor::oscBundleReceived(const juce::OSCBundle& bundle)
{
    for (auto element : bundle)
    {
        if (element.isMessage())
            oscMessageReceived(element.getMessage());
    }
}

juce::String TrackerMainProcessor::getCurrentCellOscPayload(bool refreshMachineCells)
{
    const auto mode = seqEditor.getEditMode();
    auto* viewedSequencer = getViewedSequencerInternal();
    if (viewedSequencer == nullptr)
        return {};

    switch (mode)
    {
        case SequencerEditorMode::arrangingSong:
        {
            const auto songRow = seqEditor.getCurrentSongRow();
            const auto songCol = seqEditor.getCurrentSongCol();
            if (songRow == 0)
                return juce::String(songCol == 0 ? "PLAY SONG" : "PLAY SEQ");
            const auto rowIndex = songRow - 1;
            if (rowIndex >= songRows.size())
                return "SONG";
            if (songCol == 0)
                return "SET " + juce::String(static_cast<int>(songRows[rowIndex].sequenceSetId + 1));
            if (songCol == 1)
                return "BEAT " + juce::String(songRows[rowIndex].beatCount);
            if (songCol == 2)
                return "BPM " + juce::String(getSongRowBpm(rowIndex));
            if (songCol == 4)
                return "DEL";
            return "EDIT";
        }
        case SequencerEditorMode::selectingSeqAndStep:
        {
            auto& grid = viewedSequencer->getSequenceAsGridOfStrings();
            const auto seq = seqEditor.getCurrentSequence();
            const auto step = seqEditor.getCurrentStep();
            if (seq < grid.size() && step < grid[seq].size())
                return juce::String(grid[seq][step]);
            break;
        }
        case SequencerEditorMode::editingStep:
        {
            const auto grid = viewedSequencer->getStepAsGridOfStrings(seqEditor.getCurrentSequence(),
                                                               seqEditor.getCurrentStep());
            const auto col = seqEditor.getCurrentStepCol();
            const auto row = seqEditor.getCurrentStepRow();
            if (col < grid.size() && row < grid[col].size())
                return juce::String(grid[col][row]);
            break;
        }
        case SequencerEditorMode::configuringSequence:
        {
            const auto grid = viewedSequencer->getSequenceConfigsAsGridOfStrings(seqEditor.getCurrentConfigHead());
            const auto seq = seqEditor.getCurrentSequence();
            const auto param = seqEditor.getCurrentSeqParam();
            if (seq < grid.size() && param < grid[seq].size())
                return juce::String(grid[seq][param]);
            break;
        }
        case SequencerEditorMode::machineConfig:
        {
            if (refreshMachineCells)
                seqEditor.refreshMachineStateForCurrentSequence();
            const auto& cells = seqEditor.getMachineCells();
            for (const auto& column : cells)
            {
                for (const auto& cell : column)
                {
                    if (cell.isSelected)
                        return juce::String(cell.text);
                }
            }
            break;
        }
        case SequencerEditorMode::mixer:
        {
            const auto stack = seqEditor.getSelectedMixerStack();
            const auto aux = seqEditor.getSelectedMixerAux();
            if (seqEditor.isMixerMasterSelected())
            {
                if (seqEditor.getMixerRow() == 2) return isMasterRecording() ? "RECORDING ON" : "RECORDING OFF";
                return "MASTER GAIN " + juce::String(getMasterGainDb(), 1);
            }
            if (aux)
            {
                if (seqEditor.getMixerRow() == 0) return "AUX " + juce::String(static_cast<int>(*aux + 1));
                if (seqEditor.getMixerRow() == 2) return isAuxReturnMuted(*aux) ? "AUX MUTE ON" : "AUX MUTE OFF";
                return "AUX GAIN " + juce::String(getAuxReturnGainDb(*aux), 1);
            }
            if (seqEditor.getMixerRow() == 1) return isStackMuted(*stack) ? "MUTE ON" : "MUTE OFF";
            if (seqEditor.getMixerRow() == 2) return isStackSolo(*stack) ? "SOLO ON" : "SOLO OFF";
            return "GAIN " + juce::String(getStackGainDb(*stack), 1);
        }
        case SequencerEditorMode::audioConfig:
        {
            const auto& cells = seqEditor.getAudioConfigCells();
            for (const auto& column : cells)
                for (const auto& cell : column)
                    if (cell.isSelected) return juce::String(cell.text);
            break;
        }
        case SequencerEditorMode::help:
            return "HELP";
        case SequencerEditorMode::resetConfirmation:
            return "RESET?";
    }

    return {};
}

void TrackerMainProcessor::sendCurrentCellValueOverOscIfChanged(bool refreshMachineCells)
{
    if (!oscSenderReady)
        return;

    const auto payload = withAudioThreadExclusive([&]()
    {
        return getCurrentCellOscPayload(refreshMachineCells).trim();
    });
    if (payload.isEmpty() || payload == lastSentCellOscPayload)
        return;

    if (!oscSender.send(cellValueAddress, payload))
        DBG("OSC send failed for " << juce::String(cellValueAddress) << " " << payload);
    else
        lastSentCellOscPayload = payload;
}

void TrackerMainProcessor::handleIncomingOscControlMessage(const juce::OSCMessage& message)
{
    const auto address = message.getAddressPattern().toString();

    if (address == zoomInAddress || address == zoomOutAddress)
    {
        if (message.size() < 2 || !message[0].isFloat32() || !message[1].isFloat32())
            return;

        const float normalizedX = juce::jlimit(0.0f, 1.0f, message[0].getFloat32());
        const float normalizedY = juce::jlimit(0.0f, 1.0f, message[1].getFloat32());
        const float delta = (address == zoomInAddress) ? 0.1f : -0.1f;
        queueZoomCommand(delta, normalizedX, normalizedY);
        return;
    }

    if (address == incrementAddress || address == decrementAddress)
    {
        if (message.size() < 1 || !message[0].isInt32())
            return;

        const int steps = juce::jlimit(0, 1024, message[0].getInt32());
        if (steps <= 0)
            return;

        withAudioThreadExclusive([&]()
        {
            for (int i = 0; i < steps; ++i)
            {
                if (address == incrementAddress)
                    seqEditor.incrementAtCursor();
                else
                    seqEditor.decrementAtCursor();
            }
            if (auto* viewedSequencer = getViewedSequencerInternal())
                viewedSequencer->requestStrUpdate();
        });
        return;
    }
}

void TrackerMainProcessor::queueZoomCommand(float delta, float normalizedX, float normalizedY)
{
    std::lock_guard<std::mutex> lock(pendingZoomMutex);
    pendingZoomCommands.push_back(PendingZoomCommand { delta, normalizedX, normalizedY });
}

std::vector<TrackerMainProcessor::PendingZoomCommand> TrackerMainProcessor::consumePendingZoomCommands()
{
    std::lock_guard<std::mutex> lock(pendingZoomMutex);
    std::vector<PendingZoomCommand> commands;
    commands.reserve(pendingZoomCommands.size());

    while (!pendingZoomCommands.empty())
    {
        commands.push_back(pendingZoomCommands.front());
        pendingZoomCommands.pop_front();
    }

    return commands;
}

juce::String TrackerMainProcessor::requestUiScreenshot(std::shared_ptr<UiScreenshotRequest> request)
{
    if (request == nullptr || request->completed == nullptr || request->image == nullptr || request->error == nullptr)
    {
        if (request != nullptr)
        {
            if (request->error != nullptr) *request->error = "screenshot_failed";
            if (request->completed != nullptr) request->completed->signal();
        }
        return "screenshot_failed";
    }

    auto* editor = getActiveEditor();
    auto* ui = dynamic_cast<TrackerMainUI*>(editor);
    if (ui == nullptr)
    {
        *request->error = "ui_unavailable";
        request->completed->signal();
        return "ui_unavailable";
    }

    return ui->requestScreenshot(std::move(request));
}

void TrackerMainProcessor::copyStackScope(std::size_t stackIndex, std::vector<float>& out)
{
    withAudioThreadExclusive([this, &out, stackIndex]()
    {
        const auto* stack = getMachineStack(stackIndex);
        if (stack == nullptr || stack->scopeRing.empty())
        {
            std::fill(out.begin(), out.end(), 0.0f);
            return;
        }
        const std::size_t ringSize = stack->scopeRing.size();
        out.resize(ringSize);
        // scopeHead points at the oldest sample, so read forward for time order.
        for (std::size_t i = 0; i < ringSize; ++i)
            out[i] = stack->scopeRing[(stack->scopeHead + i) % ringSize];
    });
}

void TrackerMainProcessor::copyStackScopes(const std::vector<std::size_t>& stackIndices,
                                           std::vector<std::vector<float>>& out)
{
    // This is message-thread UI work. Allocate before taking the exclusive
    // guard, keeping the section that can delay audio bounded to plain copies.
    out.resize(stackIndices.size());
    for (auto& samples : out)
        samples.resize(kScopeRingSampleCount, 0.0f);

    withAudioThreadExclusive([this, &stackIndices, &out]()
    {
        for (std::size_t index = 0; index < stackIndices.size(); ++index)
        {
            auto& destination = out[index];
            const auto* stack = getMachineStack(stackIndices[index]);
            if (stack == nullptr || stack->scopeRing.empty())
            {
                std::fill(destination.begin(), destination.end(), 0.0f);
                continue;
            }

            const std::size_t ringSize = stack->scopeRing.size();
            std::fill(destination.begin(), destination.end(), 0.0f);
            const std::size_t samplesToCopy = std::min(destination.size(), ringSize);
            for (std::size_t sample = 0; sample < samplesToCopy; ++sample)
                destination[sample] = stack->scopeRing[(stack->scopeHead + sample) % ringSize];
        }
    });
}

//==============================================================================
void TrackerMainProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    seqEditor.setHostAudioFormat(sampleRate, samplesPerBlock);
    stopMasterRecording();
    const double activeSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
    preparedSampleRate = activeSampleRate;
    // A 5 ms one-pole slew removes gain discontinuities from square/S&H LFO
    // panning while remaining quick enough for ordinary cyclic modulation.
    panModulationSmoothing = 1.0f - std::exp(-1.0f / static_cast<float>(activeSampleRate * 0.005));
    const double activeBpm = getBPM();
    samplesPerTick = static_cast<unsigned int> (juce::jmax (1, static_cast<int> (std::lround (activeSampleRate * (60.0 / activeBpm) / 8.0))));
    const double secondsPerTick = getSecondsPerTickFromBpm(activeBpm);
    const int preparedBlockSize = juce::jmax(1, samplesPerBlock);
    // Reserve ordinary dense performance workloads before audio starts. These
    // queues remain growable so unusually large sessions never lose notes.
    constexpr std::size_t eventCapacity = 4096;
    for (auto* queue : { &wavetableEvents, &drumEvents, &filterEvents, &modulationEvents })
    {
        queue->pending.reserve(eventCapacity);
        queue->scratch.reserve(eventCapacity);
    }
    samplerEventsToSend.reserve(eventCapacity);
    scratchFutureSamplerEvents.reserve(eventCapacity);
    midiToSend.ensureSize(65536);
    futureMidi.ensureSize(65536);
    auxBus1.inputBuffer.setSize(2, preparedBlockSize);
    auxBus2.inputBuffer.setSize(2, preparedBlockSize);
    auxBus1.inputBuffer.clear();
    auxBus2.inputBuffer.clear();
    masterMeterLevels.fill(0.0f);
    auxBus1.meterLevels.fill(0.0f);
    auxBus2.meterLevels.fill(0.0f);
    if (auxBus1.machine != nullptr)
        auxBus1.machine->prepareToPlay(sampleRate, samplesPerBlock);
    if (auxBus2.machine != nullptr)
        auxBus2.machine->prepareToPlay(sampleRate, samplesPerBlock);

    for (auto& stack : machineStacks)
    {
        stack.renderBuffer.setSize(1, preparedBlockSize);
        stack.renderBuffer.clear();
        stack.delayTailBuffer.setSize(1, preparedBlockSize);
        stack.delayTailBuffer.clear();
        stack.samplerMidiBuffer.clear();
        stack.wavetableBlockNotes.clear();
        stack.drumBlockNotes.clear();
        stack.filterBlockNotes.clear();
        // Fixed-size scope ring for the UI oscilloscope; independent of the block
        stack.samplerMidiBuffer.ensureSize(16384);
        stack.wavetableBlockNotes.reserve(1024);
        stack.drumBlockNotes.reserve(1024);
        stack.filterBlockNotes.reserve(1024);
        stack.modulationBlockNotes.reserve(1024);
        // Scope history remains active regardless of the visible page.
        // size so it is allocated once and reused across sample-rate changes.
        if (stack.scopeRing.empty())
        {
            stack.scopeRing.assign(kScopeRingSampleCount, 0.0f);
            stack.scopeHead = 0;
        }
        // setSecondsPerTick is a no-op for machines that do not track tick time.
        stack.forEachMachine([&sampleRate, &samplesPerBlock, secondsPerTick] (MachineInterface& machine)
        {
            machine.prepareToPlay(sampleRate, samplesPerBlock);
            machine.setSecondsPerTick(secondsPerTick);
        });
    }
    scratchFutureSamplerEvents.clear();
    wavetableEvents.scratch.clear();
    drumEvents.scratch.clear();
    filterEvents.scratch.clear();
    emptyMidiBuffer.clear();
    updateClockedMachineActivity();
}

void TrackerMainProcessor::releaseResources()
{
    stopMasterRecording();
    // When playback stops, you can use this as an opportunity to free up any
    // spare memory, etc.
    if (auxBus1.machine != nullptr)
        auxBus1.machine->releaseResources();
    if (auxBus2.machine != nullptr)
        auxBus2.machine->releaseResources();
    auxBus1.inputBuffer.setSize(0, 0);
    auxBus2.inputBuffer.setSize(0, 0);
    for (auto& stack : machineStacks)
    {
        stack.renderBuffer.setSize(0, 0);
        stack.delayTailBuffer.setSize(0, 0);
        stack.samplerMidiBuffer.clear();
        stack.wavetableBlockNotes.clear();
        stack.drumBlockNotes.clear();
        stack.filterBlockNotes.clear();
        stack.forEachMachine([] (MachineInterface& machine)
        {
            machine.releaseResources();
        });
    }
    scratchFutureSamplerEvents.clear();
    wavetableEvents.scratch.clear();
    drumEvents.scratch.clear();
    filterEvents.scratch.clear();
    emptyMidiBuffer.clear();
    masterMeterLevels.fill(0.0f);
}

#ifndef JucePlugin_PreferredChannelConfigurations
bool TrackerMainProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
  #if JucePlugin_IsMidiEffect
    juce::ignoreUnused (layouts);
    return true;
  #else
    // This is the place where you check if the layout is supported.
    // In this template code we only support mono or stereo.
    // Some plugin hosts, such as certain GarageBand versions, will only
    // load plugins that support stereo bus layouts.
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
     && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    // This checks if the input layout matches the output layout
   #if ! JucePlugin_IsSynth
    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;
   #endif

    return true;
  #endif
}
#endif

void TrackerMainProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    PerformanceMetrics::Scope callbackTiming(performanceMetrics, PerformanceMetrics::Stage::callback);
    PerformanceMetrics::Scope sequencingTiming(performanceMetrics, PerformanceMetrics::Stage::sequencing);
    processing.store(true, std::memory_order_release);
    std::lock_guard<std::mutex> audioLock(audioMutex);
    juce::ScopedNoDenormals noDenormals;
    auto* playbackSequencer = getPlaybackSequencerInternal();
    bool receivedMidi = false; 
    for (const MidiMessageMetadata metadata : midiMessages){
        if (metadata.getMessage().isNoteOn()) {
            // DBG("Got a note " << metadata.getMessage().getNoteNumber());
            // add a note to the current sequence 
            // and move it on a step 

            // get the armed sequence 

            // seqEditor.enterStepData(metadata.getMessage().getNoteNumber(), Step::noteInd);
            receivedMidi = true; 
        }
    }
    if (receivedMidi) {
        // tell the 
    }

    emptyMidiBuffer.clear();

    const int blockSizeSamples = buffer.getNumSamples();
    modulationClockTickCount = 0;
    int blockStartSample = elapsedSamples;
    int blockEndSample = (elapsedSamples + blockSizeSamples) % maxHorizon;
    bool usingHostClock = false;
    const bool useInternalClock = internalClockEnabled.load(std::memory_order_relaxed);

    if (!useInternalClock)
    {
        #if JUCE_MAJOR_VERSION >= 5
        auto* hostPlayHead = getPlayHead();
        const auto position = hostPlayHead != nullptr ? hostPlayHead->getPosition() : juce::Optional<juce::AudioPlayHead::PositionInfo>();
        if (position.hasValue()
            && position->getBpm().hasValue()
            && position->getPpqPosition().hasValue()
            && *position->getBpm() > 0.0
            && *position->getPpqPosition() >= 0.0)
        {
            const double hostBpm = *position->getBpm();
            const double hostPpqPosition = *position->getPpqPosition();
            const bool hostPlaying = position->getIsPlaying();
            if (hostPlaying != hostWasPlaying)
            {
                pendingTransportQuarterBeatReset = hostPlaying;
                hostWasPlaying = hostPlaying;
            }

            if (!hostPlaying)
            {
                // Host transport stopped: stop the sequencer and skip internal clocking.
                if (playbackSequencer != nullptr)
                    playbackSequencer->stop();
                hostPpqValid = false;
                usingHostClock = true;
            }
            else
            {
                // Host sync: derive tick timing from PPQ so transport changes are handled in real time.
                usingHostClock = true;
                if (playbackSequencer != nullptr)
                    playbackSequencer->play();
                if (std::abs(hostBpm - getBPM()) > std::numeric_limits<double>::epsilon())
                    setBPM(hostBpm);

                const double ticksPerQuarter = 8.0;
                const double samplesPerTickDouble = getSampleRate() * (60.0 / hostBpm) / ticksPerQuarter;
                double tickPosition = hostPpqPosition * ticksPerQuarter;
                double tickPhase = std::fmod(tickPosition, 1.0);
                if (tickPhase < 0.0)
                    tickPhase += 1.0;

                if (!hostPpqValid || hostPpqPosition < lastHostPpqPosition)
                {
                    // Transport restarted or jumped; re-sync tick phase from the new PPQ position.
                    hostPpqValid = true;
                }
                lastHostPpqPosition = hostPpqPosition;

                const double phaseEpsilon = 1.0e-6;
                long long tickIndex = static_cast<long long>(std::floor(tickPosition));
                double sampleOffsetToNextTick = 0.0;
                if (tickPhase > phaseEpsilon)
                {
                    ++tickIndex;
                    sampleOffsetToNextTick = (1.0 - tickPhase) * samplesPerTickDouble;
                }

                for (double offset = sampleOffsetToNextTick; offset < blockSizeSamples; offset += samplesPerTickDouble, ++tickIndex)
                {
                    const int tickSampleOffset = static_cast<int>(offset);
                    elapsedSamples = (blockStartSample + tickSampleOffset) % maxHorizon;
                    currentTickSampleOffset = tickSampleOffset;
                    processPlaybackTickBoundary();
                }
                elapsedSamples = blockEndSample;
            }
        }
        #endif
    }
    else
    {
        hostPpqValid = false;
        hostWasPlaying = false;
    }

    if (!usingHostClock && useInternalClock)
    {
        hostPpqValid = false;
        hostWasPlaying = false;
        const int samplesPerTickInt = static_cast<int>(samplesPerTick);
        int samplesAdvanced = 0;
        while (samplesPerTickInt > 0)
        {
            const int remainder = elapsedSamples % samplesPerTickInt;
            const int samplesUntilNextTick = remainder == 0
                ? samplesPerTickInt
                : (samplesPerTickInt - remainder);

            if (samplesAdvanced + samplesUntilNextTick > blockSizeSamples)
                break;

            samplesAdvanced += samplesUntilNextTick;
            elapsedSamples = (elapsedSamples + samplesUntilNextTick) % maxHorizon;
            currentTickSampleOffset = samplesAdvanced;
            processPlaybackTickBoundary();
        }
        elapsedSamples = blockEndSample;
    }
    hostClockActive.store(usingHostClock, std::memory_order_relaxed);
    const bool sequencerPlaying = playbackSequencer != nullptr && playbackSequencer->isPlaying();
    if (sequencerWasPlaying && !sequencerPlaying)
        allNotesOff();
    sequencerWasPlaying = sequencerPlaying;
    // to get sample-accurate midi as opposed to block-accurate midi (!)
    // now add any midi that should have occurred within this block
    // to the outgoing midibuffer 
    // midiMessages , but with an offset value within this block
    
    futureMidi.clear();    // store messages from midiToSend from the future here.
    for (const MidiMessageMetadata metadata : midiToSend){
        if (blockEndSample < blockStartSample){// we wrapped block end back around 
            // DBG("processBlock wrapped events");

            // after block start or before block end (as block end is before block start due to wrap around)
            if ( metadata.samplePosition >= blockStartSample ||  
                metadata.samplePosition < blockEndSample) {
                midiMessages.addEvent(metadata.getMessage(),  metadata.samplePosition - blockStartSample);
            }
            else{// it is in the future            
                futureMidi.addEvent(metadata.getMessage(),  metadata.samplePosition);
            }
            if (metadata.getMessage().isNoteOff()) outstandingNoteOffs --;
        }
        if (blockStartSample < blockEndSample){
            // normal case where block start is before block end as no wrap has occurred. 
            if ( metadata.samplePosition >= blockStartSample && 
                metadata.samplePosition < blockEndSample) {
                if (metadata.getMessage().isNoteOn()){
                    // DBG("On Event delta: " << delta << "  blockStart " << blockStartSample << " at " << metadata.samplePosition - blockStartSample);
                    lastSendOnAt = metadata.samplePosition;
                }
                midiMessages.addEvent(metadata.getMessage(),  metadata.samplePosition - blockStartSample);
            }
            else{// it is in the future            

                futureMidi.addEvent(metadata.getMessage(),  metadata.samplePosition);
            }
            if (metadata.getMessage().isNoteOff()) outstandingNoteOffs --;

        }
    }
    // copy the future events back into midiToSend (keeping its stable capacity)
    // rather than swapping, so neither buffer is freed and reallocated each block
    midiToSend.clear();
    if (!futureMidi.isEmpty())
        midiToSend.addEvents(futureMidi, 0, -1, 0);
    futureMidi.clear();

    // now set up the midi messages that 
    // we want to send to the internal sampler engines
    scratchFutureSamplerEvents.clear();
    if (scratchFutureSamplerEvents.capacity() < samplerEventsToSend.size())
        scratchFutureSamplerEvents.reserve(samplerEventsToSend.size());
    for (auto& stack : machineStacks)
    {
        stack.samplerMidiBuffer.clear();
        stack.wavetableBlockNotes.clear();
        stack.drumBlockNotes.clear();
        stack.filterBlockNotes.clear();
        stack.modulationBlockNotes.clear();
    }

    for (const auto& event : samplerEventsToSend)
    {
        bool inThisBlock = false;
        int sampleOffset = 0;
        if (blockEndSample < blockStartSample)
        {
            if (event.samplePosition >= blockStartSample || event.samplePosition < blockEndSample)
            {
                inThisBlock = true;
                sampleOffset = event.samplePosition - blockStartSample;
            }
        }
        else if (event.samplePosition >= blockStartSample && event.samplePosition < blockEndSample)
        {
            inThisBlock = true;
            sampleOffset = event.samplePosition - blockStartSample;
        }

        if (inThisBlock)
        {
            const std::size_t safeStackIndex = event.stackIndex < machineStacks.size() ? event.stackIndex : 0u;
            machineStacks[safeStackIndex].samplerMidiBuffer.addEvent(event.message, sampleOffset);
        }
        else
        {
            scratchFutureSamplerEvents.push_back(event);
        }
    }
    samplerEventsToSend.swap(scratchFutureSamplerEvents);

    // now distribute the sequencer notes destined for the wavetable synths,
    // keeping only the ones that fall within this block (with in-block offsets)
    distributeNoteEvents(wavetableEvents, blockStartSample, blockEndSample, &MachineStack::wavetableBlockNotes);

    distributeNoteEvents(drumEvents, blockStartSample, blockEndSample, &MachineStack::drumBlockNotes);

    // now distribute the sequencer notes that retrigger filter cutoff sweeps
    distributeNoteEvents(filterEvents, blockStartSample, blockEndSample, &MachineStack::filterBlockNotes);
    distributeNoteEvents(modulationEvents, blockStartSample, blockEndSample, &MachineStack::modulationBlockNotes);

    sequencingTiming.stop();

    if (auxBus1.inputBuffer.getNumChannels() != 2 || auxBus1.inputBuffer.getNumSamples() != buffer.getNumSamples())
        auxBus1.inputBuffer.setSize(2, buffer.getNumSamples(), false, false, true);
    auxBus1.inputBuffer.clear();
    if (auxBus2.inputBuffer.getNumChannels() != 2 || auxBus2.inputBuffer.getNumSamples() != buffer.getNumSamples())
        auxBus2.inputBuffer.setSize(2, buffer.getNumSamples(), false, false, true);
    auxBus2.inputBuffer.clear();

    for (std::size_t i = 0; i < machineStacks.size(); ++i)
    {
        if (auto* stack = getMachineStack(i))
        {
            if (!stack->audioProcessingActive || !isStackAudible(i))
            {
                stack->meterLevel = 0.0f;
            }
            else
            {
            if (stack->renderBuffer.getNumChannels() != 1
                || stack->renderBuffer.getNumSamples() != buffer.getNumSamples())
            {
                stack->renderBuffer.setSize(1, buffer.getNumSamples(), false, false, true);
            }
            if (stack->delayTailBuffer.getNumChannels() != 1
                || stack->delayTailBuffer.getNumSamples() != buffer.getNumSamples())
            {
                stack->delayTailBuffer.setSize(1, buffer.getNumSamples(), false, false, true);
            }

            auto& stackBuffer = stack->renderBuffer;
            stackBuffer.clear();

            if (stack->modulationMatrix != nullptr)
            {
                PerformanceMetrics::Scope timing(performanceMetrics, PerformanceMetrics::Stage::modulation);
                stack->modulationMatrix->processModulationBlock(buffer.getNumSamples(), stack->modulationEnabled,
                    sequencerPlaying, modulationClockTicks.data(), modulationClockTickCount, stack->modulationBlockNotes);
                const auto* modulation = &stack->modulationMatrix->getBlockView();
                stack->sampler->setModulationBlock(modulation);
                stack->wavetableSynth->setModulationBlock(modulation);
                stack->filterFx->setModulationBlock(modulation);
                stack->delayFx->setModulationBlock(modulation);
            }

            if (stack->samplerProcessingActive && stack->sampler != nullptr)
            {
                PerformanceMetrics::Scope timing(performanceMetrics, PerformanceMetrics::Stage::sampler);
                stack->sampler->processBlock(stackBuffer, stack->samplerMidiBuffer);
            }
            if (stack->wavetableProcessingActive && stack->wavetableSynth != nullptr)
            {
                PerformanceMetrics::Scope timing(performanceMetrics, PerformanceMetrics::Stage::wavetable);
                stack->wavetableSynth->scheduleBlockNotes(stack->wavetableBlockNotes);
                stack->wavetableSynth->processBlock(stackBuffer, emptyMidiBuffer);
            }
            if (stack->drumProcessingActive && stack->drumSynth != nullptr)
            {
                PerformanceMetrics::Scope timing(performanceMetrics, PerformanceMetrics::Stage::drum);
                stack->drumSynth->scheduleBlockNotes(stack->drumBlockNotes);
                stack->drumSynth->processBlock(stackBuffer, emptyMidiBuffer);
            }
            // The filter's audio runs through the effect slot loop below; this
            // only hands over the notes that retrigger its cutoff envelope.
            if (stack->filterProcessingActive && stack->filterFx != nullptr)
            {
                PerformanceMetrics::Scope timing(performanceMetrics, PerformanceMetrics::Stage::filter);
                stack->filterFx->scheduleBlockNotes(stack->filterBlockNotes);
            }

            for (auto& slot : stack->slots)
            {
                const auto type = slot.type;
                if (!isAudioEffectType(type))
                    continue;

                const bool slotEnabled = slot.enabled;

                if (isAuxSendType(type))
                {
                    if (!slotEnabled)
                        continue;

                    auto* auxBus = getAuxBusForType(type);
                    if (auxBus == nullptr
                        || auxBus->inputBuffer.getNumChannels() < 2
                        || auxBus->inputBuffer.getNumSamples() != buffer.getNumSamples())
                    {
                        continue;
                    }

                    const float sendGainLinear = slot.sendGain.get(slot.sendLevelDb);
                    // Stacks are intentionally mono until this final mixer path;
                    // feed the shared stereo reverb equally before stack panning.
                    auxBus->inputBuffer.addFrom(0, 0, stackBuffer, 0, 0, stackBuffer.getNumSamples(), sendGainLinear);
                    auxBus->inputBuffer.addFrom(1, 0, stackBuffer, 0, 0, stackBuffer.getNumSamples(), sendGainLinear);
                    continue;
                }

                const float returnGainLinear = slot.returnGain.get(slot.returnLevelDb);
                if (auto* effect = getAudioEffectForStackType(*stack, type))
                {
                    const auto effectStage = type == CommandType::DistortionFx ? PerformanceMetrics::Stage::distortion
                        : type == CommandType::DelayFx ? PerformanceMetrics::Stage::delay
                        : type == CommandType::ChannelStripFx ? PerformanceMetrics::Stage::channel
                        : PerformanceMetrics::Stage::filter;
                    PerformanceMetrics::Scope timing(performanceMetrics, effectStage);
                    if (!slotEnabled && type == CommandType::DelayFx)
                    {
                        auto& delayTailBuffer = stack->delayTailBuffer;
                        delayTailBuffer.clear();
                        effect->processAudioBuffer(delayTailBuffer);
                        if (std::abs(returnGainLinear - 1.0f) > std::numeric_limits<float>::epsilon())
                            delayTailBuffer.applyGain(returnGainLinear);
                        stackBuffer.addFrom(0, 0, delayTailBuffer, 0, 0, delayTailBuffer.getNumSamples());
                        continue;
                    }

                    if (!slotEnabled)
                        continue;

                    const float sendGainLinear = slot.sendGain.get(slot.sendLevelDb);
                    if (std::abs(sendGainLinear - 1.0f) > std::numeric_limits<float>::epsilon())
                        stackBuffer.applyGain(sendGainLinear);

                    effect->processAudioBuffer(stackBuffer);

                    if (std::abs(returnGainLinear - 1.0f) > std::numeric_limits<float>::epsilon())
                        stackBuffer.applyGain(returnGainLinear);
                }
            }

            PerformanceMetrics::Scope scopeMeterTiming(performanceMetrics, PerformanceMetrics::Stage::scopeMeter);
            const float stackGainLinear = stack->gain.get(stack->gainDb);
            stackBuffer.applyGain(stackGainLinear);

            // Record the final per-stack mono output for the UI oscilloscope.
            // Audio-thread only; the ring is pre-allocated in prepareToPlay, so
            // this performs no allocation on the audio path.
            if (!stack->scopeRing.empty())
            {
                const int numSamples = stackBuffer.getNumSamples();
                auto& scopeRing = stack->scopeRing;
                const std::size_t ringSize = scopeRing.size();
                // Keep the most recent ringSize samples of this block.
                const int startInBlock = (numSamples > static_cast<int>(ringSize))
                    ? (numSamples - static_cast<int>(ringSize)) : 0;
                const int samplesToWrite = numSamples - startInBlock;
                for (int s = 0; s < samplesToWrite; ++s)
                {
                    const int idx = startInBlock + s;
                    scopeRing[stack->scopeHead] = stackBuffer.getSample(0, idx);
                    if (++stack->scopeHead == ringSize)
                        stack->scopeHead = 0;
                }
            }

            const float meterTarget = linearToMeterNormalised(measureBufferRms(stackBuffer));
            const float attack = 0.65f;
            const float decay = 0.12f;
            const float smoothing = meterTarget > stack->meterLevel ? attack : decay;
            stack->meterLevel += (meterTarget - stack->meterLevel) * smoothing;
            stack->meterLevel = juce::jlimit(0.0f, 1.0f, stack->meterLevel);
            const auto* panModulation = stack->modulationMatrix != nullptr
                ? &stack->modulationMatrix->getBlockView() : nullptr;
            const bool modulatedPan = panModulation != nullptr
                && panModulation->isActive(ModulationDestination::stackPan);
            if (buffer.getNumChannels() < 2)
            {
                buffer.addFrom(0, 0, stackBuffer, 0, 0, stackBuffer.getNumSamples());
            }
            else if (!modulatedPan && stack->panRampSamples == 0
                     && std::abs(stack->panSmoothed - stack->panCurrent) < 0.00001f)
            {
                const auto gains = equalPowerPan(stack->panSmoothed);
                buffer.addFrom(0, 0, stackBuffer, 0, 0, stackBuffer.getNumSamples(), gains.left);
                buffer.addFrom(1, 0, stackBuffer, 0, 0, stackBuffer.getNumSamples(), gains.right);
            }
            else
            {
                const auto* samples = stackBuffer.getReadPointer(0);
                auto* left = buffer.getWritePointer(0);
                auto* right = buffer.getWritePointer(1);
                for (int sample = 0; sample < stackBuffer.getNumSamples(); ++sample)
                {
                    const float targetPan = MixerState::clampPan(advancePan(stack->panCurrent, stack->panTarget, stack->panRampSamples)
                        + (modulatedPan ? panModulation->get(ModulationDestination::stackPan, sample) : 0.0f));
                    stack->panSmoothed += (targetPan - stack->panSmoothed) * panModulationSmoothing;
                    const auto gains = equalPowerPan(stack->panSmoothed);
                    left[sample] += samples[sample] * gains.left;
                    right[sample] += samples[sample] * gains.right;
                }
            }
            }
        }

    }

    auto processAuxReturn = [&buffer, this](SharedAuxBus& auxBus, PerformanceMetrics::Stage stage)
    {
        PerformanceMetrics::Scope timing(performanceMetrics, stage);
        if (auxBus.machine == nullptr
            || auxBus.inputBuffer.getNumChannels() == 0
            || auxBus.inputBuffer.getNumSamples() != buffer.getNumSamples())
        {
            return;
        }

        auxBus.machine->processAudioBuffer(auxBus.inputBuffer);
        if (!auxBus.muted && std::abs(auxBus.gainLinear - 1.0f) > std::numeric_limits<float>::epsilon())
            auxBus.inputBuffer.applyGain(auxBus.gainLinear);

        // AUX returns remain stereo: pan is a balance, not a mono collapse.
        if (!auxBus.muted && auxBus.inputBuffer.getNumChannels() >= 2)
        {
            if (auxBus.panRampSamples == 0)
            {
                const float leftGain = auxBus.panCurrent > 0.0f ? 1.0f - auxBus.panCurrent : 1.0f;
                const float rightGain = auxBus.panCurrent < 0.0f ? 1.0f + auxBus.panCurrent : 1.0f;
                auxBus.inputBuffer.applyGain(0, 0, auxBus.inputBuffer.getNumSamples(), leftGain);
                auxBus.inputBuffer.applyGain(1, 0, auxBus.inputBuffer.getNumSamples(), rightGain);
            }
            else
            {
                auto* left = auxBus.inputBuffer.getWritePointer(0);
                auto* right = auxBus.inputBuffer.getWritePointer(1);
                for (int sample = 0; sample < auxBus.inputBuffer.getNumSamples(); ++sample)
                {
                    const float pan = advancePan(auxBus.panCurrent, auxBus.panTarget, auxBus.panRampSamples);
                    left[sample] *= pan > 0.0f ? 1.0f - pan : 1.0f;
                    right[sample] *= pan < 0.0f ? 1.0f + pan : 1.0f;
                }
            }
        }

        for (std::size_t channel = 0; channel < auxBus.meterLevels.size(); ++channel)
        {
            const int sourceChannel = auxBus.inputBuffer.getNumChannels() > 1
                ? static_cast<int>(channel) : 0;
            const float target = auxBus.muted ? 0.0f
                : linearToMeterNormalised(measureChannelRms(auxBus.inputBuffer, sourceChannel));
            const float smoothing = target > auxBus.meterLevels[channel] ? 0.65f : 0.12f;
            auxBus.meterLevels[channel] += (target - auxBus.meterLevels[channel]) * smoothing;
            auxBus.meterLevels[channel] = juce::jlimit(0.0f, 1.0f, auxBus.meterLevels[channel]);
        }

        if (!auxBus.muted)
            for (int channel = 0; channel < juce::jmin(buffer.getNumChannels(), auxBus.inputBuffer.getNumChannels()); ++channel)
                buffer.addFrom(channel, 0, auxBus.inputBuffer, channel, 0, auxBus.inputBuffer.getNumSamples());
    };

    processAuxReturn(auxBus1, PerformanceMetrics::Stage::aux1);
    processAuxReturn(auxBus2, PerformanceMetrics::Stage::aux2);

    PerformanceMetrics::Scope masterTiming(performanceMetrics, PerformanceMetrics::Stage::master);
    const float outputGainLinear = masterGainLinear;
    if (std::abs(masterGainDb) > 0.0001f)
        buffer.applyGain(outputGainLinear);
    if (masterMuted)
        buffer.clear();

    for (std::size_t channel = 0; channel < masterMeterLevels.size(); ++channel)
    {
        const int sourceChannel = buffer.getNumChannels() > 1
            ? static_cast<int>(channel)
            : 0;
        const float target = linearToMeterNormalised(measureChannelRms(buffer, sourceChannel));
        const float smoothing = target > masterMeterLevels[channel] ? 0.65f : 0.12f;
        masterMeterLevels[channel] += (target - masterMeterLevels[channel]) * smoothing;
        masterMeterLevels[channel] = juce::jlimit(0.0f, 1.0f, masterMeterLevels[channel]);
    }

    if (auto* writer = activeRecordingWriter.load(std::memory_order_acquire);
        writer != nullptr && buffer.getNumChannels() > 0 && buffer.getNumSamples() > 0)
    {
        const float* stereoChannels[] {
            buffer.getReadPointer(0),
            buffer.getReadPointer(buffer.getNumChannels() > 1 ? 1 : 0)
        };
        writer->write(stereoChannels, buffer.getNumSamples());
    }
    processing.store(false, std::memory_order_release);
}

//==============================================================================
bool TrackerMainProcessor::hasEditor() const
{
    return true; // (change this to false if you choose to not supply an editor)
}

juce::AudioProcessorEditor* TrackerMainProcessor::createEditor()
{
    return new TrackerMainUI (*this);
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout TrackerMainProcessor::createParameterLayout()
{
    // No traditional parameters yet – we just want the APVTS machinery for state persistence.
    return {};
}


//==============================================================================
// This creates new instances of the plugin..
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new TrackerMainProcessor();
}

Sequencer* TrackerMainProcessor::getSequencer()
{
    return getViewedSequencerInternal();
}

SequencerEditor* TrackerMainProcessor::getSequenceEditor()
{
    return &seqEditor;
}

TrackerController* TrackerMainProcessor::getTrackerController()
{
    return &trackerController;
}

TrackerControlService& TrackerMainProcessor::getControlService()
{
    jassert(controlService != nullptr);
    return *controlService;
}

std::size_t TrackerMainProcessor::getSequenceSetCount() const
{
    return sequenceSets.size();
}

std::size_t TrackerMainProcessor::getViewedSequenceSetIndex() const
{
    return viewedSequenceSetIndex;
}

void TrackerMainProcessor::setViewedSequenceSetIndex(std::size_t index)
{
    if (sequenceSets.empty())
        return;
    viewedSequenceSetIndex = std::min(index, sequenceSets.size() - 1);
    // While a song is running, its active sequence set owns the live stack
    // state until the next scheduled song boundary.  Browsing another set
    // must not change the sound early.
    if (!isSongPlaybackRunning())
    {
        if (viewedSequenceSetIndex < sequenceSetBpms.size())
            setBPM(sequenceSetBpms[viewedSequenceSetIndex]);
        applyMachineEnableStateForSequenceSet(viewedSequenceSetIndex);
    }
    bindViewedSequenceSetToEditor();
}

std::size_t TrackerMainProcessor::getSongRowCount() const
{
    return songRows.size();
}

std::size_t TrackerMainProcessor::getSelectedSongRow() const
{
    return selectedSongRow;
}

std::size_t TrackerMainProcessor::getCurrentPlaybackSongRow() const
{
    return currentSongRow;
}

void TrackerMainProcessor::setSelectedSongRow(std::size_t row)
{
    if (songRows.empty())
        return;
    selectedSongRow = std::min(row, songRows.size() - 1);
    currentSongRowBeatCounter = juce::jmax(1, songRows[selectedSongRow].beatCount);
    if (songPlayMode == SongPlayMode::sequence)
    {
        if (auto* playbackSequencer = getPlaybackSequencerInternal())
        {
            if (playbackSequencer->isPlaying())
            {
                currentSongRow = selectedSongRow;
                currentSongRowBeatCounter = juce::jmax(1, songRows[selectedSongRow].beatCount);
                schedulePlaybackSequenceSetSwitch(songRows[selectedSongRow].sequenceSetId);
            }
            else
            {
                setViewedSequenceSetIndex(songRows[selectedSongRow].sequenceSetId);
            }
        }
        else
        {
            setViewedSequenceSetIndex(songRows[selectedSongRow].sequenceSetId);
        }
    }
}

std::size_t TrackerMainProcessor::getSongRowSequenceSetId(std::size_t row) const
{
    if (row >= songRows.size())
        return 0;
    return songRows[row].sequenceSetId;
}

int TrackerMainProcessor::getSongRowBeatCount(std::size_t row) const
{
    if (row >= songRows.size())
        return 16;
    return songRows[row].beatCount;
}

int TrackerMainProcessor::getSongRowBpm(std::size_t row) const
{
    if (row >= songRows.size())
        return static_cast<int>(std::lround(bpm.load(std::memory_order_relaxed)));
    const auto setId = songRows[row].sequenceSetId;
    return static_cast<int>(std::lround(setId < sequenceSetBpms.size()
        ? sequenceSetBpms[setId]
        : bpm.load(std::memory_order_relaxed)));
}

SongPlayMode TrackerMainProcessor::getSongPlayMode() const
{
    return songPlayMode;
}

void TrackerMainProcessor::setSongPlayMode(SongPlayMode mode)
{
    songPlayMode = mode;
    if (mode == SongPlayMode::sequence)
    {
        setViewedSequenceSetIndex(activePlaybackSequenceSetIndex);
        setSelectedSongRow(selectedSongRow);
    }
}

std::size_t TrackerMainProcessor::addSongRowByCloningViewedSet()
{
    auto* viewedSequencer = getViewedSequencerInternal();
    if (viewedSequencer == nullptr)
        return 0;

    auto newSequenceSet = createDefaultSequenceSet();
    restoreSingleSequencer(*newSequenceSet, serializeSingleSequencer(*viewedSequencer));
    const auto sourceSetIndex = std::min(viewedSequenceSetIndex, sequenceSets.size() - 1);
    if (sequenceSetMachineEnableStates.size() < sequenceSets.size())
        normaliseAllMachineEnableStates();
    const auto sourceEnableState = sourceSetIndex < sequenceSetMachineEnableStates.size()
        ? sequenceSetMachineEnableStates[sourceSetIndex]
        : captureMachineEnableState();
    sequenceSets.push_back(std::move(newSequenceSet));
    sequenceSetBpms.push_back(sourceSetIndex < sequenceSetBpms.size() ? sequenceSetBpms[sourceSetIndex] : getBPM());
    sequenceSetMachineEnableStates.push_back(sourceEnableState);
    const std::size_t newSetIndex = sequenceSets.size() - 1;
    songRows.push_back({ newSetIndex, 16 });
    return songRows.size() - 1;
}

void TrackerMainProcessor::removeSongRow(std::size_t row)
{
    if (row >= songRows.size() || songRows.size() <= 1 || sequenceSets.empty())
        return;

    const std::size_t removedSetId = songRows[row].sequenceSetId;
    songRows.erase(songRows.begin() + static_cast<std::ptrdiff_t>(row));

    bool setStillReferenced = false;
    for (const auto& songRow : songRows)
    {
        if (songRow.sequenceSetId == removedSetId)
        {
            setStillReferenced = true;
            break;
        }
    }

    if (!setStillReferenced && removedSetId < sequenceSets.size() && sequenceSets.size() > 1)
    {
        sequenceSets.erase(sequenceSets.begin() + static_cast<std::ptrdiff_t>(removedSetId));
        if (removedSetId < sequenceSetBpms.size())
            sequenceSetBpms.erase(sequenceSetBpms.begin() + static_cast<std::ptrdiff_t>(removedSetId));
        if (removedSetId < sequenceSetMachineEnableStates.size())
            sequenceSetMachineEnableStates.erase(sequenceSetMachineEnableStates.begin() + static_cast<std::ptrdiff_t>(removedSetId));
        for (auto& songRow : songRows)
            if (songRow.sequenceSetId > removedSetId)
                --songRow.sequenceSetId;

        if (viewedSequenceSetIndex > removedSetId && viewedSequenceSetIndex > 0)
            --viewedSequenceSetIndex;
        else if (viewedSequenceSetIndex >= sequenceSets.size())
            viewedSequenceSetIndex = sequenceSets.size() - 1;

        if (activePlaybackSequenceSetIndex > removedSetId && activePlaybackSequenceSetIndex > 0)
            --activePlaybackSequenceSetIndex;
        else if (activePlaybackSequenceSetIndex >= sequenceSets.size())
            activePlaybackSequenceSetIndex = sequenceSets.size() - 1;

        if (pendingPlaybackSequenceSetIndex.has_value())
        {
            if (*pendingPlaybackSequenceSetIndex > removedSetId)
                pendingPlaybackSequenceSetIndex = *pendingPlaybackSequenceSetIndex - 1;
            else if (*pendingPlaybackSequenceSetIndex >= sequenceSets.size())
                pendingPlaybackSequenceSetIndex = sequenceSets.size() - 1;
        }
    }

    if (selectedSongRow >= songRows.size())
        selectedSongRow = songRows.size() - 1;
    if (currentSongRow >= songRows.size())
        currentSongRow = songRows.size() - 1;
    if (!songRows.empty())
        currentSongRowBeatCounter = juce::jmax(1, songRows[currentSongRow].beatCount);

    setSelectedSongRow(selectedSongRow);
}

void TrackerMainProcessor::adjustSongRowSequenceSetId(std::size_t row, int direction)
{
    if (row >= songRows.size() || sequenceSets.empty() || direction == 0)
        return;
    const int current = static_cast<int>(songRows[row].sequenceSetId);
    const int maxId = static_cast<int>(sequenceSets.size() - 1);
    songRows[row].sequenceSetId = static_cast<std::size_t>(juce::jlimit(0, maxId, current + direction));
    if (songPlayMode == SongPlayMode::sequence && row == selectedSongRow)
        setSelectedSongRow(row);
    else if (songPlayMode == SongPlayMode::song && row == currentSongRow)
    {
        // Song-row edits must not replace the currently playing set mid-beat.
        // The pending switch is consumed only at the next four-tick beat boundary.
        schedulePlaybackSequenceSetSwitch(songRows[row].sequenceSetId);
    }
}

void TrackerMainProcessor::adjustSongRowBeatCount(std::size_t row, int direction)
{
    if (row >= songRows.size() || direction == 0)
        return;
    songRows[row].beatCount = juce::jmax(1, songRows[row].beatCount + direction);
    if (row == currentSongRow)
        currentSongRowBeatCounter = juce::jmax(1, songRows[row].beatCount);
}

void TrackerMainProcessor::adjustSongRowBpm(std::size_t row, int direction)
{
    if (row >= songRows.size() || direction == 0)
        return;
    const auto setId = songRows[row].sequenceSetId;
    if (setId >= sequenceSetBpms.size())
        return;
    setSequenceSetBpm(setId, sequenceSetBpms[setId] + static_cast<double>(direction));
}

void TrackerMainProcessor::setSequenceSetBpm(std::size_t setId, double newBpm)
{
    if (setId >= sequenceSetBpms.size() || !std::isfinite(newBpm))
        return;
    sequenceSetBpms[setId] = juce::jlimit(1.0, 999.0, newBpm);
    if (setId == activePlaybackSequenceSetIndex && !isHostClockActive())
        setBPM(sequenceSetBpms[setId]);
}

void TrackerMainProcessor::toggleSongPlayback()
{
    auto* playbackSequencer = getPlaybackSequencerInternal();
    if (playbackSequencer == nullptr || songRows.empty())
        return;

    CommandProcessor::sendAllNotesOff();
    if (playbackSequencer->isPlaying())
    {
        playbackSequencer->stop();
        pendingPlaybackSequenceSetIndex.reset();
        pendingTransportQuarterBeatReset = false;
        pendingTransportStartOnQuarterBeat = false;
        updateClockedMachineActivity();
        return;
    }

    currentSongRow = std::min(selectedSongRow, songRows.size() - 1);
    currentSongRowBeatCounter = juce::jmax(1, songRows[currentSongRow].beatCount);
    pendingTransportQuarterBeatReset = true;
    pendingTransportStartOnQuarterBeat = true;
    if (!songRows.empty())
        schedulePlaybackSequenceSetSwitch(songRows[currentSongRow].sequenceSetId);
    else if (auto* targetSequencer = getPlaybackSequencerInternal())
    {
        targetSequencer->stop();
        targetSequencer->resetForTransportStart();
    }
}

void TrackerMainProcessor::rewindSongTransport()
{
    CommandProcessor::sendAllNotesOff();
    auto* playbackSequencer = getPlaybackSequencerInternal();
    const bool wasPlaying = playbackSequencer != nullptr && playbackSequencer->isPlaying();
    currentSongRow = std::min(selectedSongRow, songRows.empty() ? 0u : songRows.size() - 1);
    if (!songRows.empty())
        currentSongRowBeatCounter = juce::jmax(1, songRows[currentSongRow].beatCount);
    pendingTransportQuarterBeatReset = true;
    pendingTransportStartOnQuarterBeat = wasPlaying;
    if (!songRows.empty())
        schedulePlaybackSequenceSetSwitch(songRows[currentSongRow].sequenceSetId);
    else if (auto* targetSequencer = getPlaybackSequencerInternal())
    {
        targetSequencer->stop();
        targetSequencer->resetForTransportStart();
    }
    updateClockedMachineActivity();
}

std::size_t TrackerMainProcessor::getMachineCount(CommandType type) const
{
    switch (type)
    {
        case CommandType::MidiNote:
        case CommandType::Log:
            return type == CommandType::MidiNote ? machineStacks.size() : 0;
        case CommandType::Sampler:
        case CommandType::WavetableSynth:
        case CommandType::DrumSynth:
        case CommandType::DistortionFx:
        case CommandType::DelayFx:
        case CommandType::ChannelStripFx:
        case CommandType::FilterFx:
        case CommandType::ModulationMatrix:
            return machineStacks.size();
        case CommandType::AuxSend1Fx:
        case CommandType::AuxSend2Fx:
            return 1;
        case CommandType::ToggleAux1:
        case CommandType::ToggleAux2:
        case CommandType::FilterCutoff:
            return 0;
    }
    return 0;
}

MachineInterface* TrackerMainProcessor::getMachine(CommandType type, std::size_t index)
{
    if (isAuxSendType(type))
        return getAuxBusForType(type) != nullptr ? getAuxBusForType(type)->machine.get() : nullptr;
    if (auto* stack = getMachineStack(index))
        return getMachineForStackType(*stack, type);
    return nullptr;
}

const MachineInterface* TrackerMainProcessor::getMachine(CommandType type, std::size_t index) const
{
    if (isAuxSendType(type))
        return getAuxBusForType(type) != nullptr ? getAuxBusForType(type)->machine.get() : nullptr;
    if (const auto* stack = getMachineStack(index))
        return getMachineForStackType(*stack, type);
    return nullptr;
}

std::size_t TrackerMainProcessor::getMachineStackCount() const
{
    return machineStacks.size();
}

std::vector<CommandType> TrackerMainProcessor::getMachineStackTypes(std::size_t stackIndex) const
{
    if (const auto* stack = getMachineStack(stackIndex))
    {
        std::vector<CommandType> result;
        result.reserve(stack->slots.size());
        for (const auto& slot : stack->slots)
            result.push_back(slot.type);
        return result;
    }
    return {};
}

float TrackerMainProcessor::getStackMeterLevel(std::size_t stackIndex) const
{
    if (const auto* stack = getMachineStack(stackIndex))
        return stack->meterLevel;
    return 0.0f;
}

float TrackerMainProcessor::getStackGainDb(std::size_t stackIndex) const
{
    if (const auto* stack = getMachineStack(stackIndex))
        return stack->gainDb;
    return 0.0f;
}

float TrackerMainProcessor::getMasterMeterLevel(std::size_t channel) const
{
    return channel < masterMeterLevels.size() ? masterMeterLevels[channel] : 0.0f;
}

float TrackerMainProcessor::getMasterGainDb() const
{
    return masterGainDb;
}

void TrackerMainProcessor::setMasterGainDb(float gainDb)
{
    masterGainDb = MixerState::clampMasterGainDb(gainDb);
    masterGainLinear = MixerState::masterGainToLinear(masterGainDb);
}

bool TrackerMainProcessor::isMasterMuted() const
{
    return masterMuted;
}

void TrackerMainProcessor::setMasterMuted(bool muted)
{
    masterMuted = muted;
}

float TrackerMainProcessor::getAuxReturnMeterLevel(std::size_t auxIndex, std::size_t channel) const
{
    if (const auto* auxBus = getAuxBus(auxIndex); auxBus != nullptr && channel < auxBus->meterLevels.size())
        return auxBus->meterLevels[channel];
    return 0.0f;
}

float TrackerMainProcessor::getAuxReturnGainDb(std::size_t auxIndex) const
{
    if (const auto* auxBus = getAuxBus(auxIndex))
        return auxBus->gainDb;
    return 0.0f;
}

void TrackerMainProcessor::setAuxReturnGainDb(std::size_t auxIndex, float gainDb)
{
    if (auto* auxBus = getAuxBus(auxIndex))
    {
        auxBus->gainDb = MixerState::clampMasterGainDb(gainDb);
        auxBus->gainLinear = MixerState::masterGainToLinear(auxBus->gainDb);
    }
}

float TrackerMainProcessor::getAuxReturnPan(std::size_t auxIndex) const
{
    if (const auto* auxBus = getAuxBus(auxIndex))
        return auxBus->pan;
    return 0.0f;
}

void TrackerMainProcessor::setAuxReturnPan(std::size_t auxIndex, float pan)
{
    if (auto* auxBus = getAuxBus(auxIndex))
    {
        auxBus->pan = MixerState::clampPan(pan);
        auxBus->panTarget = auxBus->pan;
        auxBus->panRampSamples = juce::jmax(1, static_cast<int>(std::lround(preparedSampleRate * 0.01)));
    }
}

bool TrackerMainProcessor::isAuxReturnMuted(std::size_t auxIndex) const
{
    if (const auto* auxBus = getAuxBus(auxIndex))
        return auxBus->muted;
    return false;
}

void TrackerMainProcessor::setAuxReturnMuted(std::size_t auxIndex, bool muted)
{
    if (auto* auxBus = getAuxBus(auxIndex))
        auxBus->muted = muted;
}

bool TrackerMainProcessor::isMasterRecording() const
{
    return activeRecordingWriter.load(std::memory_order_acquire) != nullptr;
}

bool TrackerMainProcessor::startMasterRecording()
{
    stopMasterRecording();

    const double activeSampleRate = getSampleRate();
    if (activeSampleRate <= 0.0)
        return false;

    auto recordingDirectory = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
                                  .getChildFile("MYK Tracker Recordings");
    if (const auto result = recordingDirectory.createDirectory(); result.failed())
        return false;

    const auto timestamp = juce::Time::getCurrentTime().formatted("%Y-%m-%d_%H-%M-%S");
    const auto outputFile = recordingDirectory.getNonexistentChildFile(
        "MYK Tracker Recording " + timestamp, ".wav", false);
    std::unique_ptr<juce::OutputStream> outputStream { outputFile.createOutputStream() };
    if (outputStream == nullptr)
        return false;

    juce::WavAudioFormat wavFormat;
    using WriterOptions = juce::AudioFormatWriterOptions;
    auto writer = wavFormat.createWriterFor(
        outputStream,
        WriterOptions{}.withSampleRate(activeSampleRate)
                       .withNumChannels(2)
                       .withBitsPerSample(24));
    if (writer == nullptr)
        return false;

    auto newSession = std::make_shared<RecordingSession>();
    if (!newSession->thread.startThread())
        return false;
    const int fifoSamples = juce::jmax(32768, static_cast<int>(std::lround(activeSampleRate * 2.0)));
    newSession->writer = std::make_unique<juce::AudioFormatWriter::ThreadedWriter>(
        writer.release(), newSession->thread, fifoSamples);
    newSession->writer->setFlushInterval(juce::jmax(1, static_cast<int>(std::lround(activeSampleRate))));
    recordingSession = std::move(newSession);
    activeRecordingWriter.store(recordingSession->writer.get(), std::memory_order_release);
    return true;
}

void TrackerMainProcessor::stopMasterRecording(bool deferFlush)
{
    activeRecordingWriter.store(nullptr, std::memory_order_release);
    if (deferFlush && recordingSession != nullptr
        && juce::MessageManager::getInstanceWithoutCreating() != nullptr)
    {
        auto sessionToClose = std::move(recordingSession);
        if (juce::MessageManager::callAsync([sessionToClose]() {}))
            return;
        recordingSession = std::move(sessionToClose);
    }
    recordingSession.reset();
}

bool TrackerMainProcessor::toggleMasterRecording()
{
    if (isMasterRecording())
    {
        // Clearing the active pointer stops the audio thread immediately. Let
        // the message thread finish flushing the WAV after the editor's audio
        // exclusivity guard has been released.
        stopMasterRecording(true);
        return false;
    }
    return startMasterRecording();
}

void TrackerMainProcessor::setStackGainDb(std::size_t stackIndex, float gainDb)
{
    if (auto* stack = getMachineStack(stackIndex))
        stack->gainDb = juce::jlimit(-48.0f, 6.0f, gainDb);
}

float TrackerMainProcessor::getStackPan(std::size_t stackIndex) const
{
    if (const auto* stack = getMachineStack(stackIndex))
        return stack->pan;
    return 0.0f;
}

void TrackerMainProcessor::setStackPan(std::size_t stackIndex, float pan)
{
    if (auto* stack = getMachineStack(stackIndex))
    {
        stack->pan = MixerState::clampPan(pan);
        stack->panTarget = stack->pan;
        stack->panRampSamples = juce::jmax(1, static_cast<int>(std::lround(preparedSampleRate * 0.01)));
    }
}

bool TrackerMainProcessor::isStackMuted(std::size_t stackIndex) const
{
    if (const auto* stack = getMachineStack(stackIndex)) return stack->muted;
    return false;
}

bool TrackerMainProcessor::isStackSolo(std::size_t stackIndex) const
{
    if (const auto* stack = getMachineStack(stackIndex)) return stack->solo;
    return false;
}

bool TrackerMainProcessor::isStackAudible(std::size_t stackIndex) const
{
    const auto* stack = getMachineStack(stackIndex);
    if (stack == nullptr || stack->muted) return false;
    const bool anySolo = std::any_of(machineStacks.begin(), machineStacks.end(), [](const auto& item) { return item.solo; });
    return !anySolo || stack->solo;
}

void TrackerMainProcessor::setStackMuted(std::size_t stackIndex, bool muted)
{
    const bool wasAudible = isStackAudible(stackIndex);
    if (auto* stack = getMachineStack(stackIndex)) stack->muted = muted;
    if (wasAudible && !isStackAudible(stackIndex)) allNotesOffForStack(stackIndex);
}

void TrackerMainProcessor::setStackSolo(std::size_t stackIndex, bool solo)
{
    std::vector<bool> wasAudible(machineStacks.size());
    for (std::size_t i = 0; i < wasAudible.size(); ++i) wasAudible[i] = isStackAudible(i);
    if (auto* stack = getMachineStack(stackIndex)) stack->solo = solo;
    for (std::size_t i = 0; i < wasAudible.size(); ++i)
        if (wasAudible[i] && !isStackAudible(i)) allNotesOffForStack(i);
}

void TrackerMainProcessor::silenceStack(std::size_t stackIndex)
{
    allNotesOffForStack(stackIndex);
}

int TrackerMainProcessor::getStackMidiOutputChannel(std::size_t stackIndex) const
{
    if (const auto* stack = getMachineStack(stackIndex))
        return juce::jlimit(1, 16, stack->midiOutputChannel);
    return 1;
}

void TrackerMainProcessor::adjustStackMidiOutputChannel(std::size_t stackIndex, int direction)
{
    if (direction == 0)
        return;

    if (auto* stack = getMachineStack(stackIndex))
        stack->midiOutputChannel = juce::jlimit(1, 16, stack->midiOutputChannel + (direction < 0 ? -1 : 1));
}

void TrackerMainProcessor::addMachineToStack(std::size_t stackIndex)
{
    if (auto* stack = getMachineStack(stackIndex))
    {
        for (const auto type : kSlotCycleTypes)
        {
            const bool duplicate = std::any_of(stack->slots.begin(), stack->slots.end(),
                [type](const MachineStack::SlotState& slot) { return slot.type == type; });
            if (!duplicate || slotAllowsDuplicate(type))
            {
                stack->slots.push_back(makeDefaultSlotState(type));
                refreshStackProcessingState(*stack);
                normaliseAllMachineEnableStates();
                updateClockedMachineActivity();
                break;
            }
        }
    }
}

void TrackerMainProcessor::removeMachineFromStack(std::size_t stackIndex, std::size_t slotIndex)
{
    if (auto* stack = getMachineStack(stackIndex))
    {
        if (slotIndex < stack->slots.size())
        {
            stack->slots.erase(stack->slots.begin() + static_cast<long>(slotIndex));
            refreshStackProcessingState(*stack);
            normaliseAllMachineEnableStates();
            updateClockedMachineActivity();
        }
    }
}

void TrackerMainProcessor::cycleMachineTypeInStack(std::size_t stackIndex, std::size_t slotIndex, int direction)
{
    if (auto* stack = getMachineStack(stackIndex))
    {
        if (slotIndex >= stack->slots.size())
            return;

        const int cycleCount = static_cast<int>(kSlotCycleTypes.size());
        int currentIndex = machineTraits(stack->slots[slotIndex].type).slotCycleIndex;
        if (currentIndex < 0)
            return;

        for (int attempt = 0; attempt < cycleCount; ++attempt)
        {
            currentIndex = (currentIndex + direction + cycleCount) % cycleCount;
            const auto candidate = kSlotCycleTypes[static_cast<std::size_t>(currentIndex)];
            const bool duplicate = std::any_of(stack->slots.begin(), stack->slots.end(),
                [candidate, slotIndex, stack](const MachineStack::SlotState& slot)
                {
                    return &slot != &stack->slots[slotIndex] && slot.type == candidate;
                });
            if (!duplicate || candidate == stack->slots[slotIndex].type || slotAllowsDuplicate(candidate))
            {
                const auto stableId = stack->slots[slotIndex].id;
                stack->slots[slotIndex] = makeDefaultSlotState(candidate);
                stack->slots[slotIndex].id = stableId;
                refreshStackProcessingState(*stack);
                normaliseAllMachineEnableStates();
                updateClockedMachineActivity();
                return;
            }
        }
    }
}

void TrackerMainProcessor::moveMachineInStack(std::size_t stackIndex, std::size_t slotIndex, int direction)
{
    if (auto* stack = getMachineStack(stackIndex))
    {
        if (slotIndex >= stack->slots.size() || direction == 0)
            return;

        const int targetIndex = juce::jlimit(0,
                                             static_cast<int>(stack->slots.size()) - 1,
                                             static_cast<int>(slotIndex) + (direction < 0 ? -1 : 1));
        if (targetIndex == static_cast<int>(slotIndex))
            return;

        std::swap(stack->slots[slotIndex], stack->slots[static_cast<std::size_t>(targetIndex)]);
        refreshStackProcessingState(*stack);
        normaliseAllMachineEnableStates();
    }
}

bool TrackerMainProcessor::isMachineEnabledInStack(std::size_t stackIndex, std::size_t slotIndex) const
{
    if (const auto* slot = getMachineSlot(stackIndex, slotIndex))
        return slot->enabled;
    return true;
}

void TrackerMainProcessor::toggleMachineEnabledInStack(std::size_t stackIndex, std::size_t slotIndex)
{
    if (auto* slot = getMachineSlot(stackIndex, slotIndex))
    {
        slot->enabled = !slot->enabled;
        if (auto* stack = getMachineStack(stackIndex))
            refreshStackProcessingState(*stack);
        captureMachineEnableStateForSequenceSet(getMachineEnableStateEditingSetIndex());
        updateClockedMachineActivity();
    }
}

bool TrackerMainProcessor::isModulationEnabledInStack(std::size_t stackIndex) const
{
    if (const auto* stack = getMachineStack(stackIndex)) return stack->modulationEnabled;
    return false;
}

void TrackerMainProcessor::toggleModulationEnabledInStack(std::size_t stackIndex)
{
    if (auto* stack = getMachineStack(stackIndex))
    {
        stack->modulationEnabled = !stack->modulationEnabled;
        captureMachineEnableStateForSequenceSet(getMachineEnableStateEditingSetIndex());
    }
}

float TrackerMainProcessor::getMachineSendLevelDbInStack(std::size_t stackIndex, std::size_t slotIndex) const
{
    if (const auto* slot = getMachineSlot(stackIndex, slotIndex))
        return slot->sendLevelDb;
    return 0.0f;
}

void TrackerMainProcessor::adjustMachineSendLevelDbInStack(std::size_t stackIndex, std::size_t slotIndex, int direction)
{
    if (direction == 0)
        return;
    if (auto* slot = getMachineSlot(stackIndex, slotIndex))
        slot->sendLevelDb = juce::jlimit(-60.0f, 12.0f, slot->sendLevelDb + static_cast<float>(direction));
}

bool TrackerMainProcessor::machineHasReturnLevelInStack(std::size_t stackIndex, std::size_t slotIndex) const
{
    if (const auto* slot = getMachineSlot(stackIndex, slotIndex))
        return slotSupportsReturnLevel(slot->type);
    return false;
}

float TrackerMainProcessor::getMachineReturnLevelDbInStack(std::size_t stackIndex, std::size_t slotIndex) const
{
    if (const auto* slot = getMachineSlot(stackIndex, slotIndex))
        return slot->returnLevelDb;
    return 0.0f;
}

void TrackerMainProcessor::adjustMachineReturnLevelDbInStack(std::size_t stackIndex, std::size_t slotIndex, int direction)
{
    if (direction == 0)
        return;
    if (auto* slot = getMachineSlot(stackIndex, slotIndex))
    {
        if (!slotSupportsReturnLevel(slot->type))
            return;
        slot->returnLevelDb = juce::jlimit(-60.0f, 12.0f, slot->returnLevelDb + static_cast<float>(direction));
    }
}

////////////// MIDIUtils interface 

void TrackerMainProcessor::allNotesOff()
{
    midiToSend.clear();// remove anything that's hanging around. 
    for (int chan = 1; chan < 17; ++chan){
        midiToSend.addEvent(MidiMessage::allNotesOff(chan), static_cast<int>(elapsedSamples));
    }
    samplerEventsToSend.clear();
    wavetableEvents.clear();
    drumEvents.clear();
    filterEvents.clear();
    for (std::size_t i = 0; i < machineStacks.size(); ++i)
        allNotesOffForStack(i);
}

std::string TrackerMainProcessor::describeStepNote(CommandType machineType, unsigned short machineId, unsigned short note) const
{
    if (note == 0)
        return "----";

    if (machineTraits(machineType).isStackRoutable)
    {
        const auto stackIndex = static_cast<std::size_t>(machineId);
        if (const auto* stack = getMachineStack(stackIndex))
        {
            for (auto slot = stack->slots.rbegin(); slot != stack->slots.rend(); ++slot)
            {
                if (!slot->enabled)
                    continue;
                if (slot->type == CommandType::DrumSynth)
                    return stack->drumSynth != nullptr
                        ? stack->drumSynth->describeNote(static_cast<int>(note))
                        : "----";
                if (slot->type == CommandType::Sampler)
                {
                    if (const auto* sampler = dynamic_cast<const SuperSamplerProcessor*>(getMachine(CommandType::Sampler, stackIndex)))
                        return sampler->describeNoteForSequencer(static_cast<int>(note));
                    break;
                }
                if (slot->type == CommandType::MidiNote || slot->type == CommandType::WavetableSynth)
                    break;
            }
        }
    }

    return formatMidiNoteLabel(note);
}

void TrackerMainProcessor::sendMessageToMachine(CommandType machineType, unsigned short machineId, unsigned short note, unsigned short velocity, unsigned short durInTicks)
{
    if (machineTraits(machineType).isStackRoutable)
    {
        dispatchNoteThroughStack(static_cast<std::size_t>(machineId), note, velocity, durInTicks);
        return;
    }
    enqueueMachineMidi(midiToSend,
                       static_cast<unsigned short>(getStackMidiOutputChannel(static_cast<std::size_t>(machineId))),
                       note,
                       velocity,
                       durInTicks);
}

void TrackerMainProcessor::sendTimedMessageToMachine(CommandType machineType, unsigned short machineId,
                                                      unsigned short note, unsigned short velocity,
                                                      double durationTicks, bool gateSampler)
{
    durationTicks = juce::jmax(0.001, durationTicks);
    if (machineTraits(machineType).isStackRoutable)
    {
        dispatchNoteThroughStack(static_cast<std::size_t>(machineId), note, velocity,
                                 durationTicks, 0, false, gateSampler);
        return;
    }
    enqueueMachineMidi(midiToSend, static_cast<unsigned short>(getStackMidiOutputChannel(static_cast<std::size_t>(machineId))),
                       note, velocity, durationTicks);
}
void TrackerMainProcessor::beginHeldNote(CommandType machineType, unsigned short machineId,
                                         unsigned short note, unsigned short velocity)
{
    if (machineTraits(machineType).isStackRoutable)
    {
        dispatchNoteThroughStack(static_cast<std::size_t>(machineId), note, velocity, 0, 0, true);
        return;
    }
    midiToSend.addEvent(MidiMessage::noteOn(getStackMidiOutputChannel(static_cast<std::size_t>(machineId)), static_cast<int>(note), static_cast<uint8>(velocity)), elapsedSamples);
}

void TrackerMainProcessor::endHeldNote(CommandType machineType, unsigned short machineId,
                                       unsigned short note)
{
    if (!machineTraits(machineType).isStackRoutable)
    {
        midiToSend.addEvent(MidiMessage::noteOff(getStackMidiOutputChannel(static_cast<std::size_t>(machineId)), note), elapsedSamples);
        return;
    }

    const auto stackIndex = static_cast<std::size_t>(machineId);
    auto* stack = getMachineStack(stackIndex);
    if (stack == nullptr)
        return;

    bool hasExplicitTerminalRoute = false;
    for (const auto& slot : stack->slots)
    {
        if (slot.type == CommandType::MidiNote || slot.type == CommandType::Sampler
            || slot.type == CommandType::WavetableSynth || slot.type == CommandType::DrumSynth)
            hasExplicitTerminalRoute = true;
        if (!slot.enabled)
            continue;
        if (slot.type == CommandType::MidiNote)
            midiToSend.addEvent(MidiMessage::noteOff(getStackMidiOutputChannel(stackIndex), note), elapsedSamples);
        else if (slot.type == CommandType::Sampler)
            samplerEventsToSend.push_back({ stackIndex, MidiMessage::noteOff(1, note), elapsedSamples });
        else if (slot.type == CommandType::WavetableSynth)
            enqueueStackNote(wavetableEvents, stackIndex, note, 0, 0, true);
        else if (slot.type == CommandType::DrumSynth)
            enqueueStackNote(drumEvents, stackIndex, note, 0, 0, true);
        else if (slot.type == CommandType::FilterFx)
            enqueueStackNote(filterEvents, stackIndex, note, 0, 0, true);
    }
    if (!hasExplicitTerminalRoute)
        midiToSend.addEvent(MidiMessage::noteOff(getStackMidiOutputChannel(stackIndex), note), elapsedSamples);
}
void TrackerMainProcessor::toggleAuxSendForStack(unsigned short machineId, bool isAux1)
{
    // Executed from sequence step commands on the audio thread, so this must
    // remain allocation-free and must not enter the UI or clock-listener path.
    const auto stackIndex = static_cast<std::size_t>(machineId);
    const auto auxType = isAux1 ? CommandType::AuxSend1Fx : CommandType::AuxSend2Fx;
    const auto slotIndex = findMachineInStack(stackIndex, auxType);
    if (!slotIndex.has_value())
        return;
    if (auto* slot = getMachineSlot(stackIndex, *slotIndex))
    {
        slot->enabled = !slot->enabled;
        if (auto* stack = getMachineStack(stackIndex))
            refreshStackProcessingState(*stack);
    }
}

void TrackerMainProcessor::setFilterCutoffHzForStack(unsigned short machineId, double cutoffHz)
{
    // Executed from sequence step commands on the audio thread; the machine
    // setter takes its short state mutex and clamps the persisted value range.
    const auto stackIndex = static_cast<std::size_t>(machineId);
    if (findMachineInStack(stackIndex, CommandType::FilterFx).has_value())
        if (auto* stack = getMachineStack(stackIndex))
            stack->filterFx->setCutoffHz(cutoffHz);
}

void TrackerMainProcessor::sendQueuedMessages(long tick)
{
    juce::ignoreUnused(tick);
    // this is blank as midi gets sent by moving it from midiToSend to the processBlock's midi buffer

}

////////////// end MIDIUtils interface 


void TrackerMainProcessor::setBPM(double _bpm)
{   
    assert(_bpm > 0);
    const double activeSampleRate = getSampleRate() > 0.0 ? getSampleRate() : 44100.0;
    // update tick interval in samples 
    samplesPerTick = static_cast<unsigned int> (juce::jmax (1, static_cast<int> (std::lround (activeSampleRate * (60.0 / _bpm) / 8.0))));
    bpm.store(_bpm, std::memory_order_relaxed);
    const double secondsPerTick = getSecondsPerTickFromBpm(_bpm);
    for (auto& stack : machineStacks)
        stack.forEachMachine([&secondsPerTick] (MachineInterface& machine)
        {
            machine.setSecondsPerTick(secondsPerTick);
        });
}

double TrackerMainProcessor::getBPM()
{
    return bpm.load(std::memory_order_relaxed);
}

void TrackerMainProcessor::setInternalClockEnabled(bool enabled)
{
    internalClockEnabled.store(enabled, std::memory_order_relaxed);
}

bool TrackerMainProcessor::isInternalClockEnabled() const
{
    return internalClockEnabled.load(std::memory_order_relaxed);
}

bool TrackerMainProcessor::isHostClockActive() const
{
    return hostClockActive.load(std::memory_order_relaxed);
}


void TrackerMainProcessor::clearPendingEvents()
{
    midiToSend.clear();
    samplerEventsToSend.clear();
    wavetableEvents.clear();
    drumEvents.clear();
    filterEvents.clear();
}

TrackerMainProcessor::MachineStack* TrackerMainProcessor::getMachineStack(std::size_t stackIndex)
{
    if (machineStacks.empty())
        return nullptr;
    return &machineStacks[stackIndex % machineStacks.size()];
}

const TrackerMainProcessor::MachineStack* TrackerMainProcessor::getMachineStack(std::size_t stackIndex) const
{
    if (machineStacks.empty())
        return nullptr;
    return &machineStacks[stackIndex % machineStacks.size()];
}

TrackerMainProcessor::MachineStack::SlotState* TrackerMainProcessor::getMachineSlot(std::size_t stackIndex, std::size_t slotIndex)
{
    if (auto* stack = getMachineStack(stackIndex))
    {
        if (slotIndex < stack->slots.size())
            return &stack->slots[slotIndex];
    }
    return nullptr;
}

const TrackerMainProcessor::MachineStack::SlotState* TrackerMainProcessor::getMachineSlot(std::size_t stackIndex, std::size_t slotIndex) const
{
    if (const auto* stack = getMachineStack(stackIndex))
    {
        if (slotIndex < stack->slots.size())
            return &stack->slots[slotIndex];
    }
    return nullptr;
}

MachineInterface* TrackerMainProcessor::getMachineForStackType(MachineStack& stack, CommandType type)
{
    switch (type)
    {
        case CommandType::Sampler: return stack.sampler.get();
        case CommandType::WavetableSynth: return stack.wavetableSynth.get();
        case CommandType::DrumSynth: return stack.drumSynth.get();
        case CommandType::DistortionFx: return stack.distortionFx.get();
        case CommandType::DelayFx: return stack.delayFx.get();
        case CommandType::ChannelStripFx: return stack.channelStripFx.get();
        case CommandType::FilterFx: return stack.filterFx.get();
        case CommandType::ModulationMatrix: return stack.modulationMatrix.get();
        case CommandType::AuxSend1Fx: return auxBus1.machine.get();
        case CommandType::AuxSend2Fx: return auxBus2.machine.get();
        case CommandType::MidiNote:
        case CommandType::Log:
        case CommandType::ToggleAux1:
        case CommandType::ToggleAux2:
        case CommandType::FilterCutoff:
            return nullptr;
    }
    return nullptr;
}

// Read-only with respect to the stack and machines, so delegating from the
// const overload is safe despite the const_cast.
const MachineInterface* TrackerMainProcessor::getMachineForStackType(const MachineStack& stack, CommandType type) const
{
    return const_cast<TrackerMainProcessor*>(this)->getMachineForStackType(const_cast<MachineStack&>(stack), type);
}

TrackerMainProcessor::SharedAuxBus* TrackerMainProcessor::getAuxBusForType(CommandType type)
{
    switch (type)
    {
        case CommandType::MidiNote:
        case CommandType::Log:
        case CommandType::Sampler:
        case CommandType::WavetableSynth:
        case CommandType::DrumSynth:
        case CommandType::DistortionFx:
        case CommandType::DelayFx:
        case CommandType::ChannelStripFx:
        case CommandType::FilterFx:
        case CommandType::ToggleAux1:
        case CommandType::ToggleAux2:
        case CommandType::FilterCutoff:
        case CommandType::ModulationMatrix:
            return nullptr;
        case CommandType::AuxSend1Fx: return &auxBus1;
        case CommandType::AuxSend2Fx: return &auxBus2;
    }
    return nullptr;
}

const TrackerMainProcessor::SharedAuxBus* TrackerMainProcessor::getAuxBusForType(CommandType type) const
{
    switch (type)
    {
        case CommandType::MidiNote:
        case CommandType::Log:
        case CommandType::Sampler:
        case CommandType::WavetableSynth:
        case CommandType::DrumSynth:
        case CommandType::DistortionFx:
        case CommandType::DelayFx:
        case CommandType::ChannelStripFx:
        case CommandType::FilterFx:
        case CommandType::ToggleAux1:
        case CommandType::ToggleAux2:
        case CommandType::FilterCutoff:
        case CommandType::ModulationMatrix:
            return nullptr;
        case CommandType::AuxSend1Fx: return &auxBus1;
        case CommandType::AuxSend2Fx: return &auxBus2;
    }
    return nullptr;
}

TrackerMainProcessor::SharedAuxBus* TrackerMainProcessor::getAuxBus(std::size_t auxIndex)
{
    switch (auxIndex)
    {
        case 0: return &auxBus1;
        case 1: return &auxBus2;
        default: return nullptr;
    }
}

const TrackerMainProcessor::SharedAuxBus* TrackerMainProcessor::getAuxBus(std::size_t auxIndex) const
{
    return const_cast<TrackerMainProcessor*>(this)->getAuxBus(auxIndex);
}
