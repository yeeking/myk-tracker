#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <vector>

#include <JuceHeader.h>

#include "../MachineInterface.h"

/** Six-part, mono drum synthesizer.  Parameter storage is atomic so the UI
    never blocks the audio callback; note and filter state belongs exclusively
    to the audio thread. */
class DrumSynthMachine final : public MachineInterface
{
public:
    enum class Drum : int { kick, crash, snare, ride, closedHat, tom, openHat, none };

    DrumSynthMachine();
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override;
    std::vector<std::vector<UIBox>> getUIBoxes(const MachineUiContext& context) override;
    bool handleIncomingNote(unsigned short, unsigned short, unsigned short, MachineNoteEvent&) override;
    void scheduleBlockNotes(const std::vector<MachineScheduledNote>& notes) override;
    void setSecondsPerTick(double secondsPerTick) override;
    void allNotesOff() override;
    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;
    void resetParametersToDefaults();

    Drum drumForNote(int midiNote) const noexcept;
    const char* describeNote(int midiNote) const noexcept;

public:
    static constexpr int kTableSize = 2048;
    static constexpr int kWaveCount = 9;
    static constexpr int kSquareMipCount = 7;

    struct Tables
    {
        std::array<std::array<float, kTableSize>, kWaveCount> waves {};
        std::array<std::array<float, kTableSize>, kSquareMipCount> squareMips {};
        std::array<float, 145> pitchRatios {};
        std::array<float, 128> midiHz {};
        std::array<float, kTableSize + 1> sineCycle {};
        std::array<float, kTableSize + 1> cosineCycle {};
        Tables();
    };

    struct Biquad
    {
        float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;
        float z1 = 0.0f, z2 = 0.0f;
        float process(float input) noexcept;
        void reset() noexcept { z1 = z2 = 0.0f; }
        void setBandPass(float hz, float q, double sampleRate, const Tables& tables) noexcept;
        void setLowPass(float hz, float q, double sampleRate, const Tables& tables) noexcept;
        void setHighPass(float hz, float q, double sampleRate, const Tables& tables) noexcept;
    };

private:

    struct Voice
    {
        bool active = false;
        bool releasing = false;
        bool openHat = false;
        bool oneShot = true;
        int triggerNote = -1;
        int age = 0;
        int gateSamples = 0;
        int attackSamples = 0;
        float envelope = 0.0f;
        float envelope2 = 0.0f;
        float envelope3 = 0.0f;
        float bodyEnvelope2 = 0.0f;
        float decayMultiplier = 1.0f;
        float decayMultiplier2 = 1.0f;
        float decayMultiplier3 = 1.0f;
        float bodyDecayMultiplier2 = 1.0f;
        float gateGain = 1.0f;
        float velocityGain = 0.0f;
        float accent = 0.0f;
        float tuneRatio = 1.0f;
        float pitchAmount = 0.0f;
        int pitchUpSamples = 0;
        int pitchDownSamples = 1;
        float component1 = 1.0f;
        float component2 = 0.0f;
        float tone = 0.5f;
        float resonance = 0.3f;
        float spread = 0.5f;
        float fmRatio = 3.0f;
        float fmIndex = 2.5f;
        float ring = 0.0f;
        float noiseMemory = 0.0f;
        float releaseMultiplier = 0.0f;
        int waveform = 0;
        int noiseType = 0;
        int profile = 0;
        int noiseHoldSamples = 0;
        std::array<double, 8> phases {};
        std::array<Biquad, 4> filters {};
    };

    struct KickParameters
    {
        std::atomic<int> enabled { 1 }, triggerNote { 72 }, wave { 0 }, tune { 36 }, pitchAmount { 24 }, pitchUpMs { 0 }, pitchDownMs { 60 }, decayMs { 1200 }, noiseType { 0 }, oneShot { 1 };
        std::atomic<float> click { 45.0f }, body { 100.0f }, level { 95.0f };
    } kick;
    struct SnareParameters
    {
        std::atomic<int> enabled { 1 }, triggerNote { 73 }, type { 1 }, tune { 0 }, bodyDecayMs { 300 }, noiseDecayMs { 220 }, oneShot { 1 };
        std::atomic<float> body { 65.0f }, snap { 70.0f }, tone { 60.0f }, level { 80.0f };
    } snare;
    struct HatParameters
    {
        std::atomic<int> enabled { 1 }, triggerNote { 74 }, tune { 0 }, closedDecayMs { 70 }, openDecayMs { 700 }, oneShot { 1 };
        std::atomic<float> spread { 50.0f }, highPassHz { 5000.0f }, bandPassHz { 7000.0f }, resonance { 30.0f }, grit { 20.0f }, level { 70.0f };
    } hat;
    struct CrashParameters
    {
        std::atomic<int> enabled { 1 }, triggerNote { 75 }, tune { 0 }, attackMs { 2 }, decayMs { 1800 }, oneShot { 1 };
        std::atomic<float> spread { 50.0f }, metal { 75.0f }, noise { 35.0f }, ring { 20.0f }, tone { 55.0f }, resonance { 45.0f }, level { 65.0f };
    } crash;
    struct RideParameters
    {
        std::atomic<int> enabled { 1 }, triggerNote { 76 }, tune { 0 }, decayMs { 2500 }, oneShot { 1 };
        std::atomic<float> ratio { 3.0f }, fm { 2.5f }, bell { 70.0f }, wash { 35.0f }, ring { 15.0f }, tone { 65.0f }, level { 65.0f };
    } ride;
    struct TomParameters
    {
        std::atomic<int> enabled { 1 }, triggerNote { 77 }, wave { 0 }, tune { 48 }, pitchAmount { 12 }, pitchDownMs { 100 }, decayMs { 500 }, oneShot { 1 };
        std::atomic<float> click { 15.0f }, body { 100.0f }, level { 75.0f };
    } tom;

    static const Tables& tables();
    static float wrapPhase(double phase) noexcept;
    static float sampleTable(const std::array<float, kTableSize>& table, double phase) noexcept;
    static float sampleWave(const Tables& source, int waveform, double phase) noexcept;
    static float sampleSquare(const Tables& source, double phase, float hz) noexcept;
    static float pitchRatio(const Tables& source, float semitones) noexcept;
    static float softSaturate(float sample) noexcept;

    void trigger(Drum drum, const MachineScheduledNote& event) noexcept;
    bool isDrumEnabled(Drum drum) const noexcept;
    void release(Drum drum, int note) noexcept;
    void beginRelease(Voice& voice, int releaseMs = 3) noexcept;
    void configureGate(Voice& voice, const MachineScheduledNote& event, bool oneShot) noexcept;
    void configureMetalPhases(Voice& voice, const std::array<float, 6>& frequencies) noexcept;
    void updateVoiceEnvelope(Voice& voice) noexcept;
    float renderKick(Voice& voice) noexcept;
    float renderSnare(Voice& voice) noexcept;
    float renderHat(Voice& voice) noexcept;
    float renderCrash(Voice& voice) noexcept;
    float renderRide(Voice& voice) noexcept;
    float renderTom(Voice& voice) noexcept;
    float renderMetalBank(Voice& voice, const std::array<float, 6>& baseFrequencies) noexcept;
    float nextNoise(Voice& voice) noexcept;
    float decayMultiplierForMs(float milliseconds) const noexcept;
    float smoothedOutputLevel(float current, float target) const noexcept;

    std::array<Voice, 6> voices {};
    /** A short-lived copy of an open hat while a closed hat chokes it. */
    Voice hatChokeTail;
    const std::vector<MachineScheduledNote>* scheduledBlockNotes = nullptr;
    double currentSampleRate = 44100.0;
    double currentSecondsPerTick = 60.0 / (120.0 * 8.0);
    std::array<float, 12001> decayMultipliers {};
    float outputSmoothing = 1.0f;
    float releaseMultiplier3ms = 0.0f;
    float releaseMultiplier2ms = 0.0f;
    std::array<float, 6> outputLevels {{ 0.95f, 0.8f, 0.7f, 0.65f, 0.65f, 0.75f }};
    std::uint32_t randomState = 0x6d2b79f5u;
    std::uint64_t sampleCounter = 0;
};
