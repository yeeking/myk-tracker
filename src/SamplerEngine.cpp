#include "SamplerEngine.h"
#include "WaveformSVGRenderer.h"
#include <algorithm>
#include <sstream>

SamplerEngine::SamplerEngine()
{
    formatManager.registerBasicFormats();
    vuJson = "{\"dB_out\":[]}";
}

int SamplerEngine::addSamplePlayer()
{
    const std::lock_guard<std::mutex> lock (playerMutex);
    auto id = nextId++;
    players.push_back (std::make_unique<SamplePlayer> (id));
    return id;
}

bool SamplerEngine::removeSamplePlayer (int playerId)
{
    const std::lock_guard<std::mutex> lock (playerMutex);
    auto it = std::find_if (players.begin(), players.end(),
        [playerId](const std::unique_ptr<SamplePlayer>& player)
        {
            return player->getId() == playerId;
        });
    if (it == players.end())
        return false;
    players.erase (it);
    return true;
}

void SamplerEngine::processBlock (juce::AudioBuffer<float>& buffer, const juce::MidiBuffer& midi)
{
    // Build a per-sample list of note-ons for sample-accurate triggering
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
        // trigger any note-ons scheduled for this sample
        if (! noteOns[(size_t) sample].empty())
        {
            for (int note : noteOns[(size_t) sample])
            {
                for (auto& player : players)
                {
                    if (player->acceptsNote (note)){
                        player->triggerNote (note);
                    }
                }
            }
        }

        // mix current sample from all players
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

    std::ostringstream vuBuilder;
    vuBuilder.setf (std::ios::fixed);
    vuBuilder.precision (2);
    vuBuilder << "{\"dB_out\":[";
    for (size_t i = 0; i < players.size(); ++i)
    {
        if (i > 0)
            vuBuilder << ',';
        vuBuilder << players[i]->getLastVuDb();
    }
    vuBuilder << "]}";
    {
        const juce::SpinLock::ScopedLockType guard (vuLock);
        vuJson = vuBuilder.str();
    }
}

juce::var SamplerEngine::toVar() const
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

void SamplerEngine::loadSampleAsync (int playerId, const juce::File& file, std::function<void (bool, juce::String)> onComplete)
{
    // simple background task; avoids blocking audio thread or message thread
    std::thread ([this, playerId, file, cb = std::move (onComplete)]() mutable
    {
        juce::String error;
        const bool ok = loadSampleInternal (playerId, file, error);

        if (cb != nullptr)
        {
            juce::MessageManager::callAsync ([cb = std::move (cb), ok, error]() mutable
            {
                cb (ok, error);
            });
        }
    }).detach();
}

bool SamplerEngine::loadSampleInternal (int playerId, const juce::File& file, juce::String& error)
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

SamplePlayer* SamplerEngine::getPlayer (int playerId) const
{
    for (auto& p : players)
    {
        if (p->getId() == playerId)
            return p.get();
    }
    return nullptr;
}

bool SamplerEngine::setMidiRange (int playerId, int low, int high)
{
    const std::lock_guard<std::mutex> lock (playerMutex);
    if (auto* player = getPlayer (playerId))
    {
        player->setMidiRange (low, high);
        return true;
    }
    return false;
}

bool SamplerEngine::setGain (int playerId, float gain)
{
    const std::lock_guard<std::mutex> lock (playerMutex);
    if (auto* player = getPlayer (playerId))
    {
        player->setGain (gain);
        return true;
    }
    return false;
}

bool SamplerEngine::trigger (int playerId)
{
    const std::lock_guard<std::mutex> lock (playerMutex);
    if (auto* player = getPlayer (playerId))
    {
        DBG("SamplerEngine::trigger: playing sampler " << playerId);
        player->trigger();
        return true;
    }
    return false;
}

juce::String SamplerEngine::getWaveformSVG (int playerId) const
{
    const std::lock_guard<std::mutex> lock (playerMutex);
    if (auto* player = getPlayer (playerId))
        return player->getWaveformSVG();

    return WaveformSVGRenderer::generateBlankWaveformSVG();
}

std::vector<float> SamplerEngine::getWaveformPoints (int playerId) const
{
    const std::lock_guard<std::mutex> lock (playerMutex);
    if (auto* player = getPlayer (playerId))
        return player->getWaveformPoints();

    return {};
}

std::shared_ptr<std::string> SamplerEngine::getVuJson() const
{
    const juce::SpinLock::ScopedLockType guard (vuLock);
    return std::make_shared<std::string>(vuJson);
}

juce::ValueTree SamplerEngine::exportToValueTree() const
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

void SamplerEngine::importFromValueTree (const juce::ValueTree& tree)
{
    struct PendingPlayer
    {
        SamplePlayer::State state;
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
            auto player = std::make_unique<SamplePlayer> (p.state.id);
            player->setMidiRange (p.state.midiLow, p.state.midiHigh);
            player->setGain (p.state.gain);
            player->setFilePathAndStatus (p.path, p.path.isNotEmpty() ? "pending" : "empty");

            nextId = std::max (nextId, p.state.id + 1);
            players.push_back (std::move (player));
        }
    }

    // Load files outside the lock to avoid blocking.
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
