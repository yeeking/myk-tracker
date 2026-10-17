#include "FilterFxMachine.h"
#include "ModulationMatrixMachine.h"
#include "MachineStateCodec.h"
#include <algorithm>
#include <cmath>

namespace
{
constexpr double kFilterStateVersion = 2.0;
constexpr float kMaxAttackSeconds = 2.0f;
constexpr float kMaxDecaySeconds = 2.0f;
constexpr float kMaxReleaseSeconds = 3.0f;
constexpr float kMinEnvelopeBend = -2.0f;
constexpr float kMaxEnvelopeBend = 2.0f;
}

void FilterFxMachine::prepareDsp(double sampleRate, int samplesPerBlock)
{
    const std::lock_guard<std::mutex> lock(stateMutex);
    currentSampleRate = sanitizedSampleRate(sampleRate);

    juce::dsp::ProcessSpec spec;
    spec.sampleRate = currentSampleRate;
    spec.maximumBlockSize = samplesPerBlock > 0
        ? static_cast<juce::uint32>(samplesPerBlock)
        : static_cast<juce::uint32>(512);
    spec.numChannels = 2;
    filter.prepare(spec);
    secondFilter.prepare(spec);
    secondFilter.setResonance(kFlatResonance);
    appliedCutoff = appliedSecondCutoff = appliedResonance = -1.0f;
    planEvents.reserve(1024);
    carryReleases.reserve(1024);
    envelope.setSampleRate(currentSampleRate);
}

std::vector<std::vector<UIBox>> FilterFxMachine::getUIBoxes(const MachineUiContext& context)
{
    juce::ignoreUnused(context);
    const std::lock_guard<std::mutex> lock(stateMutex);

    // Two compact label/value blocks: filter controls on the left and the
    // note-triggered envelope on the right.
    std::vector<std::vector<UIBox>> boxes(4, std::vector<UIBox>(5));

    auto buildFloatCell = [this](float* target, float step, float minValue, float maxValue, int decimals)
    {
        return makeFloatCell(*target, minValue, maxValue, decimals,
            [this, target, step, minValue, maxValue](int direction)
            {
                const std::lock_guard<std::mutex> guard(stateMutex);
                *target = juce::jlimit(minValue, maxValue, *target + step * static_cast<float>(direction));
            });
    };

    auto buildToggleCell = [this](bool* target, const char* nameOff, const char* nameOn)
    {
        return makeToggleCell(*target, nameOff, nameOn,
            [this, target](int direction)
            {
                const std::lock_guard<std::mutex> guard(stateMutex);
                if (direction != 0)
                    *target = !*target;
            });
    };

    boxes[0][0].kind = UIBox::Kind::TrackerCell;
    boxes[0][0].text = "MODE";
    boxes[1][0].isDisabled = false;
    boxes[1][0].kind = UIBox::Kind::TrackerCell;
    boxes[1][0].text = filterModeName(filterMode);
    boxes[1][0].onAdjust = [this](int direction)
    {
        const std::lock_guard<std::mutex> guard(stateMutex);
        const int modeCount = static_cast<int>(FilterMode::count);
        int next = (static_cast<int>(filterMode) + direction) % modeCount;
        if (next < 0)
            next += modeCount;
        filterMode = static_cast<FilterMode>(next);
    };
    boxes[1][0].hasValueScale = true;
    boxes[1][0].valueNorm = static_cast<float>(filterMode)
        / static_cast<float>(static_cast<int>(FilterMode::count) - 1);

    boxes[0][1].kind = UIBox::Kind::TrackerCell;
    boxes[0][1].text = "COFF";
    boxes[1][1] = buildFloatCell(&cutoffHz, 50.0f, kMinCutoffHz, kMaxCutoffHz, 0);

    boxes[0][2].kind = UIBox::Kind::TrackerCell;
    boxes[0][2].text = "RES";
    boxes[1][2] = buildFloatCell(&resonance, 0.05f, 0.0f, 1.0f, 2);

    boxes[0][3].kind = UIBox::Kind::TrackerCell;
    boxes[0][3].text = "AMT";
    boxes[1][3] = buildFloatCell(&amount, 0.05f, 0.0f, 1.0f, 2);

    boxes[0][4].kind = UIBox::Kind::TrackerCell;
    boxes[0][4].text = "POL";
    boxes[1][4] = buildToggleCell(&positivePolarity, "NEG", "POS");

    boxes[2][0].kind = UIBox::Kind::TrackerCell;
    boxes[2][0].text = "A";
    boxes[3][0] = buildFloatCell(&attackSeconds, 0.01f, 0.0f, kMaxAttackSeconds, 2);

    boxes[2][1].kind = UIBox::Kind::TrackerCell;
    boxes[2][1].text = "D";
    boxes[3][1] = buildFloatCell(&decaySeconds, 0.01f, 0.0f, kMaxDecaySeconds, 2);

    boxes[2][2].kind = UIBox::Kind::TrackerCell;
    boxes[2][2].text = "S";
    boxes[3][2] = buildFloatCell(&sustainLevel, 0.05f, 0.0f, 1.0f, 2);

    boxes[2][3].kind = UIBox::Kind::TrackerCell;
    boxes[2][3].text = "R";
    boxes[3][3] = buildFloatCell(&releaseSeconds, 0.01f, 0.0f, kMaxReleaseSeconds, 2);

    boxes[2][4].kind = UIBox::Kind::TrackerCell;
    boxes[2][4].text = "BEND";
    boxes[3][4] = buildFloatCell(&envelopeBend, 0.05f, kMinEnvelopeBend, kMaxEnvelopeBend, 2);

    return boxes;
}

void FilterFxMachine::processAudioBuffer(juce::AudioBuffer<float>& buffer)
{
    // One uncontended lock per block snapshots every UI-owned parameter; the
    // per-sample path below runs lock-free.
    ParamSnapshot snap;
    {
        const std::lock_guard<std::mutex> lock(stateMutex);
        snap.sampleRate = currentSampleRate;
        snap.secondsPerTick = currentSecondsPerTick;
        snap.cutoffHz = cutoffHz;
        snap.resonance = resonance;
        snap.amount = amount;
        snap.positivePolarity = positivePolarity;
        snap.mode = filterMode;
        snap.attackSeconds = attackSeconds;
        snap.decaySeconds = decaySeconds;
        snap.sustainLevel = sustainLevel;
        snap.releaseSeconds = releaseSeconds;
        snap.envelopeBend = envelopeBend;
    }

    const int numSamples = buffer.getNumSamples();
    if (numSamples <= 0)
        return;

    // Build this block's attack/release plan. Notes longer than the block
    // carry their release time over into later blocks.
    planEvents.clear();
    for (auto it = carryReleases.begin(); it != carryReleases.end();)
    {
        if (*it < numSamples)
        {
            planEvents.push_back({ *it, false });
            it = carryReleases.erase(it);
        }
        else
        {
            *it -= numSamples;
            ++it;
        }
    }
    if (blockNotes != nullptr)
    {
        for (const auto& note : *blockNotes)
        {
            const int on = note.sampleOffset;
            if (on < 0 || on >= numSamples)
                continue;
            const int durSamples = static_cast<int>(std::lround(
                snap.sampleRate * snap.secondsPerTick
                * static_cast<double>(juce::jmax(1, static_cast<int>(note.durationTicks)))));
            const int off = on + juce::jmax(1, durSamples);
            planEvents.push_back({ on, true });
            if (off < numSamples)
                planEvents.push_back({ off, false });
            else
                carryReleases.push_back(off - numSamples);
        }
    }
    // At equal sample positions process releases before attacks so adjacent
    // notes retrigger the envelope instead of merging into one ramp.
    // Events with equal time and kind are interchangeable; std::sort needs no
    // temporary allocation, unlike stable_sort.
    std::sort(planEvents.begin(), planEvents.end(),
        [](const PlanEvent& a, const PlanEvent& b)
        {
            if (a.sample != b.sample)
                return a.sample < b.sample;
            return !a.isAttack && b.isAttack;
        });

    // Live A/D/S/R/BEND edits apply on the next retrigger so an in-flight ramp
    // is never retimed mid-note.
    CurvedAdsr::Parameters params;
    params.attack = snap.attackSeconds;
    params.decay = snap.decaySeconds;
    params.sustain = snap.sustainLevel;
    params.release = snap.releaseSeconds;
    params.bend = snap.envelopeBend;

    // Log-space sweep: COFF is the top of the sweep, AMT sets how far the low
    // end falls (AMT=0 leaves the cutoff static at COFF, AMT=1 reaches 20 Hz).
    const double maxFreq = 0.45 * snap.sampleRate;
    const double maxTopOctaves = std::log2(maxFreq / kMinCutoffHz);
    const double baseTopOctaves = std::log2(juce::jmax(1.0, static_cast<double>(snap.cutoffHz) / kMinCutoffHz));

    const auto filterType = filterTypeForMode(snap.mode);
    const bool useSecondStage = is24DbMode(snap.mode);
    filter.setType(filterType);
    secondFilter.setType(filterType);
    // Keep the extra stage neutral so RES has a comparable peak in both slopes.
    if (useSecondStage != secondStageActive)
    {
        secondFilter.reset();
        secondStageActive = useSecondStage;
    }

    std::size_t cursor = 0;
    for (int sample = 0; sample < numSamples; ++sample)
    {
        while (cursor < planEvents.size() && planEvents[cursor].sample <= sample)
        {
            if (planEvents[cursor].isAttack)
            {
                ++activeNotes;
                // Retrigger from the bottom of the sweep with the current
                // envelope parameters (reset before noteOn, per CurvedAdsr).
                envelope.reset();
                auto attackParams = params;
                const float attackMod = modulationBlock != nullptr ? modulationBlock->get(ModulationDestination::filterAttack, sample) : 0.0f;
                attackParams.attack = juce::jlimit(0.0f, 2.0f, attackParams.attack + attackMod * 2.0f);
                envelope.setParameters(attackParams);
                envelope.noteOn();
            }
            else if (--activeNotes == 0)
            {
                envelope.noteOff();
            }
            ++cursor;
        }

        const float cutoffMod = modulationBlock != nullptr ? modulationBlock->get(ModulationDestination::filterCutoff, sample) : 0.0f;
        const double topOctaves = juce::jlimit(0.0, maxTopOctaves, baseTopOctaves + static_cast<double>(cutoffMod) * 4.0);
        if (std::abs(topOctaves - cachedTopOctaves) > std::numeric_limits<double>::epsilon()
            || std::abs(snap.amount - cachedAmount) > std::numeric_limits<float>::epsilon())
        {
            const double top = kMinCutoffHz * std::exp2(topOctaves);
            cachedLowEnd = kMinCutoffHz * std::pow(top / kMinCutoffHz, 1.0 - snap.amount);
            cachedSweepRatio = top / cachedLowEnd;
            cachedTopOctaves = topOctaves;
            cachedAmount = snap.amount;
        }
        const float resonanceMod = modulationBlock != nullptr ? modulationBlock->get(ModulationDestination::filterResonance, sample) : 0.0f;
        const float resonanceControl = juce::jlimit(0.0f, 1.0f, snap.resonance + resonanceMod);
        if (std::abs(resonanceControl - appliedResonance) > std::numeric_limits<float>::epsilon())
        {
            filter.setResonance(toFilterResonance(resonanceControl));
            appliedResonance = resonanceControl;
        }
        const double env = static_cast<double>(envelope.getNextSample());
        const double t = snap.positivePolarity ? env : 1.0 - env;
        const double freq = cachedLowEnd * std::pow(cachedSweepRatio, t);

        const float cutoff = static_cast<float>(juce::jlimit(static_cast<double>(kMinCutoffHz), maxFreq, freq));
        if (std::abs(cutoff - appliedCutoff) > std::numeric_limits<float>::epsilon())
        {
            filter.setCutoffFrequency(cutoff);
            appliedCutoff = cutoff;
        }
        if (useSecondStage && std::abs(cutoff - appliedSecondCutoff) > std::numeric_limits<float>::epsilon())
        {
            secondFilter.setCutoffFrequency(cutoff);
            appliedSecondCutoff = cutoff;
        }
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        {
            const float stageOne = filter.processSample(channel, buffer.getSample(channel, sample));
            buffer.setSample(channel, sample, useSecondStage
                ? secondFilter.processSample(channel, stageOne)
                : stageOne);
        }
    }
}

void FilterFxMachine::scheduleBlockNotes(const std::vector<MachineScheduledNote>& notes)
{
    // Called on the audio thread immediately before processBlock; the
    // referenced vector is processor-owned per-stack storage that stays valid
    // until the next block's distribution.
    blockNotes = &notes;
}

void FilterFxMachine::setSecondsPerTick(double secondsPerTick)
{
    const std::lock_guard<std::mutex> lock(stateMutex);
    if (secondsPerTick > 0.0)
        currentSecondsPerTick = secondsPerTick;
}

void FilterFxMachine::allNotesOff()
{
    clearTransientState();
}

void FilterFxMachine::getStateInformation(juce::MemoryBlock& destData)
{
    const std::lock_guard<std::mutex> lock(stateMutex);
    juce::DynamicObject::Ptr root = new juce::DynamicObject();
    root->setProperty("version", kFilterStateVersion);
    root->setProperty("mode", static_cast<int>(filterMode));
    root->setProperty("cutoff", cutoffHz);
    root->setProperty("resonance", resonance);
    root->setProperty("amount", amount);
    root->setProperty("polarity", positivePolarity ? 1 : 0);
    root->setProperty("attack", attackSeconds);
    root->setProperty("decay", decaySeconds);
    root->setProperty("sustain", sustainLevel);
    root->setProperty("release", releaseSeconds);
    root->setProperty("bend", envelopeBend);

    writeMachineStateJson(destData, juce::var(root.get()));
}

void FilterFxMachine::setStateInformation(const void* data, int sizeInBytes)
{
    const juce::var parsed = parseMachineStateJson(data, sizeInBytes);
    if (parsed.isVoid())
        return;

    {
        const std::lock_guard<std::mutex> lock(stateMutex);
        const double version = static_cast<double>(parsed.getProperty("version", 0.0));
        if (version < 2.0)
        {
            filterMode = getBoolProperty(parsed, "mode", false)
                ? FilterMode::highpass12
                : FilterMode::lowpass12;
        }
        else
        {
            filterMode = static_cast<FilterMode>(getIntProperty(parsed, "mode",
                static_cast<int>(filterMode), 0, static_cast<int>(FilterMode::count) - 1));
        }
        cutoffHz = getFloatProperty(parsed, "cutoff", cutoffHz, kMinCutoffHz, kMaxCutoffHz);
        resonance = getFloatProperty(parsed, "resonance", resonance, 0.0f, 1.0f);
        amount = getFloatProperty(parsed, "amount", amount, 0.0f, 1.0f);
        positivePolarity = getBoolProperty(parsed, "polarity", positivePolarity);
        attackSeconds = getFloatProperty(parsed, "attack", attackSeconds, 0.0f, 2.0f);
        decaySeconds = getFloatProperty(parsed, "decay", decaySeconds, 0.0f, 2.0f);
        sustainLevel = getFloatProperty(parsed, "sustain", sustainLevel, 0.0f, 1.0f);
        releaseSeconds = getFloatProperty(parsed, "release", releaseSeconds, 0.0f, kMaxReleaseSeconds);
        envelopeBend = getFloatProperty(parsed, "bend", envelopeBend, kMinEnvelopeBend, kMaxEnvelopeBend);
        if (!std::isfinite(envelopeBend))
            envelopeBend = 0.0f;
    }

    clearTransientState();
}

void FilterFxMachine::getEnvelopeSettings(float& attack, float& decay, float& sustain, float& release,
                                          float& bend, float& maxAttack, float& maxDecay, float& maxRelease) const
{
    const std::lock_guard<std::mutex> lock(stateMutex);
    attack = attackSeconds;
    decay = decaySeconds;
    sustain = sustainLevel;
    release = releaseSeconds;
    bend = envelopeBend;
    maxAttack = kMaxAttackSeconds;
    maxDecay = kMaxDecaySeconds;
    maxRelease = kMaxReleaseSeconds;
}

void FilterFxMachine::setCutoffHz(double hz)
{
    const std::lock_guard<std::mutex> lock(stateMutex);
    cutoffHz = juce::jlimit(kMinCutoffHz, kMaxCutoffHz, static_cast<float>(hz));
}

float FilterFxMachine::toFilterResonance(float resonanceControl)
{
    // Exponential mapping gives the upper half of RES a pronounced musical
    // peak while retaining useful resolution around the flat response.
    const float normalized = juce::jlimit(0.0f, 1.0f, resonanceControl);
    return kFlatResonance * std::pow(kMaxResonance / kFlatResonance, normalized);
}

const char* FilterFxMachine::filterModeName(FilterMode mode)
{
    switch (mode)
    {
        case FilterMode::lowpass12: return "LP12";
        case FilterMode::lowpass24: return "LP24";
        case FilterMode::bandpass12: return "BP12";
        case FilterMode::bandpass24: return "BP24";
        case FilterMode::highpass12: return "HP12";
        case FilterMode::highpass24: return "HP24";
        case FilterMode::count: break;
    }
    return "LP12";
}

juce::dsp::StateVariableTPTFilterType FilterFxMachine::filterTypeForMode(FilterMode mode)
{
    switch (mode)
    {
        case FilterMode::lowpass12:
        case FilterMode::lowpass24:
            return juce::dsp::StateVariableTPTFilterType::lowpass;
        case FilterMode::bandpass12:
        case FilterMode::bandpass24:
            return juce::dsp::StateVariableTPTFilterType::bandpass;
        case FilterMode::highpass12:
        case FilterMode::highpass24:
            return juce::dsp::StateVariableTPTFilterType::highpass;
        case FilterMode::count:
            break;
    }
    return juce::dsp::StateVariableTPTFilterType::lowpass;
}

bool FilterFxMachine::is24DbMode(FilterMode mode)
{
    return mode == FilterMode::lowpass24
        || mode == FilterMode::bandpass24
        || mode == FilterMode::highpass24;
}

void FilterFxMachine::clearTransientState()
{
    const std::lock_guard<std::mutex> lock(stateMutex);
    filter.reset();
    secondFilter.reset();
    secondStageActive = false;
    envelope.reset();
    planEvents.clear();
    carryReleases.clear();
    activeNotes = 0;
}
