#include "FilterFxMachine.h"
#include "MachineStateCodec.h"
#include <algorithm>
#include <cmath>

namespace
{
constexpr double kFilterStateVersion = 1.0;
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
    envelope.setSampleRate(currentSampleRate);
}

std::vector<std::vector<UIBox>> FilterFxMachine::getUIBoxes(const MachineUiContext& context)
{
    juce::ignoreUnused(context);
    const std::lock_guard<std::mutex> lock(stateMutex);

    std::vector<std::vector<UIBox>> boxes(2, std::vector<UIBox>(10));

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
    boxes[1][0] = buildToggleCell(&highPassMode, "LP", "HP");

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

    boxes[0][5].kind = UIBox::Kind::TrackerCell;
    boxes[0][5].text = "A";
    boxes[1][5] = buildFloatCell(&attackSeconds, 0.01f, 0.0f, kMaxAttackSeconds, 2);

    boxes[0][6].kind = UIBox::Kind::TrackerCell;
    boxes[0][6].text = "D";
    boxes[1][6] = buildFloatCell(&decaySeconds, 0.01f, 0.0f, kMaxDecaySeconds, 2);

    boxes[0][7].kind = UIBox::Kind::TrackerCell;
    boxes[0][7].text = "S";
    boxes[1][7] = buildFloatCell(&sustainLevel, 0.05f, 0.0f, 1.0f, 2);

    boxes[0][8].kind = UIBox::Kind::TrackerCell;
    boxes[0][8].text = "R";
    boxes[1][8] = buildFloatCell(&releaseSeconds, 0.01f, 0.0f, kMaxReleaseSeconds, 2);

    boxes[0][9].kind = UIBox::Kind::TrackerCell;
    boxes[0][9].text = "BEND";
    boxes[1][9] = buildFloatCell(&envelopeBend, 0.05f, kMinEnvelopeBend, kMaxEnvelopeBend, 2);

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
        snap.highPassMode = highPassMode;
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
    std::stable_sort(planEvents.begin(), planEvents.end(),
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
    const double top = juce::jlimit(static_cast<double>(kMinCutoffHz), maxFreq, static_cast<double>(snap.cutoffHz));
    const double lowEnd = static_cast<double>(kMinCutoffHz)
        * std::pow(top / static_cast<double>(kMinCutoffHz), 1.0 - static_cast<double>(snap.amount));
    const double sweepRatio = top / lowEnd;

    filter.setType(snap.highPassMode
        ? juce::dsp::StateVariableTPTFilterType::highpass
        : juce::dsp::StateVariableTPTFilterType::lowpass);
    filter.setResonance(toFilterResonance(snap.resonance));

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
                envelope.setParameters(params);
                envelope.noteOn();
            }
            else if (--activeNotes == 0)
            {
                envelope.noteOff();
            }
            ++cursor;
        }

        const double env = static_cast<double>(envelope.getNextSample());
        const double t = snap.positivePolarity ? env : 1.0 - env;
        const double freq = lowEnd * std::pow(sweepRatio, t);

        filter.setCutoffFrequency(static_cast<float>(juce::jlimit(static_cast<double>(kMinCutoffHz), maxFreq, freq)));
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
            buffer.setSample(channel, sample, filter.processSample(channel, buffer.getSample(channel, sample)));
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
    root->setProperty("mode", highPassMode ? 1 : 0);
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
        highPassMode = getBoolProperty(parsed, "mode", highPassMode);
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
    // RES 0 is a plain 12 dB/oct filter (no peak); RES 1 approaches the TPT
    // stability limit for a strongly peaked resonance.
    return kFlatResonance + juce::jlimit(0.0f, 1.0f, resonanceControl) * (kMaxResonance - kFlatResonance);
}

void FilterFxMachine::clearTransientState()
{
    const std::lock_guard<std::mutex> lock(stateMutex);
    filter.reset();
    envelope.reset();
    planEvents.clear();
    carryReleases.clear();
    activeNotes = 0;
}
