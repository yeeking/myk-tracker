#include "SuperSamplePlayer.h"
#include "WaveformSVGRenderer.h"
#include <algorithm>
#include <limits>

namespace
{
constexpr int kWaveformPlotPoints = 128;

std::vector<float> buildWaveformPoints(const juce::AudioBuffer<float>& buffer, int numPoints)
{
    std::vector<float> points;
    if (numPoints < 2)
        numPoints = 2;

    points.reserve(static_cast<size_t>(numPoints) * 2);

    if (buffer.getNumSamples() == 0 || buffer.getNumChannels() == 0)
    {
        for (int i = 0; i < numPoints; ++i)
        {
            points.push_back(0.0f);
            points.push_back(0.0f);
        }
        return points;
    }

    const int totalSamples = buffer.getNumSamples();
    const int samplesPerPoint = std::max(1, totalSamples / numPoints);

    for (int start = 0; start < totalSamples; start += samplesPerPoint)
    {
        const int end = std::min(totalSamples, start + samplesPerPoint);
        float localMin = std::numeric_limits<float>::max();
        float localMax = std::numeric_limits<float>::lowest();
        bool hasSample = false;

        for (int chan = 0; chan < buffer.getNumChannels(); ++chan)
        {
            const float* data = buffer.getReadPointer(chan);
            for (int i = start; i < end; ++i)
            {
                const float sample = data[i];
                hasSample = true;
                localMin = std::min(localMin, sample);
                localMax = std::max(localMax, sample);
            }
        }

        if (!hasSample)
        {
            localMin = 0.0f;
            localMax = 0.0f;
        }

        points.push_back(localMin);
        points.push_back(localMax);
    }

    if (points.size() < static_cast<size_t>(numPoints) * 2)
    {
        const size_t missing = static_cast<size_t>(numPoints) * 2 - points.size();
        points.insert(points.end(), missing, 0.0f);
    }

    return points;
}
} // namespace

SuperSamplePlayer::SuperSamplePlayer (int newId)
{
    state.id = newId;
    state.waveformSVG = WaveformSVGRenderer::generateBlankWaveformSVG();
    vuBuffer.assign ((size_t) vuBufferSize, 0.0f);
    waveformPoints = buildWaveformPoints(sampleBuffer, kWaveformPlotPoints);
}

void SuperSamplePlayer::setMidiRange (int low, int high) noexcept
{
    low = juce::jlimit (0, 127, low);
    high = juce::jlimit (0, 127, high);
    state.midiLow = juce::jmin (low, high);
    state.midiHigh = juce::jmax (low, high);
}

void SuperSamplePlayer::setGain (float g) noexcept
{
    state.gain = juce::jlimit (0.0f, 2.0f, g);
}

void SuperSamplePlayer::setFilePathAndStatus (const juce::String& path, const juce::String& statusLabel, const juce::String& displayName)
{
    state.filePath = path;
    state.fileName = displayName.isNotEmpty() ? displayName : juce::File (path).getFileName();
    state.status = statusLabel;
}

SuperSamplePlayer::State SuperSamplePlayer::getState() const noexcept
{
    auto snapshot = state;
    snapshot.vuDb = lastVuDb;
    return snapshot;
}

bool SuperSamplePlayer::acceptsNote (int midiNote) const noexcept
{
    return midiNote >= state.midiLow && midiNote <= state.midiHigh && sampleBuffer.getNumSamples() > 0;
}

void SuperSamplePlayer::trigger()
{
        // DBG("SamplePlayer trigger ");

    if (sampleBuffer.getNumSamples() > 0)
    {
        // DBG("SamplePlayer triggered ");
        playHead = 0;
        state.isPlaying = true;
    }
}

void SuperSamplePlayer::triggerNote (int midiNote)
{
    juce::ignoreUnused (midiNote);
    trigger();
}

float SuperSamplePlayer::getNextSampleForChannel (int channel)
{
    // DBG("SamplerPlayer getNextSampleForChannel  " << playHead);

    if (! state.isPlaying || sampleBuffer.getNumSamples() == 0)
        return 0.0f;

    const int totalSamples = sampleBuffer.getNumSamples();
    if (playHead >= totalSamples)
    {
        state.isPlaying = false;
        return 0.0f;
    }

    const int numSampleChans = sampleBuffer.getNumChannels();
    const float* src = sampleBuffer.getReadPointer (juce::jmin (channel, numSampleChans - 1), playHead);
    float sample = src[0] * state.gain;

    if (channel == 0)
        pushVuSample (sample);

    ++playHead;
    if (playHead >= totalSamples)
        state.isPlaying = false;

    return sample;
}

bool SuperSamplePlayer::setLoadedBuffer (juce::AudioBuffer<float>&& newBuffer, const juce::String& name)
{
    sampleBuffer = std::move (newBuffer);
    state.status = "loaded";
    state.fileName = name;
    // Preserve path if already set, otherwise infer from name.
    if (state.filePath.isEmpty())
        state.filePath = name;
    playHead = 0;
    state.isPlaying = false;
    state.waveformSVG = WaveformSVGRenderer::generateWaveformSVG (sampleBuffer, 320);
    waveformPoints = buildWaveformPoints(sampleBuffer, kWaveformPlotPoints);
    vuBuffer.assign ((size_t) vuBufferSize, 0.0f);
    vuWritePos = 0;
    vuSum = 0.0f;
    lastVuDb = -60.0f;
    return true;
}

void SuperSamplePlayer::markError (const juce::String& path, const juce::String& message)
{
    sampleBuffer.setSize (0, 0);
    state.status = "error";
    state.filePath = path;
    state.fileName = message.isNotEmpty() ? message : juce::File (path).getFileName();
    state.waveformSVG = WaveformSVGRenderer::generateBlankWaveformSVG();
    waveformPoints = buildWaveformPoints(sampleBuffer, kWaveformPlotPoints);
    vuBuffer.assign ((size_t) vuBufferSize, 0.0f);
    vuWritePos = 0;
    vuSum = 0.0f;
    lastVuDb = -60.0f;
}

void SuperSamplePlayer::beginBlock() noexcept
{
    // no-op for now; samples are pushed per-sample
}

void SuperSamplePlayer::endBlock() noexcept
{
    const float average = vuBufferSize > 0 ? (vuSum / (float) vuBufferSize) : 0.0f;
    float db = juce::Decibels::gainToDecibels (average + 1.0e-6f, -80.0f);
    if (db < lastVuDb) {// hold peaks a bit
        db = (lastVuDb + db) / 2.0f; 
    }
    lastVuDb = juce::jlimit (-60.0f, 6.0f, db);
}

void SuperSamplePlayer::pushVuSample (float sample) noexcept
{
    if (vuBuffer.empty())
        return;

    const float mag = std::abs (sample);
    vuSum -= vuBuffer[(size_t) vuWritePos];
    vuBuffer[(size_t) vuWritePos] = mag;
    vuSum += mag;
    vuWritePos = (vuWritePos + 1) % vuBufferSize;
}
