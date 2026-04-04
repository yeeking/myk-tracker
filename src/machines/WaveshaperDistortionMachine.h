#pragma once

#include <mutex>
#include <string>
#include <vector>

#include <JuceHeader.h>

#include "AudioEffectMachine.h"

class WaveshaperDistortionMachine final : public AudioEffectMachine
{
public:
    /** Creates the distortion machine with its default drive, tone, and mix. */
    WaveshaperDistortionMachine() = default;

    /** Prepares sample-rate-dependent processing state. */
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    /** Releases processing state back to its idle defaults. */
    void releaseResources() override;
    /** Builds the machine-editor UI cells for the distortion controls. */
    std::vector<std::vector<UIBox>> getUIBoxes(const MachineUiContext& context) override;
    /** Processes the stack audio buffer through the waveshaper. */
    void processAudioBuffer(juce::AudioBuffer<float>& buffer) override;
    /** Serialises the current distortion settings. */
    void getStateInformation(juce::MemoryBlock& destData) override;
    /** Restores the current distortion settings. */
    void setStateInformation(const void* data, int sizeInBytes) override;

private:
    /** Minimum allowed drive multiplier. */
    static constexpr float kMinDrive = 1.0f;
    /** Maximum allowed drive multiplier. */
    static constexpr float kMaxDrive = 50.0f;

    /** Protects parameters shared between the UI and audio threads. */
    mutable std::mutex stateMutex;
    /** Current sample rate used for tone filtering. */
    double currentSampleRate = 44100.0;
    /** Input gain applied before soft clipping. */
    float drive = 4.0f;
    /** Tone blend between darker and brighter shaped output. */
    float tone = 0.6f;
    /** Wet/dry mix control. */
    float mix = 1.0f;
    /** Final output gain after shaping. */
    float output = 0.8f;
    /** Per-channel filter state for the tone stage. */
    std::array<float, 2> toneState {};

    /** Formats floating point values for compact tracker display. */
    static std::string formatFloat(float value, int decimals);
    /** Applies the distortion transfer function. */
    static float softClip(float input);
    /** Resets the tone filter state for both channels. */
    void resetToneState();
};
