#pragma once

#include <array>
#include <atomic>
#include <string>
#include <vector>

#include <JuceHeader.h>

#include "AudioEffectMachine.h"

class ChannelStripMachine final : public AudioEffectMachine
{
public:
    ChannelStripMachine();

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    std::vector<std::vector<UIBox>> getUIBoxes(const MachineUiContext& context) override;
    void processAudioBuffer(juce::AudioBuffer<float>& buffer) override;
    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

private:
    using Filter = juce::dsp::ProcessorDuplicator<
        juce::dsp::IIR::Filter<float>,
        juce::dsp::IIR::Coefficients<float>>;

    static constexpr double kStateVersion = 1.0;
    static constexpr std::size_t kMaxChannels = 2;

    static constexpr float kMinSatDriveDb = 0.0f;
    static constexpr float kMaxSatDriveDb = 24.0f;
    static constexpr float kMinSatMix = 0.0f;
    static constexpr float kMaxSatMix = 1.0f;
    static constexpr float kMinDistDriveDb = 0.0f;
    static constexpr float kMaxDistDriveDb = 18.0f;
    static constexpr float kMinDistOutputDb = -18.0f;
    static constexpr float kMaxDistOutputDb = 6.0f;
    static constexpr float kMinCompInputDb = -18.0f;
    static constexpr float kMaxCompInputDb = 18.0f;
    static constexpr float kMinCompThresholdDb = -36.0f;
    static constexpr float kMaxCompThresholdDb = 0.0f;
    static constexpr float kMinCompRatio = 1.0f;
    static constexpr float kMaxCompRatio = 20.0f;
    static constexpr float kMinCompAttackMs = 0.1f;
    static constexpr float kMaxCompAttackMs = 100.0f;
    static constexpr float kMinCompOutputDb = -18.0f;
    static constexpr float kMaxCompOutputDb = 18.0f;
    static constexpr float kMinLimiterThresholdDb = -6.0f;
    static constexpr float kMaxLimiterThresholdDb = 0.0f;
    static constexpr float kMinEqDb = -15.0f;
    static constexpr float kMaxEqDb = 15.0f;
    static constexpr float kMinMidFreqHz = 250.0f;
    static constexpr float kMaxMidFreqHz = 5000.0f;

    std::atomic<float> satDriveDb { 6.0f };
    std::atomic<float> satMix { 1.0f };
    std::atomic<float> distDriveDb { 3.0f };
    std::atomic<float> distOutputDb { 0.0f };
    std::atomic<float> compInputDb { 0.0f };
    std::atomic<float> compThresholdDb { -18.0f };
    std::atomic<float> compRatio { 4.0f };
    std::atomic<float> compAttackMs { 10.0f };
    std::atomic<float> compOutputDb { 0.0f };
    std::atomic<float> limiterThresholdDb { -1.0f };
    std::atomic<float> bassDb { 0.0f };
    std::atomic<float> midDb { 0.0f };
    std::atomic<float> midFreqHz { 1000.0f };
    std::atomic<float> trebleDb { 0.0f };

    double currentSampleRate = 44100.0;
    juce::dsp::ProcessSpec processSpec {};
    juce::dsp::ProcessSpec oversampledSpec {};

    juce::dsp::Oversampling<float> oversampling;
    juce::AudioBuffer<float> saturationDryBuffer;
    juce::dsp::Gain<float> saturatorInputGain;
    juce::dsp::WaveShaper<float> saturator;
    juce::dsp::Gain<float> distGain;
    juce::dsp::WaveShaper<float> distShaper;
    juce::dsp::Gain<float> distOutputGain;
    juce::dsp::Gain<float> compInputGain;
    juce::dsp::Compressor<float> compressor;
    juce::dsp::Gain<float> compOutputGain;
    Filter bassShelf;
    Filter midPeak;
    Filter trebleShelf;
    juce::dsp::Limiter<float> limiter;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> satMixSmoothed;

    std::atomic<bool> dspDirty { true };
    std::atomic<bool> dspResetRequested { false };

    struct ParameterSnapshot
    {
        float satDriveDb = 6.0f;
        float satMix = 1.0f;
        float distDriveDb = 3.0f;
        float distOutputDb = 0.0f;
        float compInputDb = 0.0f;
        float compThresholdDb = -18.0f;
        float compRatio = 4.0f;
        float compAttackMs = 10.0f;
        float compOutputDb = 0.0f;
        float limiterThresholdDb = -1.0f;
        float bassDb = 0.0f;
        float midDb = 0.0f;
        float midFreqHz = 1000.0f;
        float trebleDb = 0.0f;
    };

    ParameterSnapshot captureParameters() const;
    void updateDSPSettings(const ParameterSnapshot& parameters);
    void updateEQCoefficients(const ParameterSnapshot& parameters);
    void resetDSPState();

    static float softClip(float x);
    static std::string formatFloat(float value, int decimals);
    static std::string formatDb(float value, int decimals);
    static std::string formatRatio(float value);
    static std::string formatHz(float hz);
    static float clampParameter(float value, float minValue, float maxValue);

    UIBox makeLabelCell(const std::string& text) const;
    UIBox makeValueCell(std::atomic<float>& target, float step, float minValue, float maxValue, int decimals);
    UIBox makeDbCell(std::atomic<float>& target, float step, float minValue, float maxValue, int decimals);
    UIBox makeFrequencyCell(std::atomic<float>& target, float step, float minValue, float maxValue);
};
