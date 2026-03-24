#pragma once

#include <mutex>
#include <vector>

#include "MachineInterface.h"

class PolyArpeggiatorMachine final : public MachineInterface
{
public:
    /** Playback ordering modes for each read head. */
    enum class PlayMode
    {
        pingPong,
        up,
        down,
        random
    };

    /** Creates a polyphonic arpeggiator with shared note memory. */
    PolyArpeggiatorMachine();

    /** Prepares any realtime state required for playback. */
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    /** Releases any realtime playback resources. */
    void releaseResources() override;
    /** Processes block-level state; currently unused for audio generation. */
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override;

    /** Builds the machine-editor UI cells for the poly arp. */
    std::vector<std::vector<UIBox>> getUIBoxes(const MachineUiContext& context) override;

    /** Records incoming notes into the shared arp memory when enabled. */
    bool handleIncomingNote(unsigned short note,
                            unsigned short velocity,
                            unsigned short durationTicks,
                            MachineNoteEvent& outEvent) override;
    /** Advances all read heads and emits any notes due on this tick. */
    bool handleClockTickBatch(std::vector<MachineNoteEvent>& outEvents) override;
    /** Adds a new read head up to the machine limit. */
    void addEntry() override;
    /** Removes an existing read head by index. */
    void removeEntry(int entryIndex) override;
    /** Clears active playback state across all heads. */
    void allNotesOff() override;

    /** Serialises the poly arp state. */
    void getStateInformation(juce::MemoryBlock& destData) override;
    /** Restores the poly arp state. */
    void setStateInformation(const void* data, int sizeInBytes) override;

private:
    /** Stored note slot within the shared arp memory. */
    struct NoteSlot
    {
        /** MIDI note number stored in the slot. */
        int note = -1;
        /** Velocity stored for the slot. */
        int velocity = 0;
        /** Note duration in tracker ticks. */
        int durationTicks = 0;
        /** True when the slot currently holds a note. */
        bool hasNote = false;
    };

    /** Playback state for one poly-arp read head. */
    struct ReadHead
    {
        /** Number of clock ticks required per step for this head. */
        int ticksPerStep = 1;
        /** Current playback head index. */
        int playHead = -1;
        /** Current ping-pong direction. */
        int pingPongDirection = 1;
        /** Clock divider accumulator for this head. */
        int tickAccumulator = 0;
        /** Playback mode for this head. */
        PlayMode playMode = PlayMode::pingPong;
    };

    /** Maximum number of note slots in shared memory. */
    static constexpr int kMaxLength = 16;
    /** Maximum number of note columns before the UI wraps. */
    static constexpr int kMaxWidth = 8;
    /** Maximum number of simultaneous read heads. */
    static constexpr int kMaxReadHeads = 8;

    /** Active visible length of the shared note memory. */
    int length = 8;
    /** True when incoming notes should write into shared memory. */
    bool recordEnabled = false;
    /** Write head used while recording notes. */
    int recordHead = 0;
    /** Number of active read heads. */
    int readHeadCount = 2;
    /** Shared note memory. */
    std::vector<NoteSlot> slots;
    /** Playback state for each read head. */
    std::vector<ReadHead> readHeads;
    /** Protects poly-arp state shared between UI and audio threads. */
    mutable std::mutex stateMutex;

    /** Clamps all editable state into valid ranges. */
    void clampState();
    /** Resets playback state for all read heads. */
    void resetReadHeads();
    /** Counts how many note slots currently contain a note. */
    int countActiveSlots() const;
    /** Sorts active note slots according to ordered playback modes. */
    void sortActiveSlots();
    /** Returns true when any read head requires ordered note layout. */
    bool anyOrderedHeadActive() const;
    /** Advances a read head and returns the next slot index. */
    int advancePlayHead(ReadHead& head);
    /** Returns a random playable slot index. */
    int getRandomPlayableIndex() const;
    /** Returns the UI label for a play mode. */
    static const char* formatPlayMode(PlayMode mode);
    /** Formats a MIDI note for compact tracker display. */
    static std::string formatNote(int midiNote);
    /** Returns a stable fill colour for a read head index. */
    static std::uint32_t getHeadFillColour(std::size_t headIndex);
};
