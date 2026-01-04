#include "SuperSamplerProcessor.h"
#include "SuperSamplerEditor.h"

//==============================================================================
SuperSamplerProcessor::SuperSamplerProcessor()
     : AudioProcessor (BusesProperties()
                     #if ! JucePlugin_IsMidiEffect
                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                     #endif
                       ),
    //    apiServer{*this},
       apvts (*this, nullptr, "Params", createParameterLayout())
{

    // apiServer.startThread();  // Launch server in the background
}

SuperSamplerProcessor::~SuperSamplerProcessor()
{
    // apiServer.stopServer();
}

juce::AudioProcessorValueTreeState::ParameterLayout SuperSamplerProcessor::createParameterLayout()
{
    return {};
}

//==============================================================================
const juce::String SuperSamplerProcessor::getName() const
{
    return JucePlugin_Name;
}

bool SuperSamplerProcessor::acceptsMidi() const
{
   #if JucePlugin_WantsMidiInput
    return true;
   #else
    return false;
   #endif
}

bool SuperSamplerProcessor::producesMidi() const
{
   #if JucePlugin_ProducesMidiOutput
    return true;
   #else
    return false;
   #endif
}

bool SuperSamplerProcessor::isMidiEffect() const
{
   #if JucePlugin_IsMidiEffect
    return true;
   #else
    return false;
   #endif
}

double SuperSamplerProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int SuperSamplerProcessor::getNumPrograms()
{
    return 1;   // NB: some hosts don't cope very well if you tell them there are 0 programs,
                // so this should be at least 1, even if you're not really implementing programs.
}

int SuperSamplerProcessor::getCurrentProgram()
{
    return 0;
}

void SuperSamplerProcessor::setCurrentProgram (int index)
{
    juce::ignoreUnused (index);
}

const juce::String SuperSamplerProcessor::getProgramName (int index)
{
    juce::ignoreUnused (index);
    return {};
}

void SuperSamplerProcessor::changeProgramName (int index, const juce::String& newName)
{
    juce::ignoreUnused (index, newName);
}

//==============================================================================
void SuperSamplerProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    // Use this method as the place to do any pre-playback
    // initialisation that you need..
    juce::ignoreUnused (sampleRate, samplesPerBlock);
}

void SuperSamplerProcessor::releaseResources()
{
    // When playback stops, you can use this as an opportunity to free up any
    // spare memory, etc.
}

bool SuperSamplerProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
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

void SuperSamplerProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                              juce::MidiBuffer& midiMessages)
{
    juce::ignoreUnused (midiMessages);
    juce::ScopedNoDenormals noDenormals;
    auto totalNumInputChannels  = getTotalNumInputChannels();
    auto totalNumOutputChannels = getTotalNumOutputChannels();

    // In case we have more outputs than inputs, this code clears any output
    // channels that didn't contain input data, (because these aren't
    // guaranteed to be empty - they may contain garbage).
    // This is here to avoid people getting screaming feedback
    // when they first compile a plugin, but obviously you don't need to keep
    // this code if your algorithm always overwrites all the output channels.
    for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear (i, 0, buffer.getNumSamples());

    // This is the place where you'd normally do the guts of your plugin's
    // audio processing...
    // Make sure to reset the state if your inner loop is processing
    // the samples and the outer loop is handling the channels.
    // Alternatively, you can process the samples with the channels
    // interleaved by keeping the same state.
    for (int channel = 0; channel < totalNumInputChannels; ++channel)
    {
        auto* channelData = buffer.getWritePointer (channel);
        juce::ignoreUnused (channelData);
        // ..do something to the data...
    }

    sampler.processBlock (buffer, midiMessages);
}

//==============================================================================
bool SuperSamplerProcessor::hasEditor() const
{
    return true; // (change this to false if you choose to not supply an editor)
}

juce::AudioProcessorEditor* SuperSamplerProcessor::createEditor()
{
    return new SuperSamplerEditor (*this);
}

//==============================================================================
void SuperSamplerProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::ValueTree state ("PluginState");
    state.addChild (apvts.copyState(), -1, nullptr);
    state.addChild (sampler.exportToValueTree(), -1, nullptr);

    juce::MemoryOutputStream stream (destData, false);
    state.writeToStream (stream);
}

void SuperSamplerProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto tree = juce::ValueTree::readFromData (data, (size_t) sizeInBytes);

    if (! tree.isValid())
        return;

    auto paramsTree = tree.getChildWithName (apvts.state.getType());
    if (paramsTree.isValid())
        apvts.replaceState (paramsTree);

    auto samplerTree = tree.getChildWithName ("SamplerState");
    if (samplerTree.isValid())
        sampler.importFromValueTree (samplerTree);

    sendSamplerStateToUI();
}

void SuperSamplerProcessor::messageReceivedFromWebAPI(std::string msg)
{
    DBG("PluginProcess received a message " << msg);
    broadcastMessage ("Got your message " + msg);
}

void SuperSamplerProcessor::addSamplePlayerFromWeb()
{
    sampler.addSamplePlayer();
    sendSamplerStateToUI();
}

void SuperSamplerProcessor::requestSampleLoadFromWeb (int playerId)
{
    auto chooser = std::make_shared<juce::FileChooser> ("Select an audio file",
                                                        lastSampleDirectory,
                                                        "*.wav;*.aif;*.aiff;*.mp3;*.flac;*.ogg;*.*");

    auto chooserFlags = juce::FileBrowserComponent::openMode
                      | juce::FileBrowserComponent::canSelectFiles;

    chooser->launchAsync (chooserFlags, [this, chooser, playerId] (const juce::FileChooser& fc)
    {
        juce::ignoreUnused (chooser);

        auto file = fc.getResult();

        if (! file.existsAsFile())
        {
            broadcastMessage ("Load cancelled");
            return;
        }

        lastSampleDirectory = file.getParentDirectory();

        sampler.loadSampleAsync (playerId, file, [this] (bool ok, juce::String error)
        {
            if (! ok)
                broadcastMessage ("Load failed: " + error);

            sendSamplerStateToUI();
        });
    });
}

void SuperSamplerProcessor::setSampleRangeFromWeb (int playerId, int low, int high)
{
    if (sampler.setMidiRange (playerId, low, high))
        sendSamplerStateToUI();
    else
        broadcastMessage ("Failed to set range for player " + juce::String (playerId));
}

void SuperSamplerProcessor::triggerFromWeb (int playerId)
{
    sampler.trigger(playerId);
}

void SuperSamplerProcessor::setGainFromUI (int playerId, float gain)
{
    if (sampler.setGain (playerId, gain))
        sendSamplerStateToUI();
    else
        broadcastMessage ("Failed to set gain for player " + juce::String (playerId));
}

void SuperSamplerProcessor::sendSamplerStateToUI()
{
    DBG("sendSamplerStateToUI");
    auto payload = sampler.toVar();
    juce::MessageManager::callAsync ([this, payload]()
    {
        if (auto* editor = dynamic_cast<SuperSamplerEditor*> (getActiveEditor()))
            editor->updateUIFromProcessor (payload);
    });
}

juce::var SuperSamplerProcessor::getSamplerState() const
{
    return sampler.toVar();
}

juce::String SuperSamplerProcessor::getWaveformSVGForPlayer (int playerId) const
{
    return sampler.getWaveformSVG (playerId);
}

std::vector<float> SuperSamplerProcessor::getWaveformPointsForPlayer (int playerId) const
{
    return sampler.getWaveformPoints (playerId);
}

std::string SuperSamplerProcessor::getVuStateJson() const
{
    auto ptr = sampler.getVuJson();
    if (ptr != nullptr)
        return *ptr;

    return "{\"dB_out\":[]}";
}

void SuperSamplerProcessor::broadcastMessage (const juce::String& msg)
{
    juce::DynamicObject::Ptr obj = new juce::DynamicObject();
    obj->setProperty ("msg", msg);
    juce::var payload (obj);

    juce::MessageManager::callAsync ([this, payload]()
    {
        if (auto* editor = dynamic_cast<SuperSamplerEditor*> (getActiveEditor()))
            editor->updateUIFromProcessor (payload);
    });
}
