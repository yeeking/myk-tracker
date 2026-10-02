#pragma once

#include <mutex>
#include <string>
#include <vector>

#include <JuceHeader.h>

#include "AudioEffectMachine.h"
#include "MachineUi.h"

class FilterFxMachine final : public AudioEffectMachine
{
public:
    /** Creates the filter with default low-pass, no resonance, half sweep. */
    FilterFxMachine() = default;

    /** Resets the filter state, envelope and pending note plan under the state mutex. */
    void clearTransientState() override;
    /** Builds the machine-editor UI cells for the filter controls. */
    std::vector<std::vector<UIBox>> getUIBoxes(const MachineUiContext& context) override;
    /** Applies the resonant filter to the stack audio buffer. */
    void processAudioBuffer(juce::AudioBuffer<float>& buffer) override;
    /** Delivers sequencer note events scheduled within the current block. */
    void scheduleBlockNotes(const std::vector<MachineScheduledNote>& notes) override;
    /** Updates tick duration used to convert note durations to samples. */
    void setSecondsPerTick(double secondsPerTick) override;
    /** Resets the filter state and envelope when transport or notes are stopped. */
    void allNotesOff() override;
    /** Serialises the filter settings. */
    void getStateInformation(juce::MemoryBlock& destData) override;
    /** Restores the filter settings from serialised state. */
    void setStateInformation(const void* data, int sizeInBytes) override;

protected:
    /** Prepares filter and envelope sample-rate state without clearing transient state. */
    void prepareDsp(double sampleRate, int samplesPerBlock) override;

private:
    /** Per-sample plan entry built from the block's scheduled notes. */
    struct PlanEvent
    {
        int sample = 0;
        bool isAttack = false;
    };

    /** Per-block snapshot of the UI-owned parameters, taken under stateMutex. */
    struct ParamSnapshot
    {
        double sampleRate = 44100.0;
        double secondsPerTick = kDefaultTrackerSecondsPerTick;
        float cutoffHz = 2000.0f;
        float resonance = 0.0f;
        float amount = 0.5f;
        bool positivePolarity = true;
        bool highPassMode = false;
        float attackSeconds = 0.05f;
        float decaySeconds = 0.1f;
        float sustainLevel = 0.65f;
        float releaseSeconds = 0.2f;
    };

    /** Protects settings shared between UI and audio threads. */
    mutable std::mutex stateMutex;
    /** Current host/sample playback rate. */
    double currentSampleRate = 44100.0;
    /** Current tracker tick duration used for note durations. */
    double currentSecondsPerTick = kDefaultTrackerSecondsPerTick;
    /** Top of the cutoff sweep in Hz (cutoff when the envelope is fully up). */
    float cutoffHz = 2000.0f;
    /** Resonance control, mapped to the TPT filter's R2 coefficient. */
    float resonance = 0.0f;
    /** How far the low end of the sweep falls below cutoff, 0..1 (log scale). */
    float amount = 0.5f;
    /** True: attack sweeps low->high; false: attack sweeps high->low. */
    bool positivePolarity = true;
    /** Filter mode: low-pass or high-pass. */
    bool highPassMode = false;
    /** Envelope times in seconds, applied to the cutoff sweep. */
    float attackSeconds = 0.05f;
    float decaySeconds = 0.1f;
    float sustainLevel = 0.65f;
    float releaseSeconds = 0.2f;

    /** TPT state-variable filter, audio-thread only. */
    juce::dsp::StateVariableTPTFilter<float> filter;
    /** Cutoff-sweep envelope, audio-thread only, retriggers on each note. */
    juce::ADSR envelope;
    /** Pointer to the processor-owned per-stack note list for the current block. */
    const std::vector<MachineScheduledNote>* blockNotes = nullptr;
    /** Per-block attack/release plan, rebuilt each block (no RT allocation). */
    std::vector<PlanEvent> planEvents;
    /** Release times (block-relative) carried over for notes longer than one block. */
    std::vector<int> carryReleases;
    /** Number of scheduled notes still sounding. */
    int activeNotes = 0;

    static constexpr float kMinCutoffHz = 20.0f;
    static constexpr float kMaxCutoffHz = 20000.0f;
    /** TPT resonance coefficient for a plain 12 dB/oct filter (no peak). */
    static constexpr float kFlatResonance = 0.7071068f;
    /** Maximum TPT resonance coefficient (below the stability limit of 2). */
    static constexpr float kMaxResonance = 1.9f;

    /** Maps the 0..1 resonance control to the TPT filter coefficient. */
    static float toFilterResonance(float resonanceControl);
};
