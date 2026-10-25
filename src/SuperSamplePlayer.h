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
        /** Inclusive MIDI velocity range accepted by this player. */
        int velocityLow { 0 };
        int velocityHigh { 127 };
        /** Whether incoming velocity also scales this player's output gain. */
        bool velocityScalesGain { true };
        /** Static gain applied to playback from this player. */
        float gain { 1.0f };
        /** Fixed playback offset in semitones, clamped to -36 through +36. */
        int coarseTuneSemitones { 0 };
        /** Fixed playback offset in cents, clamped to -99 through +99. */
        int fineTuneCents { 0 };
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
    };

    /** Creates a sample player with a fixed player id. */
    explicit SuperSamplePlayer (int newId);

    /** Returns the player id. */
    int getId() const noexcept { return state.id; }

    /** Sets the MIDI note range that will trigger this player. */
    void setMidiRange (int low, int high) noexcept;
    /** Sets the inclusive velocity range that will trigger this player. */
    void setVelocityRange (int low, int high) noexcept;
    /** Selects whether trigger velocity is also applied as output gain. */
    void setVelocityScalesGain (bool enabled) noexcept;
    /** Sets the static gain multiplier for this player. */
    void setGain (float g) noexcept;
    /** Sets the fixed coarse/fine playback tuning for this player. */
    void setTuning (int coarseSemitones, int fineCents) noexcept;
    /** Updates the file metadata and status shown in the UI. */
    void setFilePathAndStatus (const juce::String& path, const juce::String& statusLabel, const juce::String& displayName = {});
    /** Returns the current UI-facing player state. */
    State getState() const noexcept;

    /** Returns true when the player should respond to the given MIDI note. */
    bool acceptsNote (int midiNote) const noexcept;
    /** Returns true when both note and velocity select this player. */
    bool acceptsTrigger (int midiNote, int velocity) const noexcept;
    /** Starts playback from the start of the buffer. */
    void trigger();
    /** Starts playback and applies the note velocity as gain. */
    void triggerNote (int midiNote, int velocity);
    /** Stops playback immediately. */
    void stop() noexcept;
    /** Starts the short click-free performance-gate release. */
    void gateRelease() noexcept;
    /** Updates the player for the current output sample rate. */
    void prepareToPlay (double sampleRate, int samplesPerBlock);
    /** Adds the next segment of playback into the destination buffer. */
    void renderToBuffer (juce::AudioBuffer<float>& buffer, int startSample, int numSamples, const float* positionModulation = nullptr) noexcept;

    /** Replaces the loaded sample buffer and metadata. */
    bool setLoadedBuffer (juce::AudioBuffer<float>&& newBuffer, const juce::String& name, double newSourceSampleRate);
    /** Copies the loaded decoded source for a portable state archive. */
    bool copyLoadedBuffer (juce::AudioBuffer<float>& destination, double& sampleRate) const;
    /** Marks the player as having failed to load a sample. */
    void markError (const juce::String& path, const juce::String& message);
    /** Returns the cached waveform SVG string. */
    juce::String getWaveformSVG() const noexcept { return waveformSVG; }
    /** Returns the cached waveform points used by the UI. */
    const std::vector<float>& getWaveformPoints() const noexcept { return waveformPoints; }
    /** Starts VU accumulation for the next audio block. */
    void beginBlock() noexcept;
    /** Finishes VU accumulation for the current audio block. */
    void endBlock() noexcept;
    /** Returns the last computed block VU value in decibels. */
    float getLastVuDb() const noexcept { return lastVuDb; }
    /** Returns the cached source samples consumed per output sample. */
    double getPlaybackRatio() const noexcept { return playbackRatio; }

private:
    /** Adds a sample to the running VU calculation. */
    void pushVuSample (float sample) noexcept;
    /** Resets playback and interpolation state after discontinuities. */
    void resetPlaybackState (bool keepPlaying = false) noexcept;
    /** Recomputes the source-to-output playback ratio. */
    void updatePlaybackRatio() noexcept;

    /** UI-facing player state. */
    State state;
    /** Cached waveform preview SVG string, kept out of `State` to avoid copying it into UI snapshots. */
    juce::String waveformSVG;
    /** Loaded sample audio buffer. */
    juce::AudioBuffer<float> sampleBuffer;
    /** Original sample rate of the loaded file. */
    double sourceSampleRate { 44100.0 };
    /** Current device/output sample rate. */
    double outputSampleRate { 44100.0 };
    /** Number of source samples consumed per output sample. */
    double playbackRatio { 1.0 };
    /** Fractional playback position in source-sample units. */
    double fractionalPlaybackPosition { 0.0 };
    /** Current integer source read position. */
    int sourceReadIndex { 0 };
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
    int gateFadeSamplesRemaining { 0 };
    int gateFadeSamplesTotal { 0 };
    /** Cached waveform points for UI rendering. */
    std::vector<float> waveformPoints;
};
