#include "AuxReverbMachine.h"

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

void AuxReverbMachine::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    juce::dsp::ProcessSpec spec;
    spec.sampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
    spec.maximumBlockSize = static_cast<juce::uint32>(juce::jmax(1, samplesPerBlock));
    spec.numChannels = 2;

    reverb.prepare(spec);
    reverb.reset();
    updateParameters();
}

void AuxReverbMachine::releaseResources()
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
    boxes[0][1] = makeValueCell(roomSize, 0.05f, 2);
    boxes[1][1] = makeValueCell(damping, 0.05f, 2);
    boxes[2][1] = makeValueCell(wetLevel, 0.05f, 2);

    boxes[0][2] = makeLabelCell("DRY");
    boxes[1][2] = makeLabelCell("WID");
    boxes[2][2] = makeLabelCell("FRZ");
    boxes[0][3] = makeValueCell(dryLevel, 0.05f, 2);
    boxes[1][3] = makeValueCell(width, 0.05f, 2);
    boxes[2][3] = makeValueCell(freezeMode, 0.05f, 2);

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

    const auto json = juce::JSON::toString(juce::var(root.get()));
    destData.reset();
    destData.append(json.toRawUTF8(), json.getNumBytesAsUTF8());
}

void AuxReverbMachine::setStateInformation(const void* data, int sizeInBytes)
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

    roomSize.store(clampUnit(static_cast<float>(parsed.getProperty("roomSize", roomSize.load(std::memory_order_relaxed)))), std::memory_order_relaxed);
    damping.store(clampUnit(static_cast<float>(parsed.getProperty("damping", damping.load(std::memory_order_relaxed)))), std::memory_order_relaxed);
    wetLevel.store(clampUnit(static_cast<float>(parsed.getProperty("wetLevel", wetLevel.load(std::memory_order_relaxed)))), std::memory_order_relaxed);
    dryLevel.store(clampUnit(static_cast<float>(parsed.getProperty("dryLevel", dryLevel.load(std::memory_order_relaxed)))), std::memory_order_relaxed);
    width.store(clampUnit(static_cast<float>(parsed.getProperty("width", width.load(std::memory_order_relaxed)))), std::memory_order_relaxed);
    freezeMode.store(clampUnit(static_cast<float>(parsed.getProperty("freezeMode", freezeMode.load(std::memory_order_relaxed)))), std::memory_order_relaxed);
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

std::string AuxReverbMachine::formatFloat(float value, int decimals)
{
    return juce::String(value, decimals).toStdString();
}

UIBox AuxReverbMachine::makeLabelCell(const std::string& text) const
{
    UIBox cell;
    cell.kind = UIBox::Kind::TrackerCell;
    cell.text = text;
    return cell;
}

UIBox AuxReverbMachine::makeValueCell(std::atomic<float>& target, float step, int decimals)
{
    UIBox cell;
    cell.kind = UIBox::Kind::TrackerCell;
    cell.text = formatFloat(target.load(std::memory_order_relaxed), decimals);
    cell.onAdjust = [this, targetPtr = &target, step](int direction)
    {
        const float next = clampUnit(targetPtr->load(std::memory_order_relaxed) + (step * static_cast<float>(direction)));
        targetPtr->store(next, std::memory_order_relaxed);
        dspDirty.store(true, std::memory_order_release);
    };
    return cell;
}
