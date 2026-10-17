#pragma once

#include <array>
#include <cstddef>

enum class ModulationSource : std::size_t
{
    lfo1, lfo2, lfo3, lfo4,
    clock1, clock2, clock3,
    /** One-sample pulse when a note reaches the stack. */
    noteTrigger,
    /** Persistent last note, interpreted destination-dependently. */
    lastNote,
    count
};

enum class ModulationDestination : std::size_t
{
    waveFrequency, waveSubFrequency, wavePosition, waveSubPosition,
    filterCutoff, filterResonance, filterAttack,
    /** LFO-rate targets; trigger sources reset phase when routed here. */
    lfo1Frequency, lfo2Frequency, lfo3Frequency, lfo4Frequency,
    samplerPosition, count
};

constexpr std::size_t kModulationSourceCount = static_cast<std::size_t>(ModulationSource::count);
constexpr std::size_t kModulationDestinationCount = static_cast<std::size_t>(ModulationDestination::count);

struct ModulationBlockView
{
    std::array<const float*, kModulationDestinationCount> values {};
    int numSamples = 0;

    float get(ModulationDestination destination, int sample) const noexcept
    {
        const auto index = static_cast<std::size_t>(destination);
        return sample >= 0 && sample < numSamples && values[index] != nullptr ? values[index][sample] : 0.0f;
    }
};

struct ModulationClockTick
{
    int sampleOffset = 0;
    long tick = 0;
};
