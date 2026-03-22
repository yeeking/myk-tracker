#pragma once

#include <array>
#include <mutex>
#include <vector>

#include <JuceHeader.h>

#include "MachineInterface.h"

class WavetableSynthMachine final : public MachineInterface
{
public:
    WavetableSynthMachine();

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override;
    std::vector<std::vector<UIBox>> getUIBoxes(const MachineUiContext& context) override;
    bool handleIncomingNote(unsigned short note,
                            unsigned short velocity,
                            unsigned short durationTicks,
                            MachineNoteEvent& outEvent) override;
    void setSecondsPerTick(double secondsPerTick) override;
    void allNotesOff() override;
    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

private:
    enum class Waveform
    {
        sine = 0,
        triangle,
        saw,
        square
    };

    struct Voice
    {
        int midiNote = -1;
        float velocity = 0.0f;
        double phase = 0.0;
        double phaseDelta = 0.0;
        int ageSamples = 0;
        int noteDurationSamples = 0;
        int samplesUntilRelease = 0;
        bool releaseStarted = false;
        bool active = false;
        juce::ADSR envelope;
    };

    static constexpr int kTableSize = 128;
    static constexpr int kVoiceCount = 8;
    static constexpr int kMaxWaveSteps = 6;

    std::array<std::array<float, kTableSize>, 4> tables {};
    std::array<Voice, kVoiceCount> voices {};
    std::array<Waveform, kMaxWaveSteps> waveSteps {
        Waveform::sine,
        Waveform::square,
        Waveform::square,
        Waveform::square,
        Waveform::square,
        Waveform::square
    };
    mutable std::mutex stateMutex;

    double currentSampleRate = 44100.0;
    double currentSecondsPerTick = 60.0 / (120.0 * 8.0);
    int nextVoiceIndex = 0;
    int waveStepCount = 2;

    float attackSeconds = 0.05f;
    float decaySeconds = 0.15f;
    float sustainLevel = 0.65f;
    float releaseSeconds = 0.2f;

    void initialiseTables();
    void updateVoiceEnvelopeParameters();
    Voice& allocateVoice();
    float sampleVoice(Voice& voice) const;
    float sampleWaveform(Waveform waveform, double phase) const;
    static const char* getWaveformName(Waveform waveform);
    static std::string formatFloat(float value, int decimals);
    std::string buildEnvelopePlot() const;
};
