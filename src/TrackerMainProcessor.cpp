
/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#include "TrackerMainProcessor.h"
#include "TrackerMainUI.h"
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

unsigned short getMidiChannelForStackId(std::size_t stackId)
{
    const std::size_t oneBasedId = stackId == 0 ? 1u : stackId;
    return static_cast<unsigned short>(juce::jlimit<std::size_t>(1u, 16u, oneBasedId));
}

bool isAudioSourceType(CommandType type)
{
    return type == CommandType::Sampler || type == CommandType::WavetableSynth;
}

bool isAudioEffectType(CommandType type)
{
    return type == CommandType::DistortionFx || type == CommandType::DelayFx;
}

float gainDbToLinear(float gainDb)
{
    return std::pow(10.0f, gainDb / 20.0f);
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
                                              unsigned short outDurTicks)
{
    const int samplesPerTickInt = static_cast<int>(samplesPerTick);
    const int onSample = elapsedSamples;
    const int offsetSamples = (samplesPerTickInt * static_cast<int>(outDurTicks)) % maxHorizon;
    const int offSample = onSample + offsetSamples;
    const int samplesSinceLast = onSample - lastQdOnAt; 
    lastQdOnAt = onSample;

    DBG("q-ing midi: delta since last note on " << samplesSinceLast << " on at " << onSample << " off at " << offSample);
    
    targetBuffer.addEvent(MidiMessage::noteOn((int)channel, (int)outNote, (uint8)outVelocity), onSample);
    targetBuffer.addEvent(MidiMessage::noteOff((int)channel, (int)outNote, (uint8)outVelocity), offSample);
    outstandingNoteOffs++;
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

        if (sequence->isMuted())
            continue;

        if (static_cast<std::size_t>(sequence->getMachineId()) == stackIndex)
            return true;
    }

    return false;
}

std::unique_ptr<Sequencer> TrackerMainProcessor::createDefaultSequenceSet()
{
    auto sequencer = std::make_unique<Sequencer>(16, 8);
    sequencer->stop();
    for (std::size_t seqIndex = 0; seqIndex < sequencer->howManySequences(); ++seqIndex)
        if (auto* sequence = sequencer->getSequence(seqIndex))
            sequence->setMachineId(static_cast<double>(seqIndex + 1));
    sequencer->requestStrUpdate();
    return sequencer;
}

void TrackerMainProcessor::resetSongState()
{
    sequenceSets.clear();
    sequenceSets.push_back(createDefaultSequenceSet());
    songRows.clear();
    songRows.push_back({ 0, 1 });
    songPlayMode = SongPlayMode::sequence;
    viewedSequenceSetIndex = 0;
    activePlaybackSequenceSetIndex = 0;
    pendingPlaybackSequenceSetIndex.reset();
    selectedSongRow = 0;
    currentSongRow = 0;
    currentSongRowRepeatIndex = 0;
    playbackAdvancedSinceRowStart = false;
    pendingTransportQuarterBeatReset = false;
    pendingTransportStartOnQuarterBeat = false;
    quarterBeatTicksAccumulator = 0;
    resetClockTicks();
    setCurrentQuarterBeat(0);
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
    activePlaybackSequenceSetIndex = std::min(index, sequenceSets.size() - 1);
    pendingPlaybackSequenceSetIndex.reset();
    playbackAdvancedSinceRowStart = false;
    if (previousPlaybackSequencer != nullptr)
        previousPlaybackSequencer->stop();
    if (songPlayMode == SongPlayMode::sequence)
        setViewedSequenceSetIndex(activePlaybackSequenceSetIndex);

    if (auto* playbackSequencer = getPlaybackSequencerInternal())
    {
        CommandProcessor::sendAllNotesOff();
        if (rewindNow)
        {
            playbackSequencer->stop();
            playbackSequencer->resetForTransportStart();
        }
        if (wasPlaying)
            playbackSequencer->play();
    }
}

void TrackerMainProcessor::applyPendingSequenceSetSwitchForCurrentQuarterBeat()
{
    if (!pendingPlaybackSequenceSetIndex.has_value() || ((getCurrentQuarterBeat() - 1) % 4) != 0)
        return;

    switchPlaybackSequenceSetImmediately(*pendingPlaybackSequenceSetIndex, false);
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

void TrackerMainProcessor::handleSongAdvanceAfterTick()
{
    auto* playbackSequencer = getPlaybackSequencerInternal();
    if (playbackSequencer == nullptr || !playbackSequencer->isPlaying() || songRows.empty())
        return;

    if (!playbackAdvancedSinceRowStart)
    {
        playbackAdvancedSinceRowStart = !isPlaybackSequencerAtBoundary();
        return;
    }

    if (!isPlaybackSequencerAtBoundary())
        return;

    playbackAdvancedSinceRowStart = false;
    if (songPlayMode == SongPlayMode::sequence)
        return;

    if (currentSongRow >= songRows.size())
        currentSongRow = 0;

    ++currentSongRowRepeatIndex;
    const int repeatCount = juce::jmax(1, songRows[currentSongRow].repeatCount);
    if (currentSongRowRepeatIndex < repeatCount)
        return;

    currentSongRowRepeatIndex = 0;
    currentSongRow = (currentSongRow + 1) % songRows.size();
    selectedSongRow = currentSongRow;
    schedulePlaybackSequenceSetSwitch(songRows[currentSongRow].sequenceSetId);
}

bool TrackerMainProcessor::stackContainsType(std::size_t stackIndex, CommandType machineType) const
{
    if (const auto* stack = getMachineStack(stackIndex))
    {
        const auto it = std::find(stack->order.begin(), stack->order.end(), machineType);
        if (it == stack->order.end())
            return false;
        if (const auto* enabled = getMachineEnabledFlag(*stack, machineType))
            return *enabled;
        return true;
    }
    return false;
}

std::optional<std::size_t> TrackerMainProcessor::findMachineInStack(std::size_t stackIndex, CommandType type) const
{
    if (const auto* stack = getMachineStack(stackIndex))
    {
        for (std::size_t i = 0; i < stack->order.size(); ++i)
            if (stack->order[i] == type)
                return i;
    }
    return std::nullopt;
}

void TrackerMainProcessor::allNotesOffForStack(std::size_t stackIndex)
{
    if (auto* stack = getMachineStack(stackIndex))
    {
        if (stack->arpeggiator != nullptr)
        {
            stack->arpeggiator->resetPlayback();
            stack->arpeggiator->setClockActive(false);
        }
        if (stack->polyArpeggiator != nullptr)
        {
            stack->polyArpeggiator->allNotesOff();
            stack->polyArpeggiator->setClockActive(false);
        }
        if (stack->wavetableSynth != nullptr)
            stack->wavetableSynth->allNotesOff();
        if (stack->delayFx != nullptr)
            stack->delayFx->allNotesOff();
        samplerEventsToSend.push_back({ stackIndex, MidiMessage::allNotesOff(1), elapsedSamples });
        midiToSend.addEvent(MidiMessage::allNotesOff(static_cast<int>(getMidiChannelForStackId(stackIndex))), elapsedSamples);
        stack->arpeggiatorClockActive = false;
    }
}

void TrackerMainProcessor::enqueueStackSamplerMidi(std::size_t stackIndex,
                                                   unsigned short outNote,
                                                   unsigned short outVelocity,
                                                   unsigned short outDurTicks)
{
    const int samplesPerTickInt = static_cast<int>(samplesPerTick);
    const int offsetSamples = (samplesPerTickInt * static_cast<int>(outDurTicks)) % maxHorizon;
    const int offSample = elapsedSamples + offsetSamples;
    samplerEventsToSend.push_back({ stackIndex, MidiMessage::noteOn(1, static_cast<int>(outNote), static_cast<uint8>(outVelocity)), elapsedSamples });
    samplerEventsToSend.push_back({ stackIndex, MidiMessage::noteOff(1, static_cast<int>(outNote), static_cast<uint8>(outVelocity)), offSample });
}

void TrackerMainProcessor::dispatchNoteThroughStack(std::size_t stackIndex,
                                                    unsigned short note,
                                                    unsigned short velocity,
                                                    unsigned short durInTicks,
                                                    std::size_t startSlotIndex)
{
    auto* stack = getMachineStack(stackIndex);
    if (stack == nullptr)
        return;

    bool anyTerminalTriggered = false;

    for (std::size_t slotIndex = startSlotIndex; slotIndex < stack->order.size(); ++slotIndex)
    {
        const auto slotType = stack->order[slotIndex];
        if (const auto* enabled = getMachineEnabledFlag(*stack, slotType))
            if (!*enabled)
                continue;

        switch (slotType)
        {
            case CommandType::MidiNote:
                enqueueMachineMidi(midiToSend,
                                   getMidiChannelForStackId(stackIndex),
                                   note,
                                   velocity,
                                   durInTicks);
                anyTerminalTriggered = true;
                break;
            case CommandType::Arpeggiator:
                if (stack->arpeggiator != nullptr)
                {
                    MachineNoteEvent outEvent;
                    if (stack->arpeggiator->handleIncomingNote(note, velocity, durInTicks, outEvent))
                        dispatchNoteThroughStack(stackIndex, outEvent.note, outEvent.velocity, outEvent.durationTicks, slotIndex + 1);
                }
                return;
            case CommandType::PolyArpeggiator:
                if (stack->polyArpeggiator != nullptr)
                {
                    MachineNoteEvent outEvent;
                    stack->polyArpeggiator->handleIncomingNote(note, velocity, durInTicks, outEvent);
                }
                return;
            case CommandType::Sampler:
                enqueueStackSamplerMidi(stackIndex, note, velocity, durInTicks);
                anyTerminalTriggered = true;
                break;
            case CommandType::WavetableSynth:
                if (stack->wavetableSynth != nullptr)
                {
                    MachineNoteEvent ignoredEvent;
                    stack->wavetableSynth->handleIncomingNote(note, velocity, durInTicks, ignoredEvent);
                }
                anyTerminalTriggered = true;
                break;
            case CommandType::DistortionFx:
            case CommandType::DelayFx:
            case CommandType::Log:
                break;
            default:
                break;
        }
    }

    if (!anyTerminalTriggered)
    {
        enqueueMachineMidi(midiToSend,
                           getMidiChannelForStackId(stackIndex),
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

        if (stack->arpeggiator != nullptr)
        {
            stack->arpeggiator->setClockEventCallback([this, i](const MachineNoteEvent& event)
            {
                emitClockedMachineEvent(i, CommandType::Arpeggiator, event);
            });
            ClockAbs::addListener(*stack->arpeggiator);
        }

        if (stack->polyArpeggiator != nullptr)
        {
            stack->polyArpeggiator->setClockEventCallback([this, i](const MachineNoteEvent& event)
            {
                emitClockedMachineEvent(i, CommandType::PolyArpeggiator, event);
            });
            ClockAbs::addListener(*stack->polyArpeggiator);
        }

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
    auto* playbackSequencer = getPlaybackSequencerInternal();
    const bool sequencerPlaying = playbackSequencer != nullptr && playbackSequencer->isPlaying();

    for (std::size_t i = 0; i < machineStacks.size(); ++i)
    {
        auto* stack = getMachineStack(i);
        if (stack == nullptr)
            continue;

        const bool stackAssigned = sequencerPlaying && isStackAssigned(i);
        const bool arpShouldBeActive = stackAssigned
            && stackContainsType(i, CommandType::Arpeggiator)
            && stack->arpeggiatorEnabled;
        const bool polyShouldBeActive = stackAssigned
            && stackContainsType(i, CommandType::PolyArpeggiator)
            && stack->polyArpeggiatorEnabled;

        if (stack->arpeggiator != nullptr)
            stack->arpeggiator->setClockActive(arpShouldBeActive);
        if (stack->polyArpeggiator != nullptr)
            stack->polyArpeggiator->setClockActive(polyShouldBeActive);
        stack->arpeggiatorClockActive = arpShouldBeActive || polyShouldBeActive;
    }
}

void TrackerMainProcessor::emitQuarterBeatTickIfNeeded()
{
    const int nextQuarterBeat = getCurrentQuarterBeat() <= 0
        ? 1
        : ((getCurrentQuarterBeat() % 16) + 1);
    const bool isBeatStart = ((nextQuarterBeat - 1) % 4) == 0;
    const bool shouldResetOnThisBoundary = pendingTransportQuarterBeatReset && isBeatStart;

    setCurrentQuarterBeat(nextQuarterBeat);
    if (shouldResetOnThisBoundary)
    {
        resetClockTicks();
        applyPendingSequenceSetSwitchForCurrentQuarterBeat();
        if (auto* playbackSequencer = getPlaybackSequencerInternal())
            playbackSequencer->resetForTransportStart();
        notifyClockReset();
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
        applyPendingSequenceSetSwitchForCurrentQuarterBeat();
    }

    updateClockedMachineActivity();
    notifyClockTick(getCurrentQuarterBeat());
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

    // sequencer.decrementSeqParam(0, 1);
    // sequencer.decrementSeqParam(0, 1);

    // put some test notes into the sequencer to see if they flow through
    initialiseOsc();
}

TrackerMainProcessor::~TrackerMainProcessor()
{
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
        DBG("OSC receiver failed to listen on port " << oscListenPort);
    else
        DBG("OSC receiver listening on port " << oscListenPort);

    oscSenderReady = oscSender.connect(lcdDisplayHost, lcdDisplayPort);
    if (!oscSenderReady)
        DBG("OSC sender failed to connect to " << lcdDisplayHost << ":" << lcdDisplayPort);
    else
        DBG("OSC sender connected to " << lcdDisplayHost << ":" << lcdDisplayPort);
}

void TrackerMainProcessor::initialiseMachines()
{
    removeClockListeners();
    machineStacks.clear();
    machineStacks.resize(kMachineStackCount);
    for (auto& stack : machineStacks)
    {
        stack.sampler = std::make_unique<SuperSamplerProcessor>();
        stack.arpeggiator = std::make_unique<ArpeggiatorMachine>();
        stack.polyArpeggiator = std::make_unique<PolyArpeggiatorMachine>();
        stack.wavetableSynth = std::make_unique<WavetableSynthMachine>();
        stack.distortionFx = std::make_unique<WaveshaperDistortionMachine>();
        stack.delayFx = std::make_unique<DelayFxMachine>();
        stack.order = { CommandType::MidiNote };
        stack.arpeggiatorClockActive = false;
        stack.gainDb = 0.0f;
        stack.meterLevel = 0.0f;
    }
    samplerEventsToSend.clear();

    const double secondsPerTick = getSecondsPerTickFromBpm(getBPM());
    for (auto& stack : machineStacks)
    {
        if (stack.wavetableSynth != nullptr)
            stack.wavetableSynth->setSecondsPerTick(secondsPerTick);
        if (stack.delayFx != nullptr)
            stack.delayFx->setSecondsPerTick(secondsPerTick);
    }
    configureClockListeners();
    updateClockedMachineActivity();
}

void TrackerMainProcessor::recreateSequencersAndMachines()
{
    suspendProcessing(true);
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

juce::String TrackerMainProcessor::getCurrentCellOscPayload()
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
                return "REP " + juce::String(songRows[rowIndex].repeatCount);
            if (songCol == 3)
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
            const auto grid = viewedSequencer->getSequenceConfigsAsGridOfStrings();
            const auto seq = seqEditor.getCurrentSequence();
            const auto param = seqEditor.getCurrentSeqParam();
            if (seq < grid.size() && param < grid[seq].size())
                return juce::String(grid[seq][param]);
            break;
        }
        case SequencerEditorMode::machineConfig:
        {
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
        case SequencerEditorMode::resetConfirmation:
            return "RESET?";
    }

    return {};
}

void TrackerMainProcessor::sendCurrentCellValueOverOscIfChanged()
{
    if (!oscSenderReady)
        return;

    const auto payload = getCurrentCellOscPayload().trim();
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
        const float delta = (address == zoomInAddress) ? 0.2f : -0.2f;
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

//==============================================================================
void TrackerMainProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    const double activeSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
    const double activeBpm = getBPM();
    samplesPerTick = static_cast<unsigned int> (juce::jmax (1, static_cast<int> (std::lround (activeSampleRate * (60.0 / activeBpm) / 8.0))));
    const double secondsPerTick = getSecondsPerTickFromBpm(activeBpm);

    for (auto& stack : machineStacks)
    {
        if (stack.sampler != nullptr)
            stack.sampler->prepareToPlay(sampleRate, samplesPerBlock);
        if (stack.arpeggiator != nullptr)
            stack.arpeggiator->prepareToPlay(sampleRate, samplesPerBlock);
        if (stack.polyArpeggiator != nullptr)
            stack.polyArpeggiator->prepareToPlay(sampleRate, samplesPerBlock);
        if (stack.wavetableSynth != nullptr)
        {
            stack.wavetableSynth->prepareToPlay(sampleRate, samplesPerBlock);
            stack.wavetableSynth->setSecondsPerTick(secondsPerTick);
        }
        if (stack.distortionFx != nullptr)
            stack.distortionFx->prepareToPlay(sampleRate, samplesPerBlock);
        if (stack.delayFx != nullptr)
        {
            stack.delayFx->prepareToPlay(sampleRate, samplesPerBlock);
            stack.delayFx->setSecondsPerTick(secondsPerTick);
        }
    }
    updateClockedMachineActivity();
}

void TrackerMainProcessor::releaseResources()
{
    // When playback stops, you can use this as an opportunity to free up any
    // spare memory, etc.
    for (auto& stack : machineStacks)
    {
        if (stack.sampler != nullptr)
            stack.sampler->releaseResources();
        if (stack.arpeggiator != nullptr)
            stack.arpeggiator->releaseResources();
        if (stack.polyArpeggiator != nullptr)
            stack.polyArpeggiator->releaseResources();
        if (stack.wavetableSynth != nullptr)
            stack.wavetableSynth->releaseResources();
        if (stack.distortionFx != nullptr)
            stack.distortionFx->releaseResources();
        if (stack.delayFx != nullptr)
            stack.delayFx->releaseResources();
    }
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

    const int blockSizeSamples = getBlockSize();
    int blockStartSample = elapsedSamples;
    int blockEndSample = (elapsedSamples + blockSizeSamples) % maxHorizon;
    bool usingHostClock = false;
    const bool useInternalClock = internalClockEnabled.load(std::memory_order_relaxed);

    if (!useInternalClock)
    {
        #if JUCE_MAJOR_VERSION >= 5
        juce::AudioPlayHead::CurrentPositionInfo posInfo;
        auto* playHead = getPlayHead();
        if (playHead != nullptr && playHead->getCurrentPosition(posInfo)
            && posInfo.bpm > 0.0 && posInfo.ppqPosition >= 0.0)
        {
            const bool hostPlaying = posInfo.isPlaying;
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
                setBPM(posInfo.bpm);

                const double ticksPerQuarter = 8.0;
                const double samplesPerTickDouble = getSampleRate() * (60.0 / posInfo.bpm) / ticksPerQuarter;
                double tickPosition = posInfo.ppqPosition * ticksPerQuarter;
                double tickPhase = std::fmod(tickPosition, 1.0);
                if (tickPhase < 0.0)
                    tickPhase += 1.0;

                if (!hostPpqValid || posInfo.ppqPosition < lastHostPpqPosition)
                {
                    // Transport restarted or jumped; re-sync tick phase from the new PPQ position.
                    hostPpqValid = true;
                }
                lastHostPpqPosition = posInfo.ppqPosition;

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
                    advanceClockTick();
                    emitQuarterBeatTickIfNeeded();
                    playbackSequencer = getPlaybackSequencerInternal();
                    // this will cause any pending messages to be added to 'midiToSend'
                    // and trigger any sample players
                    if (playbackSequencer != nullptr)
                    {
                        playbackSequencer->tick();
                        handleSongAdvanceAfterTick();
                        playbackSequencer = getPlaybackSequencerInternal();
                    }
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
        for (int i = 0; i < blockSizeSamples; ++i){
            // weird but since juce midi sample offsets are int not unsigned long, 
            // I set a maximum elapsedSamples and mod on that, instead of just elapsedSamples ++; forever
            // otherwise, behaviour after 13 hours is undefined (samples @441k you can fit in an int)
            elapsedSamples = (elapsedSamples + 1) % maxHorizon;
            if (samplesPerTickInt > 0 && (elapsedSamples % samplesPerTickInt) == 0){
                advanceClockTick();
                emitQuarterBeatTickIfNeeded();
                playbackSequencer = getPlaybackSequencerInternal();
                // this will cause any pending messages to be added to 'midiToSend'
                // and trigger any sample players
                if (playbackSequencer != nullptr)
                {
                    playbackSequencer->tick();
                    handleSongAdvanceAfterTick();
                    playbackSequencer = getPlaybackSequencerInternal();
                }
            }
        }
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
    
    juce::MidiBuffer futureMidi;    // store messages from midiToSend from the future here. 
    juce::MidiMessage message;
    // int samplePosition;
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
                    const int delta = metadata.samplePosition - lastSendOnAt; 

                    DBG("On Event delta: " << delta << "  blockStart " << blockStartSample << " at " << metadata.samplePosition - blockStartSample);
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
    midiToSend.clear();
    midiToSend.swapWith(futureMidi);

    // now set up the midi messages that 
    // we want to send to the internal sampler engines
    std::vector<ScheduledSamplerEvent> futureSamplerEvents;
    std::vector<juce::MidiBuffer> samplerMidiByStack(machineStacks.size());
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
            const std::size_t safeStackIndex = event.stackIndex < samplerMidiByStack.size() ? event.stackIndex : 0u;
            samplerMidiByStack[safeStackIndex].addEvent(event.message, sampleOffset);
        }
        else
        {
            futureSamplerEvents.push_back(event);
        }
    }
    samplerEventsToSend.swap(futureSamplerEvents);

    for (std::size_t i = 0; i < machineStacks.size(); ++i)
    {
        juce::AudioBuffer<float> stackBuffer(buffer.getNumChannels(), buffer.getNumSamples());
        stackBuffer.clear();

        juce::MidiBuffer emptyMidi;

        if (auto* stack = getMachineStack(i))
        {
            const bool samplerActive = std::find(stack->order.begin(), stack->order.end(), CommandType::Sampler) != stack->order.end()
                && stack->samplerEnabled;
            const bool wavetableActive = std::find(stack->order.begin(), stack->order.end(), CommandType::WavetableSynth) != stack->order.end()
                && stack->wavetableSynthEnabled;

            if (samplerActive && machineStacks[i].sampler != nullptr)
                machineStacks[i].sampler->processBlock(stackBuffer, samplerMidiByStack[i]);
            if (wavetableActive && machineStacks[i].wavetableSynth != nullptr)
                machineStacks[i].wavetableSynth->processBlock(stackBuffer, emptyMidi);

            for (const auto type : stack->order)
            {
                if (!isAudioEffectType(type))
                    continue;

                if (const auto* enabled = getMachineEnabledFlag(*stack, type))
                    if (!*enabled)
                        continue;

                if (auto* effect = dynamic_cast<AudioEffectMachine*>(getMachineForStackType(*stack, type)))
                    effect->processAudioBuffer(stackBuffer);
            }

            const float stackGainLinear = gainDbToLinear(stack->gainDb);
            stackBuffer.applyGain(stackGainLinear);

            const float meterTarget = linearToMeterNormalised(measureBufferRms(stackBuffer));
            const float attack = 0.65f;
            const float decay = 0.12f;
            const float smoothing = meterTarget > stack->meterLevel ? attack : decay;
            stack->meterLevel += (meterTarget - stack->meterLevel) * smoothing;
            stack->meterLevel = juce::jlimit(0.0f, 1.0f, stack->meterLevel);
        }

        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
            buffer.addFrom(channel, 0, stackBuffer, channel, 0, stackBuffer.getNumSamples());

        if (machineStacks[i].arpeggiator != nullptr && machineStacks[i].arpeggiatorEnabled)
            machineStacks[i].arpeggiator->processBlock(buffer, emptyMidi);
        if (machineStacks[i].polyArpeggiator != nullptr && machineStacks[i].polyArpeggiatorEnabled)
            machineStacks[i].polyArpeggiator->processBlock(buffer, emptyMidi);
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

void TrackerMainProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    withAudioThreadExclusive([&]()
    {
        const auto stateVar = serializeSequencerState();
        const auto json = juce::JSON::toString(stateVar);
        apvts.state.setProperty("json", json, nullptr);

        juce::MemoryOutputStream stream(destData, true);
        if (auto xml = apvts.copyState().createXml())
            xml->writeTo(stream);
    });
}

void TrackerMainProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    withAudioThreadExclusive([&]()
    {
        juce::MemoryInputStream input(data, static_cast<size_t>(sizeInBytes), false);
        if (auto xml = juce::parseXML(input.readEntireStreamAsString()))
        {
            auto vt = juce::ValueTree::fromXml(*xml);
            if (vt.isValid())
                apvts.replaceState(vt);
        }

        const juce::String json = apvts.state.getProperty("json").toString();
        if (json.isNotEmpty())
        {
            const auto parsed = juce::JSON::fromString(json);
            restoreSequencerState(parsed);
        }
    });
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

juce::var TrackerMainProcessor::getUiState()
{
    auto* viewedSequencer = getViewedSequencerInternal();
    auto* playbackSequencer = getPlaybackSequencerInternal();
    if (viewedSequencer == nullptr)
        return {};

    viewedSequencer->updateSeqStringGrid();

    juce::DynamicObject::Ptr state = new juce::DynamicObject();
    state->setProperty("bpm", getBPM());
    state->setProperty("isPlaying", playbackSequencer != nullptr && playbackSequencer->isPlaying());

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
        case SequencerEditorMode::resetConfirmation:
            modeStr = "reset";
            break;
    }
    state->setProperty("mode", modeStr);

    state->setProperty("currentSequence", static_cast<int>(seqEditor.getCurrentSequence()));
    state->setProperty("currentStep", static_cast<int>(seqEditor.getCurrentStep()));
    state->setProperty("currentStepRow", static_cast<int>(seqEditor.getCurrentStepRow()));
    state->setProperty("currentStepCol", static_cast<int>(seqEditor.getCurrentStepCol()));
    state->setProperty("armedSequence", static_cast<int>(seqEditor.getArmedSequence()));
    state->setProperty("currentSeqParam", static_cast<int>(seqEditor.getCurrentSeqParam()));

    state->setProperty("sequenceGrid", stringGridToVar(viewedSequencer->getSequenceAsGridOfStrings()));
    state->setProperty("stepGrid", stringGridToVar(viewedSequencer->getStepAsGridOfStrings(seqEditor.getCurrentSequence(), seqEditor.getCurrentStep())));
    state->setProperty("sequenceConfigs", stringGridToVar(viewedSequencer->getSequenceConfigsAsGridOfStrings()));
    state->setProperty("stepData", numberGridToVar(viewedSequencer->getStepData(seqEditor.getCurrentSequence(), seqEditor.getCurrentStep())));

    juce::Array<juce::var> playHeads;
    for (std::size_t col = 0; col < viewedSequencer->howManySequences(); ++col)
    {
        juce::DynamicObject::Ptr ph = new juce::DynamicObject();
        ph->setProperty("sequence", static_cast<int>(col));
        ph->setProperty("step", static_cast<int>(viewedSequencer->getCurrentStep(col)));
        playHeads.add(juce::var(ph));
    }
    state->setProperty("playHeads", playHeads);

    juce::Array<juce::var> seqLengths;
    for (std::size_t col = 0; col < viewedSequencer->howManySequences(); ++col)
        seqLengths.add(static_cast<int>(viewedSequencer->howManySteps(col)));
    state->setProperty("sequenceLengths", seqLengths);

    Sequence* currentSequence = viewedSequencer->getSequence(seqEditor.getCurrentSequence());
    state->setProperty("machineId", currentSequence->getMachineId());
    state->setProperty("machineType", currentSequence->getMachineType());
    state->setProperty("triggerProbability", currentSequence->getTriggerProbability());

    state->setProperty("ticksPerStep", static_cast<int>(viewedSequencer->getSequence(seqEditor.getCurrentSequence())->getTicksPerStep()));

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
        seqObj->setProperty("ticksPerStep", static_cast<int>(seq->getTicksPerStep()));
        seqObj->setProperty("muted", seq->isMuted());
        seqObj->setProperty("machineId", seq->getMachineId());
        seqObj->setProperty("machineType", seq->getMachineType());
        seqObj->setProperty("triggerProbability", seq->getTriggerProbability());

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
    const auto seqCount = juce::jmin<std::size_t>(static_cast<std::size_t>(seqArray.size()), target.howManySequences());
    for (std::size_t i = 0; i < seqCount; ++i)
    {
        const auto& seqObj = seqArray[static_cast<int>(i)];
        if (!seqObj.isObject())
            continue;

        Sequence* seq = target.getSequence(i);
        const int length = juce::jmax(1, static_cast<int>(seqObj.getProperty("length", static_cast<int>(seq->getLength()))));
        seq->ensureEnoughStepsForLength(static_cast<std::size_t>(length));
        seq->setLength(static_cast<std::size_t>(length));

        const auto typeInt = static_cast<int>(seqObj.getProperty("type", static_cast<int>(seq->getType())));
        seq->setType(static_cast<SequenceType>(typeInt));

        const std::size_t tps = static_cast<std::size_t>(static_cast<int>(seqObj.getProperty("ticksPerStep", static_cast<int>(seq->getTicksPerStep()))));
        seq->setTicksPerStep(tps);
        seq->onZeroSetTicksPerStep(tps);

        const double machineId = static_cast<double>(seqObj.getProperty("machineId", seq->getMachineId()));
        const double machineType = static_cast<double>(seqObj.getProperty("machineType", seq->getMachineType()));
        const double triggerProbability = static_cast<double>(seqObj.getProperty("triggerProbability", seq->getTriggerProbability()));

        const auto stepsVar = seqObj.getProperty("steps", juce::var());
        if (stepsVar.isArray())
        {
            const auto& stepsArray = *stepsVar.getArray();
            const auto stepsToLoad = juce::jmin<std::size_t>(static_cast<std::size_t>(stepsArray.size()), static_cast<std::size_t>(length));
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
                        if (!row.empty())
                        {
                            if (row.size() == Step::maxInd + 2)
                            {
                                std::vector<double> remapped = { row[Step::cmdInd], row[2], row[3], row[4], row[5] };
                                data.push_back(std::move(remapped));
                            }
                            else
                            {
                                if (row.size() < Step::maxInd + 1)
                                    row.resize(Step::maxInd + 1, 0.0);
                                if (row.size() > Step::maxInd + 1)
                                    row.resize(Step::maxInd + 1);
                                data.push_back(std::move(row));
                            }
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
        seq->setTriggerProbability(triggerProbability);

        const bool mutedTarget = static_cast<bool>(seqObj.getProperty("muted", false));
        if (seq->isMuted() != mutedTarget)
            target.toggleSequenceMute(i);
    }
}

juce::var TrackerMainProcessor::serializeSequencerState()
{
    juce::DynamicObject::Ptr root = new juce::DynamicObject();
    juce::Array<juce::var> sequenceSetStates;
    for (const auto& sequenceSet : sequenceSets)
        if (sequenceSet != nullptr)
            sequenceSetStates.add(serializeSingleSequencer(*sequenceSet));
    root->setProperty("sequenceSets", sequenceSetStates);
    root->setProperty("viewedSequenceSetIndex", static_cast<int>(viewedSequenceSetIndex));
    root->setProperty("activePlaybackSequenceSetIndex", static_cast<int>(activePlaybackSequenceSetIndex));
    root->setProperty("selectedSongRow", static_cast<int>(selectedSongRow));
    root->setProperty("currentSongRow", static_cast<int>(currentSongRow));
    root->setProperty("currentSongRowRepeatIndex", currentSongRowRepeatIndex);
    root->setProperty("songPlayMode", songPlayMode == SongPlayMode::song ? "song" : "sequence");
    root->setProperty("currentSequence", static_cast<int>(seqEditor.getCurrentSequence()));
    root->setProperty("currentStep", static_cast<int>(seqEditor.getCurrentStep()));
    root->setProperty("currentStepRow", static_cast<int>(seqEditor.getCurrentStepRow()));
    root->setProperty("currentStepCol", static_cast<int>(seqEditor.getCurrentStepCol()));
    root->setProperty("currentSongRowCursor", static_cast<int>(seqEditor.getCurrentSongRow()));
    root->setProperty("currentSongColCursor", static_cast<int>(seqEditor.getCurrentSongCol()));

    juce::String modeStr = "sequence";
    const auto uiState = getUiState();
    if (uiState.isObject())
        modeStr = uiState.getProperty("mode", "sequence").toString();
    root->setProperty("mode", modeStr);

    juce::Array<juce::var> songRowsVar;
    for (const auto& row : songRows)
    {
        juce::DynamicObject::Ptr rowObj = new juce::DynamicObject();
        rowObj->setProperty("sequenceSetId", static_cast<int>(row.sequenceSetId));
        rowObj->setProperty("repeatCount", row.repeatCount);
        songRowsVar.add(rowObj.get());
    }
    root->setProperty("songRows", songRowsVar);

    juce::Array<juce::var> stackStates;
    for (auto& stack : machineStacks)
    {
        juce::DynamicObject::Ptr stackObj = new juce::DynamicObject();
        juce::Array<juce::var> order;
        for (const auto type : stack.order)
            order.add(static_cast<int>(type));
        stackObj->setProperty("order", order);
        stackObj->setProperty("gainDb", stack.gainDb);
        stackObj->setProperty("samplerEnabled", stack.samplerEnabled);
        stackObj->setProperty("arpeggiatorEnabled", stack.arpeggiatorEnabled);
        stackObj->setProperty("polyArpeggiatorEnabled", stack.polyArpeggiatorEnabled);
        stackObj->setProperty("wavetableSynthEnabled", stack.wavetableSynthEnabled);
        stackObj->setProperty("distortionFxEnabled", stack.distortionFxEnabled);
        stackObj->setProperty("delayFxEnabled", stack.delayFxEnabled);

        auto encodeMachineState = [](MachineInterface* machine)
        {
            if (machine == nullptr)
                return juce::String();
            juce::MemoryBlock state;
            machine->getStateInformation(state);
            return juce::Base64::toBase64(state.getData(), state.getSize());
        };

        stackObj->setProperty("sampler", encodeMachineState(stack.sampler.get()));
        stackObj->setProperty("arpeggiator", encodeMachineState(stack.arpeggiator.get()));
        stackObj->setProperty("polyArpeggiator", encodeMachineState(stack.polyArpeggiator.get()));
        stackObj->setProperty("wavetableSynth", encodeMachineState(stack.wavetableSynth.get()));
        stackObj->setProperty("distortionFx", encodeMachineState(stack.distortionFx.get()));
        stackObj->setProperty("delayFx", encodeMachineState(stack.delayFx.get()));
        stackStates.add(stackObj.get());
    }
    root->setProperty("machineStacks", stackStates);

    return root.get();
}

void TrackerMainProcessor::restoreSequencerState(const juce::var& stateVar)
{
    if (!stateVar.isObject())
        return;

    resetSongState();

    const auto sequenceSetsVar = stateVar.getProperty("sequenceSets", juce::var());
    if (sequenceSetsVar.isArray() && !sequenceSetsVar.getArray()->isEmpty())
    {
        sequenceSets.clear();
        for (const auto& sequenceSetVar : *sequenceSetsVar.getArray())
        {
            auto sequenceSet = createDefaultSequenceSet();
            restoreSingleSequencer(*sequenceSet, sequenceSetVar);
            sequenceSets.push_back(std::move(sequenceSet));
        }
    }
    else
    {
        const auto legacySeqVar = stateVar.getProperty("sequencer", juce::var());
        if (legacySeqVar.isObject())
            restoreSingleSequencer(*sequenceSets.front(), legacySeqVar);
    }

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
            row.repeatCount = juce::jmax(1, static_cast<int>(rowVar.getProperty("repeatCount", 1)));
            if (!sequenceSets.empty())
                row.sequenceSetId = std::min(row.sequenceSetId, sequenceSets.size() - 1);
            songRows.push_back(row);
        }
    }
    if (songRows.empty())
        songRows.push_back({ 0, 1 });

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
    currentSongRowRepeatIndex = juce::jmax(0, static_cast<int>(stateVar.getProperty("currentSongRowRepeatIndex", 0)));
    songPlayMode = stateVar.getProperty("songPlayMode", "sequence").toString().equalsIgnoreCase("song")
        ? SongPlayMode::song
        : SongPlayMode::sequence;
    pendingPlaybackSequenceSetIndex.reset();
    playbackAdvancedSinceRowStart = false;

    bindViewedSequenceSetToEditor();
    auto* viewedSequencer = getViewedSequencerInternal();
    if (viewedSequencer == nullptr)
        return;

    int seq = static_cast<int>(stateVar.getProperty("currentSequence", static_cast<int>(seqEditor.getCurrentSequence())));
    const int maxSeq = static_cast<int>(juce::jmax<std::size_t>(1, viewedSequencer->howManySequences())) - 1;
    seq = juce::jlimit(0, juce::jmax(0, maxSeq), seq);
    seqEditor.setCurrentSequence(seq);

    int step = static_cast<int>(stateVar.getProperty("currentStep", static_cast<int>(seqEditor.getCurrentStep())));
    const int maxStep = static_cast<int>(juce::jmax<std::size_t>(1, viewedSequencer->howManySteps(static_cast<std::size_t>(seq)))) - 1;
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
    else
        seqEditor.setEditMode(SequencerEditorMode::selectingSeqAndStep);

    const auto machineStacksVar = stateVar.getProperty("machineStacks", juce::var());
    if (machineStacksVar.isArray())
    {
        const auto& stackArray = *machineStacksVar.getArray();
        const auto stackCount = juce::jmin<std::size_t>(static_cast<std::size_t>(stackArray.size()), machineStacks.size());
        for (std::size_t i = 0; i < stackCount; ++i)
        {
            if (!stackArray[static_cast<int>(i)].isObject())
                continue;
            auto& stack = machineStacks[i];
            const auto orderVar = stackArray[static_cast<int>(i)].getProperty("order", juce::var());
            stack.order.clear();
            if (orderVar.isArray())
            {
                for (const auto& orderEntry : *orderVar.getArray())
                {
                    const auto type = static_cast<CommandType>(static_cast<int>(orderEntry));
                    if (type == CommandType::MidiNote
                        || type == CommandType::Sampler
                        || type == CommandType::Arpeggiator
                        || type == CommandType::PolyArpeggiator
                        || type == CommandType::WavetableSynth
                        || type == CommandType::DistortionFx
                        || type == CommandType::DelayFx)
                        stack.order.push_back(type);
                }
            }
            if (stack.order.empty())
                stack.order.push_back(CommandType::MidiNote);
            stack.gainDb = juce::jlimit(-48.0f, 6.0f,
                                        static_cast<float>(stackArray[static_cast<int>(i)].getProperty("gainDb", stack.gainDb)));
            stack.meterLevel = 0.0f;
            stack.samplerEnabled = static_cast<bool>(stackArray[static_cast<int>(i)].getProperty("samplerEnabled", stack.samplerEnabled));
            stack.arpeggiatorEnabled = static_cast<bool>(stackArray[static_cast<int>(i)].getProperty("arpeggiatorEnabled", stack.arpeggiatorEnabled));
            stack.polyArpeggiatorEnabled = static_cast<bool>(stackArray[static_cast<int>(i)].getProperty("polyArpeggiatorEnabled", stack.polyArpeggiatorEnabled));
            stack.wavetableSynthEnabled = static_cast<bool>(stackArray[static_cast<int>(i)].getProperty("wavetableSynthEnabled", stack.wavetableSynthEnabled));
            stack.distortionFxEnabled = static_cast<bool>(stackArray[static_cast<int>(i)].getProperty("distortionFxEnabled", stack.distortionFxEnabled));
            stack.delayFxEnabled = static_cast<bool>(stackArray[static_cast<int>(i)].getProperty("delayFxEnabled", stack.delayFxEnabled));

            auto decodeMachineState = [](const juce::var& encodedVar, MachineInterface* machine)
            {
                if (machine == nullptr)
                    return;
                const auto encoded = encodedVar.toString();
                if (encoded.isEmpty())
                    return;
                juce::MemoryBlock state;
                juce::MemoryOutputStream stream(state, false);
                if (!juce::Base64::convertFromBase64(stream, encoded))
                    return;
                if (state.getSize() == 0)
                    return;
                machine->setStateInformation(state.getData(), static_cast<int>(state.getSize()));
            };

            decodeMachineState(stackArray[static_cast<int>(i)].getProperty("sampler", juce::var()), stack.sampler.get());
            decodeMachineState(stackArray[static_cast<int>(i)].getProperty("arpeggiator", juce::var()), stack.arpeggiator.get());
            decodeMachineState(stackArray[static_cast<int>(i)].getProperty("polyArpeggiator", juce::var()), stack.polyArpeggiator.get());
            decodeMachineState(stackArray[static_cast<int>(i)].getProperty("wavetableSynth", juce::var()), stack.wavetableSynth.get());
            decodeMachineState(stackArray[static_cast<int>(i)].getProperty("distortionFx", juce::var()), stack.distortionFx.get());
            decodeMachineState(stackArray[static_cast<int>(i)].getProperty("delayFx", juce::var()), stack.delayFx.get());
        }
    }

    // syncSequenceStrings();
    
    viewedSequencer->updateSeqStringGrid();

    // msSinceLastStateUpdate = stateUpdateIntervalMs;
    // {
    //     const juce::SpinLock::ScopedLockType lock(stateLock);
    //     latestStateJson = juce::JSON::toString(getUiState());
    //     stateDirty.store(true, std::memory_order_release);
    // }
    sendChangeMessage();
}

////// end of state saving and restoring stuff 
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
    if (songPlayMode == SongPlayMode::sequence)
    {
        if (auto* playbackSequencer = getPlaybackSequencerInternal())
        {
            if (playbackSequencer->isPlaying())
            {
                currentSongRow = selectedSongRow;
                currentSongRowRepeatIndex = 0;
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

int TrackerMainProcessor::getSongRowRepeatCount(std::size_t row) const
{
    if (row >= songRows.size())
        return 1;
    return songRows[row].repeatCount;
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
    sequenceSets.push_back(std::move(newSequenceSet));
    const std::size_t newSetIndex = sequenceSets.size() - 1;
    songRows.push_back({ newSetIndex, 1 });
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
}

void TrackerMainProcessor::adjustSongRowRepeatCount(std::size_t row, int direction)
{
    if (row >= songRows.size() || direction == 0)
        return;
    songRows[row].repeatCount = juce::jmax(1, songRows[row].repeatCount + direction);
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
        playbackAdvancedSinceRowStart = false;
        pendingTransportQuarterBeatReset = false;
        pendingTransportStartOnQuarterBeat = false;
        updateClockedMachineActivity();
        return;
    }

    currentSongRow = std::min(selectedSongRow, songRows.size() - 1);
    currentSongRowRepeatIndex = 0;
    playbackAdvancedSinceRowStart = false;
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
    currentSongRowRepeatIndex = 0;
    playbackAdvancedSinceRowStart = false;
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
            return 0;
        case CommandType::Sampler:
        case CommandType::Arpeggiator:
        case CommandType::PolyArpeggiator:
        case CommandType::WavetableSynth:
        case CommandType::DistortionFx:
        case CommandType::DelayFx:
            return machineStacks.size();
        default:
            return 0;
    }
}

MachineInterface* TrackerMainProcessor::getMachine(CommandType type, std::size_t index)
{
    if (auto* stack = getMachineStack(index))
        return getMachineForStackType(*stack, type);
    return nullptr;
}

const MachineInterface* TrackerMainProcessor::getMachine(CommandType type, std::size_t index) const
{
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
        return stack->order;
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

void TrackerMainProcessor::setStackGainDb(std::size_t stackIndex, float gainDb)
{
    if (auto* stack = getMachineStack(stackIndex))
        stack->gainDb = juce::jlimit(-48.0f, 6.0f, gainDb);
}

void TrackerMainProcessor::addMachineToStack(std::size_t stackIndex)
{
    if (auto* stack = getMachineStack(stackIndex))
    {
        const std::vector<CommandType> cycle = {
            CommandType::MidiNote,
            CommandType::WavetableSynth,
            CommandType::Sampler,
            CommandType::Arpeggiator,
            CommandType::PolyArpeggiator,
            CommandType::DistortionFx,
            CommandType::DelayFx
        };
        for (const auto type : cycle)
        {
            if (std::find(stack->order.begin(), stack->order.end(), type) == stack->order.end())
            {
                stack->order.push_back(type);
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
        if (slotIndex < stack->order.size())
        {
            stack->order.erase(stack->order.begin() + static_cast<long>(slotIndex));
            updateClockedMachineActivity();
        }
    }
}

void TrackerMainProcessor::cycleMachineTypeInStack(std::size_t stackIndex, std::size_t slotIndex, int direction)
{
    if (auto* stack = getMachineStack(stackIndex))
    {
        if (slotIndex >= stack->order.size())
            return;

        const std::vector<CommandType> cycle = {
            CommandType::MidiNote,
            CommandType::WavetableSynth,
            CommandType::Sampler,
            CommandType::Arpeggiator,
            CommandType::PolyArpeggiator,
            CommandType::DistortionFx,
            CommandType::DelayFx
        };

        auto currentIt = std::find(cycle.begin(), cycle.end(), stack->order[slotIndex]);
        if (currentIt == cycle.end())
            return;

        int currentIndex = static_cast<int>(std::distance(cycle.begin(), currentIt));
        for (int attempt = 0; attempt < static_cast<int>(cycle.size()); ++attempt)
        {
            currentIndex = (currentIndex + direction + static_cast<int>(cycle.size())) % static_cast<int>(cycle.size());
            const auto candidate = cycle[static_cast<std::size_t>(currentIndex)];
            const bool duplicate = std::find(stack->order.begin(), stack->order.end(), candidate) != stack->order.end();
            if (!duplicate || candidate == stack->order[slotIndex])
            {
                stack->order[slotIndex] = candidate;
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
        if (slotIndex >= stack->order.size() || direction == 0)
            return;

        const int targetIndex = juce::jlimit(0,
                                             static_cast<int>(stack->order.size()) - 1,
                                             static_cast<int>(slotIndex) + (direction < 0 ? -1 : 1));
        if (targetIndex == static_cast<int>(slotIndex))
            return;

        std::swap(stack->order[slotIndex], stack->order[static_cast<std::size_t>(targetIndex)]);
    }
}

bool TrackerMainProcessor::isMachineEnabledInStack(std::size_t stackIndex, std::size_t slotIndex) const
{
    if (const auto* stack = getMachineStack(stackIndex))
    {
        if (slotIndex >= stack->order.size())
            return false;
        if (const auto* enabled = getMachineEnabledFlag(*stack, stack->order[slotIndex]))
            return *enabled;
        return true;
    }
    return false;
}

void TrackerMainProcessor::toggleMachineEnabledInStack(std::size_t stackIndex, std::size_t slotIndex)
{
    if (auto* stack = getMachineStack(stackIndex))
    {
        if (slotIndex >= stack->order.size())
            return;
        if (auto* enabled = getMachineEnabledFlag(*stack, stack->order[slotIndex]))
        {
            *enabled = !*enabled;
            updateClockedMachineActivity();
        }
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
    for (std::size_t i = 0; i < machineStacks.size(); ++i)
        allNotesOffForStack(i);
}

std::string TrackerMainProcessor::describeStepNote(CommandType machineType, unsigned short machineId, unsigned short note) const
{
    if (note == 0)
        return "----";

    if (machineType == CommandType::MidiNote
        || machineType == CommandType::Sampler
        || machineType == CommandType::Arpeggiator
        || machineType == CommandType::PolyArpeggiator
        || machineType == CommandType::WavetableSynth
        || machineType == CommandType::DistortionFx
        || machineType == CommandType::DelayFx)
    {
        const auto stackIndex = static_cast<std::size_t>(machineId);
        if (const auto* stack = getMachineStack(stackIndex))
        {
            if (!stack->order.empty() && stack->order.back() == CommandType::Sampler)
            {
                if (const auto* sampler = dynamic_cast<const SuperSamplerProcessor*>(getMachine(CommandType::Sampler, stackIndex)))
                    return sampler->describeNoteForSequencer(static_cast<int>(note));
            }
        }
    }

    return formatMidiNoteLabel(note);
}

void TrackerMainProcessor::sendMessageToMachine(CommandType machineType, unsigned short machineId, unsigned short note, unsigned short velocity, unsigned short durInTicks)
{
    if (machineType == CommandType::MidiNote
        || machineType == CommandType::Arpeggiator
        || machineType == CommandType::PolyArpeggiator
        || machineType == CommandType::Sampler
        || machineType == CommandType::WavetableSynth
        || machineType == CommandType::DistortionFx
        || machineType == CommandType::DelayFx)
    {
        dispatchNoteThroughStack(static_cast<std::size_t>(machineId), note, velocity, durInTicks);
        return;
    }
    enqueueMachineMidi(midiToSend,
                       getMidiChannelForStackId(machineId),
                       note,
                       velocity,
                       durInTicks);
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
    {
        if (stack.wavetableSynth != nullptr)
            stack.wavetableSynth->setSecondsPerTick(secondsPerTick);
        if (stack.delayFx != nullptr)
            stack.delayFx->setSecondsPerTick(secondsPerTick);
    }
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

MachineInterface* TrackerMainProcessor::getMachineForStackType(MachineStack& stack, CommandType type)
{
    switch (type)
    {
        case CommandType::MidiNote:
        case CommandType::Log:
            return nullptr;
        case CommandType::Sampler: return stack.sampler.get();
        case CommandType::Arpeggiator: return stack.arpeggiator.get();
        case CommandType::PolyArpeggiator: return stack.polyArpeggiator.get();
        case CommandType::WavetableSynth: return stack.wavetableSynth.get();
        case CommandType::DistortionFx: return stack.distortionFx.get();
        case CommandType::DelayFx: return stack.delayFx.get();
        default: return nullptr;
    }
}

const MachineInterface* TrackerMainProcessor::getMachineForStackType(const MachineStack& stack, CommandType type) const
{
    switch (type)
    {
        case CommandType::MidiNote:
        case CommandType::Log:
            return nullptr;
        case CommandType::Sampler: return stack.sampler.get();
        case CommandType::Arpeggiator: return stack.arpeggiator.get();
        case CommandType::PolyArpeggiator: return stack.polyArpeggiator.get();
        case CommandType::WavetableSynth: return stack.wavetableSynth.get();
        case CommandType::DistortionFx: return stack.distortionFx.get();
        case CommandType::DelayFx: return stack.delayFx.get();
        default: return nullptr;
    }
}

bool* TrackerMainProcessor::getMachineEnabledFlag(MachineStack& stack, CommandType type)
{
    switch (type)
    {
        case CommandType::MidiNote:
        case CommandType::Log:
            return nullptr;
        case CommandType::Sampler: return &stack.samplerEnabled;
        case CommandType::Arpeggiator: return &stack.arpeggiatorEnabled;
        case CommandType::PolyArpeggiator: return &stack.polyArpeggiatorEnabled;
        case CommandType::WavetableSynth: return &stack.wavetableSynthEnabled;
        case CommandType::DistortionFx: return &stack.distortionFxEnabled;
        case CommandType::DelayFx: return &stack.delayFxEnabled;
        default: return nullptr;
    }
}

const bool* TrackerMainProcessor::getMachineEnabledFlag(const MachineStack& stack, CommandType type) const
{
    switch (type)
    {
        case CommandType::MidiNote:
        case CommandType::Log:
            return nullptr;
        case CommandType::Sampler: return &stack.samplerEnabled;
        case CommandType::Arpeggiator: return &stack.arpeggiatorEnabled;
        case CommandType::PolyArpeggiator: return &stack.polyArpeggiatorEnabled;
        case CommandType::WavetableSynth: return &stack.wavetableSynthEnabled;
        case CommandType::DistortionFx: return &stack.distortionFxEnabled;
        case CommandType::DelayFx: return &stack.delayFxEnabled;
        default: return nullptr;
    }
}
