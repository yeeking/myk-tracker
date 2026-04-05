#pragma once

#include <atomic>
#include <string>
#include <vector>

#include <JuceHeader.h>

#include "AudioEffectMachine.h"

class AuxReverbMachine final : public AudioEffectMachine
{
public:
    explicit AuxReverbMachine(const juce::Reverb::Parameters& defaults = {});

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    std::vector<std::vector<UIBox>> getUIBoxes(const MachineUiContext& context) override;
    void processAudioBuffer(juce::AudioBuffer<float>& buffer) override;
    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

private:
    static constexpr double kStateVersion = 1.0;

    std::atomic<float> roomSize { 0.55f };
    std::atomic<float> damping { 0.35f };
    std::atomic<float> wetLevel { 0.33f };
    std::atomic<float> dryLevel { 0.0f };
    std::atomic<float> width { 1.0f };
    std::atomic<float> freezeMode { 0.0f };

    juce::Reverb::Parameters defaultParameters;
    juce::dsp::Reverb reverb;
    std::atomic<bool> dspDirty { true };

    juce::Reverb::Parameters captureParameters() const;
    void updateParameters();

    static float clampUnit(float value);
    static std::string formatFloat(float value, int decimals);
    UIBox makeLabelCell(const std::string& text) const;
    UIBox makeValueCell(std::atomic<float>& target, float step, int decimals);
};
