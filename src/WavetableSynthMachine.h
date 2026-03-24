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
    /** Updates tick duration for note-length scheduling. */
    void setSecondsPerTick(double secondsPerTick) override;
    /** Silences all active voices immediately. */
    void allNotesOff() override;
    /** Serialises the synth state. */
    void getStateInformation(juce::MemoryBlock& destData) override;
    /** Restores the synth state. */
    void setStateInformation(const void* data, int sizeInBytes) override;

private:
    /** Available base waveforms for wavetable morphing. */
    enum class Waveform
    {
        sine = 0,
        triangle,
        saw,
        square
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
    /** Fixed number of polyphonic voices. */
    static constexpr int kVoiceCount = 8;
    /** Maximum number of wavetable steps in the morph sequence. */
    static constexpr int kMaxWaveSteps = 6;

    /** Precomputed waveform tables. */
    std::array<std::array<float, kTableSize>, 4> tables {};
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
