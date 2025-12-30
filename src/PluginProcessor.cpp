
/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "HTTPServer.h"
#include "SequencerCommands.h"

//==============================================================================
PluginProcessor::PluginProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
     : AudioProcessor (BusesProperties()
                     #if ! JucePlugin_IsMidiEffect
                      #if ! JucePlugin_IsSynth
                       .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                      #endif
                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                     #endif
                       ), 
                       apvts(*this, nullptr, "params", createParameterLayout()),
                       sequencer{8, 16}, seqEditor{&sequencer}, 
                       // seq, clock, editor
                       trackerController{&sequencer, this, &seqEditor},
                       elapsedSamples{0},maxHorizon{44100 * 3600}, 
                       samplesPerTick{44100/(120/60)/8}, bpm{120}, 
                       outstandingNoteOffs{0}
#endif
{
    
    CommandProcessor::assignMasterClock(this);
    CommandProcessor::assignMidiUtils(this);


    // sequencer.decrementSeqParam(0, 1);
    // sequencer.decrementSeqParam(0, 1);

    // put some test notes into the sequencer to see if they flow through
    apiServer = std::make_unique<HttpServerThread>(*this);
    apiServer->startThread();
}

PluginProcessor::~PluginProcessor()
{
    if (apiServer)
        apiServer->stopServer();
}


//==============================================================================
const juce::String PluginProcessor::getName() const
{
    return JucePlugin_Name;
}

bool PluginProcessor::acceptsMidi() const
{
   #if JucePlugin_WantsMidiInput
    return true;
   #else
    return false;
   #endif
}

bool PluginProcessor::producesMidi() const
{
   #if JucePlugin_ProducesMidiOutput
    return true;
   #else
    return false;
   #endif
}

bool PluginProcessor::isMidiEffect() const
{
   #if JucePlugin_IsMidiEffect
    return true;
   #else
    return false;
   #endif
}

double PluginProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int PluginProcessor::getNumPrograms()
{
    return 1;   // NB: some hosts don't cope very well if you tell them there are 0 programs,
                // so this should be at least 1, even if you're not really implementing programs.
}

int PluginProcessor::getCurrentProgram()
{
    return 0;
}

void PluginProcessor::setCurrentProgram (int index)
{
}

const juce::String PluginProcessor::getProgramName (int index)
{
    return {};
}

void PluginProcessor::changeProgramName (int index, const juce::String& newName)
{
}

//==============================================================================
void PluginProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    // Use this method as the place to do any pre-playback
    // initialisation that you need..
    juce::ignoreUnused(sampleRate, samplesPerBlock);
    msSinceLastStateUpdate = stateUpdateIntervalMs;
}

void PluginProcessor::releaseResources()
{
    // When playback stops, you can use this as an opportunity to free up any
    // spare memory, etc.
}

#ifndef JucePlugin_PreferredChannelConfigurations
bool PluginProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
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

void PluginProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    bool receivedMidi = false; 
    for (const MidiMessageMetadata metadata : midiMessages){
        if (metadata.getMessage().isNoteOn()) {
            DBG("Got a note " << metadata.getMessage().getNoteNumber());
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

    int blockSize = getBlockSize();
    int blockStartSample = elapsedSamples;
    int blockEndSample = (elapsedSamples + blockSize) % maxHorizon;
    for (int i=0;i<blockSize; ++i){
        // weird but since juce midi sample offsets are int not unsigned long, 
        // I set a maximum elapsedSamples and mod on that, instead of just elapsedSamples ++; forever
        // otherwise, behaviour after 13 hours is undefined (samples @441k you can fit in an int)
        elapsedSamples = (++elapsedSamples) % maxHorizon;
        if (elapsedSamples % samplesPerTick == 0){
            // tick is from the clockabs class and it keeps track of the absolute tick 
            this->tick(); 
            // this will cause any pending messages to be added to 'midiToSend'
            sequencer.tick();
            ticked = true; 
        }
    }
    // to get sample-accurate midi as opposed to block-accurate midi (!)
    // now add any midi that should have occurred within this block
    // to midiMessages, but with an offset value within this block
    
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
    if (ticked){
        const double sr = getSampleRate();
        const double blockMs = sr > 0.0 ? (static_cast<double>(buffer.getNumSamples()) * 1000.0 / sr) : 0.0;
        maybeUpdateUiState(blockMs);
    }
}

//==============================================================================
bool PluginProcessor::hasEditor() const
{
    return true; // (change this to false if you choose to not supply an editor)
}

juce::AudioProcessorEditor* PluginProcessor::createEditor()
{
    return new PluginEditor (*this);
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout PluginProcessor::createParameterLayout()
{
    // No traditional parameters yet – we just want the APVTS machinery for state persistence.
    return {};
}

void PluginProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    const auto stateVar = serializeSequencerState();
    const auto json = juce::JSON::toString(stateVar);
    apvts.state.setProperty("json", json, nullptr);

    juce::MemoryOutputStream stream(destData, true);
    if (auto xml = apvts.copyState().createXml())
        xml->writeTo(stream);
}

void PluginProcessor::setStateInformation (const void* data, int sizeInBytes)
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
}

void PluginProcessor::syncSequenceStrings()
{
    sequencer.updateSeqStringGrid();
}

juce::var PluginProcessor::stringGridToVar(const std::vector<std::vector<std::string>>& grid)
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

juce::var PluginProcessor::numberGridToVar(const std::vector<std::vector<double>>& grid)
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

void PluginProcessor::maybeUpdateUiState(double blockDurationMs)
{
    msSinceLastStateUpdate += blockDurationMs;
    // commented it out so it always updates 
    // if (msSinceLastStateUpdate < stateUpdateIntervalMs)
        // return;

    msSinceLastStateUpdate = 0.0;

    const auto state = getUiState();
    const auto json = juce::JSON::toString(state);

    {
        const juce::SpinLock::ScopedLockType lock(stateLock);
        latestStateJson = json;
        stateDirty.store(true, std::memory_order_release);
    }

    sendChangeMessage();
}

void PluginProcessor::applyPendingSequenceChanges() {}
void PluginProcessor::addSequenceImmediate() {}
void PluginProcessor::removeSequenceImmediate() {}

bool PluginProcessor::tryGetLatestSerializedUiState(juce::String& outJson)
{
    juce::SpinLock::ScopedTryLockType lock(stateLock);
    if (!lock.isLocked())
        return false;

    if (!stateDirty.load(std::memory_order_acquire))
        return false;

    outJson = latestStateJson;
    stateDirty.store(false, std::memory_order_release);
    return true;
}

void PluginProcessor::setStateUpdateIntervalMs(double ms)
{
    stateUpdateIntervalMs = juce::jmax(1.0, ms);
}

juce::var PluginProcessor::getUiState()
{
    syncSequenceStrings();

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

    double channel = sequencer.getStepDataAt(seqEditor.getCurrentSequence(), seqEditor.getCurrentStep(), 0, Step::chanInd);
    state->setProperty("channel", channel);

    state->setProperty("ticksPerStep", static_cast<int>(sequencer.getSequence(seqEditor.getCurrentSequence())->getTicksPerStep()));

    return state.get();
}

juce::var PluginProcessor::serializeSequencerState()
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
            seqObj->setProperty("channel", sequencer.getStepDataAt(seqIndex, 0, 0, Step::chanInd));

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

    return root.get();
}

void PluginProcessor::restoreSequencerState(const juce::var& stateVar)
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
            const auto seqCount = juce::jmin<std::size_t>(seqArray.size(), sequencer.howManySequences());
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

                const double channel = static_cast<double>(seqObj.getProperty("channel", sequencer.getStepDataAt(i, 0, 0, Step::chanInd)));

                const auto stepsVar = seqObj.getProperty("steps", juce::var());
                if (stepsVar.isArray())
                {
                    const auto& stepsArray = *stepsVar.getArray();
                    const auto stepsToLoad = juce::jmin<std::size_t>(stepsArray.size(), static_cast<std::size_t>(length));
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
                                    data.push_back(row);
                            }
                            if (!data.empty())
                                sequencer.setStepData(i, step, data);
                        }

                        const bool active = static_cast<bool>(stepVar.getProperty("active", true));
                        if (sequencer.isStepActive(i, step) != active)
                            sequencer.toggleStepActive(i, step);
                    }
                }

                for (std::size_t step = 0; step < seq->getLength(); ++step)
                    sequencer.setStepDataAt(i, step, 0, Step::chanInd, channel);

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
    const int maxStep = static_cast<int>(juce::jmax<std::size_t>(1, sequencer.howManySteps(seq))) - 1;
    step = juce::jlimit(0, juce::jmax(0, maxStep), step);
    seqEditor.setCurrentStep(step);

    int stepRow = static_cast<int>(stateVar.getProperty("currentStepRow", static_cast<int>(seqEditor.getCurrentStepRow())));
    stepRow = juce::jmax(0, stepRow);
    seqEditor.setCurrentStepRow(stepRow);

    int stepCol = static_cast<int>(stateVar.getProperty("currentStepCol", static_cast<int>(seqEditor.getCurrentStepCol())));
    stepCol = juce::jmax(0, stepCol);
    seqEditor.setCurrentStepCol(stepCol);

    juce::String mode = stateVar.getProperty("mode", "sequence").toString().toLowerCase();
    if (mode == "step")
        seqEditor.setEditMode(SequencerEditorMode::editingStep);
    else if (mode == "config")
        seqEditor.setEditMode(SequencerEditorMode::configuringSequence);
    else
        seqEditor.setEditMode(SequencerEditorMode::selectingSeqAndStep);

    syncSequenceStrings();
    msSinceLastStateUpdate = stateUpdateIntervalMs;
    {
        const juce::SpinLock::ScopedLockType lock(stateLock);
        latestStateJson = juce::JSON::toString(getUiState());
        stateDirty.store(true, std::memory_order_release);
    }
    sendChangeMessage();
}

bool PluginProcessor::handleKeyCommand(const juce::var& payload, juce::String& error)
{
    juce::String key = payload.getProperty("key", "").toString();
    juce::String code = payload.getProperty("code", "").toString();
    bool shift = static_cast<bool>(payload.getProperty("shift", false));

    juce::ignoreUnused(shift);

    if (key.isEmpty() && code.isEmpty())
    {
        error = "No key provided";
        return false;
    }

    auto updateState = [this]() {
        syncSequenceStrings();
        return true;
    };

    if (key == " " || key == "Spacebar")
    {
        CommandProcessor::sendAllNotesOff();
        if (sequencer.isPlaying())
            sequencer.stop();
        else
        {
            sequencer.rewindAtNextZero();
            sequencer.play();
        }
        return updateState();
    }

    if (key == "Tab")
    {
        seqEditor.nextStep();
        return updateState();
    }
    if (key == "Backspace")
    {
        seqEditor.resetAtCursor();
        CommandProcessor::sendAllNotesOff();
        return updateState();
    }
    if (key == "Enter" || key == "\n" || code == "Enter")
    {
        seqEditor.enterAtCursor();
        return updateState();
    }
    if (key == "ArrowUp")
    {
        seqEditor.moveCursorUp();
        return updateState();
    }
    if (key == "ArrowDown")
    {
        seqEditor.moveCursorDown();
        return updateState();
    }
    if (key == "ArrowLeft")
    {
        seqEditor.moveCursorLeft();
        return updateState();
    }
    if (key == "ArrowRight")
    {
        seqEditor.moveCursorRight();
        return updateState();
    }

    // character-driven controls
    juce::String lowered = key.toLowerCase();
    juce::juce_wchar ch = lowered.isNotEmpty() ? lowered[0] : 0;

    switch (ch)
    {
        case 'a':
            seqEditor.setArmedSequence(seqEditor.getCurrentSequence());
            return updateState();
        case 'r':
            CommandProcessor::sendAllNotesOff();
            sequencer.rewindAtNextZero();
            return updateState();
        case 'm':
            sequencer.toggleSequenceMute(seqEditor.getCurrentSequence());
            return updateState();
        case 's':
            seqEditor.gotoSequenceConfigPage();
            return updateState();
        default:
            break;
    }

    if (key == "_" || ch == '_')
    {
        trackerController.decrementBPM();
        return updateState();
    }
    if (key == "+" || ch == '+')
    {
        trackerController.incrementBPM();
        return updateState();
    }
    if (ch == '-' || key == "-")
    {
        seqEditor.removeRow();
        return updateState();
    }
    if (ch == '=' || key == "=")
    {
        seqEditor.addRow();
        return updateState();
    }
    if (key == "[" || ch == '[')
    {
        seqEditor.decrementAtCursor();
        return updateState();
    }
    if (key == "]" || ch == ']')
    {
        seqEditor.incrementAtCursor();
        return updateState();
    }
    if (ch == ',')
    {
        seqEditor.decrementOctave();
        return updateState();
    }
    if (ch == '.')
    {
        seqEditor.incrementOctave();
        return updateState();
    }

    // numeric velocity shortcuts
    if (juce::CharacterFunctions::isDigit(ch))
    {
        int num = static_cast<int>(ch - '0');
        if (num > 0 && num < 5)
        {
            seqEditor.enterStepData(num * (128 / 4), Step::velInd);
            return updateState();
        }
    }

    // piano-key style note entry
    auto keyToNote = MidiUtilsAbs::getKeyboardToMidiNotes(0);
    auto it = keyToNote.find(static_cast<char>(ch));
    if (it != keyToNote.end())
    {
        seqEditor.enterStepData(it->second, Step::noteInd);
        return updateState();
    }

    error = "Unhandled key";
    return false;
}

bool PluginProcessor::handleCommand(const juce::var& body, juce::String& error)
{
    if (!body.isObject())
    {
        error = "Command must be an object";
        return false;
    }

    juce::String action = body.getProperty("action", "").toString();
    juce::var payload = body.getProperty("payload", juce::var());
    if (action.isEmpty())
    {
        error = "Missing action";
        return false;
    }

    auto updateState = [this]() {
        syncSequenceStrings();
        return true;
    };

    juce::String command = action.toLowerCase();
    if (command == "key" || command == "keypress")
        return handleKeyCommand(payload, error);

    if (command == "toggleplay")
    {
        CommandProcessor::sendAllNotesOff();
        if (sequencer.isPlaying())
            sequencer.stop();
        else
        {
            sequencer.rewindAtNextZero();
            sequencer.play();
        }
        return updateState();
    }

    if (command == "rewind")
    {
        CommandProcessor::sendAllNotesOff();
        sequencer.rewindAtNextZero();
        return updateState();
    }

    if (command == "move")
    {
        juce::String dir = payload.getProperty("direction", "").toString().toLowerCase();
        if (dir == "up")
            seqEditor.moveCursorUp();
        else if (dir == "down")
            seqEditor.moveCursorDown();
        else if (dir == "left")
            seqEditor.moveCursorLeft();
        else if (dir == "right")
            seqEditor.moveCursorRight();
        else
            error = "Unknown move direction";
        return updateState();
    }

    if (command == "nextstep")
    {
        seqEditor.nextStep();
        return updateState();
    }

    if (command == "addrow")
    {
        seqEditor.addRow();
        return updateState();
    }

    if (command == "removerow")
    {
        seqEditor.removeRow();
        return updateState();
    }

    if (command == "increment")
    {
        seqEditor.incrementAtCursor();
        return updateState();
    }

    if (command == "decrement")
    {
        seqEditor.decrementAtCursor();
        return updateState();
    }

    if (command == "incrementoctave")
    {
        seqEditor.incrementOctave();
        return updateState();
    }

    if (command == "decrementoctave")
    {
        seqEditor.decrementOctave();
        return updateState();
    }

    if (command == "enter")
    {
        seqEditor.enterAtCursor();
        return updateState();
    }

    if (command == "reset")
    {
        seqEditor.resetAtCursor();
        CommandProcessor::sendAllNotesOff();
        return updateState();
    }

    if (command == "setmode")
    {
        juce::String mode = payload.getProperty("mode", "").toString().toLowerCase();
        if (mode == "sequence")
            seqEditor.setEditMode(SequencerEditorMode::selectingSeqAndStep);
        else if (mode == "step")
            seqEditor.setEditMode(SequencerEditorMode::editingStep);
        else if (mode == "config")
            seqEditor.setEditMode(SequencerEditorMode::configuringSequence);
        return updateState();
    }

    if (command == "setcursor")
    {
        int seq = static_cast<int>(payload.getProperty("sequence", static_cast<int>(seqEditor.getCurrentSequence())));
        int step = static_cast<int>(payload.getProperty("step", static_cast<int>(seqEditor.getCurrentStep())));
        seqEditor.setCurrentSequence(seq);
        seqEditor.setCurrentStep(step);
        return updateState();
    }

    if (command == "armsequence")
    {
        std::size_t seq = static_cast<std::size_t>(static_cast<int>(payload.getProperty("sequence", static_cast<int>(seqEditor.getCurrentSequence()))));
        seqEditor.setArmedSequence(seq);
        return updateState();
    }

    if (command == "togglemute")
    {
        std::size_t seq = static_cast<std::size_t>(static_cast<int>(payload.getProperty("sequence", static_cast<int>(seqEditor.getCurrentSequence()))));
        sequencer.toggleSequenceMute(seq);
        return updateState();
    }

    if (command == "incrementbpm")
    {
        trackerController.incrementBPM();
        return updateState();
    }

    if (command == "decrementbpm")
    {
        trackerController.decrementBPM();
        return updateState();
    }

    if (command == "setbpm")
    {
        double newBpm = static_cast<double>(payload.getProperty("bpm", getBPM()));
        if (newBpm > 0)
            trackerController.setBPM(static_cast<unsigned int>(newBpm));
        return updateState();
    }

    if (command == "setstepvalue")
    {
        juce::String field = payload.getProperty("field", "").toString().toLowerCase();
        double value = static_cast<double>(payload.getProperty("value", 0.0));
        std::size_t seq = static_cast<std::size_t>(static_cast<int>(payload.getProperty("sequence", static_cast<int>(seqEditor.getCurrentSequence()))));
        std::size_t step = static_cast<std::size_t>(static_cast<int>(payload.getProperty("step", static_cast<int>(seqEditor.getCurrentStep()))));
        std::size_t row = static_cast<std::size_t>(static_cast<int>(payload.getProperty("row", 0)));

        int col = -1;
        if (field == "note")
            col = Step::noteInd;
        else if (field == "velocity" || field == "vel")
            col = Step::velInd;
        else if (field == "length")
            col = Step::lengthInd;
        else if (field == "probability" || field == "prob")
            col = Step::probInd;
        else if (field == "channel" || field == "chan")
            col = Step::chanInd;

        if (col >= 0)
            sequencer.setStepDataAt(seq, step, row, static_cast<std::size_t>(col), value);
        return updateState();
    }

    if (command == "enterstepdata")
    {
        double value = static_cast<double>(payload.getProperty("value", 0.0));
        int column = static_cast<int>(payload.getProperty("column", static_cast<int>(Step::noteInd)));
        bool applyOctave = static_cast<bool>(payload.getProperty("applyOctave", true));
        seqEditor.enterStepData(value, column, applyOctave);
        return updateState();
    }

    error = "Unknown action";
    return false;
}

//==============================================================================
// This creates new instances of the plugin..
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PluginProcessor();
}

Sequencer* PluginProcessor::getSequencer()
{
    return &sequencer;
}

SequencerEditor* PluginProcessor::getSequenceEditor()
{
    return &seqEditor;
}

TrackerController* PluginProcessor::getTrackerController()
{
    return &trackerController;
}

////////////// MIDIUtils interface 

void PluginProcessor::allNotesOff()
{
    midiToSend.clear();// remove anything that's hanging around. 
    for (int chan = 1; chan < 17; ++chan){
        midiToSend.addEvent(MidiMessage::allNotesOff(chan), static_cast<int>(elapsedSamples));
    }
}
void PluginProcessor::playSingleNote(unsigned short channel, unsigned short note, unsigned short velocity, unsigned short durInTicks)
{
    channel ++; // channels come in 0-15 but we want 1-16
    // offtick is an absolute tick from the start of time 
    // but we have a max horizon which is how far in the future we can set things 
    int offSample =  elapsedSamples +  (samplesPerTick * static_cast<int>(durInTicks)) % maxHorizon;
    // DBG("playSingleNote note start/ end " << elapsedSamples << " -> " << offSample << " tick length " << durInTicks << " hor " << maxHorizon);
    // generate a note on and a note off 
    // note on is right now 
    midiToSend.addEvent(MidiMessage::noteOn((int)channel, (int)note, (uint8)velocity), elapsedSamples);
    // note off is now + length 
    midiToSend.addEvent(MidiMessage::noteOff((int)channel, (int)note, (uint8)velocity), offSample);
    // assert()
    outstandingNoteOffs ++ ;
}
void PluginProcessor::sendQueuedMessages(long tick)
{
    // this is blank as midi gets sent by moving it from midiToSend to the processBlock's midi buffer

}

////////////// end MIDIUtils interface 


void PluginProcessor::setBPM(double _bpm)
{   
    assert(_bpm > 0);
    // update tick interval in samples 
    samplesPerTick = getSampleRate() *  (60/_bpm) /8;
    bpm = _bpm;
}

double PluginProcessor::getBPM()
{
    return bpm;
}


void PluginProcessor::clearPendingEvents()
{
    midiToSend.clear();
}

