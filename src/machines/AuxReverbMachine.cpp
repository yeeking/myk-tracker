#include "AuxReverbMachine.h"
#include "MachineStateCodec.h"
#include "MachineUi.h"

AuxReverbMachine::AuxReverbMachine(const juce::Reverb::Parameters& defaults)
    : defaultParameters(defaults)
{
    roomSize.store(clampUnit(defaultParameters.roomSize), std::memory_order_relaxed);
    damping.store(clampUnit(defaultParameters.damping), std::memory_order_relaxed);
    wetLevel.store(clampUnit(defaultParameters.wetLevel), std::memory_order_relaxed);
    dryLevel.store(clampUnit(defaultParameters.dryLevel), std::memory_order_relaxed);
    width.store(clampUnit(defaultParameters.width), std::memory_order_relaxed);
    freezeMode.store(clampUnit(defaultParameters.freezeMode), std::memory_order_relaxed);
}

void AuxReverbMachine::prepareDsp(double sampleRate, int samplesPerBlock)
{
    juce::dsp::ProcessSpec spec;
    spec.sampleRate = sanitizedSampleRate(sampleRate);
    spec.maximumBlockSize = static_cast<juce::uint32>(juce::jmax(1, samplesPerBlock));
    spec.numChannels = 2;

    reverb.prepare(spec);
    updateParameters();
}

void AuxReverbMachine::clearTransientState()
{
    reverb.reset();
}

std::vector<std::vector<UIBox>> AuxReverbMachine::getUIBoxes(const MachineUiContext& context)
{
    juce::ignoreUnused(context);

    std::vector<std::vector<UIBox>> boxes(3, std::vector<UIBox>(4));

    boxes[0][0] = makeLabelCell("ROOM");
    boxes[1][0] = makeLabelCell("DAMP");
    boxes[2][0] = makeLabelCell("WET");
    const auto markDirty = [this] { dspDirty.store(true, std::memory_order_release); };
    boxes[0][1] = makeAtomicFloatCell(roomSize, 0.05f, 0.0f, 1.0f, 2, markDirty);
    boxes[1][1] = makeAtomicFloatCell(damping, 0.05f, 0.0f, 1.0f, 2, markDirty);
    boxes[2][1] = makeAtomicFloatCell(wetLevel, 0.05f, 0.0f, 1.0f, 2, markDirty);

    boxes[0][2] = makeLabelCell("DRY");
    boxes[1][2] = makeLabelCell("WID");
    boxes[2][2] = makeLabelCell("FRZ");
    boxes[0][3] = makeAtomicFloatCell(dryLevel, 0.05f, 0.0f, 1.0f, 2, markDirty);
    boxes[1][3] = makeAtomicFloatCell(width, 0.05f, 0.0f, 1.0f, 2, markDirty);
    boxes[2][3] = makeAtomicFloatCell(freezeMode, 0.05f, 0.0f, 1.0f, 2, markDirty);

    return boxes;
}

void AuxReverbMachine::processAudioBuffer(juce::AudioBuffer<float>& buffer)
{
    if (dspDirty.exchange(false, std::memory_order_acq_rel))
        updateParameters();

    auto block = juce::dsp::AudioBlock<float>(buffer);
    auto context = juce::dsp::ProcessContextReplacing<float>(block);
    reverb.process(context);
}

void AuxReverbMachine::getStateInformation(juce::MemoryBlock& destData)
{
    const auto parameters = captureParameters();
    juce::DynamicObject::Ptr root = new juce::DynamicObject();
    root->setProperty("version", kStateVersion);
    root->setProperty("roomSize", parameters.roomSize);
    root->setProperty("damping", parameters.damping);
    root->setProperty("wetLevel", parameters.wetLevel);
    root->setProperty("dryLevel", parameters.dryLevel);
    root->setProperty("width", parameters.width);
    root->setProperty("freezeMode", parameters.freezeMode);

    writeMachineStateJson(destData, juce::var(root.get()));
}

void AuxReverbMachine::setStateInformation(const void* data, int sizeInBytes)
{
    const juce::var parsed = parseMachineStateJson(data, sizeInBytes);
    if (parsed.isVoid() || static_cast<int>(parsed.getProperty("version", 0)) != static_cast<int>(kStateVersion))
        return;

    roomSize.store(getFloatProperty(parsed, "roomSize", roomSize.load(std::memory_order_relaxed), 0.0f, 1.0f), std::memory_order_relaxed);
    damping.store(getFloatProperty(parsed, "damping", damping.load(std::memory_order_relaxed), 0.0f, 1.0f), std::memory_order_relaxed);
    wetLevel.store(getFloatProperty(parsed, "wetLevel", wetLevel.load(std::memory_order_relaxed), 0.0f, 1.0f), std::memory_order_relaxed);
    dryLevel.store(getFloatProperty(parsed, "dryLevel", dryLevel.load(std::memory_order_relaxed), 0.0f, 1.0f), std::memory_order_relaxed);
    width.store(getFloatProperty(parsed, "width", width.load(std::memory_order_relaxed), 0.0f, 1.0f), std::memory_order_relaxed);
    freezeMode.store(getFloatProperty(parsed, "freezeMode", freezeMode.load(std::memory_order_relaxed), 0.0f, 1.0f), std::memory_order_relaxed);
    dspDirty.store(true, std::memory_order_release);
}

juce::Reverb::Parameters AuxReverbMachine::captureParameters() const
{
    juce::Reverb::Parameters parameters = defaultParameters;
    parameters.roomSize = roomSize.load(std::memory_order_relaxed);
    parameters.damping = damping.load(std::memory_order_relaxed);
    parameters.wetLevel = wetLevel.load(std::memory_order_relaxed);
    parameters.dryLevel = dryLevel.load(std::memory_order_relaxed);
    parameters.width = width.load(std::memory_order_relaxed);
    parameters.freezeMode = freezeMode.load(std::memory_order_relaxed);
    return parameters;
}

void AuxReverbMachine::updateParameters()
{
    reverb.setParameters(captureParameters());
}

float AuxReverbMachine::clampUnit(float value)
{
    return juce::jlimit(0.0f, 1.0f, value);
}
