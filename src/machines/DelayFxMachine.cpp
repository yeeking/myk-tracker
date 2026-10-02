#include "DelayFxMachine.h"
#include "MachineStateCodec.h"
#include <cmath>

namespace
{
constexpr double kDelayStateVersion = 1.0;
}

void DelayFxMachine::prepareDsp(double sampleRate, int samplesPerBlock)
{
    juce::ignoreUnused(samplesPerBlock);
    const std::lock_guard<std::mutex> lock(stateMutex);
    currentSampleRate = sanitizedSampleRate(sampleRate);
    resizeDelayBuffer();
}

void DelayFxMachine::clearTransientState()
{
    const std::lock_guard<std::mutex> lock(stateMutex);
    clearDelayBuffer();
}

std::vector<std::vector<UIBox>> DelayFxMachine::getUIBoxes(const MachineUiContext& context)
{
    juce::ignoreUnused(context);
    const std::lock_guard<std::mutex> lock(stateMutex);

    std::vector<std::vector<UIBox>> boxes(2, std::vector<UIBox>(5));

    auto buildFloatCell = [this](float* target, float step, float minValue, float maxValue, int decimals)
    {
        return makeFloatCell(*target, minValue, maxValue, decimals,
            [this, target, step, minValue, maxValue](int direction)
            {
                const std::lock_guard<std::mutex> guard(stateMutex);
                *target = juce::jlimit(minValue, maxValue, *target + step * static_cast<float>(direction));
            });
    };

    boxes[0][0].kind = UIBox::Kind::TrackerCell;
    boxes[0][0].text = "MODE";
    boxes[1][0].kind = UIBox::Kind::TrackerCell;
    boxes[1][0].text = getModeName(mode);
    boxes[1][0].onAdjust = [this](int direction)
    {
        const std::lock_guard<std::mutex> guard(stateMutex);
        int next = static_cast<int>(mode) + direction;
        if (next < 0)
            next = static_cast<int>(DelayMode::milliseconds);
        if (next > static_cast<int>(DelayMode::milliseconds))
            next = static_cast<int>(DelayMode::sync);
        mode = static_cast<DelayMode>(next);
    };

    boxes[0][1].kind = UIBox::Kind::TrackerCell;
    boxes[0][1].text = "TIME";
    boxes[1][1].kind = UIBox::Kind::TrackerCell;
    boxes[1][1].text = std::to_string(syncTicks);
    boxes[1][1].isDisabled = mode != DelayMode::sync;
    boxes[1][1].onAdjust = [this](int direction)
    {
        const std::lock_guard<std::mutex> guard(stateMutex);
        syncTicks = juce::jlimit(1, 64, syncTicks + direction);
    };
    boxes[1][1].hasValueScale = true;
    boxes[1][1].valueNorm = juce::jlimit(0.0f, 1.0f, (syncTicks - 1) / 63.0f);

    boxes[0][2].kind = UIBox::Kind::TrackerCell;
    boxes[0][2].text = "MS";
    boxes[1][2] = buildFloatCell(&delayMs, 5.0f, 1.0f, static_cast<float>(kMaxDelaySeconds * 1000), 0);
    boxes[1][2].isDisabled = mode != DelayMode::milliseconds;

    boxes[0][3].kind = UIBox::Kind::TrackerCell;
    boxes[0][3].text = "FDBK";
    boxes[1][3] = buildFloatCell(&feedback, 0.05f, 0.0f, 0.95f, 2);

    boxes[0][4].kind = UIBox::Kind::TrackerCell;
    boxes[0][4].text = "MIX";
    boxes[1][4] = buildFloatCell(&mix, 0.05f, 0.0f, 1.0f, 2);

    return boxes;
}

void DelayFxMachine::processAudioBuffer(juce::AudioBuffer<float>& buffer)
{
    const std::lock_guard<std::mutex> lock(stateMutex);
    if (delayBuffer.getNumSamples() == 0 || buffer.getNumSamples() == 0)
        return;

    const int delaySamples = getDelaySamples();
    const int delayBufferSamples = delayBuffer.getNumSamples();
    const int readPositionBase = (writePosition - delaySamples + delayBufferSamples) % delayBufferSamples;

    for (int sampleIndex = 0; sampleIndex < buffer.getNumSamples(); ++sampleIndex)
    {
        const int readPosition = (readPositionBase + sampleIndex) % delayBufferSamples;
        const int writeIndex = (writePosition + sampleIndex) % delayBufferSamples;

        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        {
            const int delayChannel = juce::jlimit(0, delayBuffer.getNumChannels() - 1, channel);
            const float dry = buffer.getSample(channel, sampleIndex);
            const float delayed = delayBuffer.getSample(delayChannel, readPosition);
            const float wet = dry + (delayed * feedback);

            delayBuffer.setSample(delayChannel, writeIndex, wet);
            buffer.setSample(channel, sampleIndex, juce::jmap(mix, dry, delayed));
        }
    }

    writePosition = (writePosition + buffer.getNumSamples()) % delayBufferSamples;
}

void DelayFxMachine::setSecondsPerTick(double secondsPerTick)
{
    const std::lock_guard<std::mutex> lock(stateMutex);
    if (secondsPerTick > 0.0)
        currentSecondsPerTick = secondsPerTick;
}

void DelayFxMachine::allNotesOff()
{
    clearTransientState();
}

void DelayFxMachine::tick(int quarterBeat, bool isQuarterNoteBoundary)
{
    const std::lock_guard<std::mutex> lock(stateMutex);
    juce::ignoreUnused(isQuarterNoteBoundary);
    currentQuarterBeat = juce::jlimit(0, 16, quarterBeat);
}

void DelayFxMachine::reset()
{
    const std::lock_guard<std::mutex> lock(stateMutex);
    currentQuarterBeat = 0;
    clearDelayBuffer();
}

void DelayFxMachine::getStateInformation(juce::MemoryBlock& destData)
{
    const std::lock_guard<std::mutex> lock(stateMutex);
    juce::DynamicObject::Ptr root = new juce::DynamicObject();
    root->setProperty("version", kDelayStateVersion);
    root->setProperty("mode", static_cast<int>(mode));
    root->setProperty("syncTicks", syncTicks);
    root->setProperty("delayMs", delayMs);
    root->setProperty("feedback", feedback);
    root->setProperty("mix", mix);

    writeMachineStateJson(destData, juce::var(root.get()));
}

void DelayFxMachine::setStateInformation(const void* data, int sizeInBytes)
{
    const juce::var parsed = parseMachineStateJson(data, sizeInBytes);
    if (parsed.isVoid())
        return;

    const std::lock_guard<std::mutex> lock(stateMutex);
    mode = static_cast<DelayMode>(getIntProperty(parsed, "mode", static_cast<int>(mode), 0, 1));
    syncTicks = getIntProperty(parsed, "syncTicks", syncTicks, 1, 64);
    delayMs = getFloatProperty(parsed, "delayMs", delayMs, 1.0f, static_cast<float>(kMaxDelaySeconds * 1000));
    feedback = getFloatProperty(parsed, "feedback", feedback, 0.0f, 0.95f);
    mix = getFloatProperty(parsed, "mix", mix, 0.0f, 1.0f);
    clearDelayBuffer();
}

void DelayFxMachine::resizeDelayBuffer()
{
    const int maxDelaySamples = juce::jmax(1, static_cast<int>(std::ceil(currentSampleRate * static_cast<double>(kMaxDelaySeconds))));
    delayBuffer.setSize(2, maxDelaySamples);
    writePosition = 0;
}

void DelayFxMachine::clearDelayBuffer()
{
    delayBuffer.clear();
    writePosition = 0;
}

int DelayFxMachine::getDelaySamples() const
{
    if (mode == DelayMode::sync)
        return juce::jlimit(1, juce::jmax(1, delayBuffer.getNumSamples() - 1), static_cast<int>(std::round(currentSecondsPerTick * static_cast<double>(syncTicks) * currentSampleRate)));

    return juce::jlimit(1, juce::jmax(1, delayBuffer.getNumSamples() - 1), static_cast<int>(std::round((static_cast<double>(delayMs) / 1000.0) * currentSampleRate)));
}

const char* DelayFxMachine::getModeName(DelayMode modeValue)
{
    switch (modeValue)
    {
        case DelayMode::sync: return "SYNC";
        case DelayMode::milliseconds: return "MS";
        default: return "---";
    }
}
