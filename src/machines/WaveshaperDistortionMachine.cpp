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

    boxes[0][0].kind = UIBox::Kind::TrackerCell;
    boxes[0][0].text = "DRV";
    boxes[1][0] = makeAtomicFloatCell(drive, 0.5f, kMinDrive, kMaxDrive, 1);

    boxes[0][1].kind = UIBox::Kind::TrackerCell;
    boxes[0][1].text = "TONE";
    boxes[1][1] = makeAtomicFloatCell(tone, 0.05f, 0.0f, 1.0f, 2);

    boxes[0][2].kind = UIBox::Kind::TrackerCell;
    boxes[0][2].text = "MIX";
    boxes[1][2] = makeAtomicFloatCell(mix, 0.05f, 0.0f, 1.0f, 2);

    boxes[0][3].kind = UIBox::Kind::TrackerCell;
    boxes[0][3].text = "OUT";
    boxes[1][3] = makeAtomicFloatCell(output, 0.05f, 0.0f, 2.0f, 2);

    return boxes;
}

void WaveshaperDistortionMachine::processAudioBuffer(juce::AudioBuffer<float>& buffer)
{
    auto focusValue = [this](int target)
    {
        const int slot = focusedSlots[static_cast<std::size_t>(target)].load(std::memory_order_relaxed);
        return slot >= 0 && modulationBlock != nullptr
            ? modulationBlock->get(focusDestinationForSlot(static_cast<std::size_t>(slot)), 0) : 0.0f;
    };
    // Distortion coefficients are intentionally block-rate. It avoids an
    // extra per-sample exponential while still making F slots responsive.
    const float driveValue = juce::jlimit(kMinDrive, kMaxDrive,
        drive.load(std::memory_order_relaxed) * std::exp2(focusValue(0) * 2.0f));
    const float toneValue = juce::jlimit(0.0f, 1.0f,
        tone.load(std::memory_order_relaxed) + focusValue(1) * 0.5f);
    const float mixValue = juce::jlimit(0.0f, 1.0f,
        mix.load(std::memory_order_relaxed) + focusValue(2) * 0.5f);
    const float outputValue = juce::jlimit(0.0f, 2.0f,
        output.load(std::memory_order_relaxed) * std::exp2(focusValue(3)));
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

void WaveshaperDistortionMachine::setFocusedParameterBinding(std::size_t slot, const std::string& key)
{
    if (slot >= kFocusedModulationDestinationCount) return;
    // Keys are generated from the unbanked logical grid: values are col 1.
    for (auto& bound : focusedSlots)
        if (bound.load(std::memory_order_relaxed) == static_cast<int>(slot))
            bound.store(-1, std::memory_order_relaxed);
    if (key == "grid-1-0") focusedSlots[0].store(static_cast<int>(slot), std::memory_order_relaxed);
    else if (key == "grid-1-1") focusedSlots[1].store(static_cast<int>(slot), std::memory_order_relaxed);
    else if (key == "grid-1-2") focusedSlots[2].store(static_cast<int>(slot), std::memory_order_relaxed);
    else if (key == "grid-1-3") focusedSlots[3].store(static_cast<int>(slot), std::memory_order_relaxed);
}

bool WaveshaperDistortionMachine::supportsFocusedModulation(const std::string& key) const
{
    return key == "grid-1-0" || key == "grid-1-1" || key == "grid-1-2" || key == "grid-1-3";
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
