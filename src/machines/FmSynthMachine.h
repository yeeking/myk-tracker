#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <vector>

#include <JuceHeader.h>

#include "MachineInterface.h"

/** Four-operator, Chowning-style phase-modulation synthesizer. */
class FmSynthMachine final : public MachineInterface
{
public:
    static constexpr int kOperatorCount = 4;
    static constexpr int kMaxVoiceCount = 8;

    enum class Waveform : int { sine, triangle, saw, square, odd5, even5, noise, sampleHold, count };
    enum class FrequencyMode : int { ratio, fixed };
    enum class Algorithm : int { pair, cascade, parallelModulators, twoPairs, sharedModulator, branch, additive, feedback, custom, count };
    enum class Preset : int { init, brass, bell, gong, drum, pipe, harpsichord, voice, residual, parallelModulators, cascade, noise, evolve, custom, count };
    enum class UiBank : int { core, envelope, scaling, route, global, count };

    struct UiEnvelopeVoice
    {
        bool active = false;
        std::uint64_t age = 0;
        int midiNote = -1;
        std::array<int, kOperatorCount> stages {};
        std::array<float, kOperatorCount> progress {};
        std::array<float, kOperatorCount> levels {};
    };

    struct UiEnvelopeSnapshot
    {
        std::array<UiEnvelopeVoice, kMaxVoiceCount> voices {};
    };

    struct UiEnvelopeDefinition
    {
        std::array<std::array<int, 4>, kOperatorCount> rates {};
        std::array<std::array<float, 4>, kOperatorCount> levels {};
        std::uint64_t revision = 0;
    };

    FmSynthMachine();

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override;
    std::vector<std::vector<UIBox>> getUIBoxes(const MachineUiContext&) override;
    std::vector<std::vector<UIBox>> getControlBoxes(const MachineUiContext&) override;
    void onCursorMoved(int row, int col) override;
    bool cycleUiPanel(int direction) override;
    std::optional<std::pair<int, int>> consumePreferredCursorPosition() override;
    bool handleIncomingNote(unsigned short, unsigned short, unsigned short, MachineNoteEvent&) override;
    void scheduleBlockNotes(const std::vector<MachineScheduledNote>& notes) override;
    void setSecondsPerTick(double secondsPerTick) override;
    void allNotesOff() override;
    void setModulationBlock(const ModulationBlockView* block) override { modulationBlock = block; }
    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    void applyAlgorithm(Algorithm algorithm);
    void applyPreset(Preset preset);
    int getVoiceCount() const noexcept { return voiceCount.load(std::memory_order_relaxed); }
    UiBank getUiBank() const noexcept { return uiBank; }
    int getUiSelectedOperator() const noexcept { return uiSelectedOperator; }
    void setUiEnvelopeMonitoring(bool enabled) noexcept { uiEnvelopeMonitoring.store(enabled, std::memory_order_relaxed); }
    void getUiEnvelopeSnapshot(UiEnvelopeSnapshot& destination) const noexcept;
    UiEnvelopeDefinition getUiEnvelopeDefinition() const noexcept;

private:
    static constexpr int kTableSize = 2048;
    static constexpr int kMipLevels = 7;
    static constexpr int kPeriodicWaveCount = 6;
    static constexpr float kTwoPi = juce::MathConstants<float>::twoPi;

    struct OperatorParameters
    {
        std::atomic<int> enabled { 1 };
        std::atomic<int> waveform { static_cast<int>(Waveform::sine) };
        std::atomic<int> frequencyMode { static_cast<int>(FrequencyMode::ratio) };
        std::atomic<int> coarse { 1 };
        std::atomic<int> fine { 0 };
        std::atomic<float> fixedHz { 440.0f };
        std::atomic<float> detuneCents { 0.0f };
        std::atomic<float> output { 0.0f };
        std::array<std::atomic<int>, 4> rates;
        std::array<std::atomic<float>, 4> levels;
        std::atomic<int> velocitySensitivity { 0 };
        std::atomic<int> breakpoint { 60 };
        std::atomic<int> leftSlope { 0 };
        std::atomic<int> rightSlope { 0 };
        std::atomic<int> rateScaling { 0 };

        OperatorParameters();
    };

    struct OperatorSnapshot
    {
        bool enabled = true;
        Waveform waveform = Waveform::sine;
        FrequencyMode frequencyMode = FrequencyMode::ratio;
        float ratio = 1.0f;
        float fixedHz = 440.0f;
        float detuneMultiplier = 1.0f;
        float output = 0.0f;
        std::array<int, 4> rates {};
        std::array<float, 4> levels {};
        int velocitySensitivity = 0;
        int breakpoint = 60;
        int leftSlope = 0;
        int rightSlope = 0;
        int rateScaling = 0;
    };

    struct PatchSnapshot
    {
        std::array<OperatorSnapshot, kOperatorCount> operators;
        std::array<std::array<float, kOperatorCount>, kOperatorCount> matrix {};
        std::array<std::array<bool, kOperatorCount>, kOperatorCount> delayed {};
        std::array<int, kOperatorCount> order {{ 0, 1, 2, 3 }};
        int voiceCount = 4;
        float level = 0.8f;
        int octave = 0;
        float glideMs = 0.0f;
        float indexScale = 1.0f;
        float feedbackScale = 1.0f;
        float color = 1.0f;
        float drive = 0.0f;
        float octaveMultiplier = 1.0f;
    };

    struct Envelope
    {
        float level = 0.0f;
        float target = 0.0f;
        float increment = 0.0f;
        int remaining = 0;
        int stageSamples = 0;
        int stage = 0;
        bool released = false;
        bool active = false;
    };

    struct OperatorVoice
    {
        double phase = 0.0;
        float currentBaseHz = -1.0f;
        float previous = 0.0f;
        float sampleHold = 0.0f;
        std::uint32_t randomState = 0x12345678u;
        Envelope envelope;
    };

    struct Voice
    {
        std::array<OperatorVoice, kOperatorCount> operators;
        std::array<float, kOperatorCount> operatorGains {{ 1.0f, 1.0f, 1.0f, 1.0f }};
        int midiNote = -1;
        float velocity = 1.0f;
        float currentNoteHz = 440.0f;
        float targetNoteHz = 440.0f;
        float glideStep = 0.0f;
        int glideRemaining = 0;
        int samplesUntilRelease = 0;
        std::uint64_t age = 0;
        bool releaseStarted = false;
        bool held = false;
        bool active = false;
    };

    std::array<OperatorParameters, kOperatorCount> operatorParameters;
    std::array<std::array<std::atomic<float>, kOperatorCount>, kOperatorCount> modulationIndices;
    std::atomic<int> selectedAlgorithm { static_cast<int>(Algorithm::pair) };
    std::atomic<int> selectedPreset { static_cast<int>(Preset::init) };
    std::atomic<int> voiceCount { 4 };
    std::atomic<float> masterLevel { 0.8f };
    std::atomic<int> octave { 0 };
    std::atomic<float> glideMs { 0.0f };
    std::atomic<float> indexPercent { 100.0f };
    std::atomic<float> feedbackPercent { 100.0f };
    std::atomic<float> colorPercent { 100.0f };
    std::atomic<float> drivePercent { 0.0f };
    /** Seqlock-style guard for multi-parameter preset/state writes. Audio never waits. */
    std::atomic<bool> patchWriteInProgress { false };
    std::atomic<std::uint64_t> parameterRevision { 1 };

    struct UiEnvelopeVoiceAtomic
    {
        std::atomic<int> active { 0 };
        std::atomic<std::uint64_t> age { 0 };
        std::atomic<int> midiNote { -1 };
        std::array<std::atomic<int>, kOperatorCount> stages;
        std::array<std::atomic<float>, kOperatorCount> progress;
        std::array<std::atomic<float>, kOperatorCount> levels;
    };
    std::array<UiEnvelopeVoiceAtomic, kMaxVoiceCount> uiEnvelopeVoices;
    std::atomic<bool> uiEnvelopeMonitoring { false };
    int uiEnvelopeSamplesUntilPublish = 0;

    UiBank uiBank { UiBank::core };
    std::array<std::pair<int, int>, static_cast<std::size_t>(UiBank::count)> uiBankCursor {};
    std::array<bool, static_cast<std::size_t>(UiBank::count)> uiBankCursorValid {};
    std::optional<std::pair<int, int>> preferredUiCursor;
    int uiSelectedOperator = 0;

    std::array<Voice, kMaxVoiceCount> voices;
    std::array<std::array<std::array<float, kTableSize>, kMipLevels>, kPeriodicWaveCount> tables {};
    std::array<float, 2049> pitchModulationTable {};
    std::array<float, 2049> indexModulationTable {};
    std::array<float, 128> midiFrequencyTable {};
    std::array<float, 100> envelopeRateSecondsTable {};
    std::array<std::array<float, 128>, 201> rateScalingTable {};
    std::array<std::array<float, 128>, 25> keyboardGainTable {};
    std::array<std::array<float, kOperatorCount>, kOperatorCount> smoothedMatrix {};
    std::array<float, kOperatorCount> smoothedOutputs {};
    float smoothedIndexScale = 1.0f;
    float smoothedFeedbackScale = 1.0f;
    float smoothedColor = 1.0f;
    float smoothingCoefficient = 1.0f;
    int smoothingSamplesRemaining = 0;
    PatchSnapshot cachedPatch;
    std::uint64_t cachedRevision = 0;
    double currentSampleRate = 44100.0;
    double secondsPerTick = 60.0 / (120.0 * 8.0);
    std::uint64_t voiceAgeCounter = 0;
    const std::vector<MachineScheduledNote>* scheduledBlockNotes = nullptr;
    const ModulationBlockView* modulationBlock = nullptr;

    void initialiseTables();
    PatchSnapshot snapshotPatch() const;
    static void calculateRouting(PatchSnapshot& patch);
    static float rateSeconds(int rate) noexcept;
    int rateSamples(const OperatorSnapshot& op, int stage, int midiNote) const noexcept;
    void startEnvelope(Envelope& envelope, const OperatorSnapshot& op, int midiNote, bool preserveLevel) noexcept;
    void advanceEnvelopeStage(Envelope& envelope, const OperatorSnapshot& op, int midiNote) noexcept;
    float advanceEnvelope(Envelope& envelope, const OperatorSnapshot& op, int midiNote) noexcept;
    void releaseVoice(Voice& voice, const PatchSnapshot& patch) noexcept;
    Voice& allocateVoice(int limit) noexcept;
    void triggerVoice(const MachineScheduledNote& event, const PatchSnapshot& patch) noexcept;
    void releaseNote(int midiNote, const PatchSnapshot& patch) noexcept;
    void updateVoiceGains(Voice& voice, const PatchSnapshot& patch) noexcept;
    void publishUiEnvelopeSnapshot() noexcept;
    float renderVoice(Voice& voice, const PatchSnapshot& patch, float pitchMod,
                      float externalIndex, float externalFeedback, float color) noexcept;
    float samplePeriodic(Waveform waveform, double phase, float hz) const noexcept;
    float sampleWave(OperatorVoice& voice, Waveform waveform, double phase, float hz, float color) const noexcept;
    static float nextNoise(OperatorVoice& voice) noexcept;
    static float wrapPhase(double phase) noexcept;
    float lookupPitchMod(float normalised) const noexcept;
    float lookupIndexMod(float normalised) const noexcept;
    static const char* waveformName(Waveform waveform) noexcept;
    static const char* algorithmName(Algorithm algorithm) noexcept;
    static const char* presetName(Preset preset) noexcept;
    static std::string noteName(int midiNote);
    void markCustom() noexcept;
    void markAlgorithmCustom() noexcept;
};
