#pragma once

#include <mutex>
#include <vector>

#include "MachineInterface.h"

// Simple note accumulator/arpeggiator machine driven by incoming MIDI notes.
class ArpeggiatorMachine final : public MachineInterface
{
public:
    enum class PlayMode
    {
        pingPong,
        up,
        down,
        random
    };

    ArpeggiatorMachine();

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override;

    std::vector<std::vector<UIBox>> getUIBoxes(const MachineUiContext& context) override;

    bool handleIncomingNote(unsigned short note,
                            unsigned short velocity,
                            unsigned short durationTicks,
                            MachineNoteEvent& outEvent) override;
    bool handleClockTick(MachineNoteEvent& outEvent) override;
    void resetPlayback();

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

private:
    struct NoteSlot
    {
        int note = -1;
        int velocity = 0;
        int durationTicks = 0;
        bool hasNote = false;
    };

    static constexpr int kMaxLength = 16;
    static constexpr int kMaxWidth = 8;
    static constexpr int kMaxTicksPerBeat = 8;

    int length = 8;
    int ticksPerBeat = kMaxTicksPerBeat;
    bool recordEnabled = false;
    int recordHead = 0;
    int playHead = -1;
    int pingPongDirection = 1;
    int tickAccumulator = 0;
    PlayMode playMode = PlayMode::pingPong;
    std::vector<NoteSlot> slots;
    mutable std::mutex stateMutex;

    void clampLength();
    void resetPlaybackState();
    int countActiveSlots() const;
    void sortActiveSlots();
    int advancePlayHead();
    int getRandomPlayableIndex() const;
    static const char* formatPlayMode(PlayMode mode);
    static std::string formatNote(int midiNote);
};
