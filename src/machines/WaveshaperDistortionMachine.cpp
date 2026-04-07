#include "WaveshaperDistortionMachine.h"
#include <cmath>

namespace
{
constexpr double kDistortionStateVersion = 1.0;
}

void WaveshaperDistortionMachine::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    juce::ignoreUnused(samplesPerBlock);
    currentSampleRate.store(sampleRate > 0.0 ? sampleRate : 44100.0, std::memory_order_relaxed);
    resetToneState();
}

void WaveshaperDistortionMachine::releaseResources()
{
    resetToneState();
}

std::vector<std::vector<UIBox>> WaveshaperDistortionMachine::getUIBoxes(const MachineUiContext& context)
{
    juce::ignoreUnused(context);
    std::vector<std::vector<UIBox>> boxes(2, std::vector<UIBox>(4));

    auto makeValueCell = [](std::atomic<float>& target, float step, float minValue, float maxValue, int decimals)
    {
        UIBox cell;
        cell.kind = UIBox::Kind::TrackerCell;
        cell.text = formatFloat(target.load(std::memory_order_relaxed), decimals);
        cell.onAdjust = [&target, step, minValue, maxValue](int direction)
        {
            const float currentValue = target.load(std::memory_order_relaxed);
            const float nextValue = juce::jlimit(minValue, maxValue, currentValue + step * static_cast<float>(direction));
            target.store(nextValue, std::memory_order_relaxed);
        };
        return cell;
    };

    boxes[0][0].kind = UIBox::Kind::TrackerCell;
    boxes[0][0].text = "DRV";
    boxes[1][0] = makeValueCell(drive, 0.5f, kMinDrive, kMaxDrive, 1);

    boxes[0][1].kind = UIBox::Kind::TrackerCell;
    boxes[0][1].text = "TONE";
    boxes[1][1] = makeValueCell(tone, 0.05f, 0.0f, 1.0f, 2);

    boxes[0][2].kind = UIBox::Kind::TrackerCell;
    boxes[0][2].text = "MIX";
    boxes[1][2] = makeValueCell(mix, 0.05f, 0.0f, 1.0f, 2);

    boxes[0][3].kind = UIBox::Kind::TrackerCell;
    boxes[0][3].text = "OUT";
    boxes[1][3] = makeValueCell(output, 0.05f, 0.0f, 2.0f, 2);

    return boxes;
}

void WaveshaperDistortionMachine::processAudioBuffer(juce::AudioBuffer<float>& buffer)
{
    const float driveValue = drive.load(std::memory_order_relaxed);
    const float toneValue = tone.load(std::memory_order_relaxed);
    const float mixValue = mix.load(std::memory_order_relaxed);
    const float outputValue = output.load(std::memory_order_relaxed);
    const float sampleRateValue = static_cast<float>(currentSampleRate.load(std::memory_order_relaxed));

    const float toneHz = juce::jmap(toneValue, 500.0f, 12000.0f);
    const float coefficient = std::exp(-juce::MathConstants<float>::twoPi * toneHz / juce::jmax(1.0f, sampleRateValue));

    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
    {
        auto* samples = buffer.getWritePointer(channel);
        float lowpassState = toneState[static_cast<std::size_t>(juce::jlimit(0, 1, channel))];
        for (int sampleIndex = 0; sampleIndex < buffer.getNumSamples(); ++sampleIndex)
        {
            const float dry = samples[sampleIndex];

            const float shaped = softClip(dry * driveValue);

            lowpassState = ((1.0f - coefficient) * shaped) + (coefficient * lowpassState);
            const float highpass = shaped - lowpassState;
            const float toned = juce::jmap(toneValue, lowpassState, highpass);
            const float wet = toned * outputValue;

            samples[sampleIndex] = juce::jmap(mixValue, dry, wet);
        }
        toneState[static_cast<std::size_t>(juce::jlimit(0, 1, channel))] = lowpassState;
    }
}

void WaveshaperDistortionMachine::getStateInformation(juce::MemoryBlock& destData)
{
    juce::DynamicObject::Ptr root = new juce::DynamicObject();
    root->setProperty("version", kDistortionStateVersion);
    root->setProperty("drive", drive.load(std::memory_order_relaxed));
    root->setProperty("tone", tone.load(std::memory_order_relaxed));
    root->setProperty("mix", mix.load(std::memory_order_relaxed));
    root->setProperty("output", output.load(std::memory_order_relaxed));

    const auto json = juce::JSON::toString(juce::var(root.get()));
    destData.reset();
    destData.append(json.toRawUTF8(), json.getNumBytesAsUTF8());
}

void WaveshaperDistortionMachine::setStateInformation(const void* data, int sizeInBytes)
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

    const float driveValue = juce::jlimit(kMinDrive, kMaxDrive, static_cast<float>(parsed.getProperty("drive", drive.load(std::memory_order_relaxed))));
    const float toneValue = juce::jlimit(0.0f, 1.0f, static_cast<float>(parsed.getProperty("tone", tone.load(std::memory_order_relaxed))));
    const float mixValue = juce::jlimit(0.0f, 1.0f, static_cast<float>(parsed.getProperty("mix", mix.load(std::memory_order_relaxed))));
    const float outputValue = juce::jlimit(0.0f, 2.0f, static_cast<float>(parsed.getProperty("output", output.load(std::memory_order_relaxed))));
    drive.store(driveValue, std::memory_order_relaxed);
    tone.store(toneValue, std::memory_order_relaxed);
    mix.store(mixValue, std::memory_order_relaxed);
    output.store(outputValue, std::memory_order_relaxed);
    resetToneState();
}

std::string WaveshaperDistortionMachine::formatFloat(float value, int decimals)
{
    return juce::String(value, decimals).toStdString();
}

float WaveshaperDistortionMachine::softClip(float input)
{
    return std::tanh(input);
}

void WaveshaperDistortionMachine::resetToneState()
{
    toneState.fill(0.0f);
}
