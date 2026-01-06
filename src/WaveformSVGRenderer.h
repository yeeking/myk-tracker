#pragma once

#include <JuceHeader.h>

// Helper for generating simple waveform SVGs for UI display.
struct WaveformSVGRenderer
{
    static juce::String generateBlankWaveformSVG()
    {
        return "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"320\" height=\"64\"></svg>";
    }

    static juce::String generateWaveformSVG(const juce::AudioBuffer<float>& buffer, int width)
    {
        juce::ignoreUnused(buffer, width);
        return generateBlankWaveformSVG();
    }
};
