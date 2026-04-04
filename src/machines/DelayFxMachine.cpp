#include "DelayFxMachine.h"
#include <cmath>

namespace
{
constexpr double kDelayStateVersion = 1.0;
}

void DelayFxMachine::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    juce::ignoreUnused(samplesPerBlock);
    const std::lock_guard<std::mutex> lock(stateMutex);
    currentSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
    resizeDelayBuffer();
    clearDelayBuffer();
}

void DelayFxMachine::releaseResources()
{
    const std::lock_guard<std::mutex> lock(stateMutex);
    clearDelayBuffer();
}

std::vector<std::vector<UIBox>> DelayFxMachine::getUIBoxes(const MachineUiContext& context)
{
    juce::ignoreUnused(context);
    const std::lock_guard<std::mutex> lock(stateMutex);

    std::vector<std::vector<UIBox>> boxes(2, std::vector<UIBox>(5));

    auto makeFloatCell = [this](float* target, float step, float minValue, float maxValue, int decimals)
    {
        UIBox cell;
        cell.kind = UIBox::Kind::TrackerCell;
        cell.text = formatFloat(*target, decimals);
        cell.onAdjust = [this, target, step, minValue, maxValue](int direction)
        {
            const std::lock_guard<std::mutex> guard(stateMutex);
            *target = juce::jlimit(minValue, maxValue, *target + step * static_cast<float>(direction));
        };
        return cell;
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

    boxes[0][2].kind = UIBox::Kind::TrackerCell;
    boxes[0][2].text = "MS";
    boxes[1][2] = makeFloatCell(&delayMs, 5.0f, 1.0f, static_cast<float>(kMaxDelaySeconds * 1000), 0);
    boxes[1][2].isDisabled = mode != DelayMode::milliseconds;

    boxes[0][3].kind = UIBox::Kind::TrackerCell;
    boxes[0][3].text = "FDBK";
    boxes[1][3] = makeFloatCell(&feedback, 0.05f, 0.0f, 0.95f, 2);

    boxes[0][4].kind = UIBox::Kind::TrackerCell;
    boxes[0][4].text = "MIX";
    boxes[1][4] = makeFloatCell(&mix, 0.05f, 0.0f, 1.0f, 2);

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
    const std::lock_guard<std::mutex> lock(stateMutex);
    clearDelayBuffer();
}

void DelayFxMachine::tick(int quarterBeat)
{
    const std::lock_guard<std::mutex> lock(stateMutex);
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

    const auto json = juce::JSON::toString(juce::var(root.get()));
    destData.reset();
    destData.append(json.toRawUTF8(), json.getNumBytesAsUTF8());
}

void DelayFxMachine::setStateInformation(const void* data, int sizeInBytes)
{
    if (data == nullptr || sizeInBytes <= 0)
        return;
    if (!juce::CharPointer_UTF8::isValidString(static_cast<const char*>(data), sizeInBytes))
        return;

    const auto json = juce::String::fromUTF8(static_cast<const char*>(data), sizeInBytes);
    if (json.isEmpty())
        return;

    const auto parsed = juce::JSON::fromString(json);
    if (!parsed.isObject())
        return;

    const std::lock_guard<std::mutex> lock(stateMutex);
    mode = static_cast<DelayMode>(juce::jlimit(0, 1, static_cast<int>(parsed.getProperty("mode", static_cast<int>(mode)))));
    syncTicks = juce::jlimit(1, 64, static_cast<int>(parsed.getProperty("syncTicks", syncTicks)));
    delayMs = juce::jlimit(1.0f, static_cast<float>(kMaxDelaySeconds * 1000), static_cast<float>(parsed.getProperty("delayMs", delayMs)));
    feedback = juce::jlimit(0.0f, 0.95f, static_cast<float>(parsed.getProperty("feedback", feedback)));
    mix = juce::jlimit(0.0f, 1.0f, static_cast<float>(parsed.getProperty("mix", mix)));
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

std::string DelayFxMachine::formatFloat(float value, int decimals)
{
    return juce::String(value, decimals).toStdString();
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
