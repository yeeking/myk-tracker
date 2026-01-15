#pragma once

#include <mutex>
#include <vector>

#include "MachineInterface.h"

// Simple note accumulator/arpeggiator machine driven by incoming MIDI notes.
class ArpeggiatorMachine final : public MachineInterface
{
public:
    ArpeggiatorMachine();

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override;

    std::vector<std::vector<UIBox>> getUIBoxes(const MachineUiContext& context) override;

    bool handleIncomingNote(unsigned short note,
                            unsigned short velocity,
                            unsigned short durationTicks,
                            MachineNoteEvent& outEvent) override;

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

    int length = 8;
    bool recordEnabled = false;
    int recordHead = 0;
    int playHead = -1;
    std::vector<NoteSlot> slots;
    mutable std::mutex stateMutex;

    void clampLength();
    static std::string formatNote(int midiNote);
};
