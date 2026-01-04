#pragma once

#include <JuceHeader.h>
#include <vector>

// A lightweight sample player placeholder that will later own audio data.
class SamplePlayer
{
public:
    struct State
    {
        int id {};
        int midiLow { 36 };   // default C2
        int midiHigh { 60 };  // default C4
        float gain { 1.0f };
        bool isPlaying { false };
        juce::String status { "empty" };
        juce::String fileName;
        juce::String filePath;
        juce::String waveformSVG;
    };

    explicit SamplePlayer (int newId);

    int getId() const noexcept { return state.id; }

    void setMidiRange (int low, int high) noexcept;
    void setGain (float g) noexcept;
    void setFilePathAndStatus (const juce::String& path, const juce::String& statusLabel, const juce::String& displayName = {});
    State getState() const noexcept;

    bool acceptsNote (int midiNote) const noexcept;
    void trigger();
    void triggerNote (int midiNote);
    float getNextSampleForChannel (int channel);

    bool setLoadedBuffer (juce::AudioBuffer<float>&& newBuffer, const juce::String& name);
    void markError (const juce::String& path, const juce::String& message);
    juce::String getWaveformSVG() const noexcept { return state.waveformSVG; }
    const std::vector<float>& getWaveformPoints() const noexcept { return waveformPoints; }
    void beginBlock() noexcept;
    void endBlock() noexcept;
    float getLastVuDb() const noexcept { return lastVuDb; }

private:
    void pushVuSample (float sample) noexcept;

    State state;
    juce::AudioBuffer<float> sampleBuffer;
    int playHead { 0 };
    std::vector<float> vuBuffer;
    int vuWritePos { 0 };
    float vuSum { 0.0f };
    int vuBufferSize { 1024 };
    float lastVuDb { -60.0f };
    std::vector<float> waveformPoints;
};
