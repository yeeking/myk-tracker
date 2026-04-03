#include "SuperSamplePlayer.h"
#include "WaveformSVGRenderer.h"
#include <algorithm>
#include <cmath>
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
    if (sampleBuffer.getNumSamples() > 0)
    {
        velocityGain = 1.0f;
        resetPlaybackState (true);
    }
}

void SuperSamplePlayer::triggerNote (int midiNote, int velocity)
{
    juce::ignoreUnused (midiNote);
    if (sampleBuffer.getNumSamples() > 0)
    {
        velocityGain = juce::jlimit(0.0f, 1.0f, static_cast<float>(velocity) / 127.0f);
        resetPlaybackState (true);
    }
}

void SuperSamplePlayer::stop() noexcept
{
    resetPlaybackState (false);
}

void SuperSamplePlayer::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    juce::ignoreUnused (samplesPerBlock);

    const double nextOutputRate = sampleRate > 0.0 ? sampleRate : 44100.0;
    if (std::abs (nextOutputRate - outputSampleRate) < 1.0e-9)
        return;

    outputSampleRate = nextOutputRate;
    updatePlaybackRatio();
    resetPlaybackState (false);
}

void SuperSamplePlayer::renderToBuffer (juce::AudioBuffer<float>& buffer, int startSample, int numSamples) noexcept
{
    if (! state.isPlaying || sampleBuffer.getNumSamples() == 0 || numSamples <= 0)
        return;

    const int totalSourceSamples = sampleBuffer.getNumSamples();
    const int numSourceChans = sampleBuffer.getNumChannels();
    const int numOutputChans = buffer.getNumChannels();
    const float gain = state.gain * velocityGain;

    for (int sampleOffset = 0; sampleOffset < numSamples; ++sampleOffset)
    {
        if (fractionalPlaybackPosition >= static_cast<double> (totalSourceSamples))
        {
            state.isPlaying = false;
            break;
        }

        const int sourceIndex = juce::jlimit (0, totalSourceSamples - 1, static_cast<int> (fractionalPlaybackPosition));
        const int nextSourceIndex = juce::jmin (sourceIndex + 1, totalSourceSamples - 1);
        const float fractionalPart = static_cast<float> (fractionalPlaybackPosition - static_cast<double> (sourceIndex));
        float firstOutputSample = 0.0f;

        for (int sourceChannel = 0; sourceChannel < numSourceChans; ++sourceChannel)
        {
            const float* samples = sampleBuffer.getReadPointer (sourceChannel);
            const float current = samples[sourceIndex];
            const float next = samples[nextSourceIndex];
            const float rendered = juce::jmap (fractionalPart, current, next) * gain;

            if (sourceChannel == 0)
                firstOutputSample = rendered;

            for (int outputChannel = 0; outputChannel < numOutputChans; ++outputChannel)
            {
                if (juce::jmin (outputChannel, numSourceChans - 1) == sourceChannel)
                    buffer.addSample (outputChannel, startSample + sampleOffset, rendered);
            }
        }

        pushVuSample (firstOutputSample);
        fractionalPlaybackPosition += playbackRatio;
        sourceReadIndex = static_cast<int> (fractionalPlaybackPosition);
    }
}

bool SuperSamplePlayer::setLoadedBuffer (juce::AudioBuffer<float>&& newBuffer, const juce::String& name, double newSourceSampleRate)
{
    sampleBuffer = std::move (newBuffer);
    sourceSampleRate = newSourceSampleRate > 0.0 ? newSourceSampleRate : 44100.0;
    updatePlaybackRatio();
    state.status = "loaded";
    state.fileName = name;
    // Preserve path if already set, otherwise infer from name.
    if (state.filePath.isEmpty())
        state.filePath = name;
    velocityGain = 1.0f;
    resetPlaybackState (false);
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
    sourceSampleRate = 44100.0;
    updatePlaybackRatio();
    state.status = "error";
    state.filePath = path;
    state.fileName = message.isNotEmpty() ? message : juce::File (path).getFileName();
    state.waveformSVG = WaveformSVGRenderer::generateBlankWaveformSVG();
    waveformPoints = buildWaveformPoints(sampleBuffer, kWaveformPlotPoints);
    vuBuffer.assign ((size_t) vuBufferSize, 0.0f);
    vuWritePos = 0;
    vuSum = 0.0f;
    lastVuDb = -60.0f;
    velocityGain = 1.0f;
    resetPlaybackState (false);
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

void SuperSamplePlayer::resetPlaybackState (bool keepPlaying) noexcept
{
    sourceReadIndex = 0;
    fractionalPlaybackPosition = 0.0;
    state.isPlaying = keepPlaying && sampleBuffer.getNumSamples() > 0;
}

void SuperSamplePlayer::updatePlaybackRatio() noexcept
{
    const double safeOutputRate = outputSampleRate > 0.0 ? outputSampleRate : 44100.0;
    const double safeSourceRate = sourceSampleRate > 0.0 ? sourceSampleRate : safeOutputRate;
    playbackRatio = safeSourceRate / safeOutputRate;
}
