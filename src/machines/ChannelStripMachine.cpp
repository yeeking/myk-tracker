#include "ChannelStripMachine.h"

#include <cmath>

ChannelStripMachine::ChannelStripMachine()
    : oversampling(kMaxChannels,
                   1,
                   juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR,
                   true)
{
}

void ChannelStripMachine::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;

    processSpec.sampleRate = currentSampleRate;
    processSpec.maximumBlockSize = static_cast<juce::uint32>(juce::jmax(1, samplesPerBlock));
    processSpec.numChannels = kMaxChannels;

    oversampling.reset();
    oversampling.initProcessing(processSpec.maximumBlockSize);

    const auto oversamplingFactor = static_cast<juce::uint32>(oversampling.getOversamplingFactor());
    oversampledSpec = processSpec;
    oversampledSpec.sampleRate *= static_cast<double>(oversamplingFactor);
    oversampledSpec.maximumBlockSize *= oversamplingFactor;

    saturationDryBuffer.setSize(static_cast<int>(kMaxChannels), static_cast<int>(processSpec.maximumBlockSize));
    saturationDryBuffer.clear();

    saturatorInputGain.prepare(oversampledSpec);
    saturator.prepare(oversampledSpec);
    distGain.prepare(processSpec);
    distShaper.prepare(processSpec);
    distOutputGain.prepare(processSpec);
    compInputGain.prepare(processSpec);
    compressor.prepare(processSpec);
    compOutputGain.prepare(processSpec);
    bassShelf.prepare(processSpec);
    midPeak.prepare(processSpec);
    trebleShelf.prepare(processSpec);
    limiter.prepare(processSpec);

    saturatorInputGain.setRampDurationSeconds(0.02);
    distGain.setRampDurationSeconds(0.02);
    distOutputGain.setRampDurationSeconds(0.02);
    compInputGain.setRampDurationSeconds(0.02);
    compOutputGain.setRampDurationSeconds(0.02);
    satMixSmoothed.reset(currentSampleRate, 0.02);

    saturator.functionToUse = [](float x) { return ChannelStripMachine::softClip(x); };
    distShaper.functionToUse = [](float x) { return ChannelStripMachine::softClip(x * 0.85f); };

    updateDSPSettings(captureParameters());
    resetDSPState();
}

void ChannelStripMachine::releaseResources()
{
    resetDSPState();
}

std::vector<std::vector<UIBox>> ChannelStripMachine::getUIBoxes(const MachineUiContext& context)
{
    juce::ignoreUnused(context);

    std::vector<std::vector<UIBox>> boxes(4, std::vector<UIBox>(4));

    boxes[0][0] = makeLabelCell("SAT");
    boxes[1][0] = makeDbCell(satDriveDb, 1.0f, kMinSatDriveDb, kMaxSatDriveDb, 1);
    boxes[2][0] = makeLabelCell("COUT");
    boxes[3][0] = makeDbCell(compOutputDb, 1.0f, kMinCompOutputDb, kMaxCompOutputDb, 1);

    boxes[0][1] = makeLabelCell("SMIX");
    boxes[1][1] = makeValueCell(satMix, 0.05f, kMinSatMix, kMaxSatMix, 2);
    boxes[2][1] = makeLabelCell("LIM");
    boxes[3][1] = makeDbCell(limiterThresholdDb, 0.5f, kMinLimiterThresholdDb, kMaxLimiterThresholdDb, 1);

    boxes[0][2] = makeLabelCell("DDRV");
    boxes[1][2] = makeDbCell(distDriveDb, 1.0f, kMinDistDriveDb, kMaxDistDriveDb, 1);
    boxes[2][2] = makeLabelCell("BAS");
    boxes[3][2] = makeDbCell(bassDb, 1.0f, kMinEqDb, kMaxEqDb, 1);

    boxes[0][3] = makeLabelCell("DOUT");
    boxes[1][3] = makeDbCell(distOutputDb, 1.0f, kMinDistOutputDb, kMaxDistOutputDb, 1);
    boxes[2][3] = makeLabelCell("MID");
    boxes[3][3] = makeDbCell(midDb, 1.0f, kMinEqDb, kMaxEqDb, 1);

    boxes.push_back({ makeLabelCell("CIN"), makeLabelCell("THR"), makeLabelCell("RAT"), makeLabelCell("ATT") });
    boxes.push_back({
        makeDbCell(compInputDb, 1.0f, kMinCompInputDb, kMaxCompInputDb, 1),
        makeDbCell(compThresholdDb, 1.0f, kMinCompThresholdDb, kMaxCompThresholdDb, 1),
        makeValueCell(compRatio, 0.5f, kMinCompRatio, kMaxCompRatio, 1),
        makeValueCell(compAttackMs, 1.0f, kMinCompAttackMs, kMaxCompAttackMs, 1)
    });

    boxes.push_back({ makeLabelCell("MFQ"), makeLabelCell("TRE"), makeLabelCell(""), makeLabelCell("") });
    boxes.push_back({
        makeFrequencyCell(midFreqHz, 50.0f, kMinMidFreqHz, kMaxMidFreqHz),
        makeDbCell(trebleDb, 1.0f, kMinEqDb, kMaxEqDb, 1),
        UIBox{},
        UIBox{}
    });

    for (std::size_t row = 0; row < boxes.back().size(); ++row)
    {
        if (boxes[6][row].kind == UIBox::Kind::None)
        {
            boxes[6][row].kind = UIBox::Kind::None;
            boxes[6][row].isDisabled = true;
        }
        if (boxes[7][row].kind == UIBox::Kind::None)
        {
            boxes[7][row].kind = UIBox::Kind::None;
            boxes[7][row].isDisabled = true;
        }
    }

    return boxes;
}

void ChannelStripMachine::processAudioBuffer(juce::AudioBuffer<float>& buffer)
{
    if (buffer.getNumSamples() == 0 || buffer.getNumChannels() == 0)
        return;

    if (dspResetRequested.exchange(false, std::memory_order_acq_rel))
        resetDSPState();

    if (dspDirty.exchange(false, std::memory_order_acq_rel))
        updateDSPSettings(captureParameters());

    const auto channelsToProcess = juce::jmin<int>(buffer.getNumChannels(), static_cast<int>(kMaxChannels));
    if (channelsToProcess <= 0)
        return;

    jassert(buffer.getNumSamples() <= saturationDryBuffer.getNumSamples());
    for (int channel = 0; channel < channelsToProcess; ++channel)
        saturationDryBuffer.copyFrom(channel, 0, buffer, channel, 0, buffer.getNumSamples());

    auto block = juce::dsp::AudioBlock<float>(buffer).getSubsetChannelBlock(0, static_cast<std::size_t>(channelsToProcess));
    auto upsampledBlock = oversampling.processSamplesUp(block);
    juce::dsp::ProcessContextReplacing<float> upContext(upsampledBlock);
    saturatorInputGain.process(upContext);
    saturator.process(upContext);
    oversampling.processSamplesDown(block);

    for (int channel = 0; channel < channelsToProcess; ++channel)
    {
        auto* wetSamples = buffer.getWritePointer(channel);
        const auto* drySamples = saturationDryBuffer.getReadPointer(channel);
        for (int sampleIndex = 0; sampleIndex < buffer.getNumSamples(); ++sampleIndex)
        {
            const float wet = wetSamples[sampleIndex];
            wetSamples[sampleIndex] = juce::jmap(satMixSmoothed.getNextValue(), drySamples[sampleIndex], wet);
        }
    }

    juce::dsp::ProcessContextReplacing<float> context(block);
    distGain.process(context);
    distShaper.process(context);
    distOutputGain.process(context);
    compInputGain.process(context);
    compressor.process(context);
    compOutputGain.process(context);
    bassShelf.process(context);
    midPeak.process(context);
    trebleShelf.process(context);
    limiter.process(context);
}

void ChannelStripMachine::getStateInformation(juce::MemoryBlock& destData)
{
    auto parameters = captureParameters();

    juce::DynamicObject::Ptr root = new juce::DynamicObject();
    root->setProperty("version", kStateVersion);
    root->setProperty("satDriveDb", parameters.satDriveDb);
    root->setProperty("satMix", parameters.satMix);
    root->setProperty("distDriveDb", parameters.distDriveDb);
    root->setProperty("distOutputDb", parameters.distOutputDb);
    root->setProperty("compInputDb", parameters.compInputDb);
    root->setProperty("compThresholdDb", parameters.compThresholdDb);
    root->setProperty("compRatio", parameters.compRatio);
    root->setProperty("compAttackMs", parameters.compAttackMs);
    root->setProperty("compOutputDb", parameters.compOutputDb);
    root->setProperty("limiterThresholdDb", parameters.limiterThresholdDb);
    root->setProperty("bassDb", parameters.bassDb);
    root->setProperty("midDb", parameters.midDb);
    root->setProperty("midFreqHz", parameters.midFreqHz);
    root->setProperty("trebleDb", parameters.trebleDb);

    const auto json = juce::JSON::toString(juce::var(root.get()));
    destData.reset();
    destData.append(json.toRawUTF8(), json.getNumBytesAsUTF8());
}

void ChannelStripMachine::setStateInformation(const void* data, int sizeInBytes)
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

    satDriveDb.store(clampParameter(static_cast<float>(parsed.getProperty("satDriveDb", satDriveDb.load())), kMinSatDriveDb, kMaxSatDriveDb));
    satMix.store(clampParameter(static_cast<float>(parsed.getProperty("satMix", satMix.load())), kMinSatMix, kMaxSatMix));
    distDriveDb.store(clampParameter(static_cast<float>(parsed.getProperty("distDriveDb", distDriveDb.load())), kMinDistDriveDb, kMaxDistDriveDb));
    distOutputDb.store(clampParameter(static_cast<float>(parsed.getProperty("distOutputDb", distOutputDb.load())), kMinDistOutputDb, kMaxDistOutputDb));
    compInputDb.store(clampParameter(static_cast<float>(parsed.getProperty("compInputDb", compInputDb.load())), kMinCompInputDb, kMaxCompInputDb));
    compThresholdDb.store(clampParameter(static_cast<float>(parsed.getProperty("compThresholdDb", compThresholdDb.load())), kMinCompThresholdDb, kMaxCompThresholdDb));
    compRatio.store(clampParameter(static_cast<float>(parsed.getProperty("compRatio", compRatio.load())), kMinCompRatio, kMaxCompRatio));
    compAttackMs.store(clampParameter(static_cast<float>(parsed.getProperty("compAttackMs", compAttackMs.load())), kMinCompAttackMs, kMaxCompAttackMs));
    compOutputDb.store(clampParameter(static_cast<float>(parsed.getProperty("compOutputDb", compOutputDb.load())), kMinCompOutputDb, kMaxCompOutputDb));
    limiterThresholdDb.store(clampParameter(static_cast<float>(parsed.getProperty("limiterThresholdDb", limiterThresholdDb.load())), kMinLimiterThresholdDb, kMaxLimiterThresholdDb));
    bassDb.store(clampParameter(static_cast<float>(parsed.getProperty("bassDb", bassDb.load())), kMinEqDb, kMaxEqDb));
    midDb.store(clampParameter(static_cast<float>(parsed.getProperty("midDb", midDb.load())), kMinEqDb, kMaxEqDb));
    midFreqHz.store(clampParameter(static_cast<float>(parsed.getProperty("midFreqHz", midFreqHz.load())), kMinMidFreqHz, kMaxMidFreqHz));
    trebleDb.store(clampParameter(static_cast<float>(parsed.getProperty("trebleDb", trebleDb.load())), kMinEqDb, kMaxEqDb));

    dspDirty.store(true, std::memory_order_release);
    dspResetRequested.store(true, std::memory_order_release);
}

ChannelStripMachine::ParameterSnapshot ChannelStripMachine::captureParameters() const
{
    ParameterSnapshot parameters;
    parameters.satDriveDb = satDriveDb.load(std::memory_order_relaxed);
    parameters.satMix = satMix.load(std::memory_order_relaxed);
    parameters.distDriveDb = distDriveDb.load(std::memory_order_relaxed);
    parameters.distOutputDb = distOutputDb.load(std::memory_order_relaxed);
    parameters.compInputDb = compInputDb.load(std::memory_order_relaxed);
    parameters.compThresholdDb = compThresholdDb.load(std::memory_order_relaxed);
    parameters.compRatio = compRatio.load(std::memory_order_relaxed);
    parameters.compAttackMs = compAttackMs.load(std::memory_order_relaxed);
    parameters.compOutputDb = compOutputDb.load(std::memory_order_relaxed);
    parameters.limiterThresholdDb = limiterThresholdDb.load(std::memory_order_relaxed);
    parameters.bassDb = bassDb.load(std::memory_order_relaxed);
    parameters.midDb = midDb.load(std::memory_order_relaxed);
    parameters.midFreqHz = midFreqHz.load(std::memory_order_relaxed);
    parameters.trebleDb = trebleDb.load(std::memory_order_relaxed);
    return parameters;
}

void ChannelStripMachine::updateDSPSettings(const ParameterSnapshot& parameters)
{
    saturatorInputGain.setGainDecibels(parameters.satDriveDb);
    distGain.setGainDecibels(parameters.distDriveDb);
    distOutputGain.setGainDecibels(parameters.distOutputDb);
    compInputGain.setGainDecibels(parameters.compInputDb);
    compressor.setThreshold(parameters.compThresholdDb);
    compressor.setRatio(parameters.compRatio);
    compressor.setAttack(parameters.compAttackMs);
    compressor.setRelease(80.0f);
    compOutputGain.setGainDecibels(parameters.compOutputDb);
    limiter.setThreshold(parameters.limiterThresholdDb);
    limiter.setRelease(50.0f);
    satMixSmoothed.setTargetValue(parameters.satMix);
    updateEQCoefficients(parameters);
}

void ChannelStripMachine::updateEQCoefficients(const ParameterSnapshot& parameters)
{
    constexpr float bassFreqHz = 120.0f;
    constexpr float trebleFreqHz = 7000.0f;
    constexpr float shelfQ = 0.707f;
    constexpr float midQ = 1.0f;

    if (bassShelf.state == nullptr)
        bassShelf.state = juce::dsp::IIR::Coefficients<float>::makeLowShelf(currentSampleRate, bassFreqHz, shelfQ, 1.0f);
    if (midPeak.state == nullptr)
        midPeak.state = juce::dsp::IIR::Coefficients<float>::makePeakFilter(currentSampleRate, parameters.midFreqHz, midQ, 1.0f);
    if (trebleShelf.state == nullptr)
        trebleShelf.state = juce::dsp::IIR::Coefficients<float>::makeHighShelf(currentSampleRate, trebleFreqHz, shelfQ, 1.0f);

    *bassShelf.state = *juce::dsp::IIR::Coefficients<float>::makeLowShelf(
        currentSampleRate,
        bassFreqHz,
        shelfQ,
        juce::Decibels::decibelsToGain(parameters.bassDb));

    *midPeak.state = *juce::dsp::IIR::Coefficients<float>::makePeakFilter(
        currentSampleRate,
        parameters.midFreqHz,
        midQ,
        juce::Decibels::decibelsToGain(parameters.midDb));

    *trebleShelf.state = *juce::dsp::IIR::Coefficients<float>::makeHighShelf(
        currentSampleRate,
        trebleFreqHz,
        shelfQ,
        juce::Decibels::decibelsToGain(parameters.trebleDb));
}

void ChannelStripMachine::resetDSPState()
{
    oversampling.reset();
    saturatorInputGain.reset();
    saturator.reset();
    distGain.reset();
    distShaper.reset();
    distOutputGain.reset();
    compInputGain.reset();
    compressor.reset();
    compOutputGain.reset();
    bassShelf.reset();
    midPeak.reset();
    trebleShelf.reset();
    limiter.reset();
    satMixSmoothed.reset(currentSampleRate, 0.02);
    satMixSmoothed.setCurrentAndTargetValue(satMix.load(std::memory_order_relaxed));
    saturationDryBuffer.clear();
}

float ChannelStripMachine::softClip(float x)
{
    return std::tanh(x);
}

std::string ChannelStripMachine::formatFloat(float value, int decimals)
{
    return juce::String(value, decimals).toStdString();
}

std::string ChannelStripMachine::formatDb(float value, int decimals)
{
    return juce::String(value, decimals).toStdString();
}

std::string ChannelStripMachine::formatRatio(float value)
{
    return juce::String(value, 1).toStdString();
}

std::string ChannelStripMachine::formatHz(float hz)
{
    if (hz >= 1000.0f)
        return juce::String(hz / 1000.0f, 2).toStdString() + "k";
    return juce::String(hz, 0).toStdString();
}

float ChannelStripMachine::clampParameter(float value, float minValue, float maxValue)
{
    return juce::jlimit(minValue, maxValue, value);
}

UIBox ChannelStripMachine::makeLabelCell(const std::string& text) const
{
    UIBox cell;
    cell.kind = UIBox::Kind::TrackerCell;
    cell.text = text;
    return cell;
}

UIBox ChannelStripMachine::makeValueCell(std::atomic<float>& target, float step, float minValue, float maxValue, int decimals)
{
    UIBox cell;
    cell.kind = UIBox::Kind::TrackerCell;
    cell.text = formatFloat(target.load(std::memory_order_relaxed), decimals);
    cell.onAdjust = [this, targetPtr = &target, step, minValue, maxValue](int direction)
    {
        const auto next = clampParameter(targetPtr->load(std::memory_order_relaxed) + (step * static_cast<float>(direction)),
                                         minValue,
                                         maxValue);
        targetPtr->store(next, std::memory_order_relaxed);
        dspDirty.store(true, std::memory_order_release);
    };
    return cell;
}

UIBox ChannelStripMachine::makeDbCell(std::atomic<float>& target, float step, float minValue, float maxValue, int decimals)
{
    UIBox cell = makeValueCell(target, step, minValue, maxValue, decimals);
    cell.text = formatDb(target.load(std::memory_order_relaxed), decimals);
    return cell;
}

UIBox ChannelStripMachine::makeFrequencyCell(std::atomic<float>& target, float step, float minValue, float maxValue)
{
    UIBox cell;
    cell.kind = UIBox::Kind::TrackerCell;
    cell.text = formatHz(target.load(std::memory_order_relaxed));
    cell.onAdjust = [this, targetPtr = &target, step, minValue, maxValue](int direction)
    {
        const auto next = clampParameter(targetPtr->load(std::memory_order_relaxed) + (step * static_cast<float>(direction)),
                                         minValue,
                                         maxValue);
        targetPtr->store(next, std::memory_order_relaxed);
        dspDirty.store(true, std::memory_order_release);
    };
    return cell;
}
