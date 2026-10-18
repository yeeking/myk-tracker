#pragma once

#include <JuceHeader.h>
#include <vector>

/** A decoded sampler asset carried by a portable tracker file. */
struct PortableSampleAsset
{
    int stackId = 0;
    int playerId = 0;
    juce::String filePath;
    juce::String fileName;
    double sampleRate = 44100.0;
    juce::AudioBuffer<float> audio;
};

struct PortableTrackerDocument
{
    juce::MemoryBlock state;
    std::vector<PortableSampleAsset> samples;
};

/** Writes/reads the explicit-file-only portable .myktracker ZIP container. */
namespace portable_tracker_state
{
bool write(const juce::File& destination, const juce::MemoryBlock& state,
           const std::vector<PortableSampleAsset>& samples, juce::String& error);
bool read(const juce::File& source, PortableTrackerDocument& document, juce::String& error);
bool isPortableFile(const juce::File& source);
}
