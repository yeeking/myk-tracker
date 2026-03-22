
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
}

void TrackerMainProcessor::enqueueMachineMidi(juce::MidiBuffer& targetBuffer,
                                              unsigned short channel,
                                              unsigned short outNote,
                                              unsigned short outVelocity,
                                              unsigned short outDurTicks)
{
    const int samplesPerTickInt = static_cast<int>(samplesPerTick);
    const int offsetSamples = (samplesPerTickInt * static_cast<int>(outDurTicks)) % maxHorizon;
    const int offSample = elapsedSamples + offsetSamples;
    targetBuffer.addEvent(MidiMessage::noteOn((int)channel, (int)outNote, (uint8)outVelocity), elapsedSamples);
    targetBuffer.addEvent(MidiMessage::noteOff((int)channel, (int)outNote, (uint8)outVelocity), offSample);
    outstandingNoteOffs++;
}

bool TrackerMainProcessor::isStackAssigned(std::size_t stackIndex)
{
    for (std::size_t seq = 0; seq < sequencer.howManySequences(); ++seq)
    {
        const auto* sequence = sequencer.getSequence(seq);
        if (sequence == nullptr)
            continue;

        if (sequence->isMuted())
            continue;

        if (static_cast<std::size_t>(sequence->getMachineId()) == stackIndex)
            return true;
    }

    return false;
}

bool TrackerMainProcessor::stackContainsType(std::size_t stackIndex, CommandType machineType) const
{
    if (const auto* stack = getMachineStack(stackIndex))
        return std::find(stack->order.begin(), stack->order.end(), machineType) != stack->order.end();
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
            stack->arpeggiator->resetPlayback();
        if (stack->wavetableSynth != nullptr)
            stack->wavetableSynth->allNotesOff();
        samplerEventsToSend.push_back({ stackIndex, MidiMessage::allNotesOff(1), elapsedSamples });
        midiToSend.addEvent(MidiMessage::allNotesOff(static_cast<int>(getMidiChannelForStackId(stackIndex))), elapsedSamples);
        stack->arpeggiatorClockActive = false;
    }
}

void TrackerMainProcessor::deactivateStackArpeggiator(std::size_t stackIndex)
{
    if (auto* stack = getMachineStack(stackIndex))
    {
        if (!stack->arpeggiatorClockActive)
            return;
        allNotesOffForStack(stackIndex);
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

    if (startSlotIndex >= stack->order.size())
    {
        enqueueMachineMidi(midiToSend,
                           getMidiChannelForStackId(stackIndex),
                           note,
                           velocity,
                           durInTicks);
        return;
    }

    switch (stack->order[startSlotIndex])
    {
        case CommandType::MidiNote:
            enqueueMachineMidi(midiToSend,
                               getMidiChannelForStackId(stackIndex),
                               note,
                               velocity,
                               durInTicks);
            return;
        case CommandType::Arpeggiator:
        {
            if (stack->arpeggiator != nullptr)
            {
                MachineNoteEvent outEvent;
                if (stack->arpeggiator->handleIncomingNote(note, velocity, durInTicks, outEvent))
                    dispatchNoteThroughStack(stackIndex, outEvent.note, outEvent.velocity, outEvent.durationTicks, startSlotIndex + 1);
            }
            return;
        }
        case CommandType::Sampler:
            enqueueStackSamplerMidi(stackIndex, note, velocity, durInTicks);
            return;
        case CommandType::WavetableSynth:
        {
            if (stack->wavetableSynth != nullptr)
            {
                MachineNoteEvent ignoredEvent;
                stack->wavetableSynth->handleIncomingNote(note, velocity, durInTicks, ignoredEvent);
            }
            return;
        }
        default:
            dispatchNoteThroughStack(stackIndex, note, velocity, durInTicks, startSlotIndex + 1);
            return;
    }
}

void TrackerMainProcessor::tickMachineClocks()
{
    const bool sequencerPlaying = sequencer.isPlaying();
    for (std::size_t i = 0; i < machineStacks.size(); ++i)
    {
        const bool shouldBeActive = sequencerPlaying && isStackAssigned(i) && stackContainsType(i, CommandType::Arpeggiator);
        if (!shouldBeActive)
        {
            deactivateStackArpeggiator(i);
            continue;
        }

        auto* stack = getMachineStack(i);
        if (stack == nullptr || stack->arpeggiator == nullptr)
            continue;

        stack->arpeggiatorClockActive = true;

        MachineNoteEvent outEvent;
        if (!stack->arpeggiator->handleClockTick(outEvent))
            continue;
        const auto arpIndex = findMachineInStack(i, CommandType::Arpeggiator);
        dispatchNoteThroughStack(i,
                                 outEvent.note,
                                 outEvent.velocity,
                                 outEvent.durationTicks,
                                 arpIndex.has_value() ? (*arpIndex + 1) : 0);
    }
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
                       sequencer{16, 8}, seqEditor{&sequencer},
                       // seq, clock, editor
                       trackerController{&sequencer, this, &seqEditor},
                       elapsedSamples{0}, maxHorizon{44100 * 3600},
                       samplesPerTick{44100/(120/60)/8}, bpm{120.0},
                       outstandingNoteOffs{0},
                       apvts(*this, nullptr, "params", createParameterLayout())
#endif
{
    
    CommandProcessor::assignMasterClock(this);
    CommandProcessor::assignMachineUtils(this);
    initialiseMachines();
    for (std::size_t seqIndex = 0; seqIndex < sequencer.howManySequences(); ++seqIndex)
        if (auto* sequence = sequencer.getSequence(seqIndex))
            sequence->setMachineId(static_cast<double>(seqIndex + 1));
    seqEditor.setMachineHost(this);
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
    machineStacks.clear();
    machineStacks.resize(kMachineStackCount);
    for (auto& stack : machineStacks)
    {
        stack.sampler = std::make_unique<SuperSamplerProcessor>();
        stack.arpeggiator = std::make_unique<ArpeggiatorMachine>();
        stack.wavetableSynth = std::make_unique<WavetableSynthMachine>();
        stack.order = { CommandType::MidiNote };
        stack.arpeggiatorClockActive = false;
    }
    samplerEventsToSend.clear();

    const double secondsPerTick = getSecondsPerTickFromBpm(getBPM());
    for (auto& stack : machineStacks)
        if (stack.wavetableSynth != nullptr)
            stack.wavetableSynth->setSecondsPerTick(secondsPerTick);
}

void TrackerMainProcessor::recreateSequencersAndMachines()
{
    suspendProcessing(true);
    CommandProcessor::sendAllNotesOff();
    sequencer.stop();
    clearPendingEvents();
    resetTicks();
    Sequencer newSequencer{16, 8};
    sequencer = std::move(newSequencer);
    seqEditor.setSequencer(&sequencer);
    seqEditor.resetCursor();
    seqEditor.gotoSequencePage();
    seqEditor.setMachineHost(this);
    seqEditor.setResetConfirmationHandler([this]()
    {
        recreateSequencersAndMachines();
    });
    trackerController = TrackerController{&sequencer, this, &seqEditor};
    initialiseMachines();
    for (std::size_t seqIndex = 0; seqIndex < sequencer.howManySequences(); ++seqIndex)
        if (auto* sequence = sequencer.getSequence(seqIndex))
            sequence->setMachineId(static_cast<double>(seqIndex + 1));
    sequencer.requestStrUpdate();
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

    switch (mode)
    {
        case SequencerEditorMode::selectingSeqAndStep:
        {
            auto& grid = sequencer.getSequenceAsGridOfStrings();
            const auto seq = seqEditor.getCurrentSequence();
            const auto step = seqEditor.getCurrentStep();
            if (seq < grid.size() && step < grid[seq].size())
                return juce::String(grid[seq][step]);
            break;
        }
        case SequencerEditorMode::editingStep:
        {
            const auto grid = sequencer.getStepAsGridOfStrings(seqEditor.getCurrentSequence(),
                                                               seqEditor.getCurrentStep());
            const auto col = seqEditor.getCurrentStepCol();
            const auto row = seqEditor.getCurrentStepRow();
            if (col < grid.size() && row < grid[col].size())
                return juce::String(grid[col][row]);
            break;
        }
        case SequencerEditorMode::configuringSequence:
        {
            const auto grid = sequencer.getSequenceConfigsAsGridOfStrings();
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
            sequencer.requestStrUpdate();
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
    // Use this method as the place to do any pre-playback
    // initialisation that you need..
    for (auto& stack : machineStacks)
    {
        if (stack.sampler != nullptr)
            stack.sampler->prepareToPlay(sampleRate, samplesPerBlock);
        if (stack.arpeggiator != nullptr)
            stack.arpeggiator->prepareToPlay(sampleRate, samplesPerBlock);
        if (stack.wavetableSynth != nullptr)
        {
            stack.wavetableSynth->prepareToPlay(sampleRate, samplesPerBlock);
            stack.wavetableSynth->setSecondsPerTick(getSecondsPerTickFromBpm(getBPM()));
        }
    }
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
        if (stack.wavetableSynth != nullptr)
            stack.wavetableSynth->releaseResources();
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
                pendingHostBeatReset = hostPlaying;
                hostWasPlaying = hostPlaying;
            }

            if (!hostPlaying)
            {
                // Host transport stopped: stop the sequencer and skip internal clocking.
                sequencer.stop();
                hostPpqValid = false;
                usingHostClock = true;
            }
            else
            {
                // Host sync: derive tick timing from PPQ so transport changes are handled in real time.
                usingHostClock = true;
                sequencer.play();
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

                const long long ticksPerQuarterInt = static_cast<long long>(ticksPerQuarter);
                for (double offset = sampleOffsetToNextTick; offset < blockSizeSamples; offset += samplesPerTickDouble, ++tickIndex)
                {
                    const int tickSampleOffset = static_cast<int>(offset);
                    elapsedSamples = (blockStartSample + tickSampleOffset) % maxHorizon;
                    if (pendingHostBeatReset && ticksPerQuarterInt > 0 && (tickIndex % ticksPerQuarterInt) == 0)
                    {
                        // Reset tracker timing on the first host beat after transport starts.
                        resetTicks();
                        sequencer.rewindAtNextZero();
                        pendingHostBeatReset = false;
                    }
                    // tick is from the clockabs class and it keeps track of the absolute tick 
                    this->tick(); 
                    // this will cause any pending messages to be added to 'midiToSend'
                    // and trigger any sample players
                    sequencer.tick();
                    tickMachineClocks();
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
        pendingHostBeatReset = false;
    }

    if (!usingHostClock && useInternalClock)
    {
        hostPpqValid = false;
        hostWasPlaying = false;
        pendingHostBeatReset = false;
        const int samplesPerTickInt = static_cast<int>(samplesPerTick);
        for (int i = 0; i < blockSizeSamples; ++i){
            // weird but since juce midi sample offsets are int not unsigned long, 
            // I set a maximum elapsedSamples and mod on that, instead of just elapsedSamples ++; forever
            // otherwise, behaviour after 13 hours is undefined (samples @441k you can fit in an int)
            elapsedSamples = (elapsedSamples + 1) % maxHorizon;
            if (samplesPerTickInt > 0 && (elapsedSamples % samplesPerTickInt) == 0){
                // tick is from the clockabs class and it keeps track of the absolute tick 
                this->tick(); 
                // this will cause any pending messages to be added to 'midiToSend'
                // and trigger any sample players
                sequencer.tick();
                tickMachineClocks();
            }
        }
    }
    hostClockActive.store(usingHostClock, std::memory_order_relaxed);
    const bool sequencerPlaying = sequencer.isPlaying();
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
                // DBG("Event this block " << metadata.samplePosition - blockStartSample);
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
        if (machineStacks[i].sampler != nullptr)
            machineStacks[i].sampler->processBlock(buffer, samplerMidiByStack[i]);
    }

    juce::MidiBuffer emptyMidi;
    for (auto& stack : machineStacks)
    {
        if (stack.arpeggiator != nullptr)
            stack.arpeggiator->processBlock(buffer, emptyMidi);
        if (stack.wavetableSynth != nullptr)
            stack.wavetableSynth->processBlock(buffer, emptyMidi);
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
    // syncSequenceStrings();
    sequencer.updateSeqStringGrid();

    juce::DynamicObject::Ptr state = new juce::DynamicObject();
    state->setProperty("bpm", getBPM());
    state->setProperty("isPlaying", sequencer.isPlaying());

    juce::String modeStr = "sequence";
    switch (seqEditor.getEditMode())
    {
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

    state->setProperty("sequenceGrid", stringGridToVar(sequencer.getSequenceAsGridOfStrings()));
    state->setProperty("stepGrid", stringGridToVar(sequencer.getStepAsGridOfStrings(seqEditor.getCurrentSequence(), seqEditor.getCurrentStep())));
    state->setProperty("sequenceConfigs", stringGridToVar(sequencer.getSequenceConfigsAsGridOfStrings()));
    state->setProperty("stepData", numberGridToVar(sequencer.getStepData(seqEditor.getCurrentSequence(), seqEditor.getCurrentStep())));

    juce::Array<juce::var> playHeads;
    for (std::size_t col = 0; col < sequencer.howManySequences(); ++col)
    {
        juce::DynamicObject::Ptr ph = new juce::DynamicObject();
        ph->setProperty("sequence", static_cast<int>(col));
        ph->setProperty("step", static_cast<int>(sequencer.getCurrentStep(col)));
        playHeads.add(juce::var(ph));
    }
    state->setProperty("playHeads", playHeads);

    juce::Array<juce::var> seqLengths;
    for (std::size_t col = 0; col < sequencer.howManySequences(); ++col)
        seqLengths.add(static_cast<int>(sequencer.howManySteps(col)));
    state->setProperty("sequenceLengths", seqLengths);

    Sequence* currentSequence = sequencer.getSequence(seqEditor.getCurrentSequence());
    state->setProperty("machineId", currentSequence->getMachineId());
    state->setProperty("machineType", currentSequence->getMachineType());
    state->setProperty("triggerProbability", currentSequence->getTriggerProbability());

    state->setProperty("ticksPerStep", static_cast<int>(sequencer.getSequence(seqEditor.getCurrentSequence())->getTicksPerStep()));

    return state.get();
}

juce::var TrackerMainProcessor::serializeSequencerState()
{
    juce::DynamicObject::Ptr root = new juce::DynamicObject();
    auto sequencerToVar = [this]() {
        juce::DynamicObject::Ptr seqRoot = new juce::DynamicObject();
        juce::Array<juce::var> sequencesVar;
        const auto seqCount = sequencer.howManySequences();
        for (std::size_t seqIndex = 0; seqIndex < seqCount; ++seqIndex)
        {
            juce::DynamicObject::Ptr seqObj = new juce::DynamicObject();
            Sequence* seq = sequencer.getSequence(seqIndex);
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
                stepObj->setProperty("active", sequencer.isStepActive(seqIndex, step));
                juce::Array<juce::var> rows;
                const auto data = sequencer.getStepData(seqIndex, step);
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
    };

    root->setProperty("sequencer", sequencerToVar());
    root->setProperty("currentSequence", static_cast<int>(seqEditor.getCurrentSequence()));
    root->setProperty("currentStep", static_cast<int>(seqEditor.getCurrentStep()));
    root->setProperty("currentStepRow", static_cast<int>(seqEditor.getCurrentStepRow()));
    root->setProperty("currentStepCol", static_cast<int>(seqEditor.getCurrentStepCol()));

    juce::String modeStr = "sequence";
    const auto uiState = getUiState();
    if (uiState.isObject())
        modeStr = uiState.getProperty("mode", "sequence").toString();
    root->setProperty("mode", modeStr);

    juce::Array<juce::var> stackStates;
    for (auto& stack : machineStacks)
    {
        juce::DynamicObject::Ptr stackObj = new juce::DynamicObject();
        juce::Array<juce::var> order;
        for (const auto type : stack.order)
            order.add(static_cast<int>(type));
        stackObj->setProperty("order", order);

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
        stackObj->setProperty("wavetableSynth", encodeMachineState(stack.wavetableSynth.get()));
        stackStates.add(stackObj.get());
    }
    root->setProperty("machineStacks", stackStates);

    return root.get();
}

void TrackerMainProcessor::restoreSequencerState(const juce::var& stateVar)
{
    if (!stateVar.isObject())
        return;

    const auto seqVar = stateVar.getProperty("sequencer", juce::var());
    if (seqVar.isObject())
    {
        const auto seqArrayVar = seqVar.getProperty("sequences", juce::var());
        if (seqArrayVar.isArray())
        {
            const auto& seqArray = *seqArrayVar.getArray();
            const auto seqCount = juce::jmin<std::size_t>(static_cast<std::size_t>(seqArray.size()), sequencer.howManySequences());
            for (std::size_t i = 0; i < seqCount; ++i)
            {
                const auto& seqObj = seqArray[static_cast<int>(i)];
                if (!seqObj.isObject())
                    continue;

                Sequence* seq = sequencer.getSequence(i);
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
                                        // Legacy row layout: cmd, chan, note, vel, length, prob.
                                        std::vector<double> remapped = {
                                            row[Step::cmdInd],
                                            row[2],
                                            row[3],
                                            row[4],
                                            row[5]
                                        };
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
                                sequencer.setStepData(i, step, data);
                        }

                        const bool active = static_cast<bool>(stepVar.getProperty("active", true));
                        if (sequencer.isStepActive(i, step) != active)
                            sequencer.toggleStepActive(i, step);
                    }
                }

                seq->setMachineId(machineId);
                seq->setMachineType(machineType);
                seq->setTriggerProbability(triggerProbability);

                const bool mutedTarget = static_cast<bool>(seqObj.getProperty("muted", false));
                if (seq->isMuted() != mutedTarget)
                    sequencer.toggleSequenceMute(i);
            }
        }
    }

    int seq = static_cast<int>(stateVar.getProperty("currentSequence", static_cast<int>(seqEditor.getCurrentSequence())));
    const int maxSeq = static_cast<int>(juce::jmax<std::size_t>(1, sequencer.howManySequences())) - 1;
    seq = juce::jlimit(0, juce::jmax(0, maxSeq), seq);
    seqEditor.setCurrentSequence(seq);

    int step = static_cast<int>(stateVar.getProperty("currentStep", static_cast<int>(seqEditor.getCurrentStep())));
    const int maxStep = static_cast<int>(juce::jmax<std::size_t>(1, sequencer.howManySteps(static_cast<std::size_t>(seq)))) - 1;
    step = juce::jlimit(0, juce::jmax(0, maxStep), step);
    seqEditor.setCurrentStep(step);

    int stepRow = static_cast<int>(stateVar.getProperty("currentStepRow", static_cast<int>(seqEditor.getCurrentStepRow())));
    stepRow = juce::jmax(0, stepRow);
    // seqEditor.setCurrentStepRow(stepRow);

    int stepCol = static_cast<int>(stateVar.getProperty("currentStepCol", static_cast<int>(seqEditor.getCurrentStepCol())));
    stepCol = juce::jmax(0, stepCol);
    // seqEditor.setCurrentStepCol(stepCol);

    juce::String mode = stateVar.getProperty("mode", "sequence").toString().toLowerCase();
    if (mode == "step")
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
                        || type == CommandType::WavetableSynth)
                        stack.order.push_back(type);
                }
            }
            if (stack.order.empty())
                stack.order.push_back(CommandType::MidiNote);

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
            decodeMachineState(stackArray[static_cast<int>(i)].getProperty("wavetableSynth", juce::var()), stack.wavetableSynth.get());
        }
    }

    // syncSequenceStrings();
    
    sequencer.updateSeqStringGrid();

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
    return &sequencer;
}

SequencerEditor* TrackerMainProcessor::getSequenceEditor()
{
    return &seqEditor;
}

TrackerController* TrackerMainProcessor::getTrackerController()
{
    return &trackerController;
}

std::size_t TrackerMainProcessor::getMachineCount(CommandType type) const
{
    switch (type)
    {
        case CommandType::Sampler:
        case CommandType::Arpeggiator:
        case CommandType::WavetableSynth:
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

void TrackerMainProcessor::addMachineToStack(std::size_t stackIndex)
{
    if (auto* stack = getMachineStack(stackIndex))
    {
        const std::vector<CommandType> cycle = {
            CommandType::MidiNote,
            CommandType::WavetableSynth,
            CommandType::Sampler,
            CommandType::Arpeggiator
        };
        for (const auto type : cycle)
        {
            if (std::find(stack->order.begin(), stack->order.end(), type) == stack->order.end())
            {
                stack->order.push_back(type);
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
            stack->order.erase(stack->order.begin() + static_cast<long>(slotIndex));
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
            CommandType::Arpeggiator
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
                return;
            }
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
        || machineType == CommandType::WavetableSynth)
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
        || machineType == CommandType::Sampler
        || machineType == CommandType::WavetableSynth)
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
    // update tick interval in samples 
    samplesPerTick = static_cast<unsigned int>(
        std::lround(getSampleRate() * (60.0 / _bpm) / 8.0));
    bpm.store(_bpm, std::memory_order_relaxed);
    const double secondsPerTick = getSecondsPerTickFromBpm(_bpm);
    for (auto& stack : machineStacks)
        if (stack.wavetableSynth != nullptr)
            stack.wavetableSynth->setSecondsPerTick(secondsPerTick);
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
        case CommandType::Sampler: return stack.sampler.get();
        case CommandType::Arpeggiator: return stack.arpeggiator.get();
        case CommandType::WavetableSynth: return stack.wavetableSynth.get();
        default: return nullptr;
    }
}

const MachineInterface* TrackerMainProcessor::getMachineForStackType(const MachineStack& stack, CommandType type) const
{
    switch (type)
    {
        case CommandType::Sampler: return stack.sampler.get();
        case CommandType::Arpeggiator: return stack.arpeggiator.get();
        case CommandType::WavetableSynth: return stack.wavetableSynth.get();
        default: return nullptr;
    }
}
