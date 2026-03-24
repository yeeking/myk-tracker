#pragma once

#include <mutex>
#include <vector>

#include "MachineInterface.h"

class PolyArpeggiatorMachine final : public MachineInterface
{
public:
    enum class PlayMode
    {
        pingPong,
        up,
        down,
        random
    };

    PolyArpeggiatorMachine();

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override;

    std::vector<std::vector<UIBox>> getUIBoxes(const MachineUiContext& context) override;

    bool handleIncomingNote(unsigned short note,
                            unsigned short velocity,
                            unsigned short durationTicks,
                            MachineNoteEvent& outEvent) override;
    bool handleClockTickBatch(std::vector<MachineNoteEvent>& outEvents) override;
    void addEntry() override;
    void removeEntry(int entryIndex) override;
    void allNotesOff() override;

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

    struct ReadHead
    {
        int ticksPerStep = 1;
        int playHead = -1;
        int pingPongDirection = 1;
        int tickAccumulator = 0;
        PlayMode playMode = PlayMode::pingPong;
    };

    static constexpr int kMaxLength = 16;
    static constexpr int kMaxWidth = 8;
    static constexpr int kMaxReadHeads = 8;

    int length = 8;
    bool recordEnabled = false;
    int recordHead = 0;
    int readHeadCount = 2;
    std::vector<NoteSlot> slots;
    std::vector<ReadHead> readHeads;
    mutable std::mutex stateMutex;

    void clampState();
    void resetReadHeads();
    int countActiveSlots() const;
    void sortActiveSlots();
    bool anyOrderedHeadActive() const;
    int advancePlayHead(ReadHead& head);
    int getRandomPlayableIndex() const;
    static const char* formatPlayMode(PlayMode mode);
    static std::string formatNote(int midiNote);
    static std::uint32_t getHeadFillColour(std::size_t headIndex);
};
