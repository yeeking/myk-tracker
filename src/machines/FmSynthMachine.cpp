#include "FmSynthMachine.h"

#include "MachineStateCodec.h"
#include "MachineUi.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
constexpr float kMinimumFixedHz = 0.1f;
constexpr float kMaximumFixedHz = 20000.0f;
constexpr float kMinimumIndex = -16.0f;
constexpr float kMaximumIndex = 16.0f;
constexpr float kInverseTwoPi = 1.0f / juce::MathConstants<float>::twoPi;

float finiteClamp(float value, float fallback, float minimum, float maximum)
{
    return std::isfinite(value) ? juce::jlimit(minimum, maximum, value) : fallback;
}

int wrapChoice(int current, int direction, int count)
{
    int next = current + direction;
    while (next < 0) next += count;
    while (next >= count) next -= count;
    return next;
}
}

FmSynthMachine::OperatorParameters::OperatorParameters()
{
    const int initialRates[4] = { 99, 70, 50, 45 };
    const float initialLevels[4] = { 100.0f, 80.0f, 80.0f, 0.0f };
    for (int stage = 0; stage < 4; ++stage)
    {
        rates[static_cast<std::size_t>(stage)].store(initialRates[stage]);
        levels[static_cast<std::size_t>(stage)].store(initialLevels[stage]);
    }
}

FmSynthMachine::FmSynthMachine()
{
    for (auto& row : modulationIndices)
        for (auto& value : row)
            value.store(0.0f);
    initialiseTables();
    applyPreset(Preset::init);
    cachedPatch = snapshotPatch();
    cachedRevision = parameterRevision.load(std::memory_order_acquire);
}

void FmSynthMachine::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    juce::ignoreUnused(samplesPerBlock);
    currentSampleRate = sanitizedSampleRate(sampleRate);
    smoothingCoefficient = 1.0f - std::exp(-1.0f / static_cast<float>(currentSampleRate * 0.005));
    smoothingSamplesRemaining = juce::jmax(1, static_cast<int>(currentSampleRate * 0.05));
    for (std::size_t index = 0; index < pitchModulationTable.size(); ++index)
    {
        const float x = static_cast<float>(index) / static_cast<float>(pitchModulationTable.size() - 1) * 2.0f - 1.0f;
        pitchModulationTable[index] = std::pow(2.0f, x * 4.0f);
        indexModulationTable[index] = std::pow(2.0f, x * 2.0f);
    }
    for (int note = 0; note < 128; ++note)
        midiFrequencyTable[static_cast<std::size_t>(note)] =
            440.0f * std::pow(2.0f, static_cast<float>(note - 69) / 12.0f);
    for (int rate = 0; rate < 100; ++rate)
        envelopeRateSecondsTable[static_cast<std::size_t>(rate)] = rateSeconds(rate);
    for (int scaling = -100; scaling <= 100; ++scaling)
        for (int note = 0; note < 128; ++note)
        {
            const float octaves = static_cast<float>(note - 60) / 12.0f;
            rateScalingTable[static_cast<std::size_t>(scaling + 100)][static_cast<std::size_t>(note)] =
                std::pow(2.0f, -static_cast<float>(scaling) * 0.01f * octaves);
        }
    for (int slope = -12; slope <= 12; ++slope)
        for (int distance = 0; distance < 128; ++distance)
        {
            const float keyDb = static_cast<float>(slope * distance) / 12.0f;
            keyboardGainTable[static_cast<std::size_t>(slope + 12)][static_cast<std::size_t>(distance)] =
                juce::jlimit(0.001f, 4.0f, std::pow(10.0f, keyDb / 20.0f));
        }
    allNotesOff();
}

void FmSynthMachine::releaseResources()
{
    allNotesOff();
}

bool FmSynthMachine::handleIncomingNote(unsigned short note, unsigned short velocity,
                                        unsigned short durationTicks, MachineNoteEvent& event)
{
    juce::ignoreUnused(note, velocity, durationTicks, event);
    return false;
}

void FmSynthMachine::scheduleBlockNotes(const std::vector<MachineScheduledNote>& notes)
{
    scheduledBlockNotes = &notes;
}

void FmSynthMachine::setSecondsPerTick(double value)
{
    if (std::isfinite(value) && value > 0.0)
        secondsPerTick = value;
}

void FmSynthMachine::allNotesOff()
{
    for (auto& voice : voices)
        voice = Voice {};
    voiceAgeCounter = 0;
}

float FmSynthMachine::rateSeconds(int rate) noexcept
{
    const float normalised = static_cast<float>(juce::jlimit(0, 99, rate)) / 99.0f;
    return 20.0f * std::pow(0.000025f, normalised); // 20 s down to 0.5 ms.
}

int FmSynthMachine::rateSamples(const OperatorSnapshot& op, int stage, int midiNote) const noexcept
{
    const auto rate = static_cast<std::size_t>(juce::jlimit(0, 99, op.rates[static_cast<std::size_t>(stage)]));
    const auto scaling = static_cast<std::size_t>(juce::jlimit(-100, 100, op.rateScaling) + 100);
    const auto note = static_cast<std::size_t>(juce::jlimit(0, 127, midiNote));
    const float samples = envelopeRateSecondsTable[rate] * rateScalingTable[scaling][note]
        * static_cast<float>(currentSampleRate);
    return juce::jmax(1, static_cast<int>(samples + 0.5f));
}

void FmSynthMachine::startEnvelope(Envelope& envelope, const OperatorSnapshot& op,
                                   int midiNote, bool preserveLevel) noexcept
{
    if (!preserveLevel)
        envelope.level = op.levels[3] * 0.01f;
    envelope.stage = 0;
    envelope.released = false;
    envelope.active = true;
    envelope.target = op.levels[0] * 0.01f;
    envelope.remaining = rateSamples(op, 0, midiNote);
    envelope.increment = (envelope.target - envelope.level) / static_cast<float>(envelope.remaining);
}

void FmSynthMachine::advanceEnvelopeStage(Envelope& envelope, const OperatorSnapshot& op,
                                          int midiNote) noexcept
{
    if (envelope.released)
    {
        envelope.level = envelope.target;
        if (envelope.stage == 3 && envelope.level > 0.00001f)
        {
            // A non-zero L4 is useful for release-time spectral bursts. Fade
            // it to silence after the requested R4 transition so the voice
            // still terminates without a discontinuity.
            envelope.stage = 4;
            envelope.target = 0.0f;
            envelope.remaining = juce::jmax(1, static_cast<int>(currentSampleRate * 0.005));
            envelope.increment = -envelope.level / static_cast<float>(envelope.remaining);
        }
        else
        {
            envelope.active = false;
            envelope.remaining = 0;
        }
        return;
    }

    if (envelope.stage < 2)
    {
        ++envelope.stage;
        envelope.target = op.levels[static_cast<std::size_t>(envelope.stage)] * 0.01f;
        envelope.remaining = rateSamples(op, envelope.stage, midiNote);
        envelope.increment = (envelope.target - envelope.level) / static_cast<float>(envelope.remaining);
    }
    else
    {
        envelope.level = op.levels[2] * 0.01f;
        envelope.remaining = 0; // Sustain at L3 until note release.
    }
}

float FmSynthMachine::advanceEnvelope(Envelope& envelope, const OperatorSnapshot& op,
                                      int midiNote) noexcept
{
    if (!envelope.active)
        return 0.0f;
    if (envelope.remaining > 0)
    {
        envelope.level += envelope.increment;
        if (--envelope.remaining == 0)
            advanceEnvelopeStage(envelope, op, midiNote);
    }
    return juce::jlimit(0.0f, 1.0f, envelope.level);
}

void FmSynthMachine::releaseVoice(Voice& voice, const PatchSnapshot& patch) noexcept
{
    if (!voice.active || voice.releaseStarted)
        return;
    voice.releaseStarted = true;
    voice.held = false;
    for (int op = 0; op < kOperatorCount; ++op)
    {
        auto& envelope = voice.operators[static_cast<std::size_t>(op)].envelope;
        envelope.released = true;
        envelope.stage = 3;
        envelope.target = patch.operators[static_cast<std::size_t>(op)].levels[3] * 0.01f;
        envelope.remaining = rateSamples(patch.operators[static_cast<std::size_t>(op)], 3, voice.midiNote);
        envelope.increment = (envelope.target - envelope.level) / static_cast<float>(envelope.remaining);
        envelope.active = true;
    }
}

FmSynthMachine::Voice& FmSynthMachine::allocateVoice(int limit) noexcept
{
    for (int voiceIndex = 0; voiceIndex < limit; ++voiceIndex)
        if (!voices[static_cast<std::size_t>(voiceIndex)].active)
            return voices[static_cast<std::size_t>(voiceIndex)];
    return *std::min_element(voices.begin(), voices.begin() + limit,
        [] (const Voice& left, const Voice& right) { return left.age < right.age; });
}

void FmSynthMachine::triggerVoice(const MachineScheduledNote& event, const PatchSnapshot& patch) noexcept
{
    const int note = juce::jlimit(0, 127, static_cast<int>(event.note));
    const float noteHz = midiFrequencyTable[static_cast<std::size_t>(note)];
    Voice& voice = patch.voiceCount == 1 ? voices.front() : allocateVoice(patch.voiceCount);
    const bool legato = patch.voiceCount == 1 && voice.active;

    if (!legato)
    {
        voice = Voice {};
        voice.currentNoteHz = noteHz;
        voice.targetNoteHz = noteHz;
        for (int op = 0; op < kOperatorCount; ++op)
        {
            auto& operatorVoice = voice.operators[static_cast<std::size_t>(op)];
            operatorVoice.randomState = 0x9e3779b9u ^ static_cast<std::uint32_t>((note + 1) * (op + 17));
            operatorVoice.sampleHold = nextNoise(operatorVoice);
            startEnvelope(operatorVoice.envelope, patch.operators[static_cast<std::size_t>(op)], note, false);
        }
    }
    else
    {
        voice.targetNoteHz = noteHz;
        const int glideSamples = juce::jmax(0, static_cast<int>(patch.glideMs * 0.001f * static_cast<float>(currentSampleRate)));
        voice.glideRemaining = glideSamples;
        voice.glideStep = glideSamples > 0 ? (noteHz - voice.currentNoteHz) / static_cast<float>(glideSamples) : 0.0f;
        if (glideSamples == 0)
            voice.currentNoteHz = noteHz;
    }

    voice.midiNote = note;
    voice.velocity = juce::jlimit(0.0f, 1.0f, static_cast<float>(event.velocity) / 127.0f);
    voice.samplesUntilRelease = event.durationTicks > 0.0
        ? juce::jmax(1, static_cast<int>(std::lround(event.durationTicks * secondsPerTick * currentSampleRate)))
        : 0;
    voice.releaseStarted = false;
    voice.held = event.durationTicks <= 0.0;
    voice.active = true;
    voice.age = ++voiceAgeCounter;
    updateVoiceGains(voice, patch);
}

void FmSynthMachine::releaseNote(int midiNote, const PatchSnapshot& patch) noexcept
{
    for (auto& voice : voices)
        if (voice.active && voice.midiNote == midiNote)
            releaseVoice(voice, patch);
}

void FmSynthMachine::updateVoiceGains(Voice& voice, const PatchSnapshot& patch) noexcept
{
    for (int opIndex = 0; opIndex < kOperatorCount; ++opIndex)
    {
        const auto& parameters = patch.operators[static_cast<std::size_t>(opIndex)];
        const float velocityTarget = parameters.velocitySensitivity >= 0 ? voice.velocity : (1.0f - voice.velocity);
        const float velocityAmount = std::abs(static_cast<float>(parameters.velocitySensitivity)) * 0.01f;
        const float velocityGain = 1.0f + (velocityTarget - 1.0f) * velocityAmount;
        const int noteDistance = voice.midiNote - parameters.breakpoint;
        const float slope = static_cast<float>(noteDistance < 0 ? parameters.leftSlope : parameters.rightSlope);
        const auto slopeIndex = static_cast<std::size_t>(juce::jlimit(-12, 12, static_cast<int>(slope)) + 12);
        const auto distance = static_cast<std::size_t>(juce::jlimit(0, 127, std::abs(noteDistance)));
        const float keyGain = keyboardGainTable[slopeIndex][distance];
        voice.operatorGains[static_cast<std::size_t>(opIndex)] = velocityGain * keyGain;
    }
}

float FmSynthMachine::nextNoise(OperatorVoice& voice) noexcept
{
    std::uint32_t x = voice.randomState;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    voice.randomState = x;
    return static_cast<float>(x & 0x00ffffffu) / 8388607.5f - 1.0f;
}

float FmSynthMachine::wrapPhase(double phase) noexcept
{
    // Phase modulation is safely bounded by the matrix and operator gain
    // clamps, so truncation plus a negative correction avoids a libm floor
    // call for every operator sample.
    phase -= static_cast<int>(phase);
    if (phase < 0.0)
        phase += 1.0;
    return static_cast<float>(phase);
}

float FmSynthMachine::samplePeriodic(Waveform waveform, double phase, float hz) const noexcept
{
    const int wave = juce::jlimit(0, kPeriodicWaveCount - 1, static_cast<int>(waveform));
    int mip = 0;
    static constexpr int caps[kMipLevels] = { 1, 2, 4, 8, 16, 32, 64 };
    const float bandLimit = static_cast<float>(currentSampleRate * 0.48);
    while (mip + 1 < kMipLevels && hz * static_cast<float>(caps[mip + 1]) <= bandLimit)
        ++mip;
    const float position = wrapPhase(phase) * static_cast<float>(kTableSize);
    const int first = static_cast<int>(position) & (kTableSize - 1);
    const int second = (first + 1) & (kTableSize - 1);
    const float fraction = position - static_cast<float>(first);
    const auto& table = tables[static_cast<std::size_t>(wave)][static_cast<std::size_t>(mip)];
    return table[static_cast<std::size_t>(first)]
        + (table[static_cast<std::size_t>(second)] - table[static_cast<std::size_t>(first)]) * fraction;
}

float FmSynthMachine::sampleWave(OperatorVoice& voice, Waveform waveform, double phase,
                                 float hz, float color) const noexcept
{
    if (waveform == Waveform::sine || color <= 0.0f)
        return samplePeriodic(Waveform::sine, phase, hz);

    float selected = 0.0f;
    if (waveform == Waveform::noise)
        selected = nextNoise(voice);
    else if (waveform == Waveform::sampleHold)
        selected = voice.sampleHold;
    else
        selected = samplePeriodic(waveform, phase, hz);

    if (color >= 1.0f)
        return selected;
    const float sine = samplePeriodic(Waveform::sine, phase, hz);
    return sine + (selected - sine) * color;
}

float FmSynthMachine::lookupPitchMod(float normalised) const noexcept
{
    const float position = (juce::jlimit(-1.0f, 1.0f, normalised) + 1.0f) * 0.5f
        * static_cast<float>(pitchModulationTable.size() - 1);
    const int index = juce::jlimit(0, static_cast<int>(pitchModulationTable.size() - 2), static_cast<int>(position));
    const float fraction = position - static_cast<float>(index);
    return pitchModulationTable[static_cast<std::size_t>(index)]
        + (pitchModulationTable[static_cast<std::size_t>(index + 1)] - pitchModulationTable[static_cast<std::size_t>(index)]) * fraction;
}

float FmSynthMachine::lookupIndexMod(float normalised) const noexcept
{
    const float position = (juce::jlimit(-1.0f, 1.0f, normalised) + 1.0f) * 0.5f
        * static_cast<float>(indexModulationTable.size() - 1);
    const int index = juce::jlimit(0, static_cast<int>(indexModulationTable.size() - 2), static_cast<int>(position));
    const float fraction = position - static_cast<float>(index);
    return indexModulationTable[static_cast<std::size_t>(index)]
        + (indexModulationTable[static_cast<std::size_t>(index + 1)] - indexModulationTable[static_cast<std::size_t>(index)]) * fraction;
}

float FmSynthMachine::renderVoice(Voice& voice, const PatchSnapshot& patch, float pitchMod,
                                  float externalIndex, float externalFeedback, float color) noexcept
{
    if (voice.glideRemaining > 0)
    {
        voice.currentNoteHz += voice.glideStep;
        if (--voice.glideRemaining == 0)
            voice.currentNoteHz = voice.targetNoteHz;
    }

    std::array<float, kOperatorCount> current {};
    float output = 0.0f;
    bool anyEnvelopeActive = false;

    for (int orderIndex = 0; orderIndex < kOperatorCount; ++orderIndex)
    {
        const int destination = patch.order[static_cast<std::size_t>(orderIndex)];
        const auto& parameters = patch.operators[static_cast<std::size_t>(destination)];
        auto& operatorVoice = voice.operators[static_cast<std::size_t>(destination)];
        if (!parameters.enabled)
        {
            operatorVoice.previous = 0.0f;
            continue;
        }

        float phaseOffset = 0.0f;
        for (int source = 0; source < kOperatorCount; ++source)
        {
            const float sourceValue = patch.delayed[static_cast<std::size_t>(source)][static_cast<std::size_t>(destination)]
                ? voice.operators[static_cast<std::size_t>(source)].previous
                : current[static_cast<std::size_t>(source)];
            const bool feedback = source == destination;
            const float macro = feedback
                ? smoothedFeedbackScale * externalFeedback
                : smoothedIndexScale * externalIndex;
            phaseOffset += smoothedMatrix[static_cast<std::size_t>(source)][static_cast<std::size_t>(destination)]
                * macro * sourceValue;
        }

        const float envelope = advanceEnvelope(operatorVoice.envelope, parameters, voice.midiNote);
        anyEnvelopeActive = anyEnvelopeActive || operatorVoice.envelope.active;
        const float targetBaseHz = parameters.frequencyMode == FrequencyMode::ratio
            ? voice.currentNoteHz * patch.octaveMultiplier * parameters.ratio
            : parameters.fixedHz;
        if (operatorVoice.currentBaseHz < 0.0f)
            operatorVoice.currentBaseHz = targetBaseHz;
        else
            operatorVoice.currentBaseHz += (targetBaseHz - operatorVoice.currentBaseHz) * smoothingCoefficient;
        float hz = operatorVoice.currentBaseHz
            * (parameters.frequencyMode == FrequencyMode::ratio ? pitchMod : 1.0f)
            * parameters.detuneMultiplier;
        hz = juce::jlimit(0.0f, static_cast<float>(currentSampleRate * 0.48), hz);
        const double modulatedPhase = operatorVoice.phase + static_cast<double>(phaseOffset * kInverseTwoPi);
        const float wave = sampleWave(operatorVoice, parameters.waveform, modulatedPhase, hz, color);
        const float signal = wave * envelope * voice.operatorGains[static_cast<std::size_t>(destination)];
        current[static_cast<std::size_t>(destination)] = signal;
        output += signal * smoothedOutputs[static_cast<std::size_t>(destination)];

        const bool sampleHold = parameters.waveform == Waveform::sampleHold;
        operatorVoice.phase += static_cast<double>(hz) / currentSampleRate;
        const bool wrapped = operatorVoice.phase >= 1.0;
        if (wrapped)
            operatorVoice.phase -= 1.0;
        if (sampleHold && wrapped)
            operatorVoice.sampleHold = nextNoise(operatorVoice);
    }

    for (int op = 0; op < kOperatorCount; ++op)
        voice.operators[static_cast<std::size_t>(op)].previous = current[static_cast<std::size_t>(op)];

    if (!voice.held && !voice.releaseStarted && voice.samplesUntilRelease > 0
        && --voice.samplesUntilRelease == 0)
        releaseVoice(voice, patch);
    if (voice.releaseStarted && !anyEnvelopeActive)
        voice.active = false;

    return output;
}

void FmSynthMachine::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ignoreUnused(midi);
    if (buffer.getNumChannels() <= 0 || buffer.getNumSamples() <= 0)
        return;

    PatchSnapshot patch = cachedPatch;
    const auto requestedRevision = parameterRevision.load(std::memory_order_acquire);
    bool patchChanged = false;
    if (requestedRevision != cachedRevision
        && !patchWriteInProgress.load(std::memory_order_acquire))
    {
        const PatchSnapshot candidate = snapshotPatch();
        if (!patchWriteInProgress.load(std::memory_order_acquire)
            && parameterRevision.load(std::memory_order_acquire) == requestedRevision)
        {
            patch = candidate;
            cachedPatch = candidate;
            cachedRevision = requestedRevision;
            patchChanged = true;
            smoothingSamplesRemaining = juce::jmax(1, static_cast<int>(currentSampleRate * 0.05));
        }
    }
    const auto* notes = scheduledBlockNotes;
    std::size_t noteIndex = 0;
    const std::size_t noteCount = notes != nullptr ? notes->size() : 0;
    for (int voiceIndex = patch.voiceCount; voiceIndex < kMaxVoiceCount; ++voiceIndex)
        voices[static_cast<std::size_t>(voiceIndex)] = Voice {};
    if (patchChanged)
        for (int voiceIndex = 0; voiceIndex < patch.voiceCount; ++voiceIndex)
            if (voices[static_cast<std::size_t>(voiceIndex)].active)
                updateVoiceGains(voices[static_cast<std::size_t>(voiceIndex)], patch);

    for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
    {
        while (noteIndex < noteCount && (*notes)[noteIndex].sampleOffset <= sample)
        {
            const auto& event = (*notes)[noteIndex++];
            if (event.isNoteOff)
                releaseNote(event.note, patch);
            else
                triggerVoice(event, patch);
        }

        if (smoothingSamplesRemaining > 0)
        {
            smoothedIndexScale += (patch.indexScale - smoothedIndexScale) * smoothingCoefficient;
            smoothedFeedbackScale += (patch.feedbackScale - smoothedFeedbackScale) * smoothingCoefficient;
            smoothedColor += (patch.color - smoothedColor) * smoothingCoefficient;
            for (int op = 0; op < kOperatorCount; ++op)
                smoothedOutputs[static_cast<std::size_t>(op)] +=
                    (patch.operators[static_cast<std::size_t>(op)].output - smoothedOutputs[static_cast<std::size_t>(op)])
                    * smoothingCoefficient;
            for (int source = 0; source < kOperatorCount; ++source)
                for (int destination = 0; destination < kOperatorCount; ++destination)
                    smoothedMatrix[static_cast<std::size_t>(source)][static_cast<std::size_t>(destination)] +=
                        (patch.matrix[static_cast<std::size_t>(source)][static_cast<std::size_t>(destination)]
                         - smoothedMatrix[static_cast<std::size_t>(source)][static_cast<std::size_t>(destination)])
                        * smoothingCoefficient;
            if (--smoothingSamplesRemaining == 0)
            {
                smoothedIndexScale = patch.indexScale;
                smoothedFeedbackScale = patch.feedbackScale;
                smoothedColor = patch.color;
                for (int op = 0; op < kOperatorCount; ++op)
                    smoothedOutputs[static_cast<std::size_t>(op)] = patch.operators[static_cast<std::size_t>(op)].output;
                smoothedMatrix = patch.matrix;
            }
        }

        float pitchMod = 1.0f;
        float externalIndex = 1.0f;
        float externalFeedback = 1.0f;
        float color = smoothedColor;
        if (modulationBlock != nullptr)
        {
            if (modulationBlock->isActive(ModulationDestination::fmPitch))
                pitchMod = lookupPitchMod(modulationBlock->get(ModulationDestination::fmPitch, sample));
            if (modulationBlock->isActive(ModulationDestination::fmIndex))
                externalIndex = lookupIndexMod(modulationBlock->get(ModulationDestination::fmIndex, sample));
            if (modulationBlock->isActive(ModulationDestination::fmFeedback))
                externalFeedback = lookupIndexMod(modulationBlock->get(ModulationDestination::fmFeedback, sample));
            if (modulationBlock->isActive(ModulationDestination::fmColor))
                color = juce::jlimit(0.0f, 1.0f,
                    color + modulationBlock->get(ModulationDestination::fmColor, sample));
        }

        float mixed = 0.0f;
        for (int voiceIndex = 0; voiceIndex < patch.voiceCount; ++voiceIndex)
        {
            auto& voice = voices[static_cast<std::size_t>(voiceIndex)];
            if (voice.active)
                mixed += renderVoice(voice, patch, pitchMod, externalIndex, externalFeedback, color);
        }
        mixed *= patch.level * 0.35f;
        if (patch.drive > 0.0f)
        {
            const float gain = 1.0f + patch.drive * 8.0f;
            const float driven = mixed * gain;
            const float saturated = driven / (1.0f + std::abs(driven));
            mixed += (saturated - mixed) * patch.drive;
        }
        if (!std::isfinite(mixed))
            mixed = 0.0f;
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
            buffer.addSample(channel, sample, mixed);
    }
}

FmSynthMachine::PatchSnapshot FmSynthMachine::snapshotPatch() const
{
    PatchSnapshot patch;
    for (int op = 0; op < kOperatorCount; ++op)
    {
        const auto& source = operatorParameters[static_cast<std::size_t>(op)];
        auto& target = patch.operators[static_cast<std::size_t>(op)];
        target.enabled = source.enabled.load(std::memory_order_relaxed) != 0;
        target.waveform = static_cast<Waveform>(juce::jlimit(0, static_cast<int>(Waveform::count) - 1, source.waveform.load(std::memory_order_relaxed)));
        target.frequencyMode = source.frequencyMode.load(std::memory_order_relaxed) == 0 ? FrequencyMode::ratio : FrequencyMode::fixed;
        target.ratio = juce::jlimit(0.0f, 64.0f,
            static_cast<float>(source.coarse.load(std::memory_order_relaxed))
                + static_cast<float>(source.fine.load(std::memory_order_relaxed)) * 0.01f);
        target.fixedHz = source.fixedHz.load(std::memory_order_relaxed);
        target.detuneMultiplier = std::pow(2.0f, source.detuneCents.load(std::memory_order_relaxed) / 1200.0f);
        target.output = source.output.load(std::memory_order_relaxed) * 0.01f;
        for (int stage = 0; stage < 4; ++stage)
        {
            target.rates[static_cast<std::size_t>(stage)] = source.rates[static_cast<std::size_t>(stage)].load(std::memory_order_relaxed);
            target.levels[static_cast<std::size_t>(stage)] = source.levels[static_cast<std::size_t>(stage)].load(std::memory_order_relaxed);
        }
        target.velocitySensitivity = source.velocitySensitivity.load(std::memory_order_relaxed);
        target.breakpoint = source.breakpoint.load(std::memory_order_relaxed);
        target.leftSlope = source.leftSlope.load(std::memory_order_relaxed);
        target.rightSlope = source.rightSlope.load(std::memory_order_relaxed);
        target.rateScaling = source.rateScaling.load(std::memory_order_relaxed);
        for (int destination = 0; destination < kOperatorCount; ++destination)
            patch.matrix[static_cast<std::size_t>(op)][static_cast<std::size_t>(destination)] =
                modulationIndices[static_cast<std::size_t>(op)][static_cast<std::size_t>(destination)].load(std::memory_order_relaxed);
    }
    patch.voiceCount = juce::jlimit(1, kMaxVoiceCount, voiceCount.load(std::memory_order_relaxed));
    patch.level = masterLevel.load(std::memory_order_relaxed);
    patch.octave = octave.load(std::memory_order_relaxed);
    patch.glideMs = glideMs.load(std::memory_order_relaxed);
    patch.indexScale = indexPercent.load(std::memory_order_relaxed) * 0.01f;
    patch.feedbackScale = feedbackPercent.load(std::memory_order_relaxed) * 0.01f;
    patch.color = colorPercent.load(std::memory_order_relaxed) * 0.01f;
    patch.drive = drivePercent.load(std::memory_order_relaxed) * 0.01f;
    patch.octaveMultiplier = std::ldexp(1.0f, patch.octave);
    calculateRouting(patch);
    return patch;
}

void FmSynthMachine::calculateRouting(PatchSnapshot& patch)
{
    bool reach[kOperatorCount][kOperatorCount] {};
    for (int source = 0; source < kOperatorCount; ++source)
        for (int destination = 0; destination < kOperatorCount; ++destination)
            reach[source][destination] = std::abs(patch.matrix[static_cast<std::size_t>(source)][static_cast<std::size_t>(destination)]) > 0.00001f;
    for (int intermediate = 0; intermediate < kOperatorCount; ++intermediate)
        for (int source = 0; source < kOperatorCount; ++source)
            for (int destination = 0; destination < kOperatorCount; ++destination)
                reach[source][destination] = reach[source][destination]
                    || (reach[source][intermediate] && reach[intermediate][destination]);

    int indegree[kOperatorCount] {};
    for (int source = 0; source < kOperatorCount; ++source)
        for (int destination = 0; destination < kOperatorCount; ++destination)
        {
            const bool connected = std::abs(patch.matrix[static_cast<std::size_t>(source)][static_cast<std::size_t>(destination)]) > 0.00001f;
            const bool delayed = connected && (source == destination || reach[destination][source]);
            patch.delayed[static_cast<std::size_t>(source)][static_cast<std::size_t>(destination)] = delayed;
            if (connected && !delayed)
                ++indegree[destination];
        }

    bool emitted[kOperatorCount] {};
    for (int position = 0; position < kOperatorCount; ++position)
    {
        int next = -1;
        for (int candidate = 0; candidate < kOperatorCount; ++candidate)
            if (!emitted[candidate] && indegree[candidate] == 0) { next = candidate; break; }
        if (next < 0)
            for (int candidate = 0; candidate < kOperatorCount; ++candidate)
                if (!emitted[candidate]) { next = candidate; break; }
        patch.order[static_cast<std::size_t>(position)] = next;
        emitted[next] = true;
        for (int destination = 0; destination < kOperatorCount; ++destination)
            if (!patch.delayed[static_cast<std::size_t>(next)][static_cast<std::size_t>(destination)]
                && std::abs(patch.matrix[static_cast<std::size_t>(next)][static_cast<std::size_t>(destination)]) > 0.00001f)
                --indegree[destination];
    }
}

void FmSynthMachine::initialiseTables()
{
    static constexpr int harmonicCaps[kMipLevels] = { 1, 2, 4, 8, 16, 32, 64 };
    for (int wave = 0; wave < kPeriodicWaveCount; ++wave)
        for (int mip = 0; mip < kMipLevels; ++mip)
        {
            auto& table = tables[static_cast<std::size_t>(wave)][static_cast<std::size_t>(mip)];
            float peak = 0.0f;
            for (int sample = 0; sample < kTableSize; ++sample)
            {
                const float phase = static_cast<float>(sample) / static_cast<float>(kTableSize);
                float value = 0.0f;
                const int cap = harmonicCaps[mip];
                if (wave == static_cast<int>(Waveform::sine))
                    value = std::sin(kTwoPi * phase);
                else
                    for (int harmonic = 1; harmonic <= cap; ++harmonic)
                    {
                        float amplitude = 0.0f;
                        if (wave == static_cast<int>(Waveform::triangle) && (harmonic & 1))
                            amplitude = ((harmonic / 2) & 1 ? -1.0f : 1.0f) / static_cast<float>(harmonic * harmonic);
                        else if (wave == static_cast<int>(Waveform::saw))
                            amplitude = 1.0f / static_cast<float>(harmonic);
                        else if (wave == static_cast<int>(Waveform::square) && (harmonic & 1))
                            amplitude = 1.0f / static_cast<float>(harmonic);
                        else if (wave == static_cast<int>(Waveform::odd5) && (harmonic & 1) && harmonic <= 9)
                            amplitude = 1.0f;
                        else if (wave == static_cast<int>(Waveform::even5) && (harmonic % 2 == 0) && harmonic <= 8)
                            amplitude = 1.0f;
                        value += amplitude * std::sin(kTwoPi * phase * static_cast<float>(harmonic));
                    }
                table[static_cast<std::size_t>(sample)] = value;
                peak = std::max(peak, std::abs(value));
            }
            if (peak > 0.0f)
                for (auto& value : table)
                    value /= peak;
        }
}

void FmSynthMachine::markCustom() noexcept
{
    selectedPreset.store(static_cast<int>(Preset::custom), std::memory_order_relaxed);
    parameterRevision.fetch_add(1, std::memory_order_release);
}

void FmSynthMachine::markAlgorithmCustom() noexcept
{
    selectedAlgorithm.store(static_cast<int>(Algorithm::custom), std::memory_order_relaxed);
    markCustom();
}

void FmSynthMachine::applyAlgorithm(Algorithm algorithm)
{
    if (algorithm == Algorithm::custom || algorithm >= Algorithm::count)
        return;
    const bool alreadyWriting = patchWriteInProgress.exchange(true, std::memory_order_acq_rel);
    for (auto& row : modulationIndices)
        for (auto& value : row)
            value.store(0.0f);
    for (auto& op : operatorParameters)
    {
        op.enabled.store(1);
        op.output.store(0.0f);
    }
    const auto route = [this] (int source, int destination, float index = 1.0f)
    { modulationIndices[static_cast<std::size_t>(source)][static_cast<std::size_t>(destination)].store(index); };
    const auto carrier = [this] (int op)
    { operatorParameters[static_cast<std::size_t>(op)].output.store(100.0f); };
    switch (algorithm)
    {
        case Algorithm::pair: route(1, 0); carrier(0); operatorParameters[2].enabled.store(0); operatorParameters[3].enabled.store(0); break;
        case Algorithm::cascade: route(3, 2); route(2, 1); route(1, 0); carrier(0); break;
        case Algorithm::parallelModulators: route(1, 0); route(2, 0); route(3, 0); carrier(0); break;
        case Algorithm::twoPairs: route(1, 0); route(3, 2); carrier(0); carrier(2); break;
        case Algorithm::sharedModulator: route(3, 0); route(3, 1); route(3, 2); carrier(0); carrier(1); carrier(2); break;
        case Algorithm::branch: route(3, 2); route(2, 0); route(2, 1); carrier(0); carrier(1); break;
        case Algorithm::additive: carrier(0); carrier(1); carrier(2); carrier(3); break;
        case Algorithm::feedback: route(3, 3, 1.0f); route(3, 2); route(2, 1); route(1, 0); carrier(0); break;
        case Algorithm::custom:
        case Algorithm::count: break;
    }
    selectedAlgorithm.store(static_cast<int>(algorithm));
    markCustom();
    if (!alreadyWriting)
        patchWriteInProgress.store(false, std::memory_order_release);
}

void FmSynthMachine::applyPreset(Preset preset)
{
    if (preset == Preset::custom || preset >= Preset::count)
        return;
    const bool alreadyWriting = patchWriteInProgress.exchange(true, std::memory_order_acq_rel);
    for (int opIndex = 0; opIndex < kOperatorCount; ++opIndex)
    {
        auto& op = operatorParameters[static_cast<std::size_t>(opIndex)];
        op.enabled.store(1); op.waveform.store(0); op.frequencyMode.store(0);
        op.coarse.store(opIndex == 0 ? 1 : opIndex + 1); op.fine.store(0);
        op.fixedHz.store(440.0f); op.detuneCents.store(0.0f); op.output.store(0.0f);
        const int rates[4] = { 99, 70, 50, 45 };
        const float levels[4] = { 100.0f, 80.0f, 80.0f, 0.0f };
        for (std::size_t stage = 0; stage < 4; ++stage) { op.rates[stage].store(rates[stage]); op.levels[stage].store(levels[stage]); }
        op.velocitySensitivity.store(0); op.breakpoint.store(60); op.leftSlope.store(0);
        op.rightSlope.store(0); op.rateScaling.store(0);
    }
    voiceCount.store(4); masterLevel.store(0.8f); octave.store(0); glideMs.store(0.0f);
    indexPercent.store(100.0f); feedbackPercent.store(100.0f); colorPercent.store(100.0f); drivePercent.store(0.0f);

    const auto setRatio = [this] (int op, float ratio)
    {
        const int coarseValue = juce::jlimit(0, 64, static_cast<int>(std::floor(ratio)));
        const int fineValue = juce::jlimit(0, 99, static_cast<int>(std::lround((ratio - static_cast<float>(coarseValue)) * 100.0f)));
        operatorParameters[static_cast<std::size_t>(op)].coarse.store(coarseValue);
        operatorParameters[static_cast<std::size_t>(op)].fine.store(fineValue);
    };
    const auto shortEnvelope = [this] (int op, int releaseRate)
    {
        operatorParameters[static_cast<std::size_t>(op)].rates[0].store(99);
        operatorParameters[static_cast<std::size_t>(op)].rates[1].store(releaseRate);
        operatorParameters[static_cast<std::size_t>(op)].levels[0].store(100.0f);
        operatorParameters[static_cast<std::size_t>(op)].levels[1].store(0.0f);
        operatorParameters[static_cast<std::size_t>(op)].levels[2].store(0.0f);
        operatorParameters[static_cast<std::size_t>(op)].levels[3].store(0.0f);
    };

    switch (preset)
    {
        case Preset::init:
            applyAlgorithm(Algorithm::pair); setRatio(0, 1.0f); setRatio(1, 1.0f); modulationIndices[1][0].store(1.0f); break;
        case Preset::brass:
            applyAlgorithm(Algorithm::feedback); setRatio(0, 1.0f); setRatio(1, 1.0f); setRatio(2, 1.0f); setRatio(3, 1.0f);
            modulationIndices[3][3].store(2.2f); modulationIndices[3][2].store(0.7f); modulationIndices[2][1].store(1.1f); modulationIndices[1][0].store(2.5f);
            operatorParameters[1].velocitySensitivity.store(70); operatorParameters[2].rightSlope.store(-3); drivePercent.store(15.0f); break;
        case Preset::bell:
            applyAlgorithm(Algorithm::twoPairs); setRatio(0, 0.5f); setRatio(1, 0.68f); setRatio(2, 0.58f); setRatio(3, 0.79f);
            modulationIndices[1][0].store(5.0f); modulationIndices[3][2].store(3.2f); operatorParameters[2].output.store(55.0f);
            for (int op = 0; op < 4; ++op) shortEnvelope(op, 35 + op * 5);
            break;
        case Preset::gong:
            applyAlgorithm(Algorithm::cascade); setRatio(0, 0.9f); setRatio(1, 0.72f); setRatio(2, 0.58f); setRatio(3, 1.41f);
            modulationIndices[3][2].store(2.0f); modulationIndices[2][1].store(2.8f); modulationIndices[1][0].store(4.0f);
            for (int op = 0; op < 4; ++op) shortEnvelope(op, 25 + op * 6);
            break;
        case Preset::drum:
            applyAlgorithm(Algorithm::cascade); setRatio(0, 0.5f); setRatio(1, 0.68f); setRatio(2, 0.51f); setRatio(3, 3.48f);
            modulationIndices[3][2].store(5.5f); modulationIndices[2][1].store(2.0f); modulationIndices[1][0].store(3.0f);
            for (int op = 0; op < 4; ++op) shortEnvelope(op, 70 - op * 5);
            operatorParameters[3].velocitySensitivity.store(100);
            break;
        case Preset::pipe:
            applyAlgorithm(Algorithm::parallelModulators); setRatio(0, 1.0f); setRatio(1, 2.0f); setRatio(2, 3.47f); setRatio(3, 0.74f);
            modulationIndices[1][0].store(0.8f); modulationIndices[2][0].store(1.5f); modulationIndices[3][0].store(0.25f);
            shortEnvelope(2, 82); operatorParameters[2].velocitySensitivity.store(80); break;
        case Preset::harpsichord:
            applyAlgorithm(Algorithm::cascade); setRatio(0, 1.0f); setRatio(1, 3.0f); setRatio(2, 9.0f); setRatio(3, 18.0f);
            modulationIndices[3][2].store(0.7f); modulationIndices[2][1].store(1.4f); modulationIndices[1][0].store(2.8f);
            for (int op = 0; op < 4; ++op) { shortEnvelope(op, 55 + op * 7); operatorParameters[static_cast<std::size_t>(op)].rateScaling.store(60); }
            operatorParameters[2].levels[3].store(70.0f); operatorParameters[2].rightSlope.store(-6); break;
        case Preset::voice:
            applyAlgorithm(Algorithm::sharedModulator); setRatio(0, 1.0f); setRatio(1, 2.0f); setRatio(2, 9.0f);
            operatorParameters[3].frequencyMode.store(1); operatorParameters[3].fixedHz.store(2000.0f);
            modulationIndices[3][0].store(1.2f); modulationIndices[3][1].store(0.8f); modulationIndices[3][2].store(0.5f);
            operatorParameters[0].output.store(100.0f); operatorParameters[1].output.store(45.0f); operatorParameters[2].output.store(25.0f);
            operatorParameters[3].rightSlope.store(-6); break;
        case Preset::residual:
            applyAlgorithm(Algorithm::additive); setRatio(0, 2.0f); setRatio(1, 3.0f); setRatio(2, 4.0f); setRatio(3, 5.0f);
            operatorParameters[0].output.store(80.0f); operatorParameters[1].output.store(65.0f); operatorParameters[2].output.store(50.0f); operatorParameters[3].output.store(40.0f); break;
        case Preset::parallelModulators:
            applyAlgorithm(Algorithm::parallelModulators); setRatio(0, 1.0f); setRatio(1, 1.0f); setRatio(2, 0.1f); setRatio(3, 1.41f);
            modulationIndices[1][0].store(2.0f); modulationIndices[2][0].store(1.0f); modulationIndices[3][0].store(0.7f); break;
        case Preset::cascade:
            applyAlgorithm(Algorithm::cascade); setRatio(0, 1.0f); setRatio(1, 1.0f); setRatio(2, 0.5f); setRatio(3, 0.1f);
            modulationIndices[3][2].store(2.0f); modulationIndices[2][1].store(2.5f); modulationIndices[1][0].store(3.0f); break;
        case Preset::noise:
            applyAlgorithm(Algorithm::feedback); setRatio(0, 53.63f); setRatio(1, 0.72f); setRatio(2, 0.58f); setRatio(3, 1.0f);
            modulationIndices[3][3].store(6.0f); modulationIndices[3][2].store(4.0f); modulationIndices[2][1].store(3.0f); modulationIndices[1][0].store(5.0f);
            operatorParameters[3].waveform.store(static_cast<int>(Waveform::noise)); colorPercent.store(35.0f); drivePercent.store(25.0f); break;
        case Preset::evolve:
        {
            // A roughly 27-second feedback cascade.  The three modulators
            // traverse deliberately different index contours so the held
            // sound opens, hollows out, and then rebuilds into a brighter
            // inharmonic spectrum instead of merely changing loudness.
            applyAlgorithm(Algorithm::feedback);
            setRatio(0, 1.0f); setRatio(1, 1.0f); setRatio(2, 1.41f); setRatio(3, 0.37f);
            modulationIndices[3][3].store(2.8f); modulationIndices[3][2].store(1.4f);
            modulationIndices[2][1].store(2.2f); modulationIndices[1][0].store(3.4f);
            const int rates[4][4] = {
                { 85, 9, 6, 24 }, { 12, 6, 9, 24 },
                { 8, 13, 4, 22 }, { 15, 7, 11, 20 }
            };
            const float levels[4][4] = {
                { 100.0f, 82.0f, 95.0f, 0.0f }, { 15.0f, 100.0f, 30.0f, 0.0f },
                { 75.0f, 8.0f, 95.0f, 0.0f }, { 20.0f, 100.0f, 12.0f, 0.0f }
            };
            for (int op = 0; op < 4; ++op)
                for (int stage = 0; stage < 4; ++stage)
                {
                    operatorParameters[static_cast<std::size_t>(op)].rates[static_cast<std::size_t>(stage)].store(rates[op][stage]);
                    operatorParameters[static_cast<std::size_t>(op)].levels[static_cast<std::size_t>(stage)].store(levels[op][stage]);
                }
            operatorParameters[1].waveform.store(static_cast<int>(Waveform::triangle));
            operatorParameters[2].waveform.store(static_cast<int>(Waveform::odd5));
            operatorParameters[3].waveform.store(static_cast<int>(Waveform::saw));
            operatorParameters[1].velocitySensitivity.store(55);
            operatorParameters[2].velocitySensitivity.store(70);
            operatorParameters[3].velocitySensitivity.store(80);
            operatorParameters[1].rightSlope.store(-2);
            operatorParameters[2].rightSlope.store(-3);
            operatorParameters[3].rightSlope.store(-6);
            operatorParameters[2].rateScaling.store(20);
            operatorParameters[3].rateScaling.store(30);
            masterLevel.store(0.68f); indexPercent.store(85.0f); feedbackPercent.store(110.0f);
            colorPercent.store(32.0f); drivePercent.store(8.0f);
            break;
        }
        case Preset::custom:
        case Preset::count: break;
    }
    selectedPreset.store(static_cast<int>(preset));
    if (!alreadyWriting)
        patchWriteInProgress.store(false, std::memory_order_release);
}

const char* FmSynthMachine::waveformName(Waveform waveform) noexcept
{
    static constexpr const char* names[] = { "SIN", "TRI", "SAW", "SQR", "ODD5", "EVE5", "NOISE", "S&H" };
    return names[juce::jlimit(0, static_cast<int>(Waveform::count) - 1, static_cast<int>(waveform))];
}

const char* FmSynthMachine::algorithmName(Algorithm algorithm) noexcept
{
    static constexpr const char* names[] = { "PAIR", "CASC", "PARM", "2PAIR", "SHARE", "BRNCH", "ADD", "FDBK", "CUST" };
    return names[juce::jlimit(0, static_cast<int>(Algorithm::custom), static_cast<int>(algorithm))];
}

const char* FmSynthMachine::presetName(Preset preset) noexcept
{
    static constexpr const char* names[] = { "INIT", "BRASS", "BELL", "GONG", "DRUM", "PIPE", "HARP", "VOX", "RESID", "PARMOD", "CASC", "NOISE", "EVOLVE", "CUST" };
    return names[juce::jlimit(0, static_cast<int>(Preset::custom), static_cast<int>(preset))];
}

std::string FmSynthMachine::noteName(int midiNote)
{
    static constexpr const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    midiNote = juce::jlimit(0, 127, midiNote);
    return std::string(names[midiNote % 12]) + std::to_string(midiNote / 12 - 1);
}

std::vector<std::vector<UIBox>> FmSynthMachine::getUIBoxes(const MachineUiContext&)
{
    constexpr std::size_t rows = 22;
    constexpr std::size_t columns = 15;
    std::vector<std::vector<UIBox>> boxes(columns, std::vector<UIBox>(rows));
    for (auto& column : boxes)
        for (auto& cell : column) { cell.kind = UIBox::Kind::None; cell.isDisabled = true; }

    const auto intCell = [] (std::atomic<int>& target, int step, int low, int high,
                             const std::function<void()>& changed)
    {
        UIBox cell = makeLabelCell(std::to_string(target.load())); cell.isDisabled = false;
        cell.onAdjust = [&target, step, low, high, changed] (int amount) { target.store(juce::jlimit(low, high, target.load() + amount * step)); changed(); };
        cell.onInsert = [&target, low, high, changed] (double value) { if (std::isfinite(value)) { target.store(juce::jlimit(low, high, static_cast<int>(std::lround(value)))); changed(); } };
        cell.hasValueScale = true; cell.valueNorm = static_cast<float>(target.load() - low) / static_cast<float>(high - low);
        return cell;
    };
    const auto floatCell = [] (std::atomic<float>& target, float step, float low, float high, int decimals,
                               const std::function<void()>& changed)
    {
        UIBox cell = makeLabelCell(formatFloat(target.load(), decimals)); cell.isDisabled = false;
        cell.onAdjust = [&target, step, low, high, changed] (int amount) { target.store(juce::jlimit(low, high, target.load() + static_cast<float>(amount) * step)); changed(); };
        cell.onInsert = [&target, low, high, changed] (double value) { if (std::isfinite(value)) { target.store(juce::jlimit(low, high, static_cast<float>(value))); changed(); } };
        cell.hasValueScale = true; cell.valueNorm = (target.load() - low) / (high - low);
        return cell;
    };
    const auto setPair = [&boxes] (std::size_t labelColumn, std::size_t row, const char* label, UIBox value)
    { boxes[labelColumn][row] = makeLabelCell(label); boxes[labelColumn][row].isDisabled = true; boxes[labelColumn + 1][row] = std::move(value); };
    const auto changed = [this] { markCustom(); };

    UIBox presetCell = makeLabelCell(presetName(static_cast<Preset>(selectedPreset.load()))); presetCell.isDisabled = false;
    presetCell.onAdjust = [this] (int amount) { const int current = selectedPreset.load() == static_cast<int>(Preset::custom) ? 0 : selectedPreset.load(); applyPreset(static_cast<Preset>(wrapChoice(current, amount, static_cast<int>(Preset::custom)))); };
    setPair(0, 0, "PSET", std::move(presetCell));
    UIBox algorithmCell = makeLabelCell(algorithmName(static_cast<Algorithm>(selectedAlgorithm.load()))); algorithmCell.isDisabled = false;
    algorithmCell.onAdjust = [this] (int amount) { const int current = selectedAlgorithm.load() == static_cast<int>(Algorithm::custom) ? 0 : selectedAlgorithm.load(); applyAlgorithm(static_cast<Algorithm>(wrapChoice(current, amount, static_cast<int>(Algorithm::custom)))); };
    setPair(0, 1, "ALG", std::move(algorithmCell));
    setPair(0, 2, "VOI", intCell(voiceCount, 1, 1, 8, changed));
    setPair(0, 3, "LEV", floatCell(masterLevel, 0.05f, 0.0f, 1.0f, 2, changed));
    setPair(0, 4, "OCT", intCell(octave, 1, -4, 4, changed));
    UIBox glideCell = makeLabelCell(formatFloat(glideMs.load(), 1)); glideCell.isDisabled = false;
    glideCell.onAdjust = [this] (int amount) { const float value = glideMs.load(); glideMs.store(juce::jlimit(0.0f, 2000.0f, amount > 0 ? (value <= 0.0f ? 1.0f : value * 1.41421356f) : value / 1.41421356f)); markCustom(); };
    glideCell.onInsert = [this] (double value) { if (std::isfinite(value)) { glideMs.store(juce::jlimit(0.0f, 2000.0f, static_cast<float>(value))); markCustom(); } };
    setPair(0, 5, "GLIDE", std::move(glideCell));
    setPair(0, 6, "INDEX", floatCell(indexPercent, 5.0f, 0.0f, 200.0f, 0, changed));
    setPair(0, 7, "FEED", floatCell(feedbackPercent, 5.0f, 0.0f, 200.0f, 0, changed));
    setPair(0, 8, "COLOR", floatCell(colorPercent, 5.0f, 0.0f, 100.0f, 0, changed));
    setPair(0, 9, "DRIVE", floatCell(drivePercent, 5.0f, 0.0f, 100.0f, 0, changed));

    for (int opIndex = 0; opIndex < kOperatorCount; ++opIndex)
    {
        const std::size_t labelColumn = static_cast<std::size_t>(2 + opIndex * 2);
        auto& op = operatorParameters[static_cast<std::size_t>(opIndex)];
        boxes[labelColumn][0] = makeLabelCell("OP" + std::to_string(opIndex + 1)); boxes[labelColumn][0].isDisabled = true;
        UIBox onCell = makeToggleCell(op.enabled.load() != 0, "OFF", "ON", [this, &op] (int) { op.enabled.store(op.enabled.load() == 0 ? 1 : 0); markCustom(); }); onCell.isDisabled = false;
        setPair(labelColumn, 1, "ON", std::move(onCell));
        UIBox waveCell = makeLabelCell(waveformName(static_cast<Waveform>(op.waveform.load()))); waveCell.isDisabled = false;
        waveCell.onAdjust = [this, &op] (int amount) { op.waveform.store(wrapChoice(op.waveform.load(), amount, static_cast<int>(Waveform::count))); markCustom(); };
        setPair(labelColumn, 2, "WAVE", std::move(waveCell));
        UIBox modeCell = makeLabelCell(op.frequencyMode.load() == 0 ? "RAT" : "FIX"); modeCell.isDisabled = false;
        modeCell.onAdjust = [this, &op] (int) { op.frequencyMode.store(op.frequencyMode.load() == 0 ? 1 : 0); markCustom(); };
        setPair(labelColumn, 3, "MODE", std::move(modeCell));
        setPair(labelColumn, 4, "CRS", intCell(op.coarse, 1, 0, 64, changed));
        setPair(labelColumn, 5, "FINE", intCell(op.fine, 1, 0, 99, changed));
        UIBox fixedCell = makeLabelCell(formatHz(op.fixedHz.load())); fixedCell.isDisabled = false;
        fixedCell.onAdjust = [this, &op] (int amount) { op.fixedHz.store(juce::jlimit(kMinimumFixedHz, kMaximumFixedHz, amount > 0 ? op.fixedHz.load() * 1.0594631f : op.fixedHz.load() / 1.0594631f)); markCustom(); };
        fixedCell.onInsert = [this, &op] (double value) { if (std::isfinite(value)) { op.fixedHz.store(juce::jlimit(kMinimumFixedHz, kMaximumFixedHz, static_cast<float>(value))); markCustom(); } };
        setPair(labelColumn, 6, "FIX", std::move(fixedCell));
        setPair(labelColumn, 7, "DET", floatCell(op.detuneCents, 1.0f, -100.0f, 100.0f, 1, changed));
        setPair(labelColumn, 8, "OUT", floatCell(op.output, 5.0f, 0.0f, 100.0f, 0, [this] { markAlgorithmCustom(); }));
        for (int stage = 0; stage < 4; ++stage)
        {
            const auto stageIndex = static_cast<std::size_t>(stage);
            setPair(labelColumn, static_cast<std::size_t>(9 + stage * 2), ("R" + std::to_string(stage + 1)).c_str(), intCell(op.rates[stageIndex], 5, 0, 99, changed));
            setPair(labelColumn, static_cast<std::size_t>(10 + stage * 2), ("L" + std::to_string(stage + 1)).c_str(), floatCell(op.levels[stageIndex], 5.0f, 0.0f, 100.0f, 0, changed));
        }
        setPair(labelColumn, 17, "VEL", intCell(op.velocitySensitivity, 10, -100, 100, changed));
        UIBox breakpointCell = makeLabelCell(noteName(op.breakpoint.load())); breakpointCell.isDisabled = false;
        breakpointCell.onAdjust = [this, &op] (int amount) { op.breakpoint.store(juce::jlimit(0, 127, op.breakpoint.load() + amount)); markCustom(); };
        breakpointCell.onInsert = [this, &op] (double value) { if (std::isfinite(value)) { op.breakpoint.store(juce::jlimit(0, 127, static_cast<int>(std::lround(value)))); markCustom(); } };
        setPair(labelColumn, 18, "BP", std::move(breakpointCell));
        setPair(labelColumn, 19, "LSL", intCell(op.leftSlope, 1, -12, 12, changed));
        setPair(labelColumn, 20, "LSR", intCell(op.rightSlope, 1, -12, 12, changed));
        setPair(labelColumn, 21, "RSCL", intCell(op.rateScaling, 10, -100, 100, changed));
    }

    boxes[10][0] = makeLabelCell("FROM"); boxes[10][0].isDisabled = true;
    for (int destination = 0; destination < kOperatorCount; ++destination)
    {
        boxes[static_cast<std::size_t>(11 + destination)][0] = makeLabelCell("TO" + std::to_string(destination + 1));
        boxes[static_cast<std::size_t>(11 + destination)][0].isDisabled = true;
    }
    for (int source = 0; source < kOperatorCount; ++source)
    {
        const auto sourceRow = static_cast<std::size_t>(source + 1);
        boxes[10][sourceRow] = makeLabelCell("OP" + std::to_string(source + 1)); boxes[10][sourceRow].isDisabled = true;
        for (int destination = 0; destination < kOperatorCount; ++destination)
        {
            auto& target = modulationIndices[static_cast<std::size_t>(source)][static_cast<std::size_t>(destination)];
            boxes[static_cast<std::size_t>(11 + destination)][static_cast<std::size_t>(source + 1)] =
                floatCell(target, 0.25f, kMinimumIndex, kMaximumIndex, 2, [this] { markAlgorithmCustom(); });
        }
    }
    return boxes;
}

void FmSynthMachine::getStateInformation(juce::MemoryBlock& destData)
{
    auto* root = new juce::DynamicObject();
    root->setProperty("version", 2);
    root->setProperty("preset", selectedPreset.load()); root->setProperty("algorithm", selectedAlgorithm.load());
    root->setProperty("voices", voiceCount.load()); root->setProperty("level", masterLevel.load());
    root->setProperty("octave", octave.load()); root->setProperty("glideMs", glideMs.load());
    root->setProperty("index", indexPercent.load()); root->setProperty("feedback", feedbackPercent.load());
    root->setProperty("color", colorPercent.load()); root->setProperty("drive", drivePercent.load());
    juce::Array<juce::var> operators;
    for (const auto& op : operatorParameters)
    {
        auto* object = new juce::DynamicObject();
        object->setProperty("on", op.enabled.load()); object->setProperty("wave", op.waveform.load()); object->setProperty("mode", op.frequencyMode.load());
        object->setProperty("coarse", op.coarse.load()); object->setProperty("fine", op.fine.load()); object->setProperty("fixedHz", op.fixedHz.load());
        object->setProperty("detune", op.detuneCents.load()); object->setProperty("output", op.output.load());
        juce::Array<juce::var> rates, levels;
        for (std::size_t stage = 0; stage < 4; ++stage) { rates.add(op.rates[stage].load()); levels.add(op.levels[stage].load()); }
        object->setProperty("rates", rates); object->setProperty("levels", levels);
        object->setProperty("velocity", op.velocitySensitivity.load()); object->setProperty("breakpoint", op.breakpoint.load());
        object->setProperty("leftSlope", op.leftSlope.load()); object->setProperty("rightSlope", op.rightSlope.load()); object->setProperty("rateScaling", op.rateScaling.load());
        operators.add(juce::var(object));
    }
    root->setProperty("operators", operators);
    juce::Array<juce::var> matrix;
    for (const auto& row : modulationIndices) for (const auto& value : row) matrix.add(value.load());
    root->setProperty("matrix", matrix);
    writeMachineStateJson(destData, juce::var(root));
}

void FmSynthMachine::setStateInformation(const void* data, int sizeInBytes)
{
    const juce::var parsed = parseMachineStateJson(data, sizeInBytes);
    if (parsed.isVoid()) return;
    const bool alreadyWriting = patchWriteInProgress.exchange(true, std::memory_order_acq_rel);
    voiceCount.store(getIntProperty(parsed, "voices", voiceCount.load(), 1, 8));
    masterLevel.store(finiteClamp(static_cast<float>(parsed.getProperty("level", masterLevel.load())), masterLevel.load(), 0.0f, 1.0f));
    octave.store(getIntProperty(parsed, "octave", octave.load(), -4, 4));
    glideMs.store(finiteClamp(static_cast<float>(parsed.getProperty("glideMs", glideMs.load())), glideMs.load(), 0.0f, 2000.0f));
    indexPercent.store(finiteClamp(static_cast<float>(parsed.getProperty("index", indexPercent.load())), indexPercent.load(), 0.0f, 200.0f));
    feedbackPercent.store(finiteClamp(static_cast<float>(parsed.getProperty("feedback", feedbackPercent.load())), feedbackPercent.load(), 0.0f, 200.0f));
    colorPercent.store(finiteClamp(static_cast<float>(parsed.getProperty("color", colorPercent.load())), colorPercent.load(), 0.0f, 100.0f));
    drivePercent.store(finiteClamp(static_cast<float>(parsed.getProperty("drive", drivePercent.load())), drivePercent.load(), 0.0f, 100.0f));
    if (const auto* array = parsed.getProperty("operators", {}).getArray())
        for (int opIndex = 0; opIndex < juce::jmin(kOperatorCount, array->size()); ++opIndex)
        {
            const auto& value = array->getReference(opIndex); if (!value.isObject()) continue;
            auto& op = operatorParameters[static_cast<std::size_t>(opIndex)];
            op.enabled.store(getIntProperty(value, "on", op.enabled.load(), 0, 1)); op.waveform.store(getIntProperty(value, "wave", op.waveform.load(), 0, static_cast<int>(Waveform::count) - 1));
            op.frequencyMode.store(getIntProperty(value, "mode", op.frequencyMode.load(), 0, 1)); op.coarse.store(getIntProperty(value, "coarse", op.coarse.load(), 0, 64)); op.fine.store(getIntProperty(value, "fine", op.fine.load(), 0, 99));
            op.fixedHz.store(finiteClamp(static_cast<float>(value.getProperty("fixedHz", op.fixedHz.load())), op.fixedHz.load(), kMinimumFixedHz, kMaximumFixedHz));
            op.detuneCents.store(finiteClamp(static_cast<float>(value.getProperty("detune", op.detuneCents.load())), op.detuneCents.load(), -100.0f, 100.0f));
            op.output.store(finiteClamp(static_cast<float>(value.getProperty("output", op.output.load())), op.output.load(), 0.0f, 100.0f));
            if (const auto* rates = value.getProperty("rates", {}).getArray())
                for (int stage = 0; stage < juce::jmin(4, rates->size()); ++stage)
                {
                    const auto stageIndex = static_cast<std::size_t>(stage);
                    op.rates[stageIndex].store(juce::jlimit(0, 99, static_cast<int>(rates->getReference(stage))));
                }
            if (const auto* levels = value.getProperty("levels", {}).getArray())
                for (int stage = 0; stage < juce::jmin(4, levels->size()); ++stage)
                {
                    const auto stageIndex = static_cast<std::size_t>(stage);
                    op.levels[stageIndex].store(finiteClamp(static_cast<float>(levels->getReference(stage)), op.levels[stageIndex].load(), 0.0f, 100.0f));
                }
            op.velocitySensitivity.store(getIntProperty(value, "velocity", op.velocitySensitivity.load(), -100, 100)); op.breakpoint.store(getIntProperty(value, "breakpoint", op.breakpoint.load(), 0, 127));
            op.leftSlope.store(getIntProperty(value, "leftSlope", op.leftSlope.load(), -12, 12)); op.rightSlope.store(getIntProperty(value, "rightSlope", op.rightSlope.load(), -12, 12)); op.rateScaling.store(getIntProperty(value, "rateScaling", op.rateScaling.load(), -100, 100));
        }
    if (const auto* matrix = parsed.getProperty("matrix", {}).getArray())
        for (int source = 0; source < kOperatorCount; ++source) for (int destination = 0; destination < kOperatorCount; ++destination)
        {
            const int index = source * kOperatorCount + destination; if (index < matrix->size())
                modulationIndices[static_cast<std::size_t>(source)][static_cast<std::size_t>(destination)].store(
                    finiteClamp(static_cast<float>(matrix->getReference(index)), 0.0f, kMinimumIndex, kMaximumIndex));
        }
    const int stateVersion = getIntProperty(parsed, "version", 1, 1, 2);
    int restoredPreset = getIntProperty(parsed, "preset", static_cast<int>(Preset::custom), 0, static_cast<int>(Preset::custom));
    // Version 1 used numeric value 12 for CUST; EVOLVE now occupies that
    // value, so retain the identity of existing custom patches explicitly.
    if (stateVersion < 2 && restoredPreset == 12)
        restoredPreset = static_cast<int>(Preset::custom);
    selectedPreset.store(restoredPreset);
    selectedAlgorithm.store(getIntProperty(parsed, "algorithm", static_cast<int>(Algorithm::custom), 0, static_cast<int>(Algorithm::custom)));
    allNotesOff();
    parameterRevision.fetch_add(1, std::memory_order_release);
    if (!alreadyWriting)
        patchWriteInProgress.store(false, std::memory_order_release);
}
