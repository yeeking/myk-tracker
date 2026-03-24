#pragma once

#include <JuceHeader.h>
#include <vector>

// A lightweight sample player placeholder that will later own audio data.
class SuperSamplePlayer
{
public:
    /** Runtime state exposed to the sampler UI. */
    struct State
    {
        /** Unique player identifier within its sampler. */
        int id {};
        /** Lowest MIDI note accepted by this player. */
        int midiLow { 36 };   // default C2
        /** Highest MIDI note accepted by this player. */
        int midiHigh { 60 };  // default C4
        /** Static gain applied to playback from this player. */
        float gain { 1.0f };
        /** True while the player is actively playing its buffer. */
        bool isPlaying { false };
        /** Most recent block VU reading in decibels. */
        float vuDb { -60.0f };
        /** Short status string shown in the UI. */
        juce::String status { "empty" };
        /** Display name of the loaded sample file. */
        juce::String fileName;
        /** Full path to the loaded sample file. */
        juce::String filePath;
        /** Cached waveform preview SVG string. */
        juce::String waveformSVG;
    };

    /** Creates a sample player with a fixed player id. */
    explicit SuperSamplePlayer (int newId);

    /** Returns the player id. */
    int getId() const noexcept { return state.id; }

    /** Sets the MIDI note range that will trigger this player. */
    void setMidiRange (int low, int high) noexcept;
    /** Sets the static gain multiplier for this player. */
    void setGain (float g) noexcept;
    /** Updates the file metadata and status shown in the UI. */
    void setFilePathAndStatus (const juce::String& path, const juce::String& statusLabel, const juce::String& displayName = {});
    /** Returns the current UI-facing player state. */
    State getState() const noexcept;

    /** Returns true when the player should respond to the given MIDI note. */
    bool acceptsNote (int midiNote) const noexcept;
    /** Starts playback from the start of the buffer. */
    void trigger();
    /** Starts playback and applies the note velocity as gain. */
    void triggerNote (int midiNote, int velocity);
    /** Stops playback immediately. */
    void stop() noexcept;
    /** Reads the next output sample for the requested channel. */
    float getNextSampleForChannel (int channel) const;
    /** Advances playback by one sample frame. */
    void advancePlaybackFrame() noexcept;

    /** Replaces the loaded sample buffer and metadata. */
    bool setLoadedBuffer (juce::AudioBuffer<float>&& newBuffer, const juce::String& name);
    /** Marks the player as having failed to load a sample. */
    void markError (const juce::String& path, const juce::String& message);
    /** Returns the cached waveform SVG string. */
    juce::String getWaveformSVG() const noexcept { return state.waveformSVG; }
    /** Returns the cached waveform points used by the UI. */
    const std::vector<float>& getWaveformPoints() const noexcept { return waveformPoints; }
    /** Starts VU accumulation for the next audio block. */
    void beginBlock() noexcept;
    /** Finishes VU accumulation for the current audio block. */
    void endBlock() noexcept;
    /** Returns the last computed block VU value in decibels. */
    float getLastVuDb() const noexcept { return lastVuDb; }

private:
    /** Adds a sample to the running VU calculation. */
    void pushVuSample (float sample) noexcept;

    /** UI-facing player state. */
    State state;
    /** Loaded sample audio buffer. */
    juce::AudioBuffer<float> sampleBuffer;
    /** Current playback frame index into the sample buffer. */
    int playHead { 0 };
    /** Rolling VU analysis window. */
    std::vector<float> vuBuffer;
    /** Write position into the rolling VU window. */
    int vuWritePos { 0 };
    /** Running sum of squared VU samples. */
    float vuSum { 0.0f };
    /** Number of frames retained for VU analysis. */
    int vuBufferSize { 1024 };
    /** Last reported block VU value in decibels. */
    float lastVuDb { -60.0f };
    /** Velocity-derived gain applied on the next trigger. */
    float velocityGain { 1.0f };
    /** Cached waveform points for UI rendering. */
    std::vector<float> waveformPoints;
};
