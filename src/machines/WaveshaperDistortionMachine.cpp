#include "WaveshaperDistortionMachine.h"
#include "MachineStateCodec.h"
#include "MachineUi.h"
#include <cmath>

namespace
{
constexpr double kDistortionStateVersion = 1.0;
}

void WaveshaperDistortionMachine::prepareDsp(double sampleRate, int samplesPerBlock)
{
    juce::ignoreUnused(samplesPerBlock);
    currentSampleRate.store(sanitizedSampleRate(sampleRate), std::memory_order_relaxed);
}

void WaveshaperDistortionMachine::clearTransientState()
{
    resetToneState();
}

std::vector<std::vector<UIBox>> WaveshaperDistortionMachine::getUIBoxes(const MachineUiContext& context)
{
    juce::ignoreUnused(context);
    std::vector<std::vector<UIBox>> boxes(2, std::vector<UIBox>(4));

    auto makeValueCell = [](std::atomic<float>& target, float step, float minValue, float maxValue, int decimals)
    {
        return makeFloatCell(target.load(std::memory_order_relaxed), minValue, maxValue, decimals,
            [&target, step, minValue, maxValue](int direction)
            {
                const float currentValue = target.load(std::memory_order_relaxed);
                const float nextValue = juce::jlimit(minValue, maxValue, currentValue + step * static_cast<float>(direction));
                target.store(nextValue, std::memory_order_relaxed);
            });
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

    writeMachineStateJson(destData, juce::var(root.get()));
}

void WaveshaperDistortionMachine::setStateInformation(const void* data, int sizeInBytes)
{
    const juce::var parsed = parseMachineStateJson(data, sizeInBytes);
    if (parsed.isVoid())
        return;

    const float driveValue = getFloatProperty(parsed, "drive", drive.load(std::memory_order_relaxed), kMinDrive, kMaxDrive);
    const float toneValue = getFloatProperty(parsed, "tone", tone.load(std::memory_order_relaxed), 0.0f, 1.0f);
    const float mixValue = getFloatProperty(parsed, "mix", mix.load(std::memory_order_relaxed), 0.0f, 1.0f);
    const float outputValue = getFloatProperty(parsed, "output", output.load(std::memory_order_relaxed), 0.0f, 2.0f);
    drive.store(driveValue, std::memory_order_relaxed);
    tone.store(toneValue, std::memory_order_relaxed);
    mix.store(mixValue, std::memory_order_relaxed);
    output.store(outputValue, std::memory_order_relaxed);
    resetToneState();
}

void WaveshaperDistortionMachine::resetToneState()
{
    toneState.fill(0.0f);
}
