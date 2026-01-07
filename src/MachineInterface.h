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
    bool disableLearning = false;
};

struct MachineNoteEvent
{
    unsigned short note = 0;
    unsigned short velocity = 0;
    unsigned short durationTicks = 0;
};

// Interface for sequencer-controlled machines (samplers, arpeggiators, etc).
class MachineInterface
{
public:
    virtual ~MachineInterface() = default;

    virtual void prepareToPlay(double sampleRate, int samplesPerBlock) = 0;
    virtual void releaseResources() = 0;
    virtual void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) = 0;

    virtual std::vector<std::vector<UIBox>> getUIBoxes(const MachineUiContext& context) = 0;

    virtual bool handleIncomingNote(unsigned short note,
                                    unsigned short velocity,
                                    unsigned short durationTicks,
                                    MachineNoteEvent& outEvent) = 0;

    virtual void applyLearnedNote(int midiNote) { (void)midiNote; }
    virtual void addEntry() {}
    virtual void removeEntry(int entryIndex) { (void)entryIndex; }

    virtual void getStateInformation(juce::MemoryBlock& destData) = 0;
    virtual void setStateInformation(const void* data, int sizeInBytes) = 0;
};
