#pragma once

#include <mutex>
#include <vector>

#include "MachineInterface.h"

// Simple note accumulator/arpeggiator machine driven by incoming MIDI notes.
class ArpeggiatorMachine final : public MachineInterface
{
public:
    /** Playback ordering modes for the arpeggiated note memory. */
    enum class PlayMode
    {
        pingPong,
        up,
        down,
        random
    };

    /** Creates an arpeggiator with default length, mode, and rate settings. */
    ArpeggiatorMachine();

    /** Prepares any realtime state required for playback. */
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    /** Releases any realtime playback resources. */
    void releaseResources() override;
    /** Processes block-level state; currently unused for audio generation. */
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override;

    /** Builds the machine-editor UI cells for the arpeggiator. */
    std::vector<std::vector<UIBox>> getUIBoxes(const MachineUiContext& context) override;

    /** Records incoming notes into the arp memory when enabled. */
    bool handleIncomingNote(unsigned short note,
                            unsigned short velocity,
                            unsigned short durationTicks,
                            MachineNoteEvent& outEvent) override;
    /** Advances the arp clock and emits the next note when due. */
    bool handleClockTick(MachineNoteEvent& outEvent) override;
    /** Clears the playhead state and any currently sounding playback. */
    void resetPlayback();

    /** Serialises the arp state. */
    void getStateInformation(juce::MemoryBlock& destData) override;
    /** Restores the arp state. */
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

    /** Maximum number of note slots in arp memory. */
    static constexpr int kMaxLength = 16;
    /** Maximum number of note columns before the UI wraps. */
    static constexpr int kMaxWidth = 8;
    /** Highest supported ticks-per-beat value. */
    static constexpr int kMaxTicksPerBeat = 8;

    /** Active visible length of the arp memory. */
    int length = 8;
    /** Number of clock ticks required per arp step. */
    int ticksPerBeat = kMaxTicksPerBeat;
    /** True when incoming notes should overwrite arp memory. */
    bool recordEnabled = false;
    /** Write head used while recording notes into memory. */
    int recordHead = 0;
    /** Current playback head index. */
    int playHead = -1;
    /** Current ping-pong direction for playback. */
    int pingPongDirection = 1;
    /** Clock divider accumulator used for step timing. */
    int tickAccumulator = 0;
    /** Current arp playback mode. */
    PlayMode playMode = PlayMode::pingPong;
    /** Fixed note memory for the arp. */
    std::vector<NoteSlot> slots;
    /** Protects arp state shared between UI and audio threads. */
    mutable std::mutex stateMutex;

    /** Clamps the visible arp length into the supported range. */
    void clampLength();
    /** Resets playback-only state without wiping recorded notes. */
    void resetPlaybackState();
    /** Counts how many note slots currently contain a note. */
    int countActiveSlots() const;
    /** Sorts active note slots according to the current mode. */
    void sortActiveSlots();
    /** Advances the playhead and returns the next slot index. */
    int advancePlayHead();
    /** Returns a random playable slot index. */
    int getRandomPlayableIndex() const;
    /** Returns the UI label for a play mode. */
    static const char* formatPlayMode(PlayMode mode);
    /** Formats a MIDI note for compact tracker display. */
    static std::string formatNote(int midiNote);
};
