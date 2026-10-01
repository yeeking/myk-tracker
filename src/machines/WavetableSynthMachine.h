#pragma once

#include <array>
#include <mutex>
#include <vector>

#include <JuceHeader.h>

#include "MachineInterface.h"

class WavetableSynthMachine final : public MachineInterface
{
public:
    /** Creates the wavetable synth and initialises its tables. */
    WavetableSynthMachine();

    /** Prepares voice state and sample-rate-dependent parameters. */
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    /** Releases realtime resources and clears active voices. */
    void releaseResources() override;
    /** Renders active synth voices into the audio buffer. */
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override;
    /** Builds the machine-editor UI cells for the synth. */
    std::vector<std::vector<UIBox>> getUIBoxes(const MachineUiContext& context) override;
    /** Starts a note on an allocated synth voice. */
    bool handleIncomingNote(unsigned short note,
                            unsigned short velocity,
                            unsigned short durationTicks,
                            MachineNoteEvent& outEvent) override;
    /** Stores sequencer note events scheduled for the current block. */
    void scheduleBlockNotes(const std::vector<MachineScheduledNote>& notes) override;
    /** Updates tick duration for note-length scheduling. */
    void setSecondsPerTick(double secondsPerTick) override;
    /** Silences all active voices immediately. */
    void allNotesOff() override;
    /** Serialises the synth state. */
    void getStateInformation(juce::MemoryBlock& destData) override;
    /** Restores the synth state. */
    void setStateInformation(const void* data, int sizeInBytes) override;

    /** Number of active wavetable steps, for UI preview. */
    int getWaveStepCount() const;
    /** Waveform index (0..Waveform::count-1) of the given step. */
    int getWaveStepWaveform(int stepIndex) const;
    /** Number of active sub-oscillator wavetable steps, for UI preview. */
    int getSubWaveStepCount() const;
    /** Waveform index (0..Waveform::count-1) of the given sub-oscillator step. */
    int getSubWaveStepWaveform(int stepIndex) const;
    /** Current sub-oscillator octave offset (-3..+3), for the UI preview tint. */
    int getSubOctaveOffset() const;
    /** Samples a waveform by index at the given 0..1 phase, for UI preview. */
    float sampleWaveformForUi(int waveformIndex, double phase) const;
    /** Current envelope settings and their display maxima, for UI preview. */
    void getEnvelopeSettings(float& attack, float& decay, float& sustain, float& release,
                             float& maxAttack, float& maxDecay, float& maxRelease) const;

private:
    /** Available base waveforms for wavetable morphing. Indices are persisted
        in plugin state, so entries may only be appended, never reordered or
        removed. */
    enum class Waveform
    {
        sine = 0,
        triangle,
        saw,
        square,
        /** Sine partials at 1x + 2x, peak-normalised. */
        add1,
        /** Sine partials at 1x + 2x + 3x, peak-normalised. */
        add2,
        /** Sine partials at 1x..10x, peak-normalised. */
        add10,
        /** Sine partials at 1x, 2x, 4x, 6x, 8x, peak-normalised. */
        eve5,
        /** Sine partials at 1x, 3x, 5x, 7x, 9x, peak-normalised. */
        odd5,
        count
    };

    /** Clock source for the shared waveform cycler. */
    enum class CyclerMode
    {
        ad = 0,   /** Cycles per combined attack+decay envelope time. */
        cps = 1   /** Cycles per second. */
    };

    /** Runtime state for one polyphonic synth voice. */
    struct Voice
    {
        /** MIDI note currently assigned to the voice. */
        int midiNote = -1;
        /** Normalised voice velocity. */
        float velocity = 0.0f;
        /** Current oscillator phase. */
        double phase = 0.0;
        /** Phase increment per sample. */
        double phaseDelta = 0.0;
        /** Current sub-oscillator phase. */
        double subPhase = 0.0;
        /** Sub-oscillator phase increment per sample (the main oscillator's
            octave/detune-adjusted pitch scaled by the sub octave offset and
            detune). */
        double subPhaseDelta = 0.0;
        /** Age of the note in samples. */
        int ageSamples = 0;
        /** Total scheduled note duration in samples. */
        int noteDurationSamples = 0;
        /** Remaining samples before the release stage begins. */
        int samplesUntilRelease = 0;
        /** True once the release stage has been triggered. */
        bool releaseStarted = false;
        /** True while the voice is active. */
        bool active = false;
        /** ADSR envelope for the voice. */
        juce::ADSR envelope;
    };

    /** Number of samples per waveform table. */
    static constexpr int kTableSize = 128;
    /** Number of available base waveforms. */
    static constexpr std::size_t kWaveformCount = static_cast<std::size_t>(Waveform::count);
    /** Fixed number of polyphonic voices. */
    static constexpr int kVoiceCount = 8;
    /** Maximum number of wavetable steps in the morph sequence. */
    static constexpr int kMaxWaveSteps = 6;

    /** Precomputed waveform tables. */
    std::array<std::array<float, kTableSize>, kWaveformCount> tables {};
    /** Fixed voice pool. */
    std::array<Voice, kVoiceCount> voices {};
    /** Selected waveform at each wavetable step. */
    std::array<Waveform, kMaxWaveSteps> waveSteps {
        Waveform::sine,
        Waveform::square,
        Waveform::square,
        Waveform::square,
        Waveform::square,
        Waveform::square
    };
    /** Selected waveform at each sub-oscillator wavetable step. */
    std::array<Waveform, kMaxWaveSteps> subWaveSteps {
        Waveform::sine,
        Waveform::sine,
        Waveform::sine,
        Waveform::sine,
        Waveform::sine,
        Waveform::sine
    };
    /** Protects synth state shared between UI and audio threads. */
    mutable std::mutex stateMutex;

    /** Current sample rate used by the synth. */
    double currentSampleRate = 44100.0;
    /** Current tracker tick duration used for note lengths. */
    double currentSecondsPerTick = 60.0 / (120.0 * 8.0);
    /** Round-robin voice allocation cursor. */
    int nextVoiceIndex = 0;
    /** Number of active wavetable steps. */
    int waveStepCount = 2;
    /** Clock source for the waveform cycler. */
    CyclerMode cyclerMode = CyclerMode::cps;
    /** Cycler speed: cycles/second (CPS) or cycles per attack+decay time (AD). */
    float cyclerRate = 1.0f;
    /** Shared 0..1 cycler phase; advanced on the audio thread only while at
        least one voice is active, so all voices select the same waveform
        step and chords morph in lockstep. */
    double cyclerPhase = 0.0;
    /** Main oscillator mix level, 0..1. */
    float mainLevel = 1.0f;
    /** Main oscillator octave offset relative to the note frequency, -3..+3;
        the note frequency is multiplied by 2^offset. The sub-oscillator
        derives its base pitch from this offset+detuned main pitch. */
    int mainOctaveOffset = 0;
    /** Main oscillator detune in cents relative to the octave-offset pitch. */
    float mainDetuneCents = 0.0f;
    /** Cached 2^(cents/1200) scale; refreshed on the message thread so the
        audio path only ever multiplies. */
    double mainDetuneMultiplier = 1.0;
    /** Number of active sub-oscillator wavetable steps. */
    int subWaveStepCount = 2;
    /** Sub-oscillator octave offset relative to the note frequency, -3..+3;
        the note frequency is multiplied by 2^offset. */
    int subOctaveOffset = -1;
    /** Sub-oscillator mix level, 0..1. Zero disables sub sampling entirely. */
    float subLevel = 0.0f;
    /** Sub-oscillator detune in cents relative to the octave-offset pitch. */
    float subDetuneCents = 0.0f;
    /** Cached 2^(cents/1200) scale; refreshed on the message thread so the
        audio path only ever multiplies. */
    double subDetuneMultiplier = 1.0;
    /** In-block note events for the current block; set on the audio thread by the
        processor immediately before processBlock and read only there. Points at
        processor-owned per-stack storage, stable until the next block. */
    const std::vector<MachineScheduledNote>* scheduledBlockNotes = nullptr;

    /** ADSR attack time in seconds. */
    float attackSeconds = 0.05f;
    /** ADSR decay time in seconds. */
    float decaySeconds = 0.15f;
    /** ADSR sustain level. */
    float sustainLevel = 0.65f;
    /** ADSR release time in seconds. */
    float releaseSeconds = 0.2f;

    /** Fills the static waveform lookup tables. */
    void initialiseTables();
    /** Pushes the current ADSR settings to all voices. */
    void updateVoiceEnvelopeParameters();
    /** Recomputes the cached 2^(cents/1200) multiplier from subDetuneCents.
        Message-thread only (ctor, UI adjust, state restore); the audio path
        only ever multiplies. */
    void updateSubDetuneMultiplier();
    /** Recomputes the cached 2^(cents/1200) multiplier from mainDetuneCents.
        Message-thread only (ctor, UI adjust, state restore); the audio path
        only ever multiplies. */
    void updateMainDetuneMultiplier();
    /** Allocates the next voice, stealing if needed. */
    Voice& allocateVoice();
    /** Samples one voice and advances its lifecycle. */
    float sampleVoice(Voice& voice) const;
    /** Samples one named waveform at the given phase. */
    float sampleWaveform(Waveform waveform, double phase) const;
    /** Returns the short display name for a waveform. */
    static const char* getWaveformName(Waveform waveform);
    /** Formats floating point values for compact tracker display. */
    static std::string formatFloat(float value, int decimals);
};
