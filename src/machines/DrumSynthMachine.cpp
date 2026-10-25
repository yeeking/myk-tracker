#include "DrumSynthMachine.h"

#include "MachineStateCodec.h"
#include "MachineUi.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
constexpr int kStateVersion = 1;
constexpr std::array<float, 6> kMetalFrequencies {{ 245.0f, 306.0f, 365.0f, 415.0f, 437.0f, 619.0f }};
constexpr float kMinimumEnvelope = 0.00001f;

const char* const kWaveNames[] = { "SIN", "TRI", "SAW", "SQR", "ADD1", "ADD2", "ADD10", "EVE5", "ODD5" };
const char* const kNoiseNames[] = { "WHT", "PNK", "MET" };
const char* const kSnareNames[] = { "606", "808", "909" };
const char* const kTriggerModeNames[] = { "GATE", "1SHOT" };
const char* const kEnabledNames[] = { "OFF", "ON" };

}

DrumSynthMachine::Tables::Tables()
{
    for (int index = 0; index < kTableSize; ++index)
    {
        const float phase = static_cast<float>(index) / static_cast<float>(kTableSize);
        const float angle = juce::MathConstants<float>::twoPi * phase;
        waves[0][static_cast<std::size_t>(index)] = std::sin(angle);
        waves[1][static_cast<std::size_t>(index)] = 1.0f - 4.0f * std::abs(phase - 0.5f);
        waves[2][static_cast<std::size_t>(index)] = 2.0f * phase - 1.0f;
        waves[3][static_cast<std::size_t>(index)] = phase < 0.5f ? 1.0f : -1.0f;
        sineCycle[static_cast<std::size_t>(index)] = std::sin(angle);
        cosineCycle[static_cast<std::size_t>(index)] = std::cos(angle);
    }
    sineCycle.back() = sineCycle.front();
    cosineCycle.back() = cosineCycle.front();

    const std::array<std::vector<int>, 5> harmonics {{
        { 1, 2 }, { 1, 2, 3 }, { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10 },
        { 1, 2, 4, 6, 8 }, { 1, 3, 5, 7, 9 }
    }};
    for (std::size_t tableIndex = 0; tableIndex < harmonics.size(); ++tableIndex)
    {
        auto& destination = waves[tableIndex + 4];
        float peak = 0.0f;
        for (int index = 0; index < kTableSize; ++index)
        {
            const float phase = static_cast<float>(index) / static_cast<float>(kTableSize);
            float value = 0.0f;
            for (const int harmonic : harmonics[tableIndex])
                value += std::sin(juce::MathConstants<float>::twoPi * phase * static_cast<float>(harmonic));
            destination[static_cast<std::size_t>(index)] = value;
            peak = std::max(peak, std::abs(value));
        }
        if (peak > 0.0f)
            for (auto& value : destination) value /= peak;
    }

    for (int mip = 0; mip < kSquareMipCount; ++mip)
    {
        const int maximumHarmonic = std::max(1, 63 >> mip);
        float peak = 0.0f;
        for (int index = 0; index < kTableSize; ++index)
        {
            const float phase = static_cast<float>(index) / static_cast<float>(kTableSize);
            float value = 0.0f;
            for (int harmonic = 1; harmonic <= maximumHarmonic; harmonic += 2)
                value += std::sin(juce::MathConstants<float>::twoPi * phase * static_cast<float>(harmonic)) / static_cast<float>(harmonic);
            squareMips[static_cast<std::size_t>(mip)][static_cast<std::size_t>(index)] = value;
            peak = std::max(peak, std::abs(value));
        }
        for (auto& value : squareMips[static_cast<std::size_t>(mip)]) value /= std::max(peak, 0.001f);
    }

    for (int semitone = -72; semitone <= 72; ++semitone)
        pitchRatios[static_cast<std::size_t>(semitone + 72)] = std::pow(2.0f, static_cast<float>(semitone) / 12.0f);
    for (int note = 0; note < 128; ++note)
        midiHz[static_cast<std::size_t>(note)] = static_cast<float>(juce::MidiMessage::getMidiNoteInHertz(note));
}

float DrumSynthMachine::Biquad::process(float input) noexcept
{
    // A corrupt or runaway recursive state must never be allowed to poison the
    // rest of a machine stack.  Ordinary drum-filter output is comfortably
    // below this limit, so this branch is strictly a last-resort stability
    // guard rather than part of the filter's sound.
    constexpr float safetyLimit = 32.0f;
    const float output = b0 * input + z1;
    if (!std::isfinite(output) || std::abs(output) > safetyLimit)
    {
        reset();
        return 0.0f;
    }
    z1 = b1 * input - a1 * output + z2;
    z2 = b2 * input - a2 * output;
    if (!std::isfinite(z1) || !std::isfinite(z2)
        || std::abs(z1) > safetyLimit || std::abs(z2) > safetyLimit)
    {
        reset();
        return 0.0f;
    }
    return output;
}

namespace
{
void lookupSinCos(float normalisedFrequency, const DrumSynthMachine::Tables& tables, float& sine, float& cosine)
{
    const float position = juce::jlimit(0.0f, 0.499f, normalisedFrequency)
        * static_cast<float>(DrumSynthMachine::kTableSize);
    const int index = juce::jlimit(0, DrumSynthMachine::kTableSize - 1, static_cast<int>(position));
    const float fraction = position - static_cast<float>(index);
    sine = tables.sineCycle[static_cast<std::size_t>(index)]
        + fraction * (tables.sineCycle[static_cast<std::size_t>(index + 1)] - tables.sineCycle[static_cast<std::size_t>(index)]);
    cosine = tables.cosineCycle[static_cast<std::size_t>(index)]
        + fraction * (tables.cosineCycle[static_cast<std::size_t>(index + 1)] - tables.cosineCycle[static_cast<std::size_t>(index)]);
}
}

void DrumSynthMachine::Biquad::setBandPass(float hz, float q, double sampleRate, const Tables& source) noexcept
{
    float sine = 0.0f, cosine = 1.0f;
    lookupSinCos(hz / static_cast<float>(sampleRate), source, sine, cosine);
    const float alpha = sine / (2.0f * std::max(0.1f, q));
    const float inverseA0 = 1.0f / (1.0f + alpha);
    b0 = alpha * inverseA0; b1 = 0.0f; b2 = -b0;
    a1 = -2.0f * cosine * inverseA0; a2 = (1.0f - alpha) * inverseA0;
}

void DrumSynthMachine::Biquad::setLowPass(float hz, float q, double sampleRate, const Tables& source) noexcept
{
    float sine = 0.0f, cosine = 1.0f;
    lookupSinCos(hz / static_cast<float>(sampleRate), source, sine, cosine);
    const float alpha = sine / (2.0f * std::max(0.1f, q));
    const float inverseA0 = 1.0f / (1.0f + alpha);
    b0 = 0.5f * (1.0f - cosine) * inverseA0;
    b1 = (1.0f - cosine) * inverseA0;
    b2 = b0;
    a1 = -2.0f * cosine * inverseA0; a2 = (1.0f - alpha) * inverseA0;
}

void DrumSynthMachine::Biquad::setHighPass(float hz, float q, double sampleRate, const Tables& source) noexcept
{
    float sine = 0.0f, cosine = 1.0f;
    lookupSinCos(hz / static_cast<float>(sampleRate), source, sine, cosine);
    const float alpha = sine / (2.0f * std::max(0.1f, q));
    const float inverseA0 = 1.0f / (1.0f + alpha);
    b0 = 0.5f * (1.0f + cosine) * inverseA0;
    b1 = -(1.0f + cosine) * inverseA0;
    b2 = b0;
    a1 = -2.0f * cosine * inverseA0; a2 = (1.0f - alpha) * inverseA0;
}

DrumSynthMachine::DrumSynthMachine()
{
    (void) tables();
    resetParametersToDefaults();
}

const DrumSynthMachine::Tables& DrumSynthMachine::tables()
{
    static const Tables sharedTables;
    return sharedTables;
}

DrumSynthMachine::Drum DrumSynthMachine::drumForNote(int midiNote) const noexcept
{
    if (midiNote < 0 || midiNote > 127) return Drum::none;
    // Primary responders are tested first so an intentional duplicate setting
    // remains deterministic.  The hat keeps its useful open articulation one
    // octave above its displayed closed-hat responder without consuming a
    // seventh strip or a seventh default note.
    if (midiNote == kick.triggerNote.load(std::memory_order_relaxed)) return Drum::kick;
    if (midiNote == snare.triggerNote.load(std::memory_order_relaxed)) return Drum::snare;
    if (midiNote == hat.triggerNote.load(std::memory_order_relaxed)) return Drum::closedHat;
    if (midiNote == crash.triggerNote.load(std::memory_order_relaxed)) return Drum::crash;
    if (midiNote == ride.triggerNote.load(std::memory_order_relaxed)) return Drum::ride;
    if (midiNote == tom.triggerNote.load(std::memory_order_relaxed)) return Drum::tom;
    const int openHatNote = hat.triggerNote.load(std::memory_order_relaxed) + 12;
    return midiNote == openHatNote && openHatNote <= 127 ? Drum::openHat : Drum::none;
}

const char* DrumSynthMachine::describeNote(int midiNote) const noexcept
{
    switch (drumForNote(midiNote))
    {
        case Drum::kick: return "KICK";
        case Drum::crash: return "CRSH";
        case Drum::snare: return "SNAR";
        case Drum::ride: return "RIDE";
        case Drum::closedHat: return "CHAT";
        case Drum::tom: return "TOM";
        case Drum::openHat: return "OHAT";
        case Drum::none: break;
    }
    return "----";
}

bool DrumSynthMachine::isDrumEnabled(Drum drum) const noexcept
{
    switch (drum)
    {
        case Drum::kick: return kick.enabled.load(std::memory_order_relaxed) != 0;
        case Drum::snare: return snare.enabled.load(std::memory_order_relaxed) != 0;
        case Drum::closedHat:
        case Drum::openHat: return hat.enabled.load(std::memory_order_relaxed) != 0;
        case Drum::crash: return crash.enabled.load(std::memory_order_relaxed) != 0;
        case Drum::ride: return ride.enabled.load(std::memory_order_relaxed) != 0;
        case Drum::tom: return tom.enabled.load(std::memory_order_relaxed) != 0;
        case Drum::none: break;
    }
    return false;
}

void DrumSynthMachine::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    juce::ignoreUnused(samplesPerBlock);
    currentSampleRate = sanitizedSampleRate(sampleRate);
    for (std::size_t milliseconds = 0; milliseconds < decayMultipliers.size(); ++milliseconds)
    {
        const double seconds = std::max(0.001, static_cast<double>(milliseconds) * 0.001);
        decayMultipliers[milliseconds] = static_cast<float>(std::exp(std::log(0.00001) / (seconds * currentSampleRate)));
    }
    releaseMultiplier3ms = decayMultiplierForMs(3.0f);
    releaseMultiplier2ms = decayMultiplierForMs(2.0f);
    outputSmoothing = 1.0f - static_cast<float>(std::exp(-1.0 / (currentSampleRate * 0.005)));
    outputLevels = {{
        kick.level.load(std::memory_order_relaxed) * 0.01f,
        snare.level.load(std::memory_order_relaxed) * 0.01f,
        hat.level.load(std::memory_order_relaxed) * 0.01f,
        crash.level.load(std::memory_order_relaxed) * 0.01f,
        ride.level.load(std::memory_order_relaxed) * 0.01f,
        tom.level.load(std::memory_order_relaxed) * 0.01f
    }};
    allNotesOff();
}

void DrumSynthMachine::releaseResources() { allNotesOff(); }

void DrumSynthMachine::scheduleBlockNotes(const std::vector<MachineScheduledNote>& notes)
{
    scheduledBlockNotes = &notes;
}

void DrumSynthMachine::setSecondsPerTick(double secondsPerTick)
{
    if (secondsPerTick > 0.0 && std::isfinite(secondsPerTick)) currentSecondsPerTick = secondsPerTick;
}

bool DrumSynthMachine::handleIncomingNote(unsigned short, unsigned short, unsigned short, MachineNoteEvent&)
{
    return false;
}

void DrumSynthMachine::allNotesOff()
{
    for (auto& voice : voices) voice = Voice {};
    hatChokeTail = Voice {};
    scheduledBlockNotes = nullptr;
}

float DrumSynthMachine::wrapPhase(double phase) noexcept
{
    return static_cast<float>(phase - std::floor(phase));
}

float DrumSynthMachine::sampleTable(const std::array<float, kTableSize>& table, double phase) noexcept
{
    const float position = wrapPhase(phase) * static_cast<float>(kTableSize);
    const int first = static_cast<int>(position) % kTableSize;
    const int second = (first + 1) % kTableSize;
    const float fraction = position - static_cast<float>(first);
    return table[static_cast<std::size_t>(first)]
        + fraction * (table[static_cast<std::size_t>(second)] - table[static_cast<std::size_t>(first)]);
}

float DrumSynthMachine::sampleWave(const Tables& source, int waveform, double phase) noexcept
{
    return sampleTable(source.waves[static_cast<std::size_t>(juce::jlimit(0, kWaveCount - 1, waveform))], phase);
}

float DrumSynthMachine::sampleSquare(const Tables& source, double phase, float hz) noexcept
{
    const float harmonicsAvailable = static_cast<float>(22050.0 / std::max(1.0f, hz));
    int mip = 0;
    while (mip + 1 < kSquareMipCount && static_cast<float>(63 >> mip) > harmonicsAvailable) ++mip;
    return sampleTable(source.squareMips[static_cast<std::size_t>(mip)], phase);
}

float DrumSynthMachine::pitchRatio(const Tables& source, float semitones) noexcept
{
    const float clamped = juce::jlimit(-72.0f, 72.0f, semitones);
    const float position = clamped + 72.0f;
    const int first = juce::jlimit(0, 144, static_cast<int>(position));
    const int second = std::min(144, first + 1);
    const float fraction = position - static_cast<float>(first);
    return source.pitchRatios[static_cast<std::size_t>(first)]
        + fraction * (source.pitchRatios[static_cast<std::size_t>(second)] - source.pitchRatios[static_cast<std::size_t>(first)]);
}

float DrumSynthMachine::softSaturate(float sample) noexcept
{
    const float clamped = juce::jlimit(-1.5f, 1.5f, sample);
    return clamped - (clamped * clamped * clamped * 0.14814815f);
}

float DrumSynthMachine::decayMultiplierForMs(float milliseconds) const noexcept
{
    const float clamped = juce::jlimit(1.0f, 12000.0f, milliseconds);
    const int first = static_cast<int>(clamped);
    const int second = std::min(12000, first + 1);
    const float fraction = clamped - static_cast<float>(first);
    return decayMultipliers[static_cast<std::size_t>(first)]
        + fraction * (decayMultipliers[static_cast<std::size_t>(second)] - decayMultipliers[static_cast<std::size_t>(first)]);
}

float DrumSynthMachine::smoothedOutputLevel(float current, float target) const noexcept
{
    return current + outputSmoothing * (target - current);
}

void DrumSynthMachine::configureGate(Voice& voice, const MachineScheduledNote& event, bool oneShot) noexcept
{
    voice.triggerNote = static_cast<int>(event.note);
    voice.oneShot = oneShot;
    voice.gateSamples = oneShot || event.durationTicks <= 0.0
        ? std::numeric_limits<int>::max()
        : juce::jmax(1, static_cast<int>(std::lround(currentSampleRate * currentSecondsPerTick * event.durationTicks)));
    voice.releasing = false;
    voice.gateGain = 1.0f;
    voice.releaseMultiplier = releaseMultiplier3ms;
    voice.accent = juce::jlimit(0.0f, 1.0f, static_cast<float>(event.velocity) / 127.0f);
    // The previous square-root curve left medium and high velocities too
    // close together.  This retains a usable low-velocity floor but makes
    // normal MIDI dynamics clearly affect each drum's body and transient.
    voice.velocityGain = 0.18f + 0.82f * voice.accent * voice.accent;
    voice.age = 0;
    voice.active = event.velocity > 0;
}

void DrumSynthMachine::configureMetalPhases(Voice& voice, const std::array<float, 6>& frequencies) noexcept
{
    if (voice.active) return;
    for (std::size_t index = 0; index < frequencies.size(); ++index)
        voice.phases[index] = std::fmod(static_cast<double>(sampleCounter) * frequencies[index] / currentSampleRate, 1.0);
}

void DrumSynthMachine::trigger(Drum drum, const MachineScheduledNote& event) noexcept
{
    if (!isDrumEnabled(drum)) return;
    const auto& source = tables();
    const bool wasActive = drum == Drum::openHat || drum == Drum::closedHat
        ? voices[2].active
        : voices[static_cast<std::size_t>(drum == Drum::kick ? 0 : drum == Drum::crash ? 3 : drum == Drum::snare ? 1 : drum == Drum::ride ? 4 : 5)].active;
    Voice* voice = nullptr;
    switch (drum)
    {
        case Drum::kick: voice = &voices[0]; break;
        case Drum::snare: voice = &voices[1]; break;
        case Drum::closedHat:
        case Drum::openHat: voice = &voices[2]; break;
        case Drum::crash: voice = &voices[3]; break;
        case Drum::ride: voice = &voices[4]; break;
        case Drum::tom: voice = &voices[5]; break;
        case Drum::none: return;
    }

    if (drum == Drum::closedHat && voice->active && voice->openHat)
    {
        hatChokeTail = *voice;
        beginRelease(hatChokeTail, 2);
    }
    if (!wasActive && (drum == Drum::kick || drum == Drum::tom))
        voice->phases.fill(0.0);
    if (!wasActive && drum != Drum::kick && drum != Drum::tom)
        configureMetalPhases(*voice, kMetalFrequencies);
    if (!wasActive)
        for (auto& filter : voice->filters)
            filter.reset();

    const bool oneShot = drum == Drum::kick ? kick.oneShot.load(std::memory_order_relaxed) != 0
        : drum == Drum::snare ? snare.oneShot.load(std::memory_order_relaxed) != 0
        : (drum == Drum::closedHat || drum == Drum::openHat) ? hat.oneShot.load(std::memory_order_relaxed) != 0
        : drum == Drum::crash ? crash.oneShot.load(std::memory_order_relaxed) != 0
        : drum == Drum::ride ? ride.oneShot.load(std::memory_order_relaxed) != 0
        : tom.oneShot.load(std::memory_order_relaxed) != 0;
    configureGate(*voice, event, oneShot);
    voice->envelope = wasActive ? voice->envelope + (1.0f - voice->envelope) * 0.85f : 1.0f;
    voice->envelope2 = wasActive ? voice->envelope2 + (1.0f - voice->envelope2) * 0.85f : 1.0f;
    voice->envelope3 = wasActive ? voice->envelope3 + (1.0f - voice->envelope3) * 0.85f : 1.0f;
    voice->bodyEnvelope2 = wasActive ? voice->bodyEnvelope2 + (1.0f - voice->bodyEnvelope2) * 0.85f : 1.0f;

    switch (drum)
    {
        case Drum::kick:
            voice->envelope2 = 0.0f;
            voice->waveform = kick.wave.load(std::memory_order_relaxed);
            voice->tuneRatio = source.midiHz[static_cast<std::size_t>(juce::jlimit(0, 127, kick.tune.load()))];
            voice->pitchAmount = static_cast<float>(kick.pitchAmount.load());
            voice->pitchUpSamples = static_cast<int>(currentSampleRate * kick.pitchUpMs.load() * 0.001);
            voice->pitchDownSamples = juce::jmax(1, static_cast<int>(currentSampleRate * kick.pitchDownMs.load() * 0.001));
            voice->decayMultiplier = decayMultiplierForMs(static_cast<float>(kick.decayMs.load()));
            voice->noiseType = kick.noiseType.load();
            voice->component1 = kick.body.load() * 0.01f;
            voice->component2 = kick.click.load() * 0.01f;
            break;
        case Drum::snare:
        {
            const int profile = snare.type.load();
            const float base = profile == 0 ? 170.0f : profile == 1 ? 180.0f : 185.0f;
            voice->profile = profile;
            voice->tuneRatio = base * pitchRatio(source, static_cast<float>(snare.tune.load()));
            voice->fmRatio = profile == 0 ? 1.72f : profile == 1 ? 1.83f : 1.89f;
            voice->waveform = profile == 2 ? 1 : 0;
            voice->decayMultiplier = decayMultiplierForMs(static_cast<float>(snare.bodyDecayMs.load()));
            voice->decayMultiplier2 = decayMultiplierForMs(static_cast<float>(snare.noiseDecayMs.load()));
            voice->bodyDecayMultiplier2 = decayMultiplierForMs(static_cast<float>(snare.bodyDecayMs.load()) * 0.78f);
            voice->decayMultiplier3 = decayMultiplierForMs(45.0f);
            voice->pitchAmount = profile == 2 ? 7.0f : 0.0f;
            voice->pitchDownSamples = juce::jmax(1, static_cast<int>(currentSampleRate * 0.020));
            voice->noiseHoldSamples = profile == 2 ? static_cast<int>(currentSampleRate * 0.024) : 0;
            voice->component1 = snare.body.load() * 0.01f;
            voice->component2 = snare.snap.load() * 0.01f;
            voice->tone = snare.tone.load() * 0.01f;
            if (profile == 2)
            {
                // The hardware combines separate low- and high-frequency
                // snappy paths rather than one bright band-pass signal.
                voice->filters[0].setLowPass(4200.0f + 3600.0f * voice->tone, 0.72f, currentSampleRate, source);
                voice->filters[1].setHighPass(1800.0f + 3200.0f * voice->tone, 0.72f, currentSampleRate, source);
                // A 909 trigger establishes the starting phase of both VCOs.
                voice->phases[0] = 0.0;
                voice->phases[1] = 0.0;
            }
            else
            {
                voice->filters[0].setBandPass(900.0f + 8500.0f * voice->tone * (0.9f + 0.1f * voice->accent), profile == 0 ? 0.8f : 1.1f, currentSampleRate, source);
            }
            break;
        }
        case Drum::closedHat:
        case Drum::openHat:
            voice->envelope2 = 0.0f;
            voice->openHat = drum == Drum::openHat;
            voice->tuneRatio = pitchRatio(source, static_cast<float>(hat.tune.load()));
            voice->spread = hat.spread.load() * 0.01f;
            voice->resonance = hat.resonance.load() * 0.01f;
            voice->component1 = hat.grit.load() * 0.01f;
            voice->decayMultiplier = decayMultiplierForMs(static_cast<float>(voice->openHat ? hat.openDecayMs.load() : hat.closedDecayMs.load()));
            voice->filters[0].setBandPass(hat.bandPassHz.load() * (0.9f + 0.1f * voice->accent), 0.7f + 5.0f * voice->resonance, currentSampleRate, source);
            voice->filters[1].setHighPass(hat.highPassHz.load(), 0.707f, currentSampleRate, source);
            break;
        case Drum::crash:
            voice->envelope2 = 0.0f;
            voice->tuneRatio = pitchRatio(source, static_cast<float>(crash.tune.load()));
            voice->spread = crash.spread.load() * 0.01f;
            voice->component1 = crash.metal.load() * 0.01f;
            voice->component2 = crash.noise.load() * 0.01f;
            voice->ring = crash.ring.load() * 0.01f;
            voice->tone = crash.tone.load() * 0.01f;
            voice->resonance = crash.resonance.load() * 0.01f;
            voice->attackSamples = static_cast<int>(currentSampleRate * crash.attackMs.load() * 0.001);
            voice->decayMultiplier = decayMultiplierForMs(static_cast<float>(crash.decayMs.load()));
            for (int band = 0; band < 3; ++band)
                voice->filters[static_cast<std::size_t>(band)].setBandPass(1800.0f + voice->tone * 2500.0f + band * 2100.0f, 0.8f + 6.0f * voice->resonance, currentSampleRate, source);
            voice->filters[3].setHighPass(1000.0f + voice->tone * 3500.0f, 0.707f, currentSampleRate, source);
            break;
        case Drum::ride:
            voice->envelope2 = 0.0f;
            voice->tuneRatio = pitchRatio(source, static_cast<float>(ride.tune.load()));
            voice->fmRatio = ride.ratio.load();
            voice->fmIndex = ride.fm.load();
            voice->component1 = ride.bell.load() * 0.01f;
            voice->component2 = ride.wash.load() * 0.01f;
            voice->ring = ride.ring.load() * 0.01f;
            voice->tone = ride.tone.load() * 0.01f;
            voice->decayMultiplier = decayMultiplierForMs(static_cast<float>(ride.decayMs.load()));
            voice->filters[0].setBandPass(2500.0f + voice->tone * 7000.0f, 1.0f + voice->tone * 6.0f, currentSampleRate, source);
            voice->filters[1].setHighPass(1800.0f + voice->tone * 2500.0f, 0.707f, currentSampleRate, source);
            break;
        case Drum::tom:
            voice->envelope2 = 0.0f;
            voice->waveform = tom.wave.load();
            voice->tuneRatio = source.midiHz[static_cast<std::size_t>(juce::jlimit(0, 127, tom.tune.load()))];
            voice->pitchAmount = static_cast<float>(tom.pitchAmount.load());
            voice->pitchUpSamples = 0;
            voice->pitchDownSamples = juce::jmax(1, static_cast<int>(currentSampleRate * tom.pitchDownMs.load() * 0.001));
            voice->decayMultiplier = decayMultiplierForMs(static_cast<float>(tom.decayMs.load()));
            voice->component1 = tom.body.load() * 0.01f;
            voice->component2 = tom.click.load() * 0.01f;
            break;
        case Drum::none: break;
    }
}

void DrumSynthMachine::beginRelease(Voice& voice, int releaseMs) noexcept
{
    if (!voice.active) return;
    voice.releasing = true;
    voice.gateGain = std::min(voice.gateGain, 1.0f);
    voice.releaseMultiplier = releaseMs == 2 ? releaseMultiplier2ms : releaseMultiplier3ms;
}

void DrumSynthMachine::release(Drum drum, int note) noexcept
{
    Voice* voice = nullptr;
    switch (drum)
    {
        case Drum::kick: voice = &voices[0]; break;
        case Drum::snare: voice = &voices[1]; break;
        case Drum::closedHat:
        case Drum::openHat: voice = &voices[2]; break;
        case Drum::crash: voice = &voices[3]; break;
        case Drum::ride: voice = &voices[4]; break;
        case Drum::tom: voice = &voices[5]; break;
        case Drum::none: return;
    }
    if (voice->active && !voice->oneShot && voice->triggerNote == note) beginRelease(*voice);
}

float DrumSynthMachine::nextNoise(Voice& voice) noexcept
{
    randomState ^= randomState << 13; randomState ^= randomState >> 17; randomState ^= randomState << 5;
    const float white = static_cast<float>(static_cast<std::int32_t>(randomState)) / 2147483648.0f;
    if (voice.noiseType == 1)
    {
        voice.noiseMemory += 0.075f * (white - voice.noiseMemory);
        return voice.noiseMemory;
    }
    if (voice.noiseType == 2)
        return white > 0.0f ? 1.0f : -1.0f;
    return white;
}

void DrumSynthMachine::updateVoiceEnvelope(Voice& voice) noexcept
{
    if (!voice.active) return;
    if (!voice.releasing && voice.gateSamples != std::numeric_limits<int>::max() && --voice.gateSamples <= 0)
        beginRelease(voice);
    if (voice.releasing)
    {
        voice.gateGain *= voice.releaseMultiplier;
        if (voice.gateGain < kMinimumEnvelope)
        {
            voice.active = false;
            voice.gateGain = 0.0f;
            for (auto& filter : voice.filters)
                filter.reset();
        }
    }
    else if (voice.envelope < kMinimumEnvelope && voice.envelope2 < kMinimumEnvelope)
    {
        voice.active = false;
        for (auto& filter : voice.filters)
            filter.reset();
    }
    ++voice.age;
}

float DrumSynthMachine::renderKick(Voice& voice) noexcept
{
    float pitchSemitones = 0.0f;
    if (voice.age < voice.pitchUpSamples && voice.pitchUpSamples > 0)
        pitchSemitones = voice.pitchAmount * static_cast<float>(voice.age) / static_cast<float>(voice.pitchUpSamples);
    else
    {
        const int downAge = voice.age - voice.pitchUpSamples;
        if (downAge < voice.pitchDownSamples)
            pitchSemitones = voice.pitchAmount * (1.0f - static_cast<float>(downAge) / static_cast<float>(voice.pitchDownSamples));
    }
    const float hz = voice.tuneRatio * pitchRatio(tables(), pitchSemitones);
    const float body = sampleWave(tables(), voice.waveform, voice.phases[0]) * voice.envelope * voice.component1;
    voice.phases[0] += hz / currentSampleRate;
    const float clickEnvelope = std::max(0.0f, 1.0f - static_cast<float>(voice.age) / static_cast<float>(std::max(1.0, currentSampleRate * 0.006)));
    const float click = nextNoise(voice) * clickEnvelope * voice.component2 * (0.8f + 0.2f * voice.accent);
    voice.envelope *= voice.decayMultiplier;
    return (body + click) * voice.velocityGain * voice.gateGain;
}

float DrumSynthMachine::renderSnare(Voice& voice) noexcept
{
    if (voice.profile == 2)
    {
        const float pitchProgress = std::min(1.0f,
            static_cast<float>(voice.age) / static_cast<float>(voice.pitchDownSamples));
        const float bendRatio = pitchRatio(tables(), voice.pitchAmount * (1.0f - pitchProgress));
        const float frequency1 = voice.tuneRatio * bendRatio;
        const float frequency2 = voice.tuneRatio * voice.fmRatio * bendRatio;

        // The analogue VCOs generate triangles rounded by diode shaping.  A
        // little saturation gives a denser shell than the clean sine pair used
        // for the other two character profiles.
        const float first = softSaturate(sampleWave(tables(), 1, voice.phases[0]) * 1.25f);
        const float second = softSaturate(sampleWave(tables(), 1, voice.phases[1]) * 1.25f);
        voice.phases[0] += frequency1 / currentSampleRate;
        voice.phases[1] += frequency2 / currentSampleRate;
        const float body = (first * voice.envelope
            + second * 0.88f * voice.bodyEnvelope2) * 0.72f * voice.component1;

        // Approximate the rectangular shift-register noise used by the 909.
        // Its low/mid path holds briefly before decaying, while the independent
        // high-passed path supplies the initial snappy articulation.
        const float rawNoise = nextNoise(voice) >= 0.0f ? 1.0f : -1.0f;
        const float lowNoise = voice.filters[0].process(rawNoise);
        const float highNoise = voice.filters[1].process(rawNoise);
        const float noise = (lowNoise * 0.72f * voice.envelope2
            + highNoise * 0.42f * voice.envelope3)
            * voice.component2 * (0.75f + 0.25f * voice.accent);

        voice.envelope *= voice.decayMultiplier;
        voice.bodyEnvelope2 *= voice.bodyDecayMultiplier2;
        if (voice.age >= voice.noiseHoldSamples)
            voice.envelope2 *= voice.decayMultiplier2;
        voice.envelope3 *= voice.decayMultiplier3;
        return softSaturate((body + noise) * 1.08f) * voice.velocityGain * voice.gateGain;
    }

    const float first = sampleWave(tables(), voice.waveform, voice.phases[0]);
    const float second = sampleWave(tables(), voice.waveform, voice.phases[1]);
    voice.phases[0] += voice.tuneRatio / currentSampleRate;
    voice.phases[1] += voice.tuneRatio * voice.fmRatio / currentSampleRate;
    const float body = (first + second * 0.8f) * 0.55f * voice.envelope * voice.component1;
    const float noise = voice.filters[0].process(nextNoise(voice)) * voice.envelope2 * voice.component2 * (0.8f + 0.2f * voice.accent);
    voice.envelope *= voice.decayMultiplier;
    voice.envelope2 *= voice.decayMultiplier2;
    return (body + noise) * voice.velocityGain * voice.gateGain;
}

float DrumSynthMachine::renderMetalBank(Voice& voice, const std::array<float, 6>& baseFrequencies) noexcept
{
    float output = 0.0f;
    std::array<float, 6> oscillators {};
    for (std::size_t index = 0; index < baseFrequencies.size(); ++index)
    {
        const float spreadScale = 1.0f + (static_cast<float>(index) - 2.5f) * 0.035f * voice.spread;
        const float hz = baseFrequencies[index] * voice.tuneRatio * spreadScale;
        oscillators[index] = sampleSquare(tables(), voice.phases[index], hz);
        output += oscillators[index];
        voice.phases[index] += hz / currentSampleRate;
    }
    const float plain = output / 6.0f;
    const float ring = (oscillators[0] * oscillators[3]
        + oscillators[1] * oscillators[4]
        + oscillators[2] * oscillators[5]) / 3.0f;
    return plain + (ring - plain) * voice.ring;
}

float DrumSynthMachine::renderHat(Voice& voice) noexcept
{
    float metal = renderMetalBank(voice, kMetalFrequencies);
    metal = voice.filters[1].process(voice.filters[0].process(metal));
    metal = metal * (1.0f - voice.component1) + softSaturate(metal * 1.8f) * voice.component1;
    voice.envelope *= voice.decayMultiplier;
    return metal * voice.envelope * voice.velocityGain * voice.gateGain;
}

float DrumSynthMachine::renderCrash(Voice& voice) noexcept
{
    const float metal = renderMetalBank(voice, kMetalFrequencies) * voice.component1;
    const float noise = nextNoise(voice) * voice.component2 * (0.8f + 0.2f * voice.accent);
    const float input = metal + noise;
    float bands = 0.0f;
    for (int index = 0; index < 3; ++index) bands += voice.filters[static_cast<std::size_t>(index)].process(input);
    const float attack = voice.attackSamples <= 0 ? 1.0f : std::min(1.0f, static_cast<float>(voice.age) / static_cast<float>(voice.attackSamples));
    voice.envelope *= voice.decayMultiplier;
    return voice.filters[3].process(bands * 0.4f) * attack * voice.envelope * voice.velocityGain * voice.gateGain;
}

float DrumSynthMachine::renderRide(Voice& voice) noexcept
{
    constexpr float bellBase = 520.0f;
    const float modulator = sampleWave(tables(), 0, voice.phases[7]);
    const double carrierPhase = voice.phases[6] + static_cast<double>(modulator * voice.fmIndex / juce::MathConstants<float>::twoPi);
    const float bell = sampleWave(tables(), 0, carrierPhase) * voice.component1;
    const float frequency = bellBase * voice.tuneRatio;
    voice.phases[6] += frequency / currentSampleRate;
    voice.phases[7] += frequency * voice.fmRatio / currentSampleRate;
    const float wash = voice.filters[1].process(voice.filters[0].process(
        renderMetalBank(voice, kMetalFrequencies) + nextNoise(voice) * 0.25f)) * voice.component2;
    voice.envelope *= voice.decayMultiplier;
    return (bell + wash) * voice.envelope * voice.velocityGain * voice.gateGain;
}

float DrumSynthMachine::renderTom(Voice& voice) noexcept
{
    const float progress = std::min(1.0f, static_cast<float>(voice.age) / static_cast<float>(voice.pitchDownSamples));
    const float hz = voice.tuneRatio * pitchRatio(tables(), voice.pitchAmount * (1.0f - progress));
    const float body = sampleWave(tables(), voice.waveform, voice.phases[0]) * voice.envelope * voice.component1;
    voice.phases[0] += hz / currentSampleRate;
    const float clickEnvelope = std::max(0.0f, 1.0f - static_cast<float>(voice.age) / static_cast<float>(std::max(1.0, currentSampleRate * 0.004)));
    const float click = nextNoise(voice) * clickEnvelope * voice.component2 * (0.8f + 0.2f * voice.accent);
    voice.envelope *= voice.decayMultiplier;
    return (body + click) * voice.velocityGain * voice.gateGain;
}

void DrumSynthMachine::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ignoreUnused(midi);
    if (buffer.getNumChannels() <= 0 || buffer.getNumSamples() <= 0) return;
    const std::array<bool, 6> enabled {{
        kick.enabled.load(std::memory_order_relaxed) != 0,
        snare.enabled.load(std::memory_order_relaxed) != 0,
        hat.enabled.load(std::memory_order_relaxed) != 0,
        crash.enabled.load(std::memory_order_relaxed) != 0,
        ride.enabled.load(std::memory_order_relaxed) != 0,
        tom.enabled.load(std::memory_order_relaxed) != 0
    }};
    // Disabling a sound is a hard DSP bypass: discard its transient state at
    // the next callback and do not spend samples finishing its tail.
    for (std::size_t index = 0; index < voices.size(); ++index)
        if (!enabled[index]) voices[index].active = false;
    if (!enabled[2]) hatChokeTail.active = false;

    const int eventCount = scheduledBlockNotes != nullptr ? static_cast<int>(scheduledBlockNotes->size()) : 0;
    const bool hasActiveVoice = hatChokeTail.active
        || std::any_of(voices.begin(), voices.end(), [](const Voice& voice) { return voice.active; });
    bool hasEnabledTrigger = false;
    if (!hasActiveVoice && scheduledBlockNotes != nullptr)
        for (const auto& event : *scheduledBlockNotes)
            if (!event.isNoteOff && isDrumEnabled(drumForNote(event.note)))
            {
                hasEnabledTrigger = true;
                break;
            }
    if (!hasActiveVoice && !hasEnabledTrigger)
    {
        sampleCounter += static_cast<std::uint64_t>(buffer.getNumSamples());
        return;
    }

    const std::array<float, 6> targetOutputLevels {{
        kick.level.load(std::memory_order_relaxed) * 0.01f,
        snare.level.load(std::memory_order_relaxed) * 0.01f,
        hat.level.load(std::memory_order_relaxed) * 0.01f,
        crash.level.load(std::memory_order_relaxed) * 0.01f,
        ride.level.load(std::memory_order_relaxed) * 0.01f,
        tom.level.load(std::memory_order_relaxed) * 0.01f
    }};
    int nextEvent = 0;
    for (int sample = 0; sample < buffer.getNumSamples(); ++sample, ++sampleCounter)
    {
        while (nextEvent < eventCount && (*scheduledBlockNotes)[static_cast<std::size_t>(nextEvent)].sampleOffset <= sample)
        {
            const auto& event = (*scheduledBlockNotes)[static_cast<std::size_t>(nextEvent++)];
            const auto drum = drumForNote(event.note);
            if (event.isNoteOff) release(drum, event.note); else trigger(drum, event);
        }

        for (std::size_t voiceIndex = 0; voiceIndex < outputLevels.size(); ++voiceIndex)
            if (enabled[voiceIndex])
                outputLevels[voiceIndex] = smoothedOutputLevel(outputLevels[voiceIndex], targetOutputLevels[voiceIndex]);

        float output = 0.0f;
        if (voices[0].active) { output += renderKick(voices[0]) * outputLevels[0]; updateVoiceEnvelope(voices[0]); }
        if (voices[1].active) { output += renderSnare(voices[1]) * outputLevels[1]; updateVoiceEnvelope(voices[1]); }
        if (voices[2].active) { output += renderHat(voices[2]) * outputLevels[2]; updateVoiceEnvelope(voices[2]); }
        if (voices[3].active) { output += renderCrash(voices[3]) * outputLevels[3]; updateVoiceEnvelope(voices[3]); }
        if (voices[4].active) { output += renderRide(voices[4]) * outputLevels[4]; updateVoiceEnvelope(voices[4]); }
        if (voices[5].active) { output += renderTom(voices[5]) * outputLevels[5]; updateVoiceEnvelope(voices[5]); }
        if (hatChokeTail.active)
        {
            output += renderHat(hatChokeTail) * outputLevels[2];
            updateVoiceEnvelope(hatChokeTail);
        }
        buffer.addSample(0, sample, output * 0.42f);
    }
}

void DrumSynthMachine::resetParametersToDefaults()
{
    kick.enabled.store(1); kick.triggerNote.store(72); kick.wave.store(0); kick.tune.store(36); kick.pitchAmount.store(24); kick.pitchUpMs.store(0);
    kick.pitchDownMs.store(60); kick.decayMs.store(1200); kick.noiseType.store(0);
    kick.click.store(45.0f); kick.body.store(100.0f); kick.level.store(95.0f); kick.oneShot.store(1);
    snare.enabled.store(1); snare.triggerNote.store(73); snare.type.store(1); snare.tune.store(0); snare.bodyDecayMs.store(300); snare.noiseDecayMs.store(220);
    snare.body.store(65.0f); snare.snap.store(70.0f); snare.tone.store(60.0f); snare.level.store(80.0f); snare.oneShot.store(1);
    hat.enabled.store(1); hat.triggerNote.store(74); hat.tune.store(0); hat.closedDecayMs.store(70); hat.openDecayMs.store(700);
    hat.spread.store(50.0f); hat.highPassHz.store(5000.0f); hat.bandPassHz.store(7000.0f);
    hat.resonance.store(30.0f); hat.grit.store(20.0f); hat.level.store(70.0f); hat.oneShot.store(1);
    crash.enabled.store(1); crash.triggerNote.store(75); crash.tune.store(0); crash.attackMs.store(2); crash.decayMs.store(1800);
    crash.spread.store(50.0f); crash.metal.store(75.0f); crash.noise.store(35.0f);
    crash.ring.store(20.0f); crash.tone.store(55.0f); crash.resonance.store(45.0f); crash.level.store(65.0f); crash.oneShot.store(1);
    ride.enabled.store(1); ride.triggerNote.store(76); ride.tune.store(0); ride.decayMs.store(2500); ride.ratio.store(3.0f); ride.fm.store(2.5f);
    ride.bell.store(70.0f); ride.wash.store(35.0f); ride.ring.store(15.0f);
    ride.tone.store(65.0f); ride.level.store(65.0f); ride.oneShot.store(1);
    tom.enabled.store(1); tom.triggerNote.store(77); tom.wave.store(0); tom.tune.store(48); tom.pitchAmount.store(12); tom.pitchDownMs.store(100);
    tom.decayMs.store(500); tom.click.store(15.0f); tom.body.store(100.0f); tom.level.store(75.0f); tom.oneShot.store(1);
}

std::vector<std::vector<UIBox>> DrumSynthMachine::getUIBoxes(const MachineUiContext& context)
{
    juce::ignoreUnused(context);
    constexpr int rows = 13;
    std::vector<std::vector<UIBox>> boxes(12, std::vector<UIBox>(rows));
    const char* headings[] = { "KICK", "SNAR", "HAT", "CRSH", "RIDE", "TOM" };
    for (int strip = 0; strip < 6; ++strip)
    {
        const int labelColumn = strip * 2;
        boxes[static_cast<std::size_t>(labelColumn)][0] = makeLabelCell(headings[strip]);
        boxes[static_cast<std::size_t>(labelColumn)][0].isDisabled = true;
        boxes[static_cast<std::size_t>(labelColumn + 1)][0] = makeLabelCell("ON");
        boxes[static_cast<std::size_t>(labelColumn + 1)][0].isDisabled = true;
        for (int row = 1; row < rows; ++row)
        {
            boxes[static_cast<std::size_t>(labelColumn)][static_cast<std::size_t>(row)].isDisabled = true;
            boxes[static_cast<std::size_t>(labelColumn + 1)][static_cast<std::size_t>(row)].isDisabled = true;
        }
    }

    auto integer = [&boxes](int column, int row, const char* label, std::atomic<int>& target, int minimum, int maximum, int step = 1)
    {
        boxes[static_cast<std::size_t>(column)][static_cast<std::size_t>(row)] = makeLabelCell(label);
        boxes[static_cast<std::size_t>(column)][static_cast<std::size_t>(row)].isDisabled = true;
        auto& value = boxes[static_cast<std::size_t>(column + 1)][static_cast<std::size_t>(row)];
        value = makeFloatCell(static_cast<float>(target.load()), static_cast<float>(minimum), static_cast<float>(maximum), 0,
            [&target, minimum, maximum, step](int direction) { target.store(juce::jlimit(minimum, maximum, target.load() + direction * step)); });
        value.onInsert = [&target, minimum, maximum](double inserted) { target.store(juce::jlimit(minimum, maximum, static_cast<int>(std::lround(inserted)))); };
    };
    auto floating = [&boxes](int column, int row, const char* label, std::atomic<float>& target, float minimum, float maximum, float step, int decimals = 0)
    {
        boxes[static_cast<std::size_t>(column)][static_cast<std::size_t>(row)] = makeLabelCell(label);
        boxes[static_cast<std::size_t>(column)][static_cast<std::size_t>(row)].isDisabled = true;
        auto& value = boxes[static_cast<std::size_t>(column + 1)][static_cast<std::size_t>(row)];
        value = makeFloatCell(target.load(), minimum, maximum, decimals,
            [&target, minimum, maximum, step](int direction) { target.store(juce::jlimit(minimum, maximum, target.load() + direction * step)); });
        value.onInsert = [&target, minimum, maximum](double inserted) { target.store(juce::jlimit(minimum, maximum, static_cast<float>(inserted))); };
    };
    auto choice = [&boxes](int column, int row, const char* label, std::atomic<int>& target, const char* const* names, int count)
    {
        boxes[static_cast<std::size_t>(column)][static_cast<std::size_t>(row)] = makeLabelCell(label);
        boxes[static_cast<std::size_t>(column)][static_cast<std::size_t>(row)].isDisabled = true;
        auto& value = boxes[static_cast<std::size_t>(column + 1)][static_cast<std::size_t>(row)];
        value.kind = UIBox::Kind::TrackerCell;
        value.isDisabled = false;
        value.text = names[juce::jlimit(0, count - 1, target.load())];
        value.onAdjust = [&target, count](int direction) { int next = target.load() + (direction < 0 ? -1 : 1); if (next < 0) next = count - 1; if (next >= count) next = 0; target.store(next); };
        value.onInsert = [&target, count](double inserted) { target.store(juce::jlimit(0, count - 1, static_cast<int>(std::lround(inserted)))); };
    };
    auto enabledToggle = [&boxes](int column, std::atomic<int>& target)
    {
        auto& value = boxes[static_cast<std::size_t>(column + 1)][0];
        value.kind = UIBox::Kind::TrackerCell;
        value.isDisabled = false;
        value.text = kEnabledNames[juce::jlimit(0, 1, target.load(std::memory_order_relaxed))];
        value.onAdjust = [&target](int) { target.store(target.load(std::memory_order_relaxed) == 0 ? 1 : 0, std::memory_order_relaxed); };
        value.onInsert = [&target](double inserted) { target.store(inserted >= 0.5 ? 1 : 0, std::memory_order_relaxed); };
    };

    enabledToggle(0, kick.enabled); enabledToggle(2, snare.enabled); enabledToggle(4, hat.enabled);
    enabledToggle(6, crash.enabled); enabledToggle(8, ride.enabled); enabledToggle(10, tom.enabled);

    integer(0, 1, "NOTE", kick.triggerNote, 0, 127); choice(0, 2, "WAVE", kick.wave, kWaveNames, kWaveCount); integer(0, 3, "TUNE", kick.tune, 12, 96);
    integer(0, 4, "PENV", kick.pitchAmount, 0, 96); integer(0, 5, "UP", kick.pitchUpMs, 0, 100);
    integer(0, 6, "DOWN", kick.pitchDownMs, 5, 2000, 5); integer(0, 7, "DEC", kick.decayMs, 20, 8000, 100);
    choice(0, 8, "NTYP", kick.noiseType, kNoiseNames, 3); floating(0, 9, "CLICK", kick.click, 0, 100, 5);
    floating(0, 10, "BODY", kick.body, 0, 100, 5); floating(0, 11, "LEV", kick.level, 0, 100, 5);
    choice(0, 12, "MODE", kick.oneShot, kTriggerModeNames, 2);

    integer(2, 1, "NOTE", snare.triggerNote, 0, 127); choice(2, 2, "TYPE", snare.type, kSnareNames, 3); integer(2, 3, "TUNE", snare.tune, -48, 48);
    floating(2, 4, "BODY", snare.body, 0, 100, 5); floating(2, 5, "SNAP", snare.snap, 0, 100, 5);
    integer(2, 6, "BDEC", snare.bodyDecayMs, 20, 4000, 10); integer(2, 7, "NDEC", snare.noiseDecayMs, 10, 4000, 10);
    floating(2, 8, "TONE", snare.tone, 0, 100, 5); floating(2, 9, "LEV", snare.level, 0, 100, 5);
    choice(2, 12, "MODE", snare.oneShot, kTriggerModeNames, 2);

    integer(4, 1, "NOTE", hat.triggerNote, 0, 127); integer(4, 2, "TUNE", hat.tune, -48, 48); floating(4, 3, "SPRD", hat.spread, 0, 100, 5);
    floating(4, 4, "HPF", hat.highPassHz, 100, 18000, 250); floating(4, 5, "BPF", hat.bandPassHz, 100, 18000, 250);
    floating(4, 6, "RES", hat.resonance, 0, 100, 5); integer(4, 7, "CDEC", hat.closedDecayMs, 5, 500, 5);
    integer(4, 8, "ODEC", hat.openDecayMs, 50, 8000, 10); floating(4, 9, "GRIT", hat.grit, 0, 100, 5);
    floating(4, 10, "LEV", hat.level, 0, 100, 5);
    choice(4, 12, "MODE", hat.oneShot, kTriggerModeNames, 2);

    integer(6, 1, "NOTE", crash.triggerNote, 0, 127); integer(6, 2, "TUNE", crash.tune, -48, 48); floating(6, 3, "SPRD", crash.spread, 0, 100, 5);
    floating(6, 4, "METL", crash.metal, 0, 100, 5); floating(6, 5, "NOIS", crash.noise, 0, 100, 5);
    floating(6, 6, "RING", crash.ring, 0, 100, 5); floating(6, 7, "TONE", crash.tone, 0, 100, 5);
    floating(6, 8, "RES", crash.resonance, 0, 100, 5); integer(6, 9, "ATK", crash.attackMs, 0, 500);
    integer(6, 10, "DEC", crash.decayMs, 100, 12000, 10); floating(6, 11, "LEV", crash.level, 0, 100, 5);
    choice(6, 12, "MODE", crash.oneShot, kTriggerModeNames, 2);

    integer(8, 1, "NOTE", ride.triggerNote, 0, 127); integer(8, 2, "TUNE", ride.tune, -48, 48); floating(8, 3, "RATIO", ride.ratio, 0.25f, 16.0f, 0.25f, 2);
    floating(8, 4, "FM", ride.fm, 0, 20, 0.1f, 1); floating(8, 5, "BELL", ride.bell, 0, 100, 5);
    floating(8, 6, "WASH", ride.wash, 0, 100, 5); floating(8, 7, "RING", ride.ring, 0, 100, 5);
    floating(8, 8, "TONE", ride.tone, 0, 100, 5); integer(8, 9, "DEC", ride.decayMs, 100, 12000, 10);
    floating(8, 10, "LEV", ride.level, 0, 100, 5);
    choice(8, 12, "MODE", ride.oneShot, kTriggerModeNames, 2);

    integer(10, 1, "NOTE", tom.triggerNote, 0, 127); choice(10, 2, "WAVE", tom.wave, kWaveNames, kWaveCount); integer(10, 3, "TUNE", tom.tune, 24, 108);
    integer(10, 4, "PENV", tom.pitchAmount, 0, 72); integer(10, 5, "DOWN", tom.pitchDownMs, 5, 2000, 5);
    integer(10, 6, "DEC", tom.decayMs, 20, 6000, 10); floating(10, 7, "CLICK", tom.click, 0, 100, 5);
    floating(10, 8, "BODY", tom.body, 0, 100, 5); floating(10, 9, "LEV", tom.level, 0, 100, 5);
    choice(10, 12, "MODE", tom.oneShot, kTriggerModeNames, 2);
    return boxes;
}

void DrumSynthMachine::getStateInformation(juce::MemoryBlock& destData)
{
    auto* object = new juce::DynamicObject(); object->setProperty("version", kStateVersion);
    const auto put = [object](const char* key, auto& value) { object->setProperty(key, value.load(std::memory_order_relaxed)); };
    put("kickEnabled", kick.enabled); put("kickNote", kick.triggerNote); put("kickWave", kick.wave); put("kickTune", kick.tune); put("kickPitchAmount", kick.pitchAmount); put("kickPitchUpMs", kick.pitchUpMs); put("kickPitchDownMs", kick.pitchDownMs); put("kickDecayMs", kick.decayMs); put("kickNoiseType", kick.noiseType); put("kickClick", kick.click); put("kickBody", kick.body); put("kickLevel", kick.level); put("kickOneShot", kick.oneShot);
    put("snareEnabled", snare.enabled); put("snareNote", snare.triggerNote); put("snareType", snare.type); put("snareTune", snare.tune); put("snareBody", snare.body); put("snareSnap", snare.snap); put("snareBodyDecayMs", snare.bodyDecayMs); put("snareNoiseDecayMs", snare.noiseDecayMs); put("snareTone", snare.tone); put("snareLevel", snare.level); put("snareOneShot", snare.oneShot);
    put("hatEnabled", hat.enabled); put("hatNote", hat.triggerNote); put("hatTune", hat.tune); put("hatSpread", hat.spread); put("hatHighPassHz", hat.highPassHz); put("hatBandPassHz", hat.bandPassHz); put("hatResonance", hat.resonance); put("hatClosedDecayMs", hat.closedDecayMs); put("hatOpenDecayMs", hat.openDecayMs); put("hatGrit", hat.grit); put("hatLevel", hat.level); put("hatOneShot", hat.oneShot);
    put("crashEnabled", crash.enabled); put("crashNote", crash.triggerNote); put("crashTune", crash.tune); put("crashSpread", crash.spread); put("crashMetal", crash.metal); put("crashNoise", crash.noise); put("crashRing", crash.ring); put("crashTone", crash.tone); put("crashResonance", crash.resonance); put("crashAttackMs", crash.attackMs); put("crashDecayMs", crash.decayMs); put("crashLevel", crash.level); put("crashOneShot", crash.oneShot);
    put("rideEnabled", ride.enabled); put("rideNote", ride.triggerNote); put("rideTune", ride.tune); put("rideRatio", ride.ratio); put("rideFm", ride.fm); put("rideBell", ride.bell); put("rideWash", ride.wash); put("rideRing", ride.ring); put("rideTone", ride.tone); put("rideDecayMs", ride.decayMs); put("rideLevel", ride.level); put("rideOneShot", ride.oneShot);
    put("tomEnabled", tom.enabled); put("tomNote", tom.triggerNote); put("tomWave", tom.wave); put("tomTune", tom.tune); put("tomPitchAmount", tom.pitchAmount); put("tomPitchDownMs", tom.pitchDownMs); put("tomDecayMs", tom.decayMs); put("tomClick", tom.click); put("tomBody", tom.body); put("tomLevel", tom.level); put("tomOneShot", tom.oneShot);
    writeMachineStateJson(destData, juce::var(object));
}

void DrumSynthMachine::setStateInformation(const void* data, int sizeInBytes)
{
    const auto state = parseMachineStateJson(data, sizeInBytes);
    if (state.isVoid()) return;
    resetParametersToDefaults();
    const auto getI = [&state](const char* key, std::atomic<int>& value, int minimum, int maximum) { value.store(getIntProperty(state, key, value.load(), minimum, maximum)); };
    const auto getF = [&state](const char* key, std::atomic<float>& value, float minimum, float maximum) { const float restored = getFloatProperty(state, key, value.load(), minimum, maximum); value.store(std::isfinite(restored) ? restored : juce::jlimit(minimum, maximum, value.load())); };
    getI("kickEnabled", kick.enabled, 0, 1); getI("kickNote", kick.triggerNote, 0, 127); getI("kickWave", kick.wave, 0, kWaveCount - 1); getI("kickTune", kick.tune, 12, 96); getI("kickPitchAmount", kick.pitchAmount, 0, 96); getI("kickPitchUpMs", kick.pitchUpMs, 0, 100); getI("kickPitchDownMs", kick.pitchDownMs, 5, 2000); getI("kickDecayMs", kick.decayMs, 20, 8000); getI("kickNoiseType", kick.noiseType, 0, 2); getF("kickClick", kick.click, 0, 100); getF("kickBody", kick.body, 0, 100); getF("kickLevel", kick.level, 0, 100); getI("kickOneShot", kick.oneShot, 0, 1);
    getI("snareEnabled", snare.enabled, 0, 1); getI("snareNote", snare.triggerNote, 0, 127); getI("snareType", snare.type, 0, 2); getI("snareTune", snare.tune, -48, 48); getF("snareBody", snare.body, 0, 100); getF("snareSnap", snare.snap, 0, 100); getI("snareBodyDecayMs", snare.bodyDecayMs, 20, 4000); getI("snareNoiseDecayMs", snare.noiseDecayMs, 10, 4000); getF("snareTone", snare.tone, 0, 100); getF("snareLevel", snare.level, 0, 100); getI("snareOneShot", snare.oneShot, 0, 1);
    getI("hatEnabled", hat.enabled, 0, 1); getI("hatNote", hat.triggerNote, 0, 127); getI("hatTune", hat.tune, -48, 48); getF("hatSpread", hat.spread, 0, 100); getF("hatHighPassHz", hat.highPassHz, 100, 18000); getF("hatBandPassHz", hat.bandPassHz, 100, 18000); getF("hatResonance", hat.resonance, 0, 100); getI("hatClosedDecayMs", hat.closedDecayMs, 5, 500); getI("hatOpenDecayMs", hat.openDecayMs, 50, 8000); getF("hatGrit", hat.grit, 0, 100); getF("hatLevel", hat.level, 0, 100); getI("hatOneShot", hat.oneShot, 0, 1);
    getI("crashEnabled", crash.enabled, 0, 1); getI("crashNote", crash.triggerNote, 0, 127); getI("crashTune", crash.tune, -48, 48); getF("crashSpread", crash.spread, 0, 100); getF("crashMetal", crash.metal, 0, 100); getF("crashNoise", crash.noise, 0, 100); getF("crashRing", crash.ring, 0, 100); getF("crashTone", crash.tone, 0, 100); getF("crashResonance", crash.resonance, 0, 100); getI("crashAttackMs", crash.attackMs, 0, 500); getI("crashDecayMs", crash.decayMs, 100, 12000); getF("crashLevel", crash.level, 0, 100); getI("crashOneShot", crash.oneShot, 0, 1);
    getI("rideEnabled", ride.enabled, 0, 1); getI("rideNote", ride.triggerNote, 0, 127); getI("rideTune", ride.tune, -48, 48); getF("rideRatio", ride.ratio, 0.25f, 16.0f); getF("rideFm", ride.fm, 0, 20); getF("rideBell", ride.bell, 0, 100); getF("rideWash", ride.wash, 0, 100); getF("rideRing", ride.ring, 0, 100); getF("rideTone", ride.tone, 0, 100); getI("rideDecayMs", ride.decayMs, 100, 12000); getF("rideLevel", ride.level, 0, 100); getI("rideOneShot", ride.oneShot, 0, 1);
    getI("tomEnabled", tom.enabled, 0, 1); getI("tomNote", tom.triggerNote, 0, 127); getI("tomWave", tom.wave, 0, kWaveCount - 1); getI("tomTune", tom.tune, 24, 108); getI("tomPitchAmount", tom.pitchAmount, 0, 72); getI("tomPitchDownMs", tom.pitchDownMs, 5, 2000); getI("tomDecayMs", tom.decayMs, 20, 6000); getF("tomClick", tom.click, 0, 100); getF("tomBody", tom.body, 0, 100); getF("tomLevel", tom.level, 0, 100); getI("tomOneShot", tom.oneShot, 0, 1);
}
