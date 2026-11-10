#include "DrumSynthMachine.h"

#include "MachineStateCodec.h"
#include "MachineUi.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
constexpr int kStateVersion = 4;
// These are compact, static approximations of the two families' metal
// generators.  They are intentionally six oscillators so cymbal quality does
// not come at the cost of a larger per-sample oscillator bank.
constexpr std::array<float, 6> kLegacyMetalFrequencies {{ 245.0f, 306.0f, 365.0f, 415.0f, 437.0f, 619.0f }};
constexpr std::array<float, 6> k808MetalFrequencies {{ 205.3f, 304.4f, 369.6f, 522.7f, 540.0f, 800.0f }};
constexpr std::array<float, 6> k909MetalFrequencies {{ 191.0f, 287.0f, 421.0f, 541.0f, 683.0f, 917.0f }};
constexpr float kMinimumEnvelope = 0.00001f;

const char* const kTriggerModeNames[] = { "GATE", "1SHOT" };
const char* const kEnabledNames[] = { "OFF", "ON" };
const char* const kPresetNames[] = { "CUST", "808", "909" };
const char* const kWaveNames[] = { "SIN", "TRI", "SAW", "SQR", "ADD1", "ADD2", "ADD10", "EVE5", "ODD5" };

float characterAmount(int value) noexcept
{
    return static_cast<float>(juce::jlimit(-10, 10, value)) * 0.1f;
}

float characterValue(float base, float negative, float positive, float character) noexcept
{
    if (character < 0.0f)
        return base + (negative - base) * -character;
    return base + (positive - base) * character;
}

float characterExponential(float base, float negative, float positive, float character) noexcept
{
    if (std::abs(character) < 0.000001f)
        return base;
    const float target = character < 0.0f ? negative : positive;
    const float amount = std::abs(character);
    return base * std::pow(std::max(0.001f, target) / std::max(0.001f, base), amount);
}

int exponentialStep(int current, int minimum, int maximum, int direction) noexcept
{
    if (direction == 0)
        return current;
    constexpr float kRootTwo = 1.41421356237f;
    const float scaled = direction > 0 ? static_cast<float>(current) * kRootTwo
                                       : static_cast<float>(current) / kRootTwo;
    int next = static_cast<int>(std::lround(scaled));
    if (next == current)
        next += direction > 0 ? 1 : -1;
    return juce::jlimit(minimum, maximum, next);
}

const std::array<float, 6>& metalFrequenciesForModel(int model) noexcept
{
    if (model == 1) return k808MetalFrequencies;
    if (model == 2) return k909MetalFrequencies;
    return kLegacyMetalFrequencies;
}

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

DrumSynthMachine::KitPreset DrumSynthMachine::getKitPreset() const noexcept
{
    return static_cast<KitPreset>(juce::jlimit(0, 2, kitPreset.load(std::memory_order_relaxed)));
}

void DrumSynthMachine::markPresetCustom() noexcept
{
    kitPreset.store(static_cast<int>(KitPreset::custom), std::memory_order_relaxed);
}

void DrumSynthMachine::applyKitPreset(KitPreset preset)
{
    if (preset != KitPreset::tr808 && preset != KitPreset::tr909)
    {
        markPresetCustom();
        return;
    }

    const bool is909 = preset == KitPreset::tr909;
    snare.type.store(is909 ? 2 : 1); snare.tune.store(0);
    snare.body.store(is909 ? 60.0f : 56.0f); snare.snap.store(is909 ? 100.0f : 96.0f);
    snare.bodyDecayMs.store(is909 ? 260 : 620); snare.noiseDecayMs.store(is909 ? 520 : 760);
    snare.tone.store(is909 ? 72.0f : 62.0f); snare.level.store(is909 ? 86.0f : 80.0f);
    snare.ring.store(0);

    hatModel.store(is909 ? 2 : 1); hat.tune.store(0); hat.spread.store(is909 ? 34.0f : 55.0f);
    hat.highPassHz.store(is909 ? 3800.0f : 5300.0f); hat.bandPassHz.store(is909 ? 7400.0f : 8000.0f);
    hat.resonance.store(is909 ? 36.0f : 24.0f); hat.closedDecayMs.store(is909 ? 280 : 500);
    hat.openDecayMs.store(is909 ? 900 : 4300); hat.grit.store(is909 ? 34.0f : 15.0f);
    hat.level.store(is909 ? 72.0f : 70.0f);
    hat.metal.store(0);

    crashModel.store(is909 ? 2 : 1); crash.tune.store(0); crash.spread.store(is909 ? 38.0f : 56.0f);
    crash.metal.store(is909 ? 72.0f : 82.0f); crash.noise.store(is909 ? 42.0f : 24.0f);
    crash.ring.store(is909 ? 28.0f : 22.0f); crash.tone.store(is909 ? 42.0f : 48.0f);
    crash.resonance.store(is909 ? 30.0f : 36.0f); crash.attackMs.store(0);
    crash.decayMs.store(is909 ? 11000 : 8200); crash.level.store(is909 ? 67.0f : 65.0f);
    crash.edge.store(0);

    rideModel.store(is909 ? 2 : 1); ride.tune.store(0); ride.ratio.store(is909 ? 3.8f : 2.7f);
    ride.fm.store(is909 ? 3.2f : 2.0f); ride.bell.store(is909 ? 58.0f : 66.0f);
    ride.wash.store(is909 ? 56.0f : 42.0f); ride.ring.store(is909 ? 24.0f : 18.0f);
    ride.tone.store(is909 ? 46.0f : 64.0f); ride.decayMs.store(is909 ? 2600 : 1500);
    ride.level.store(is909 ? 67.0f : 65.0f);
    ride.bellCharacter.store(0);

    clapModel.store(is909 ? 2 : 1); clap.tone.store(is909 ? 62.0f : 92.0f);
    clap.decayMs.store(is909 ? 260 : 300); clap.density.store(is909 ? 65.0f : 45.0f);
    clap.level.store(is909 ? 78.0f : 76.0f); clap.room.store(0);
    kitPreset.store(static_cast<int>(preset), std::memory_order_relaxed);
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
    if (midiNote == clap.triggerNote.load(std::memory_order_relaxed)) return Drum::clap;
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
        case Drum::clap: return "CLAP";
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
        case Drum::clap: return clap.enabled.load(std::memory_order_relaxed) != 0;
        case Drum::none: break;
    }
    return false;
}

std::array<std::uint64_t, 7> DrumSynthMachine::getTriggerCounts() const noexcept
{
    std::array<std::uint64_t, 7> result {};
    for (std::size_t index = 0; index < result.size(); ++index)
        result[index] = triggerCounts[index].load(std::memory_order_relaxed);
    return result;
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
        tom.level.load(std::memory_order_relaxed) * 0.01f,
        clap.level.load(std::memory_order_relaxed) * 0.01f
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
        : voices[static_cast<std::size_t>(drum == Drum::kick ? 0 : drum == Drum::crash ? 3 : drum == Drum::snare ? 1 : drum == Drum::ride ? 4 : drum == Drum::tom ? 5 : 6)].active;
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
        case Drum::clap: voice = &voices[6]; break;
        case Drum::none: return;
    }

    if (drum == Drum::closedHat && voice->active && voice->openHat)
    {
        hatChokeTail = *voice;
        beginRelease(hatChokeTail, 2);
    }
    if (!wasActive && (drum == Drum::kick || drum == Drum::tom))
        voice->phases.fill(0.0);
    if (!wasActive && drum != Drum::kick && drum != Drum::tom && drum != Drum::clap)
    {
        const int metalModel = (drum == Drum::closedHat || drum == Drum::openHat) ? hatModel.load(std::memory_order_relaxed)
            : drum == Drum::crash ? crashModel.load(std::memory_order_relaxed)
            : drum == Drum::ride ? rideModel.load(std::memory_order_relaxed) : 0;
        configureMetalPhases(*voice, metalFrequenciesForModel(metalModel));
    }
    if (!wasActive)
        for (auto& filter : voice->filters)
            filter.reset();

    const bool oneShot = drum == Drum::kick ? kick.oneShot.load(std::memory_order_relaxed) != 0
        : drum == Drum::snare ? snare.oneShot.load(std::memory_order_relaxed) != 0
        : (drum == Drum::closedHat || drum == Drum::openHat) ? hat.oneShot.load(std::memory_order_relaxed) != 0
        : drum == Drum::crash ? crash.oneShot.load(std::memory_order_relaxed) != 0
        : drum == Drum::ride ? ride.oneShot.load(std::memory_order_relaxed) != 0
        : drum == Drum::tom ? tom.oneShot.load(std::memory_order_relaxed) != 0
        : clap.oneShot.load(std::memory_order_relaxed) != 0;
    configureGate(*voice, event, oneShot);
    voice->envelope = wasActive ? voice->envelope + (1.0f - voice->envelope) * 0.85f : 1.0f;
    voice->envelope2 = wasActive ? voice->envelope2 + (1.0f - voice->envelope2) * 0.85f : 1.0f;
    voice->envelope3 = wasActive ? voice->envelope3 + (1.0f - voice->envelope3) * 0.85f : 1.0f;
    voice->bodyEnvelope2 = wasActive ? voice->bodyEnvelope2 + (1.0f - voice->bodyEnvelope2) * 0.85f : 1.0f;

    switch (drum)
    {
        case Drum::kick:
        {
            const float punch = characterAmount(kick.punch.load(std::memory_order_relaxed));
            voice->envelope2 = 0.0f;
            voice->waveform = kick.wave.load(std::memory_order_relaxed);
            voice->tuneRatio = source.midiHz[static_cast<std::size_t>(juce::jlimit(0, 127, kick.tune.load()))];
            voice->pitchAmount = characterValue(static_cast<float>(kick.pitchAmount.load()), 0.0f, 96.0f, punch);
            voice->pitchUpSamples = static_cast<int>(currentSampleRate * kick.pitchUpMs.load() * 0.001);
            const float pitchDownMs = characterValue(static_cast<float>(kick.pitchDownMs.load()), 2000.0f, 5.0f, punch);
            voice->pitchDownSamples = juce::jmax(1, static_cast<int>(currentSampleRate * pitchDownMs * 0.001f));
            const float kickDecayMs = characterExponential(static_cast<float>(kick.decayMs.load()), 80.0f, 8000.0f, punch);
            voice->decayMultiplier = decayMultiplierForMs(kickDecayMs);
            voice->noiseType = kick.noiseType.load();
            voice->component1 = kick.body.load() * 0.01f;
            voice->component2 = characterValue(kick.click.load() * 0.01f, 0.0f, 1.0f, punch);
            voice->tone = characterValue(kick.tone.load(std::memory_order_relaxed) * 0.01f,
                                         0.05f, 1.0f, punch);
            if (voice->tone < 0.999f)
            {
                const float cutoffHz = 250.0f * std::pow(72.0f, voice->tone);
                voice->filters[0].setLowPass(cutoffHz, 0.707f, currentSampleRate, source);
            }
            break;
        }
        case Drum::snare:
        {
            const int profile = snare.type.load();
            const float ring = characterAmount(snare.ring.load(std::memory_order_relaxed));
            const float bodyDecayMs = static_cast<float>(snare.bodyDecayMs.load());
            voice->ring = 0.0f;
            const float base = profile == 0 ? 170.0f : profile == 1 ? 180.0f : 185.0f;
            voice->profile = profile;
            voice->tuneRatio = base * pitchRatio(source, static_cast<float>(snare.tune.load()));
            voice->fmRatio = profile == 0 ? 1.72f : profile == 1 ? 1.83f : 1.89f;
            voice->waveform = profile == 2 ? 1 : 0;
            voice->decayMultiplier = decayMultiplierForMs(bodyDecayMs);
            const float noiseDecayMs = characterExponential(static_cast<float>(snare.noiseDecayMs.load()), 15.0f, 6000.0f, ring);
            voice->decayMultiplier2 = decayMultiplierForMs(noiseDecayMs);
            voice->bodyDecayMultiplier2 = decayMultiplierForMs(bodyDecayMs * 0.78f);
            voice->decayMultiplier3 = decayMultiplierForMs(noiseDecayMs * (profile == 2 ? 0.75f : 0.55f));
            voice->pitchAmount = profile == 2 ? 7.0f : 0.0f;
            voice->pitchDownSamples = juce::jmax(1, static_cast<int>(currentSampleRate * 0.020));
            voice->noiseHoldSamples = profile == 2 ? static_cast<int>(currentSampleRate * 0.024f) : 0;
            voice->component1 = snare.body.load() * 0.01f;
            voice->component2 = snare.snap.load() * 0.01f;
            voice->tone = snare.tone.load() * 0.01f;
            if (profile == 2)
            {
                // The hardware combines separate low- and high-frequency
                // snappy paths rather than one bright band-pass signal.
                voice->filters[0].setLowPass(3400.0f + 2600.0f * voice->tone, 0.72f, currentSampleRate, source);
                voice->filters[1].setHighPass(2400.0f + 3800.0f * voice->tone, 0.72f, currentSampleRate, source);
                // A 909 trigger establishes the starting phase of both VCOs.
                voice->phases[0] = 0.0;
                voice->phases[1] = 0.0;
            }
            else
            {
                voice->filters[0].setBandPass(1500.0f + 9000.0f * voice->tone * (0.9f + 0.1f * voice->accent), profile == 0 ? 0.8f : 1.0f, currentSampleRate, source);
            }
            break;
        }
        case Drum::closedHat:
        case Drum::openHat:
        {
            const float metal = characterAmount(hat.metal.load(std::memory_order_relaxed));
            voice->envelope2 = 0.0f;
            voice->profile = hatModel.load(std::memory_order_relaxed);
            voice->openHat = drum == Drum::openHat;
            voice->tuneRatio = pitchRatio(source, static_cast<float>(hat.tune.load()));
            // At the positive endpoint the deliberately exaggerated detune is
            // still just six existing oscillators, but prevents the 909 bank
            // from collapsing into a nearly static filtered square wave.
            voice->spread = characterValue(hat.spread.load() * 0.01f, 0.0f, 4.0f, metal);
            voice->resonance = characterValue(hat.resonance.load() * 0.01f, 0.0f, 1.0f, metal);
            voice->component1 = characterValue(hat.grit.load() * 0.01f, 0.0f, 1.0f, metal);
            voice->ring = juce::jmax(0.0f, metal) * 2.0f;
            voice->tone = std::abs(metal);
            voice->tuneRatio *= pitchRatio(source, metal * 48.0f);
            const float baseHatDecayMs = static_cast<float>(voice->openHat ? hat.openDecayMs.load() : hat.closedDecayMs.load());
            const float hatDecayMs = characterExponential(baseHatDecayMs, 15.0f,
                voice->openHat ? 8000.0f : 2000.0f, metal);
            voice->decayMultiplier = decayMultiplierForMs(hatDecayMs);
            const float bandPassHz = characterExponential(hat.bandPassHz.load(), 2500.0f, 9500.0f, metal);
            const float highPassHz = characterExponential(hat.highPassHz.load(), 1200.0f, 4000.0f, metal);
            voice->filters[0].setBandPass(bandPassHz * (0.88f + 0.12f * voice->accent),
                (voice->profile == 2 ? 1.1f : 0.7f) + 4.5f * voice->resonance, currentSampleRate, source);
            voice->filters[1].setHighPass(highPassHz, voice->profile == 2 ? 0.85f : 0.707f, currentSampleRate, source);
            break;
        }
        case Drum::crash:
        {
            const float edge = characterAmount(crash.edge.load(std::memory_order_relaxed));
            voice->envelope2 = 0.0f;
            voice->profile = crashModel.load(std::memory_order_relaxed);
            voice->tuneRatio = pitchRatio(source, static_cast<float>(crash.tune.load()));
            voice->spread = crash.spread.load() * 0.01f;
            voice->component1 = characterValue(crash.metal.load() * 0.01f, 0.20f, 1.0f, edge);
            voice->component2 = characterValue(crash.noise.load() * 0.01f, 1.0f, 0.0f, edge);
            voice->ring = characterValue(crash.ring.load() * 0.01f, 0.0f, 1.0f, edge);
            voice->tone = characterValue(crash.tone.load() * 0.01f, 0.0f, 1.0f, edge);
            voice->resonance = characterValue(crash.resonance.load() * 0.01f, 0.0f, 1.0f, edge);
            voice->attackSamples = static_cast<int>(currentSampleRate * crash.attackMs.load() * 0.001);
            const float crashDecayMs = characterExponential(static_cast<float>(crash.decayMs.load()), 100.0f, 12000.0f, edge);
            voice->decayMultiplier = decayMultiplierForMs(crashDecayMs);
            voice->decayMultiplier2 = decayMultiplierForMs(crashDecayMs * (voice->profile == 2 ? 0.34f : 0.48f));
            voice->envelope2 = 1.0f;
            for (int band = 0; band < 3; ++band)
                voice->filters[static_cast<std::size_t>(band)].setBandPass((voice->profile == 2 ? 950.0f : 1200.0f) + voice->tone * 2200.0f + band * 1500.0f, 0.8f + 5.5f * voice->resonance, currentSampleRate, source);
            voice->filters[3].setHighPass((voice->profile == 2 ? 550.0f : 700.0f) + voice->tone * 2200.0f, 0.707f, currentSampleRate, source);
            break;
        }
        case Drum::ride:
        {
            const float bellCharacter = characterAmount(ride.bellCharacter.load(std::memory_order_relaxed));
            voice->envelope2 = 0.0f;
            voice->profile = rideModel.load(std::memory_order_relaxed);
            voice->tuneRatio = pitchRatio(source, static_cast<float>(ride.tune.load()));
            voice->tuneRatio *= pitchRatio(source, bellCharacter * 24.0f);
            voice->fmRatio = characterValue(ride.ratio.load(), 0.5f, 8.0f, bellCharacter);
            voice->fmIndex = characterValue(ride.fm.load(), 0.0f, 20.0f, bellCharacter);
            voice->component1 = characterValue(ride.bell.load() * 0.01f, 0.0f, 1.0f, bellCharacter);
            voice->component2 = characterValue(ride.wash.load() * 0.01f, 1.0f, 0.0f, bellCharacter);
            voice->ring = characterValue(ride.ring.load() * 0.01f, 1.0f, 0.0f, bellCharacter);
            voice->tone = characterValue(ride.tone.load() * 0.01f, 0.05f, 1.0f, bellCharacter);
            const float rideDecayMs = characterExponential(static_cast<float>(ride.decayMs.load()), 300.0f, 12000.0f, bellCharacter);
            voice->decayMultiplier = decayMultiplierForMs(rideDecayMs);
            voice->decayMultiplier2 = decayMultiplierForMs(rideDecayMs * (voice->profile == 2 ? 1.2f : 0.8f));
            voice->envelope2 = 1.0f;
            voice->filters[0].setBandPass((voice->profile == 2 ? 1900.0f : 2500.0f) + voice->tone * 7000.0f, 1.0f + voice->tone * 5.0f, currentSampleRate, source);
            voice->filters[1].setHighPass((voice->profile == 2 ? 1300.0f : 1800.0f) + voice->tone * 2500.0f, 0.707f, currentSampleRate, source);
            break;
        }
        case Drum::tom:
        {
            const float bend = characterAmount(tom.bend.load(std::memory_order_relaxed));
            voice->envelope2 = 0.0f;
            voice->waveform = tom.wave.load();
            voice->tuneRatio = source.midiHz[static_cast<std::size_t>(juce::jlimit(0, 127, tom.tune.load()))];
            voice->tuneRatio *= pitchRatio(source, bend * 24.0f);
            voice->pitchAmount = characterValue(static_cast<float>(tom.pitchAmount.load()), 0.0f, 72.0f, bend);
            voice->pitchUpSamples = 0;
            const float pitchDownMs = characterValue(static_cast<float>(tom.pitchDownMs.load()), 2000.0f, 5.0f, bend);
            voice->pitchDownSamples = juce::jmax(1, static_cast<int>(currentSampleRate * pitchDownMs * 0.001f));
            const float tomDecayMs = characterExponential(static_cast<float>(tom.decayMs.load()), 40.0f, 6000.0f, bend);
            voice->decayMultiplier = decayMultiplierForMs(tomDecayMs);
            voice->component1 = tom.body.load() * 0.01f;
            voice->component2 = characterValue(tom.click.load() * 0.01f, 0.0f, 1.0f, bend);
            break;
        }
        case Drum::clap:
        {
            const float room = characterAmount(clap.room.load(std::memory_order_relaxed));
            const float density = clap.density.load(std::memory_order_relaxed) * 0.01f;
            voice->profile = clapModel.load(std::memory_order_relaxed);
            voice->noiseType = 0;
            const float basePeriodMs = voice->profile == 2 ? 15.0f : 13.0f;
            const float periodMs = basePeriodMs + (1.0f - density) * 8.0f - density * 6.0f;
            const float burstMs = 7.5f - density * 4.5f;
            voice->burstPeriodSamples = juce::jmax(1, static_cast<int>(std::lround(currentSampleRate * periodMs * 0.001f)));
            voice->burstLengthSamples = juce::jmax(1, static_cast<int>(std::lround(currentSampleRate * burstMs * 0.001f)));
            voice->burstCount = 4;
            voice->burstEndSamples = voice->burstPeriodSamples * (voice->burstCount - 1) + voice->burstLengthSamples;
            const float tone = clap.tone.load(std::memory_order_relaxed) * 0.01f;
            const float centreHz = 500.0f * std::pow(12.0f, tone);
            const float baseQ = voice->profile == 2 ? 1.35f : 1.0f;
            const float q = characterValue(baseQ, 0.55f, 7.5f, room);
            voice->filters[0].setBandPass(centreHz, q, currentSampleRate, source);
            voice->filters[1].setBandPass(centreHz * (voice->profile == 2 ? 0.9f : 0.78f), q * 0.8f, currentSampleRate, source);
            const float baseTailMs = static_cast<float>(clap.decayMs.load(std::memory_order_relaxed));
            voice->decayMultiplier = decayMultiplierForMs(characterExponential(baseTailMs, 30.0f, 1600.0f, room));
            voice->component1 = characterValue(voice->profile == 2 ? 0.78f : 0.88f, 1.0f, 0.18f, room);
            voice->component2 = characterValue(voice->profile == 2 ? 0.58f : 0.48f, 0.02f, 1.25f, room);
            voice->ring = juce::jmax(0.0f, room);
            voice->envelope = 1.0f;
            voice->envelope2 = 0.0f;
            break;
        }
        case Drum::none: break;
    }
    const std::size_t triggerIndex = drum == Drum::kick ? 0 : drum == Drum::snare ? 1
        : (drum == Drum::closedHat || drum == Drum::openHat) ? 2 : drum == Drum::crash ? 3
        : drum == Drum::ride ? 4 : drum == Drum::tom ? 5 : 6;
    triggerCounts[triggerIndex].fetch_add(1, std::memory_order_relaxed);
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
        case Drum::clap: voice = &voices[6]; break;
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
    else if (voice.age >= voice.burstEndSamples
             && voice.envelope < kMinimumEnvelope && voice.envelope2 < kMinimumEnvelope)
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
    float body = sampleWave(tables(), voice.waveform, voice.phases[0]) * voice.envelope * voice.component1;
    if (voice.tone < 0.999f)
        body = voice.filters[0].process(body);
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
        const float noise = (lowNoise * 0.34f * voice.envelope2
            + highNoise * 0.92f * voice.envelope3)
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
    const float output = body + noise;
    return (voice.ring > 0.0001f ? softSaturate(output) : output) * voice.velocityGain * voice.gateGain;
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
    float metal = renderMetalBank(voice, metalFrequenciesForModel(voice.profile));
    if (voice.profile == 2)
        metal += nextNoise(voice) * (0.025f + 0.075f * voice.component1) * (1.0f - voice.tone);
    metal = voice.filters[1].process(voice.filters[0].process(metal));
    metal = metal * (1.0f - voice.component1) + softSaturate(metal * 1.8f) * voice.component1;
    voice.envelope *= voice.decayMultiplier;
    if (voice.tone > 0.0001f)
        metal = softSaturate(metal);
    return metal * voice.envelope * voice.velocityGain * voice.gateGain;
}

float DrumSynthMachine::renderCrash(Voice& voice) noexcept
{
    const float metal = renderMetalBank(voice, metalFrequenciesForModel(voice.profile)) * voice.component1;
    const float noise = nextNoise(voice) * voice.component2 * (0.8f + 0.2f * voice.accent) * voice.envelope2;
    const float input = metal + noise;
    float bands = 0.0f;
    for (int index = 0; index < 3; ++index) bands += voice.filters[static_cast<std::size_t>(index)].process(input);
    const float attack = voice.attackSamples <= 0 ? 1.0f : std::min(1.0f, static_cast<float>(voice.age) / static_cast<float>(voice.attackSamples));
    voice.envelope *= voice.decayMultiplier;
    voice.envelope2 *= voice.decayMultiplier2;
    return voice.filters[3].process(bands * 0.4f) * attack * voice.envelope * voice.velocityGain * voice.gateGain;
}

float DrumSynthMachine::renderRide(Voice& voice) noexcept
{
    const float bellBase = voice.profile == 2 ? 2200.0f : 3600.0f;
    const float modulator = sampleWave(tables(), 0, voice.phases[7]);
    const double carrierPhase = voice.phases[6] + static_cast<double>(modulator * voice.fmIndex / juce::MathConstants<float>::twoPi);
    const float bell = sampleWave(tables(), 0, carrierPhase) * voice.component1;
    const float frequency = bellBase * voice.tuneRatio;
    voice.phases[6] += frequency / currentSampleRate;
    voice.phases[7] += frequency * voice.fmRatio / currentSampleRate;
    const float wash = voice.filters[1].process(voice.filters[0].process(
        renderMetalBank(voice, metalFrequenciesForModel(voice.profile)) + nextNoise(voice) * 0.25f)) * voice.component2 * voice.envelope2;
    voice.envelope *= voice.decayMultiplier;
    voice.envelope2 *= voice.decayMultiplier2;
    return (bell * voice.envelope + wash) * voice.velocityGain * voice.gateGain;
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

float DrumSynthMachine::renderClap(Voice& voice) noexcept
{
    const int burstIndex = voice.burstPeriodSamples > 0 ? voice.age / voice.burstPeriodSamples : 0;
    const int burstPhase = voice.burstPeriodSamples > 0 ? voice.age % voice.burstPeriodSamples : 0;
    const bool inBurst = burstIndex < voice.burstCount && burstPhase < voice.burstLengthSamples;
    const float burstShape = inBurst
        ? 1.0f - static_cast<float>(burstPhase) / static_cast<float>(juce::jmax(1, voice.burstLengthSamples))
        : 0.0f;
    const float dry = inBurst ? voice.filters[0].process(nextNoise(voice)) * burstShape * voice.component1 : 0.0f;
    float tail = 0.0f;
    if (voice.age >= voice.burstEndSamples)
    {
        tail = voice.filters[1].process(nextNoise(voice)) * voice.envelope * voice.component2;
        voice.envelope *= voice.decayMultiplier;
    }
    const float output = dry + tail;
    return (voice.ring > 0.0001f ? softSaturate(output * (1.0f + voice.ring)) : output)
        * voice.velocityGain * voice.gateGain;
}

void DrumSynthMachine::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ignoreUnused(midi);
    if (buffer.getNumChannels() <= 0 || buffer.getNumSamples() <= 0) return;
    const std::array<bool, 7> enabled {{
        kick.enabled.load(std::memory_order_relaxed) != 0,
        snare.enabled.load(std::memory_order_relaxed) != 0,
        hat.enabled.load(std::memory_order_relaxed) != 0,
        crash.enabled.load(std::memory_order_relaxed) != 0,
        ride.enabled.load(std::memory_order_relaxed) != 0,
        tom.enabled.load(std::memory_order_relaxed) != 0,
        clap.enabled.load(std::memory_order_relaxed) != 0
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

    const std::array<float, 7> targetOutputLevels {{
        kick.level.load(std::memory_order_relaxed) * 0.01f,
        snare.level.load(std::memory_order_relaxed) * 0.01f,
        hat.level.load(std::memory_order_relaxed) * 0.01f,
        crash.level.load(std::memory_order_relaxed) * 0.01f,
        ride.level.load(std::memory_order_relaxed) * 0.01f,
        tom.level.load(std::memory_order_relaxed) * 0.01f,
        clap.level.load(std::memory_order_relaxed) * 0.01f
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
        if (voices[6].active) { output += renderClap(voices[6]) * outputLevels[6]; updateVoiceEnvelope(voices[6]); }
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
    kick.click.store(45.0f); kick.body.store(100.0f); kick.tone.store(100.0f); kick.level.store(95.0f); kick.punch.store(0); kick.oneShot.store(1);
    snare.enabled.store(1); snare.triggerNote.store(73); snare.type.store(1); snare.tune.store(0); snare.bodyDecayMs.store(300); snare.noiseDecayMs.store(220);
    snare.body.store(65.0f); snare.snap.store(70.0f); snare.tone.store(60.0f); snare.level.store(80.0f); snare.ring.store(0); snare.oneShot.store(1);
    hat.enabled.store(1); hat.triggerNote.store(74); hat.tune.store(0); hat.closedDecayMs.store(70); hat.openDecayMs.store(700);
    hat.spread.store(50.0f); hat.highPassHz.store(5000.0f); hat.bandPassHz.store(7000.0f);
    hat.resonance.store(30.0f); hat.grit.store(20.0f); hat.level.store(70.0f); hat.metal.store(0); hat.oneShot.store(1);
    crash.enabled.store(1); crash.triggerNote.store(75); crash.tune.store(0); crash.attackMs.store(2); crash.decayMs.store(1800);
    crash.spread.store(50.0f); crash.metal.store(75.0f); crash.noise.store(35.0f);
    crash.ring.store(20.0f); crash.tone.store(55.0f); crash.resonance.store(45.0f); crash.level.store(65.0f); crash.edge.store(0); crash.oneShot.store(1);
    ride.enabled.store(1); ride.triggerNote.store(76); ride.tune.store(0); ride.decayMs.store(2500); ride.ratio.store(3.0f); ride.fm.store(2.5f);
    ride.bell.store(70.0f); ride.wash.store(35.0f); ride.ring.store(15.0f);
    ride.tone.store(65.0f); ride.level.store(65.0f); ride.bellCharacter.store(0); ride.oneShot.store(1);
    tom.enabled.store(1); tom.triggerNote.store(77); tom.wave.store(0); tom.tune.store(48); tom.pitchAmount.store(12); tom.pitchDownMs.store(100);
    tom.decayMs.store(500); tom.click.store(15.0f); tom.body.store(100.0f); tom.level.store(75.0f); tom.bend.store(0); tom.oneShot.store(1);
    clap.enabled.store(1); clap.triggerNote.store(78); clap.tone.store(55.0f); clap.decayMs.store(220);
    clap.density.store(50); clap.room.store(0); clap.level.store(75.0f); clap.oneShot.store(1);
    applyKitPreset(KitPreset::tr808);
}

std::vector<std::vector<UIBox>> DrumSynthMachine::getUIBoxes(const MachineUiContext& context)
{
    juce::ignoreUnused(context);
    constexpr int rows = 9;
    std::vector<std::vector<UIBox>> boxes(14, std::vector<UIBox>(rows));
    const char* headings[] = { "KICK", "SNAR", "HAT", "CRSH", "RIDE", "TOM", "CLAP" };
    for (int strip = 0; strip < 7; ++strip)
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

    auto integer = [&boxes](int column, int row, const char* label, std::atomic<int>& target, int minimum, int maximum, int step = 1, std::function<void()> onChanged = {})
    {
        boxes[static_cast<std::size_t>(column)][static_cast<std::size_t>(row)] = makeLabelCell(label);
        boxes[static_cast<std::size_t>(column)][static_cast<std::size_t>(row)].isDisabled = true;
        auto& value = boxes[static_cast<std::size_t>(column + 1)][static_cast<std::size_t>(row)];
        value = makeFloatCell(static_cast<float>(target.load()), static_cast<float>(minimum), static_cast<float>(maximum), 0,
            [&target, minimum, maximum, step, onChanged](int direction) { target.store(juce::jlimit(minimum, maximum, target.load() + direction * step)); if (onChanged) onChanged(); });
        value.onInsert = [&target, minimum, maximum, onChanged](double inserted) { target.store(juce::jlimit(minimum, maximum, static_cast<int>(std::lround(inserted)))); if (onChanged) onChanged(); };
    };
    auto exponential = [&boxes](int column, int row, const char* label, std::atomic<int>& target, int minimum, int maximum, std::function<void()> onChanged = {})
    {
        boxes[static_cast<std::size_t>(column)][static_cast<std::size_t>(row)] = makeLabelCell(label);
        boxes[static_cast<std::size_t>(column)][static_cast<std::size_t>(row)].isDisabled = true;
        auto& value = boxes[static_cast<std::size_t>(column + 1)][static_cast<std::size_t>(row)];
        value = makeFloatCell(static_cast<float>(target.load()), static_cast<float>(minimum), static_cast<float>(maximum), 0,
            [&target, minimum, maximum, onChanged](int direction)
            {
                target.store(exponentialStep(target.load(), minimum, maximum, direction));
                if (onChanged) onChanged();
            });
        value.onInsert = [&target, minimum, maximum, onChanged](double inserted)
        {
            target.store(juce::jlimit(minimum, maximum, static_cast<int>(std::lround(inserted))));
            if (onChanged) onChanged();
        };
    };
    auto floating = [&boxes](int column, int row, const char* label, std::atomic<float>& target, float minimum, float maximum, float step, int decimals = 0, std::function<void()> onChanged = {})
    {
        boxes[static_cast<std::size_t>(column)][static_cast<std::size_t>(row)] = makeLabelCell(label);
        boxes[static_cast<std::size_t>(column)][static_cast<std::size_t>(row)].isDisabled = true;
        auto& value = boxes[static_cast<std::size_t>(column + 1)][static_cast<std::size_t>(row)];
        value = makeFloatCell(target.load(), minimum, maximum, decimals,
            [&target, minimum, maximum, step, onChanged](int direction) { target.store(juce::jlimit(minimum, maximum, target.load() + direction * step)); if (onChanged) onChanged(); });
        value.onInsert = [&target, minimum, maximum, onChanged](double inserted) { target.store(juce::jlimit(minimum, maximum, static_cast<float>(inserted))); if (onChanged) onChanged(); };
    };
    auto choice = [&boxes](int column, int row, const char* label, std::atomic<int>& target, const char* const* names, int count, std::function<void()> onChanged = {})
    {
        boxes[static_cast<std::size_t>(column)][static_cast<std::size_t>(row)] = makeLabelCell(label);
        boxes[static_cast<std::size_t>(column)][static_cast<std::size_t>(row)].isDisabled = true;
        auto& value = boxes[static_cast<std::size_t>(column + 1)][static_cast<std::size_t>(row)];
        value.kind = UIBox::Kind::TrackerCell;
        value.isDisabled = false;
        value.text = names[juce::jlimit(0, count - 1, target.load())];
        value.onAdjust = [&target, count, onChanged](int direction) { int next = target.load() + (direction < 0 ? -1 : 1); if (next < 0) next = count - 1; if (next >= count) next = 0; target.store(next); if (onChanged) onChanged(); };
        value.onInsert = [&target, count, onChanged](double inserted) { target.store(juce::jlimit(0, count - 1, static_cast<int>(std::lround(inserted)))); if (onChanged) onChanged(); };
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
    enabledToggle(6, crash.enabled); enabledToggle(8, ride.enabled); enabledToggle(10, tom.enabled); enabledToggle(12, clap.enabled);
    const auto presetChanged = [this] { markPresetCustom(); };

    // Roland-style core controls stay close to their panel names.  The final
    // control in each strip is deliberately wider-ranging and is applied when
    // a voice starts, so it is expressive without adding callback cost.
    integer(0, 1, "NOTE", kick.triggerNote, 0, 127);
    floating(0, 2, "LEV", kick.level, 0, 100, 10);
    integer(0, 3, "TUNE", kick.tune, 12, 96, 3);
    floating(0, 4, "TONE", kick.tone, 0, 100, 10);
    exponential(0, 5, "DEC", kick.decayMs, 20, 8000);
    integer(0, 6, "PUNCH", kick.punch, -10, 10);
    choice(0, 7, "MODE", kick.oneShot, kTriggerModeNames, 2);
    choice(0, 8, "WAVE", kick.wave, kWaveNames, kWaveCount);

    integer(2, 1, "NOTE", snare.triggerNote, 0, 127);
    floating(2, 2, "LEV", snare.level, 0, 100, 10, 0, presetChanged);
    integer(2, 3, "TUNE", snare.tune, -48, 48, 3, presetChanged);
    floating(2, 4, "TONE", snare.tone, 0, 100, 10, 0, presetChanged);
    floating(2, 5, "SNAP", snare.snap, 0, 100, 10, 0, presetChanged);
    integer(2, 6, "RING", snare.ring, -10, 10, 1, presetChanged);
    choice(2, 7, "MODE", snare.oneShot, kTriggerModeNames, 2);
    boxes[2][8] = makeLabelCell("PSET");
    boxes[2][8].isDisabled = true;
    auto& presetCell = boxes[3][8];
    presetCell.kind = UIBox::Kind::TrackerCell;
    presetCell.isDisabled = false;
    presetCell.text = kPresetNames[juce::jlimit(0, 2, kitPreset.load(std::memory_order_relaxed))];
    presetCell.onAdjust = [this](int direction)
    {
        const auto current = getKitPreset();
        applyKitPreset(current == KitPreset::tr808 && direction >= 0 ? KitPreset::tr909 : KitPreset::tr808);
    };
    presetCell.onInsert = [this](double inserted)
    {
        const int value = juce::jlimit(0, 2, static_cast<int>(std::lround(inserted)));
        if (value == static_cast<int>(KitPreset::custom)) markPresetCustom();
        else applyKitPreset(static_cast<KitPreset>(value));
    };
    integer(4, 1, "NOTE", hat.triggerNote, 0, 127);
    floating(4, 2, "LEV", hat.level, 0, 100, 10, 0, presetChanged);
    integer(4, 3, "TUNE", hat.tune, -48, 48, 3, presetChanged);
    exponential(4, 4, "CDEC", hat.closedDecayMs, 5, 500, presetChanged);
    exponential(4, 5, "ODEC", hat.openDecayMs, 50, 8000, presetChanged);
    integer(4, 6, "METAL", hat.metal, -10, 10, 1, presetChanged);
    choice(4, 7, "MODE", hat.oneShot, kTriggerModeNames, 2);

    integer(6, 1, "NOTE", crash.triggerNote, 0, 127);
    floating(6, 2, "LEV", crash.level, 0, 100, 10, 0, presetChanged);
    integer(6, 3, "TUNE", crash.tune, -48, 48, 3, presetChanged);
    floating(6, 4, "TONE", crash.tone, 0, 100, 10, 0, presetChanged);
    exponential(6, 5, "DEC", crash.decayMs, 100, 12000, presetChanged);
    integer(6, 6, "EDGE", crash.edge, -10, 10, 1, presetChanged);
    choice(6, 7, "MODE", crash.oneShot, kTriggerModeNames, 2);

    integer(8, 1, "NOTE", ride.triggerNote, 0, 127);
    floating(8, 2, "LEV", ride.level, 0, 100, 10, 0, presetChanged);
    integer(8, 3, "TUNE", ride.tune, -48, 48, 3, presetChanged);
    floating(8, 4, "TONE", ride.tone, 0, 100, 10, 0, presetChanged);
    exponential(8, 5, "DEC", ride.decayMs, 100, 12000, presetChanged);
    integer(8, 6, "BELL", ride.bellCharacter, -10, 10, 1, presetChanged);
    choice(8, 7, "MODE", ride.oneShot, kTriggerModeNames, 2);

    integer(10, 1, "NOTE", tom.triggerNote, 0, 127);
    floating(10, 2, "LEV", tom.level, 0, 100, 10);
    integer(10, 3, "TUNE", tom.tune, 24, 108, 3);
    exponential(10, 4, "DEC", tom.decayMs, 20, 6000);
    integer(10, 6, "BEND", tom.bend, -10, 10);
    choice(10, 7, "MODE", tom.oneShot, kTriggerModeNames, 2);

    integer(12, 1, "NOTE", clap.triggerNote, 0, 127);
    floating(12, 2, "LEV", clap.level, 0, 100, 10, 0, presetChanged);
    floating(12, 3, "TONE", clap.tone, 0, 100, 10, 0, presetChanged);
    exponential(12, 4, "DEC", clap.decayMs, 30, 1600, presetChanged);
    floating(12, 5, "DENS", clap.density, 0, 100, 10, 0, presetChanged);
    integer(12, 6, "ROOM", clap.room, -10, 10, 1, presetChanged);
    choice(12, 7, "MODE", clap.oneShot, kTriggerModeNames, 2);
    return boxes;
}

void DrumSynthMachine::getStateInformation(juce::MemoryBlock& destData)
{
    auto* object = new juce::DynamicObject(); object->setProperty("version", kStateVersion);
    const auto put = [object](const char* key, auto& value) { object->setProperty(key, value.load(std::memory_order_relaxed)); };
    put("kickEnabled", kick.enabled); put("kickNote", kick.triggerNote); put("kickWave", kick.wave); put("kickTune", kick.tune); put("kickPitchAmount", kick.pitchAmount); put("kickPitchUpMs", kick.pitchUpMs); put("kickPitchDownMs", kick.pitchDownMs); put("kickDecayMs", kick.decayMs); put("kickNoiseType", kick.noiseType); put("kickClick", kick.click); put("kickBody", kick.body); put("kickTone", kick.tone); put("kickPunch", kick.punch); put("kickLevel", kick.level); put("kickOneShot", kick.oneShot);
    put("snareEnabled", snare.enabled); put("snareNote", snare.triggerNote); put("snareType", snare.type); put("snareTune", snare.tune); put("snareBody", snare.body); put("snareSnap", snare.snap); put("snareBodyDecayMs", snare.bodyDecayMs); put("snareNoiseDecayMs", snare.noiseDecayMs); put("snareTone", snare.tone); put("snareRing", snare.ring); put("snareLevel", snare.level); put("snareOneShot", snare.oneShot);
    put("hatEnabled", hat.enabled); put("hatNote", hat.triggerNote); put("hatTune", hat.tune); put("hatSpread", hat.spread); put("hatHighPassHz", hat.highPassHz); put("hatBandPassHz", hat.bandPassHz); put("hatResonance", hat.resonance); put("hatClosedDecayMs", hat.closedDecayMs); put("hatOpenDecayMs", hat.openDecayMs); put("hatGrit", hat.grit); put("hatMetal", hat.metal); put("hatLevel", hat.level); put("hatOneShot", hat.oneShot);
    put("crashEnabled", crash.enabled); put("crashNote", crash.triggerNote); put("crashTune", crash.tune); put("crashSpread", crash.spread); put("crashMetal", crash.metal); put("crashNoise", crash.noise); put("crashRing", crash.ring); put("crashTone", crash.tone); put("crashResonance", crash.resonance); put("crashAttackMs", crash.attackMs); put("crashDecayMs", crash.decayMs); put("crashEdge", crash.edge); put("crashLevel", crash.level); put("crashOneShot", crash.oneShot);
    put("rideEnabled", ride.enabled); put("rideNote", ride.triggerNote); put("rideTune", ride.tune); put("rideRatio", ride.ratio); put("rideFm", ride.fm); put("rideBell", ride.bell); put("rideWash", ride.wash); put("rideRing", ride.ring); put("rideTone", ride.tone); put("rideDecayMs", ride.decayMs); put("rideBellCharacter", ride.bellCharacter); put("rideLevel", ride.level); put("rideOneShot", ride.oneShot);
    put("tomEnabled", tom.enabled); put("tomNote", tom.triggerNote); put("tomWave", tom.wave); put("tomTune", tom.tune); put("tomPitchAmount", tom.pitchAmount); put("tomPitchDownMs", tom.pitchDownMs); put("tomDecayMs", tom.decayMs); put("tomClick", tom.click); put("tomBody", tom.body); put("tomBend", tom.bend); put("tomLevel", tom.level); put("tomOneShot", tom.oneShot);
    put("clapEnabled", clap.enabled); put("clapNote", clap.triggerNote); put("clapTone", clap.tone); put("clapDecayMs", clap.decayMs); put("clapDensity", clap.density); put("clapRoom", clap.room); put("clapLevel", clap.level); put("clapOneShot", clap.oneShot);
    put("kitPreset", kitPreset); put("hatModel", hatModel); put("crashModel", crashModel); put("rideModel", rideModel); put("clapModel", clapModel);
    writeMachineStateJson(destData, juce::var(object));
}

void DrumSynthMachine::setStateInformation(const void* data, int sizeInBytes)
{
    const auto state = parseMachineStateJson(data, sizeInBytes);
    if (state.isVoid()) return;
    resetParametersToDefaults();
    const auto* object = state.getDynamicObject();
    const bool hasModelState = object != nullptr && object->hasProperty("hatModel");
    const bool hasClapState = object != nullptr && object->hasProperty("clapEnabled");
    const auto getI = [&state](const char* key, std::atomic<int>& value, int minimum, int maximum) { value.store(getIntProperty(state, key, value.load(), minimum, maximum)); };
    const auto getF = [&state](const char* key, std::atomic<float>& value, float minimum, float maximum) { const float restored = getFloatProperty(state, key, value.load(), minimum, maximum); value.store(std::isfinite(restored) ? restored : juce::jlimit(minimum, maximum, value.load())); };
    getI("kickEnabled", kick.enabled, 0, 1); getI("kickNote", kick.triggerNote, 0, 127); getI("kickWave", kick.wave, 0, kWaveCount - 1); getI("kickTune", kick.tune, 12, 96); getI("kickPitchAmount", kick.pitchAmount, 0, 96); getI("kickPitchUpMs", kick.pitchUpMs, 0, 100); getI("kickPitchDownMs", kick.pitchDownMs, 5, 2000); getI("kickDecayMs", kick.decayMs, 20, 8000); getI("kickNoiseType", kick.noiseType, 0, 2); getF("kickClick", kick.click, 0, 100); getF("kickBody", kick.body, 0, 100); getF("kickTone", kick.tone, 0, 100); getI("kickPunch", kick.punch, -10, 10); getF("kickLevel", kick.level, 0, 100); getI("kickOneShot", kick.oneShot, 0, 1);
    getI("snareEnabled", snare.enabled, 0, 1); getI("snareNote", snare.triggerNote, 0, 127); getI("snareType", snare.type, 0, 2); getI("snareTune", snare.tune, -48, 48); getF("snareBody", snare.body, 0, 100); getF("snareSnap", snare.snap, 0, 100); getI("snareBodyDecayMs", snare.bodyDecayMs, 20, 4000); getI("snareNoiseDecayMs", snare.noiseDecayMs, 10, 4000); getF("snareTone", snare.tone, 0, 100); getI("snareRing", snare.ring, -10, 10); getF("snareLevel", snare.level, 0, 100); getI("snareOneShot", snare.oneShot, 0, 1);
    getI("hatEnabled", hat.enabled, 0, 1); getI("hatNote", hat.triggerNote, 0, 127); getI("hatTune", hat.tune, -48, 48); getF("hatSpread", hat.spread, 0, 100); getF("hatHighPassHz", hat.highPassHz, 100, 18000); getF("hatBandPassHz", hat.bandPassHz, 100, 18000); getF("hatResonance", hat.resonance, 0, 100); getI("hatClosedDecayMs", hat.closedDecayMs, 5, 500); getI("hatOpenDecayMs", hat.openDecayMs, 50, 8000); getF("hatGrit", hat.grit, 0, 100); getI("hatMetal", hat.metal, -10, 10); getF("hatLevel", hat.level, 0, 100); getI("hatOneShot", hat.oneShot, 0, 1);
    getI("crashEnabled", crash.enabled, 0, 1); getI("crashNote", crash.triggerNote, 0, 127); getI("crashTune", crash.tune, -48, 48); getF("crashSpread", crash.spread, 0, 100); getF("crashMetal", crash.metal, 0, 100); getF("crashNoise", crash.noise, 0, 100); getF("crashRing", crash.ring, 0, 100); getF("crashTone", crash.tone, 0, 100); getF("crashResonance", crash.resonance, 0, 100); getI("crashAttackMs", crash.attackMs, 0, 500); getI("crashDecayMs", crash.decayMs, 100, 12000); getI("crashEdge", crash.edge, -10, 10); getF("crashLevel", crash.level, 0, 100); getI("crashOneShot", crash.oneShot, 0, 1);
    getI("rideEnabled", ride.enabled, 0, 1); getI("rideNote", ride.triggerNote, 0, 127); getI("rideTune", ride.tune, -48, 48); getF("rideRatio", ride.ratio, 0.25f, 16.0f); getF("rideFm", ride.fm, 0, 20); getF("rideBell", ride.bell, 0, 100); getF("rideWash", ride.wash, 0, 100); getF("rideRing", ride.ring, 0, 100); getF("rideTone", ride.tone, 0, 100); getI("rideDecayMs", ride.decayMs, 100, 12000); getI("rideBellCharacter", ride.bellCharacter, -10, 10); getF("rideLevel", ride.level, 0, 100); getI("rideOneShot", ride.oneShot, 0, 1);
    getI("tomEnabled", tom.enabled, 0, 1); getI("tomNote", tom.triggerNote, 0, 127); getI("tomWave", tom.wave, 0, kWaveCount - 1); getI("tomTune", tom.tune, 24, 108); getI("tomPitchAmount", tom.pitchAmount, 0, 72); getI("tomPitchDownMs", tom.pitchDownMs, 5, 2000); getI("tomDecayMs", tom.decayMs, 20, 6000); getF("tomClick", tom.click, 0, 100); getF("tomBody", tom.body, 0, 100); getI("tomBend", tom.bend, -10, 10); getF("tomLevel", tom.level, 0, 100); getI("tomOneShot", tom.oneShot, 0, 1);
    if (hasClapState)
    {
        getI("clapEnabled", clap.enabled, 0, 1); getI("clapNote", clap.triggerNote, 0, 127);
        getF("clapTone", clap.tone, 0, 100); getI("clapDecayMs", clap.decayMs, 30, 1600);
        getF("clapDensity", clap.density, 0, 100); getI("clapRoom", clap.room, -10, 10);
        getF("clapLevel", clap.level, 0, 100); getI("clapOneShot", clap.oneShot, 0, 1);
    }
    else
    {
        // Prior projects had no clap responder.  Keep note 78 silent when
        // loading them, while new machines retain the useful enabled default.
        clap.enabled.store(0);
    }
    if (hasModelState)
    {
        getI("kitPreset", kitPreset, 0, 2); getI("hatModel", hatModel, 0, 2);
        getI("crashModel", crashModel, 0, 2); getI("rideModel", rideModel, 0, 2); getI("clapModel", clapModel, 1, 2);
    }
    else
    {
        // Version-1 projects predate the new model tables.  Keep their old
        // metal bank instead of changing a saved song merely by loading it.
        markPresetCustom();
        hatModel.store(0); crashModel.store(0); rideModel.store(0);
    }
}
