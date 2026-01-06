
/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#include "TrackerMainProcessor.h"
#include "TrackerMainUI.h"

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

    samplers.reserve(4);
    for (int i = 0; i < 4; ++i)
    {
        samplers.push_back(std::make_unique<SuperSamplerProcessor>());
    }
    seqEditor.setSamplerHost(this);

    // sequencer.decrementSeqParam(0, 1);
    // sequencer.decrementSeqParam(0, 1);

    // put some test notes into the sequencer to see if they flow through
  
}

TrackerMainProcessor::~TrackerMainProcessor()
{
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

//==============================================================================
void TrackerMainProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    // Use this method as the place to do any pre-playback
    // initialisation that you need..
    for (auto& sampler : samplers)
    {
        sampler->prepareToPlay(sampleRate, samplesPerBlock);
    }
}

void TrackerMainProcessor::releaseResources()
{
    // When playback stops, you can use this as an opportunity to free up any
    // spare memory, etc.
    for (auto& sampler : samplers)
    {
        sampler->releaseResources();
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
        }
    }
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
    juce::MidiBuffer samplerMidiThisBlock;
    juce::MidiBuffer futureSamplerMidi;
    for (const MidiMessageMetadata metadata : midiToSendToSampler){
        if (blockEndSample < blockStartSample){// we wrapped block end back around 
            if ( metadata.samplePosition >= blockStartSample ||  
                metadata.samplePosition < blockEndSample) {
                samplerMidiThisBlock.addEvent(metadata.getMessage(),  metadata.samplePosition - blockStartSample);
            }
            else{// it is in the future            
                futureSamplerMidi.addEvent(metadata.getMessage(),  metadata.samplePosition);
            }
        }
        if (blockStartSample < blockEndSample){
            if ( metadata.samplePosition >= blockStartSample && 
                metadata.samplePosition < blockEndSample) {
                samplerMidiThisBlock.addEvent(metadata.getMessage(),  metadata.samplePosition - blockStartSample);
            }
            else{// it is in the future            
                futureSamplerMidi.addEvent(metadata.getMessage(),  metadata.samplePosition);
            }
        }
    }
    // stash future midi to the class data member
    // midiToSendToSampler so it comes back in the next
    // call to processBlock
    midiToSendToSampler.clear();
    midiToSendToSampler.swapWith(futureSamplerMidi);
    // now re-organise the sampler midi for this block
    // (samplerMidiThisBlock) by sampler 
    // this seems a bit over the top to me
    // data structure wise but hey 
    // it does make the call to processblock 
    // on the samplers much cleaner 
    std::vector<juce::MidiBuffer> samplerMidiById(samplers.size());
    for (const MidiMessageMetadata metadata : samplerMidiThisBlock)
    {
        int channelIndex = metadata.getMessage().getChannel() - 1;
        if (channelIndex < 0 || static_cast<std::size_t>(channelIndex) >= samplers.size())
            channelIndex = 0;
        samplerMidiById[static_cast<std::size_t>(channelIndex)]
            .addEvent(metadata.getMessage(), metadata.samplePosition);
    }

    // at this point, the MIDI events
    // for the samplers have been prepared and placed
    // in the correct offsets within the block
    // so we can now send that MIDI to the samplers
    // and the buffer and they can write audio into it
    for (std::size_t i = 0; i < samplers.size(); ++i)
    {
        // if (samplerMidiById[i].getNumEvents() > 0){
        //     DBG("processblock on sampler engine " << i << " events " << samplerMidiById[i].getNumEvents());
        //     for (const auto metadata : samplerMidiById[i])
        //         DBG(metadata.getMessage().getDescription());
        // }
        samplers[i]->processBlock(buffer, samplerMidiById[i]);
    }
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
    const auto stateVar = serializeSequencerState();
    const auto json = juce::JSON::toString(stateVar);
    apvts.state.setProperty("json", json, nullptr);

    juce::MemoryOutputStream stream(destData, true);
    if (auto xml = apvts.copyState().createXml())
        xml->writeTo(stream);
}

void TrackerMainProcessor::setStateInformation (const void* data, int sizeInBytes)
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

    juce::Array<juce::var> samplerStates;
    for (auto& sampler : samplers)
    {
        juce::MemoryBlock samplerState;
        if (sampler != nullptr)
        {
            sampler->getStateInformation(samplerState);
            const auto encoded = juce::Base64::toBase64(samplerState.getData(), samplerState.getSize());
            samplerStates.add(encoded);
        }
        else
        {
            samplerStates.add(juce::String());
        }
    }
    root->setProperty("samplers", samplerStates);

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

    const auto samplersVar = stateVar.getProperty("samplers", juce::var());
    if (samplersVar.isArray())
    {
        const auto& samplerArray = *samplersVar.getArray();
        const auto samplerCount = juce::jmin<std::size_t>(static_cast<std::size_t>(samplerArray.size()), samplers.size());
        for (std::size_t i = 0; i < samplerCount; ++i)
        {
            const auto encoded = samplerArray[static_cast<int>(i)].toString();
            if (encoded.isEmpty())
                continue;
            juce::MemoryBlock samplerState;
            juce::MemoryOutputStream stream(samplerState, false);
            if (! juce::Base64::convertFromBase64(stream, encoded))
                continue;
            if (samplers[i] != nullptr)
            {
                samplers[i]->setStateInformation(samplerState.getData(),
                                                 static_cast<int>(samplerState.getSize()));
            }
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

std::size_t TrackerMainProcessor::getSamplerCount() const
{
    return samplers.size();
}

juce::var TrackerMainProcessor::getSamplerState(std::size_t samplerIndex) const
{
    const auto* sampler = getSamplerForIndex(samplerIndex);
    if (sampler == nullptr)
        return {};
    return sampler->getSamplerState();
}

void TrackerMainProcessor::samplerAddPlayer(std::size_t samplerIndex)
{
    if (auto* sampler = getSamplerForIndex(samplerIndex))
        sampler->addSamplePlayerFromWeb();
}

void TrackerMainProcessor::samplerRemovePlayer(std::size_t samplerIndex, int playerId)
{
    if (auto* sampler = getSamplerForIndex(samplerIndex))
        sampler->removeSamplePlayer(playerId);
}

void TrackerMainProcessor::samplerRequestLoad(std::size_t samplerIndex, int playerId)
{
    if (auto* sampler = getSamplerForIndex(samplerIndex))
        sampler->requestSampleLoadFromWeb(playerId);
}

void TrackerMainProcessor::samplerTrigger(std::size_t samplerIndex, int playerId)
{
    if (auto* sampler = getSamplerForIndex(samplerIndex))
        sampler->triggerFromWeb(playerId);
}

void TrackerMainProcessor::samplerSetRange(std::size_t samplerIndex, int playerId, int low, int high)
{
    if (auto* sampler = getSamplerForIndex(samplerIndex))
        sampler->setSampleRangeFromWeb(playerId, low, high);
}

void TrackerMainProcessor::samplerSetGain(std::size_t samplerIndex, int playerId, float gain)
{
    if (auto* sampler = getSamplerForIndex(samplerIndex))
        sampler->setGainFromUI(playerId, gain);
}

////////////// MIDIUtils interface 

void TrackerMainProcessor::allNotesOff()
{
    midiToSend.clear();// remove anything that's hanging around. 
    midiToSendToSampler.clear();
    for (int chan = 1; chan < 17; ++chan){
        midiToSend.addEvent(MidiMessage::allNotesOff(chan), static_cast<int>(elapsedSamples));
        midiToSendToSampler.addEvent(MidiMessage::allNotesOff(chan), static_cast<int>(elapsedSamples));
    }
}
void TrackerMainProcessor::sendMessageToMachine(CommandType machineType, unsigned short machineId, unsigned short note, unsigned short velocity, unsigned short durInTicks)
{
    juce::MidiBuffer* targetBuffer = &midiToSend;
    if (machineType == CommandType::Sampler)
        targetBuffer = &midiToSendToSampler;

    unsigned short channel = machineId + 1; // channels come in 0-15 but we want 1-16
    // offtick is an absolute tick from the start of time 
    // but we have a max horizon which is how far in the future we can set things 
    const int samplesPerTickInt = static_cast<int>(samplesPerTick);
    const int offsetSamples = (samplesPerTickInt * static_cast<int>(durInTicks)) % maxHorizon;
    const int offSample = elapsedSamples + offsetSamples;
    // DBG("sendMessageToMachine note start/ end " << elapsedSamples << " -> " << offSample << " tick length " << durInTicks << " hor " << maxHorizon);
    // generate a note on and a note off 
    // note on is right now 
    targetBuffer->addEvent(MidiMessage::noteOn((int)channel, (int)note, (uint8)velocity), elapsedSamples);
    // note off is now + length 
    targetBuffer->addEvent(MidiMessage::noteOff((int)channel, (int)note, (uint8)velocity), offSample);
    // assert()
    outstandingNoteOffs ++ ;
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
    samplesPerTick = getSampleRate() *  (60/_bpm) /8;
    bpm.store(_bpm, std::memory_order_relaxed);
}

double TrackerMainProcessor::getBPM()
{
    return bpm.load(std::memory_order_relaxed);
}


void TrackerMainProcessor::clearPendingEvents()
{
    midiToSend.clear();
    midiToSendToSampler.clear();
}

SuperSamplerProcessor* TrackerMainProcessor::getSamplerForIndex(std::size_t samplerIndex)
{
    if (samplerIndex >= samplers.size())
        return nullptr;
    return samplers[samplerIndex].get();
}

const SuperSamplerProcessor* TrackerMainProcessor::getSamplerForIndex(std::size_t samplerIndex) const
{
    if (samplerIndex >= samplers.size())
        return nullptr;
    return samplers[samplerIndex].get();
}
