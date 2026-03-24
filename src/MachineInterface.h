#pragma once

#include <vector>

#include "UIBox.h"

namespace juce
{
template <typename T>
class AudioBuffer;
class MidiBuffer;
class MemoryBlock;
class var;
}

struct MachineUiContext
{
    /** True when the machine should avoid entering note-learn style modes. */
    bool disableLearning = false;
};

struct MachineNoteEvent
{
    /** MIDI note number emitted by the machine. */
    unsigned short note = 0;
    /** MIDI velocity emitted by the machine. */
    unsigned short velocity = 0;
    /** Note duration in tracker ticks. */
    unsigned short durationTicks = 0;
};

// Interface for sequencer-controlled machines (samplers, arpeggiators, etc).
class MachineInterface
{
public:
    /** Virtual destructor for machine polymorphism. */
    virtual ~MachineInterface() = default;

    /** Prepares the machine for realtime playback. */
    virtual void prepareToPlay(double sampleRate, int samplesPerBlock) = 0;
    /** Releases any realtime playback resources. */
    virtual void releaseResources() = 0;
    /** Processes audio and/or MIDI for the current block. */
    virtual void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) = 0;

    /** Builds the machine editor cells shown on the machine page. */
    virtual std::vector<std::vector<UIBox>> getUIBoxes(const MachineUiContext& context) = 0;

    /** Handles an incoming sequencer note event. */
    virtual bool handleIncomingNote(unsigned short note,
                                    unsigned short velocity,
                                    unsigned short durationTicks,
                                    MachineNoteEvent& outEvent) = 0;
    /** Handles a single tracker clock tick and optionally emits a note event. */
    virtual bool handleClockTick(MachineNoteEvent& outEvent)
    {
        (void)outEvent;
        return false;
    }
    /** Handles a clock tick that may emit multiple note events. */
    virtual bool handleClockTickBatch(std::vector<MachineNoteEvent>& outEvents)
    {
        MachineNoteEvent outEvent;
        if (!handleClockTick(outEvent))
            return false;
        outEvents.push_back(outEvent);
        return true;
    }

    /** Updates the duration of a tracker tick in seconds. */
    virtual void setSecondsPerTick(double secondsPerTick) { (void)secondsPerTick; }
    /** Silences any currently playing notes or tails. */
    virtual void allNotesOff() {}
    /** Applies a learned MIDI note value to the current UI target. */
    virtual void applyLearnedNote(int midiNote) { (void)midiNote; }
    /** Adds a machine-specific entry, such as a sampler slot or read head. */
    virtual void addEntry() {}
    /** Removes a machine-specific entry by index. */
    virtual void removeEntry(int entryIndex) { (void)entryIndex; }
    /** Dismisses transient UI such as file browsers or popups. */
    virtual bool dismissTransientUi() { return false; }
    /** Notifies the machine that the editor cursor moved within its UI. */
    virtual void onCursorMoved(int row, int col) { (void)row; (void)col; }
    /** Lets the machine consume left-navigation when needed. */
    virtual bool navigateLeft() { return false; }
    /** Passes printable text input to the machine. */
    virtual bool handleTextInput(char character) { (void)character; return false; }
    /** Passes backspace text input to the machine. */
    virtual bool handleTextBackspace() { return false; }
    /** Indicates that the machine wants exclusive keyboard input. */
    virtual bool wantsExclusiveKeyboardInput() const { return false; }
    /** Requests a preferred cursor row after the machine UI changes. */
    virtual int consumePreferredCursorRow(const std::vector<std::vector<UIBox>>& cells)
    {
        (void)cells;
        return -1;
    }

    /** Serialises the machine state. */
    virtual void getStateInformation(juce::MemoryBlock& destData) = 0;
    /** Restores the machine state. */
    virtual void setStateInformation(const void* data, int sizeInBytes) = 0;
};
