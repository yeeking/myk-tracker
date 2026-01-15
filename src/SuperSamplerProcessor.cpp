#include "SuperSamplerProcessor.h"
// #include "SuperSamplerEditor.h"
#include "SuperSamplePlayer.h"
#include "WaveformSVGRenderer.h"
#include <algorithm>
#include <sstream>
#include <thread>

namespace
{
std::string sanitizeLabel(const juce::String& input, size_t maxLen)
{
    auto trimmed = input.trim();
    if (trimmed.isEmpty())
        return {};

    auto upper = trimmed.toUpperCase();
    std::string out;
    out.reserve(static_cast<size_t>(upper.length()));
    for (auto ch : upper)
    {
        if (out.size() >= maxLen)
            break;
        if (ch == '\n' || ch == '\r')
            continue;
        if (ch < 32 || ch > 126)
            continue;
        out.push_back(static_cast<char>(ch));
    }
    return out;
}

std::string formatGain(float gain)
{
    juce::String text = juce::String(gain, 2);
    return sanitizeLabel(text, 6);
}

float vuDbToGlow(float db)
{
    const float gain = juce::Decibels::decibelsToGain(db, -60.0f);
    return juce::jlimit(0.0f, 1.0f, gain);
}
} // namespace

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
    formatManager.registerBasicFormats();
    vuJson = "{\"dB_out\":[]}";
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
    // juce::ignoreUnused (midiMessages);
    // juce::ScopedNoDenormals noDenormals;
    // auto totalNumInputChannels  = getTotalNumInputChannels();
    // auto totalNumOutputChannels = getTotalNumOutputChannels();

    // In case we have more outputs than inputs, this code clears any output
    // channels that didn't contain input data, (because these aren't
    // guaranteed to be empty - they may contain garbage).
    // This is here to avoid people getting screaming feedback
    // when they first compile a plugin, but obviously you don't need to keep
    // this code if your algorithm always overwrites all the output channels.
    // for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        // buffer.clear (i, 0, buffer.getNumSamples());

    // This is the place where you'd normally do the guts of your plugin's
    // audio processing...
    // Make sure to reset the state if your inner loop is processing
    // the samples and the outer loop is handling the channels.
    // Alternatively, you can process the samples with the channels
    // interleaved by keeping the same state.
    // for (int channel = 0; channel < totalNumInputChannels; ++channel)
    // {
    //     auto* channelData = buffer.getWritePointer (channel);
    //     juce::ignoreUnused (channelData);
    //     // ..do something to the data...
    // }

    processSamplerBlock (buffer, midiMessages);
}

std::vector<std::vector<UIBox>> SuperSamplerProcessor::getUIBoxes(const MachineUiContext& context)
{
    const auto samplerState = getSamplerState();
    if (!samplerState.isObject())
    {
        uiPlayers.clear();
        uiGlowLevels.clear();
        learningPlayerId = -1;
        return { { UIBox{} } };
    }

    const auto playersVar = samplerState.getProperty("players", juce::var());
    if (!playersVar.isArray())
    {
        uiPlayers.clear();
        uiGlowLevels.clear();
        learningPlayerId = -1;
        return { { UIBox{} } };
    }

    const auto* playersArray = playersVar.getArray();
    if (playersArray == nullptr)
    {
        uiPlayers.clear();
        uiGlowLevels.clear();
        learningPlayerId = -1;
        return { { UIBox{} } };
    }

    std::vector<UiPlayerState> nextPlayers;
    nextPlayers.reserve(static_cast<std::size_t>(playersArray->size()));
    for (const auto& entry : *playersArray)
    {
        auto* playerObj = entry.getDynamicObject();
        if (playerObj == nullptr)
            continue;

        UiPlayerState st;
        st.id = static_cast<int>(playerObj->getProperty("id"));
        st.midiLow = static_cast<int>(playerObj->getProperty("midiLow"));
        st.midiHigh = static_cast<int>(playerObj->getProperty("midiHigh"));
        st.gain = static_cast<float>(double(playerObj->getProperty("gain")));
        st.isPlaying = static_cast<bool>(playerObj->getProperty("isPlaying"));
        const auto vuVar = playerObj->getProperty("vuDb");
        st.vuDb = vuVar.isVoid() ? -60.0f : static_cast<float>(double(vuVar));
        st.status = playerObj->getProperty("status").toString().toStdString();
        st.fileName = playerObj->getProperty("fileName").toString().toStdString();
        nextPlayers.push_back(st);
    }

    std::vector<float> nextGlow;
    nextGlow.reserve(nextPlayers.size());
    bool learningStillValid = false;
    for (const auto& player : nextPlayers)
    {
        if (player.id == learningPlayerId)
            learningStillValid = true;
        float glow = 0.0f;
        for (std::size_t i = 0; i < uiPlayers.size(); ++i)
        {
            if (uiPlayers[i].id == player.id && i < uiGlowLevels.size())
            {
                glow = uiGlowLevels[i];
                break;
            }
        }
        const float targetGlow = vuDbToGlow(player.vuDb);
        glow = std::max(targetGlow, glow * 0.85f);
        if (glow < 0.02f)
            glow = 0.0f;
        nextGlow.push_back(glow);
    }

    uiPlayers = std::move(nextPlayers);
    uiGlowLevels = std::move(nextGlow);
    if (!learningStillValid)
        learningPlayerId = -1;

    const std::size_t rows = uiPlayers.size() + 1;
    const std::size_t cols = 7;
    if (rows == 0 || cols == 0)
        return { { UIBox{} } };

    std::vector<std::vector<UIBox>> cells(cols, std::vector<UIBox>(rows));
    for (std::size_t col = 0; col < cols; ++col)
    {
        for (std::size_t row = 0; row < rows; ++row)
        {
            UIBox cell;
            const bool learnDisabled = context.disableLearning;
            if (row == 0)
            {
                if (col == 0)
                {
                    cell.kind = UIBox::Kind::SamplerAction;
                    cell.text = "ADD";
                    cell.onActivate = [this]()
                    {
                        addEntry();
                    };
                }
                else
                {
                    cell.kind = UIBox::Kind::None;
                }
            }
            else
            {
                const auto& player = uiPlayers[row - 1];
                const int playerId = player.id;
                switch (col)
                {
                case 0:
                    cell.kind = UIBox::Kind::SamplerAction;
                    cell.text = "LOAD";
                    cell.onActivate = [this, playerId]()
                    {
                        requestSampleLoadFromWeb(playerId);
                    };
                    break;
                case 1:
                    cell.kind = UIBox::Kind::SamplerAction;
                    cell.text = player.isPlaying ? "PLAY" : "TRIG";
                    cell.isActive = player.isPlaying;
                    cell.onActivate = [this, playerId]()
                    {
                        triggerFromWeb(playerId);
                    };
                    break;
                case 2:
                    cell.kind = UIBox::Kind::SamplerAction;
                    cell.text = "LerN";
                    cell.isActive = (playerId == learningPlayerId);
                    cell.isDisabled = learnDisabled;
                    cell.onActivate = [this, playerId, learnDisabled]()
                    {
                        if (learnDisabled)
                            return;
                        if (learningPlayerId == playerId)
                            learningPlayerId = -1;
                        else
                            learningPlayerId = playerId;
                    };
                    break;
                case 3:
                    cell.kind = UIBox::Kind::SamplerValue;
                    cell.text = sanitizeLabel(juce::String(player.midiLow), 4);
                    cell.onAdjust = [this, playerId, low = player.midiLow, high = player.midiHigh](int direction)
                    {
                        const int nextLow = juce::jlimit(0, 127, low + direction);
                        setSampleRangeFromWeb(playerId, nextLow, high);
                    };
                    break;
                case 4:
                    cell.kind = UIBox::Kind::SamplerValue;
                    cell.text = sanitizeLabel(juce::String(player.midiHigh), 4);
                    cell.onAdjust = [this, playerId, low = player.midiLow, high = player.midiHigh](int direction)
                    {
                        const int nextHigh = juce::jlimit(0, 127, high + direction);
                        setSampleRangeFromWeb(playerId, low, nextHigh);
                    };
                    break;
                case 5:
                    cell.kind = UIBox::Kind::SamplerValue;
                    cell.text = formatGain(player.gain);
                    cell.onAdjust = [this, playerId, gain = player.gain](int direction)
                    {
                        const float nextGain = juce::jlimit(0.0f, 2.0f, gain + direction * 0.05f);
                        setGainFromUI(playerId, nextGain);
                    };
                    break;
                case 6:
                    cell.kind = UIBox::Kind::SamplerWaveform;
                    if (!player.fileName.empty())
                        cell.text = sanitizeLabel(juce::String(player.fileName), 18);
                    else
                        cell.text = sanitizeLabel(juce::String(player.status), 18);
                    break;
                default:
                    cell.kind = UIBox::Kind::None;
                    break;
                }
                if (col == 1)
                    cell.isActive = player.isPlaying;
            }

            if (row > 0 && row - 1 < uiGlowLevels.size())
                cell.glow = uiGlowLevels[row - 1];

            if (cell.kind == UIBox::Kind::None)
                cell.isDisabled = true;

            cells[col][row] = std::move(cell);
        }
    }

    return cells;
}

bool SuperSamplerProcessor::handleIncomingNote(unsigned short note,
                                               unsigned short velocity,
                                               unsigned short durationTicks,
                                               MachineNoteEvent& outEvent)
{
    juce::ignoreUnused(note, velocity, durationTicks, outEvent);
    return false;
}

void SuperSamplerProcessor::applyLearnedNote(int midiNote)
{
    if (learningPlayerId < 0)
        return;
    const int clampedNote = juce::jlimit(0, 127, midiNote);
    setSampleRangeFromWeb(learningPlayerId, clampedNote, clampedNote);
}

void SuperSamplerProcessor::addEntry()
{
    addSamplePlayerFromWeb();
}

void SuperSamplerProcessor::removeEntry(int entryIndex)
{
    if (entryIndex < 0)
        return;
    if (static_cast<std::size_t>(entryIndex) >= uiPlayers.size())
        return;
    removeSamplePlayer(uiPlayers[static_cast<std::size_t>(entryIndex)].id);
}

//==============================================================================
bool SuperSamplerProcessor::hasEditor() const
{
    return true; // (change this to false if you choose to not supply an editor)
}

juce::AudioProcessorEditor* SuperSamplerProcessor::createEditor()
{
    return new GenericAudioProcessorEditor(*this);
    // return new SuperSamplerEditor (*this);
}

//==============================================================================
void SuperSamplerProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::ValueTree state ("PluginState");
    state.addChild (apvts.copyState(), -1, nullptr);
    state.addChild (exportToValueTree(), -1, nullptr);

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
        importFromValueTree (samplerTree);

    sendSamplerStateToUI();
}

void SuperSamplerProcessor::messageReceivedFromWebAPI(std::string msg)
{
    // DBG("PluginProcess received a message " << msg);
    broadcastMessage ("Got your message " + msg);
}

void SuperSamplerProcessor::addSamplePlayerFromWeb()
{
    addSamplePlayer();
    sendSamplerStateToUI();
}

void SuperSamplerProcessor::removeSamplePlayer (int playerId)
{
    if (removeSamplePlayerInternal (playerId))
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

        loadSampleAsync (playerId, file, [this] (bool ok, juce::String error)
        {
            if (! ok)
                broadcastMessage ("Load failed: " + error);

            sendSamplerStateToUI();
        });
    });
}

void SuperSamplerProcessor::setSampleRangeFromWeb (int playerId, int low, int high)
{
    if (setMidiRange (playerId, low, high))
        sendSamplerStateToUI();
    else
        broadcastMessage ("Failed to set range for player " + juce::String (playerId));
}

void SuperSamplerProcessor::triggerFromWeb (int playerId)
{
    trigger (playerId);
}

void SuperSamplerProcessor::setGainFromUI (int playerId, float gain)
{
    if (setGain (playerId, gain))
        sendSamplerStateToUI();
    else
        broadcastMessage ("Failed to set gain for player " + juce::String (playerId));
}

void SuperSamplerProcessor::sendSamplerStateToUI()
{
    // DBG("sendSamplerStateToUI");
    auto payload = toVar();
    juce::MessageManager::callAsync ([payload]()
    {
        // if (auto* editor = dynamic_cast<SuperSamplerEditor*> (getActiveEditor()))
            // editor->updateUIFromProcessor (payload);
    });
}

juce::var SuperSamplerProcessor::getSamplerState() const
{
    return toVar();
}

juce::String SuperSamplerProcessor::getWaveformSVGForPlayer (int playerId) const
{
    return getWaveformSVG (playerId);
}

std::vector<float> SuperSamplerProcessor::getWaveformPointsForPlayer (int playerId) const
{
    return getWaveformPoints (playerId);
}

std::string SuperSamplerProcessor::getVuStateJson() const
{
    auto ptr = getVuJson();
    if (ptr != nullptr)
        return *ptr;

    return "{\"dB_out\":[]}";
}

void SuperSamplerProcessor::broadcastMessage (const juce::String& msg)
{
    juce::DynamicObject::Ptr obj = new juce::DynamicObject();
    obj->setProperty ("msg", msg);
    juce::var payload (obj);

    juce::MessageManager::callAsync ([payload]()
    {
        // if (auto* editor = dynamic_cast<SuperSamplerEditor*> (getActiveEditor()))
            // editor->updateUIFromProcessor (payload);
    });
}

void SuperSamplerProcessor::processSamplerBlock (juce::AudioBuffer<float>& buffer, const juce::MidiBuffer& midi)
{
    const int numSamples = buffer.getNumSamples();
    std::vector<std::vector<int>> noteOns ((size_t) numSamples);

    for (const auto meta : midi)
    {
        const auto msg = meta.getMessage();
        if (msg.isNoteOn())
        {
            auto pos = juce::jlimit (0, numSamples - 1, meta.samplePosition);
            noteOns[(size_t) pos].push_back (msg.getNoteNumber());
        }
    }
    const std::lock_guard<std::mutex> lock (playerMutex);

    for (auto& player : players)
        player->beginBlock();

    const int numChannels = buffer.getNumChannels();

    for (int sample = 0; sample < numSamples; ++sample)
    {
        if (! noteOns[(size_t) sample].empty())
        {
            for (int note : noteOns[(size_t) sample])
            {
                for (auto& player : players)
                {
                    if (player->acceptsNote (note)){
                        // DBG("Player playing a note " << note);
                        player->triggerNote (note);
                    }
                    // else{
                    //     DBG("Player rejects note " << note);
                    // }
                }
            }
        }

        for (int ch = 0; ch < numChannels; ++ch)
        {
            float acc = 0.0f;
            for (auto& player : players)
                acc += player->getNextSampleForChannel (ch);

            buffer.addSample (ch, sample, acc);
        }
    }
    for (auto& player : players)
        player->endBlock();

    // std::ostringstream vuBuilder;
    // vuBuilder.setf (std::ios::fixed);
    // vuBuilder.precision (2);
    // vuBuilder << "{\"dB_out\":[";
    // for (size_t i = 0; i < players.size(); ++i)
    // {
    //     if (i > 0)
    //         vuBuilder << ',';
    //     vuBuilder << players[i]->getLastVuDb();
    // }
    // vuBuilder << "]}";
    // {
    //     const juce::SpinLock::ScopedLockType guard (vuLock);
    //     vuJson = vuBuilder.str();
    // }
}

int SuperSamplerProcessor::addSamplePlayer()
{
    const std::lock_guard<std::mutex> lock (playerMutex);
    auto id = nextId++;
    players.push_back (std::make_unique<SuperSamplePlayer> (id));
    return id;
}

bool SuperSamplerProcessor::removeSamplePlayerInternal (int playerId)
{
    const std::lock_guard<std::mutex> lock (playerMutex);
    auto it = std::find_if (players.begin(), players.end(),
        [playerId](const std::unique_ptr<SuperSamplePlayer>& player)
        {
            return player->getId() == playerId;
        });
    if (it == players.end())
        return false;
    players.erase (it);
    return true;
}

juce::var SuperSamplerProcessor::toVar() const
{
    const std::lock_guard<std::mutex> lock (playerMutex);
    juce::Array<juce::var> arr;

    for (const auto& player : players)
    {
        auto st = player->getState();
        juce::DynamicObject::Ptr obj = new juce::DynamicObject();
        obj->setProperty ("id", st.id);
        obj->setProperty ("midiLow", st.midiLow);
        obj->setProperty ("midiHigh", st.midiHigh);
        obj->setProperty ("gain", st.gain);
        obj->setProperty ("isPlaying", st.isPlaying);
        obj->setProperty ("vuDb", st.vuDb);
        obj->setProperty ("status", st.status);
        obj->setProperty ("fileName", st.fileName);
        obj->setProperty ("filePath", st.filePath);
        obj->setProperty ("waveformSVG", st.waveformSVG);
        arr.add (juce::var (obj));
    }

    juce::DynamicObject::Ptr root = new juce::DynamicObject();
    root->setProperty ("players", juce::var (arr));
    root->setProperty ("count", (int) players.size());
    return juce::var (root);
}

void SuperSamplerProcessor::loadSampleAsync (int playerId, const juce::File& file, std::function<void (bool, juce::String)> onComplete)
{
    std::thread ([this, playerId, file, cb = std::move (onComplete)]() mutable
    {
        juce::String error;
        const bool ok = loadSampleInternal (playerId, file, error);

        if (cb != nullptr)
        {
            juce::MessageManager::callAsync ([cbLocal = std::move (cb), ok, error]() mutable
            {
                cbLocal (ok, error);
            });
        }
    }).detach();
}

bool SuperSamplerProcessor::setMidiRange (int playerId, int low, int high)
{
    const std::lock_guard<std::mutex> lock (playerMutex);
    if (auto* player = getPlayer (playerId))
    {
        player->setMidiRange (low, high);
        return true;
    }
    return false;
}

bool SuperSamplerProcessor::setGain (int playerId, float gain)
{
    const std::lock_guard<std::mutex> lock (playerMutex);
    if (auto* player = getPlayer (playerId))
    {
        player->setGain (gain);
        return true;
    }
    return false;
}

bool SuperSamplerProcessor::trigger (int playerId)
{
    const std::lock_guard<std::mutex> lock (playerMutex);
    if (auto* player = getPlayer (playerId))
    {
        // DBG("SuperSamplerProcessor::trigger: playing sampler " << playerId);
        player->trigger();
        return true;
    }
    return false;
}

juce::String SuperSamplerProcessor::getWaveformSVG (int playerId) const
{
    const std::lock_guard<std::mutex> lock (playerMutex);
    if (auto* player = getPlayer (playerId))
        return player->getWaveformSVG();

    return WaveformSVGRenderer::generateBlankWaveformSVG();
}

std::vector<float> SuperSamplerProcessor::getWaveformPoints (int playerId) const
{
    const std::lock_guard<std::mutex> lock (playerMutex);
    if (auto* player = getPlayer (playerId))
        return player->getWaveformPoints();

    return {};
}

std::shared_ptr<std::string> SuperSamplerProcessor::getVuJson() const
{
    const juce::SpinLock::ScopedLockType guard (vuLock);
    return std::make_shared<std::string>(vuJson);
}

juce::ValueTree SuperSamplerProcessor::exportToValueTree() const
{
    const std::lock_guard<std::mutex> lock (playerMutex);
    juce::ValueTree root ("SamplerState");
    root.setProperty ("count", (int) players.size(), nullptr);

    for (const auto& p : players)
    {
        auto st = p->getState();
        juce::ValueTree child ("Player");
        child.setProperty ("id", st.id, nullptr);
        child.setProperty ("midiLow", st.midiLow, nullptr);
        child.setProperty ("midiHigh", st.midiHigh, nullptr);
        child.setProperty ("gain", st.gain, nullptr);
        child.setProperty ("filePath", st.filePath, nullptr);
        child.setProperty ("status", st.status, nullptr);
        root.addChild (child, -1, nullptr);
    }

    return root;
}

void SuperSamplerProcessor::importFromValueTree (const juce::ValueTree& tree)
{
    struct PendingPlayer
    {
        SuperSamplePlayer::State state;
        juce::String path;
    };

    std::vector<PendingPlayer> pending;

    for (int i = 0; i < tree.getNumChildren(); ++i)
    {
        auto child = tree.getChild (i);
        if (! child.hasType ("Player"))
            continue;

        PendingPlayer p;
        p.state.id = (int) child.getProperty ("id", nextId);
        p.state.midiLow = (int) child.getProperty ("midiLow", 36);
        p.state.midiHigh = (int) child.getProperty ("midiHigh", 60);
        p.state.gain = (float) child.getProperty ("gain", 1.0f);
        p.path = child.getProperty ("filePath").toString();
        pending.push_back (p);
    }

    {
        const std::lock_guard<std::mutex> lock (playerMutex);
        players.clear();
        nextId = 1;

        for (const auto& p : pending)
        {
            auto player = std::make_unique<SuperSamplePlayer> (p.state.id);
            player->setMidiRange (p.state.midiLow, p.state.midiHigh);
            player->setGain (p.state.gain);
            player->setFilePathAndStatus (p.path, p.path.isNotEmpty() ? "pending" : "empty");

            nextId = std::max (nextId, p.state.id + 1);
            players.push_back (std::move (player));
        }
    }

    for (const auto& p : pending)
    {
        if (p.path.isNotEmpty())
        {
            juce::String error;
            auto ok = loadSampleInternal (p.state.id, juce::File (p.path), error);
            if (! ok)
                if (auto* player = getPlayer (p.state.id))
                    player->markError (p.path, error.isNotEmpty() ? error : "missing");
        }
    }
}

bool SuperSamplerProcessor::loadSampleInternal (int playerId, const juce::File& file, juce::String& error)
{
    if (! file.existsAsFile())
    {
        error = "File not found";
        return false;
    }

    std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (file));

    if (reader == nullptr)
    {
        error = "Unsupported file format";
        return false;
    }

    const int64 totalSamples = reader->lengthInSamples;
    const int numChannels = (int) juce::jmin ((int64) reader->numChannels, (int64) 2);

    if (totalSamples <= 0 || numChannels <= 0)
    {
        error = "Empty or invalid audio file";
        return false;
    }

    juce::AudioBuffer<float> tempBuffer (numChannels, (int) totalSamples);
    reader->read (&tempBuffer, 0, (int) totalSamples, 0, true, true);

    const std::lock_guard<std::mutex> lock (playerMutex);
    if (auto* player = getPlayer (playerId))
    {
        player->setFilePathAndStatus (file.getFullPathName(), "loading", file.getFileName());
        player->setLoadedBuffer (std::move (tempBuffer), file.getFileName());
        return true;
    }

    error = "Player not found";
    return false;
}

SuperSamplePlayer* SuperSamplerProcessor::getPlayer (int playerId) const
{
    for (auto& p : players)
    {
        if (p->getId() == playerId)
            return p.get();
    }
    return nullptr;
}
