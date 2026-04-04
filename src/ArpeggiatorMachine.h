#pragma once

#include <functional>
#include <mutex>
#include <vector>

#include "ClockAbs.h"
#include "MachineInterface.h"

// Simple note accumulator/arpeggiator machine driven by incoming MIDI notes.
class ArpeggiatorMachine final : public MachineInterface, public ClockListener
{
public:
    /** Playback ordering modes for the arpeggiated note memory. */
    enum class PlayMode
    {
        pingPong,
        linear,
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
    /** Clears the playhead state and any currently sounding playback. */
    void resetPlayback();
    /** Receives the global quarter-beat clock. */
    void tick(int quarterBeat) override;
    /** Resets playback counters to the start of the bar. */
    void reset() override;
    /** Sets the callback used to emit clocked arp notes. */
    void setClockEventCallback(std::function<void(const MachineNoteEvent&)> callback);
    /** Enables or disables note emission on clock ticks. */
    void setClockActive(bool shouldBeActive);
    bool shiftNoteAtCell(int row, int col, int semitones) override;
    bool clearCell(int row, int col) override;

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
    static constexpr int kMaxLength = 64;
    /** Maximum number of note columns before the UI wraps. */
    static constexpr int kMaxWidth = 8;
    /** Supported quarter-beat divisors for arp stepping. */
    static constexpr int kQuarterBeatDivisors[5] = { 1, 2, 4, 8, 16 };

    /** Active visible length of the arp memory. */
    int length = 8;
    /** Quarter-beat divisor used for arp stepping. */
    int quarterBeatDivisor = 1;
    /** Number of octaves cycled during playback. */
    int octaveSpan = 1;
    /** Number of received clock ticks since the last emitted arp step. */
    int ticksSinceStep = 0;
    /** True when incoming notes should overwrite arp memory. */
    bool recordEnabled = false;
    /** Write head used while recording notes into memory. */
    int recordHead = 0;
    /** Current playback head index. */
    int playHead = -1;
    /** Current ping-pong direction for playback. */
    int pingPongDirection = 1;
    /** Current octave layer used during playback. */
    int currentOctaveIndex = 0;
    /** Current arp playback mode. */
    PlayMode playMode = PlayMode::pingPong;
    /** Fixed note memory for the arp. */
    std::vector<NoteSlot> slots;
    /** Emits notes into the downstream machine stack. */
    std::function<void(const MachineNoteEvent&)> clockEventCallback;
    /** True when clock ticks should emit notes. */
    bool clockActive = false;
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
    /** Randomises the note memory order within the active length. */
    void shuffleSlots();
    /** Advances the playhead and returns the next slot index. */
    int advancePlayHead();
    /** Returns the playback note after applying the current octave span. */
    int getPlaybackNoteForSlot(int slotNote);
    /** Returns the UI label for a play mode. */
    static const char* formatPlayMode(PlayMode mode);
    /** Returns the display label for a quarter-beat divisor. */
    static const char* formatQuarterBeatDivisor(int divisor);
    /** Maps legacy ticks-per-beat values onto the new quarter-beat divisors. */
    static int mapLegacyTicksPerBeatToQuarterBeatDivisor(int ticksPerBeat);
    /** Formats a MIDI note for compact tracker display. */
    static std::string formatNote(int midiNote);
};
