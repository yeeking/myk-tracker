#pragma once

#include "MachineInterface.h"

// Base class for stack machines that transform an audio buffer in place.
class AudioEffectMachine : public MachineInterface
{
public:
    /** Virtual destructor for effect-machine polymorphism. */
    ~AudioEffectMachine() override = default;

    /** Prepares sample-rate-dependent DSP, then clears transient state. */
    void prepareToPlay(double sampleRate, int samplesPerBlock) final
    {
        prepareDsp(sampleRate, samplesPerBlock);
        clearTransientState();
    }

    /** Clears transient state when realtime resources are released. */
    void releaseResources() final
    {
        clearTransientState();
    }

    /** Forwards machine processing to the in-place audio effect path. */
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) final
    {
        juce::ignoreUnused(midi);
        processAudioBuffer(buffer);
    }

    /** Effects do not directly consume note events; they process stack audio instead. */
    bool handleIncomingNote(unsigned short note,
                            unsigned short velocity,
                            unsigned short durationTicks,
                            MachineNoteEvent& outEvent) override
    {
        juce::ignoreUnused(note, velocity, durationTicks, outEvent);
        return false;
    }

    /** Processes the stack audio buffer in place. */
    virtual void processAudioBuffer(juce::AudioBuffer<float>& buffer) = 0;

protected:
    /** Sets up sample-rate-dependent DSP without clearing transient state. */
    virtual void prepareDsp(double sampleRate, int samplesPerBlock) = 0;
};
