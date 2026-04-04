#pragma once

#include <mutex>
#include <string>
#include <vector>

#include <JuceHeader.h>

#include "AudioEffectMachine.h"
#include "ClockAbs.h"

class DelayFxMachine final : public AudioEffectMachine, public ClockListener
{
public:
    /** Creates the delay effect with default sync timing and mix values. */
    DelayFxMachine() = default;

    /** Prepares internal buffers for realtime processing. */
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    /** Releases realtime resources and clears delay state. */
    void releaseResources() override;
    /** Builds the machine-editor UI cells for the delay controls. */
    std::vector<std::vector<UIBox>> getUIBoxes(const MachineUiContext& context) override;
    /** Applies the delay effect to the stack audio buffer. */
    void processAudioBuffer(juce::AudioBuffer<float>& buffer) override;
    /** Updates tick duration for sync-mode delay times. */
    void setSecondsPerTick(double secondsPerTick) override;
    /** Clears buffered delay audio when transport or notes are stopped. */
    void allNotesOff() override;
    /** Tracks quarter-beat bar position for synced transport state. */
    void tick(int quarterBeat) override;
    /** Clears delay state on transport resets. */
    void reset() override;
    /** Serialises the delay settings. */
    void getStateInformation(juce::MemoryBlock& destData) override;
    /** Restores the delay settings from serialised state. */
    void setStateInformation(const void* data, int sizeInBytes) override;

private:
    /** Selects between tracker-synchronised delay time and free milliseconds. */
    enum class DelayMode
    {
        sync = 0,
        milliseconds
    };

    /** Protects delay state shared between UI and audio threads. */
    mutable std::mutex stateMutex;
    /** Current host/sample playback rate. */
    double currentSampleRate = 44100.0;
    /** Current tracker tick duration used for sync mode. */
    double currentSecondsPerTick = 60.0 / (120.0 * 8.0);
    /** Active delay timing mode. */
    DelayMode mode = DelayMode::sync;
    /** Delay length in tracker ticks when sync mode is selected. */
    int syncTicks = 8;
    /** Delay length in milliseconds when free-time mode is selected. */
    float delayMs = 250.0f;
    /** Feedback amount fed back into the delay buffer. */
    float feedback = 0.35f;
    /** Wet/dry balance for the effect output. */
    float mix = 0.3f;
    /** Circular audio buffer that stores delayed samples. */
    juce::AudioBuffer<float> delayBuffer;
    /** Current write head position into the delay buffer. */
    int writePosition = 0;
    /** Last published quarter-beat position within the bar. */
    int currentQuarterBeat = 0;

    /** Maximum allocated delay time in seconds. */
    static constexpr int kMaxDelaySeconds = 4;

    /** Resizes the circular delay buffer for the current sample rate. */
    void resizeDelayBuffer();
    /** Clears the circular delay buffer and resets the write head. */
    void clearDelayBuffer();
    /** Returns the active delay time in samples. */
    int getDelaySamples() const;
    /** Formats floating point values for compact tracker display. */
    static std::string formatFloat(float value, int decimals);
    /** Returns the display label for the current delay mode. */
    static const char* getModeName(DelayMode mode);
};
