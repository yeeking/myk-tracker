#include <JuceHeader.h>

#include "SuperSamplePlayer.h"
#include "SuperSamplerProcessor.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <utility>

namespace
{
void require(bool condition, const char* message)
{
    if (!condition)
    {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

void requireNear(double actual, double expected, double tolerance, const char* message)
{
    require(std::abs(actual - expected) <= tolerance, message);
}

juce::AudioBuffer<float> makeRamp(int samples)
{
    juce::AudioBuffer<float> result(1, samples);
    for (int sample = 0; sample < samples; ++sample)
        result.setSample(0, sample, static_cast<float>(sample) / 1000.0f);
    return result;
}

std::pair<int, int> firstPlayerTuning(const SuperSamplerProcessor& sampler)
{
    const auto state = sampler.getSamplerState();
    const auto players = state.getProperty("players", juce::var());
    const auto* array = players.getArray();
    require(array != nullptr && !array->isEmpty(), "sampler state contains a player");
    const auto* player = array->getReference(0).getDynamicObject();
    require(player != nullptr, "sampler player state is an object");
    return {
        static_cast<int>(player->getProperty("coarseTuneSemitones")),
        static_cast<int>(player->getProperty("fineTuneCents"))
    };
}

juce::MemoryBlock serialiseValueTree(const juce::ValueTree& tree)
{
    juce::MemoryBlock data;
    juce::MemoryOutputStream stream(data, false);
    tree.writeToStream(stream);
    return data;
}
}

int main()
{
    SuperSamplePlayer player(1);
    player.prepareToPlay(44100.0, 16);
    require(player.setLoadedBuffer(makeRamp(128), "ramp", 44100.0), "a ramp sample loads");

    player.setTuning(12, 0);
    requireNear(player.getPlaybackRatio(), 2.0, 1.0e-12, "+12 semitones doubles playback speed");
    player.trigger();
    juce::AudioBuffer<float> output(1, 4);
    output.clear();
    player.renderToBuffer(output, 0, output.getNumSamples());
    requireNear(output.getSample(0, 1), 0.002, 1.0e-6, "+12 advances by two source samples per output sample");

    player.setTuning(-12, 0);
    requireNear(player.getPlaybackRatio(), 0.5, 1.0e-12, "-12 semitones halves playback speed");
    player.trigger();
    output.clear();
    player.renderToBuffer(output, 0, output.getNumSamples());
    requireNear(output.getSample(0, 1), 0.0005, 1.0e-6, "-12 uses interpolated half-speed playback");

    player.setTuning(0, 99);
    const double plus99 = std::exp2(0.99 / 12.0);
    requireNear(player.getPlaybackRatio(), plus99, 1.0e-12, "+99 cents uses the expected multiplier");
    player.setTuning(1, 0);
    require(player.getPlaybackRatio() > plus99, "+99 cents remains below one coarse semitone");

    player.setTuning(999, -999);
    const auto clampedState = player.getState();
    require(clampedState.coarseTuneSemitones == 36, "coarse tuning clamps at +36");
    require(clampedState.fineTuneCents == -99, "fine tuning clamps at -99");

    player.prepareToPlay(44100.0, 16);
    require(player.setLoadedBuffer(makeRamp(128), "ramp", 22050.0), "a differently rated ramp sample loads");
    player.setTuning(12, 0);
    requireNear(player.getPlaybackRatio(), 1.0, 1.0e-12, "tuning multiplies source/output sample-rate conversion");

    SuperSamplerProcessor saved;
    const int savedPlayerId = saved.addSamplePlayerForControl(12, 24);
    saved.setTuningFromUI(savedPlayerId, -24, 75);
    juce::MemoryBlock savedState;
    saved.getStateInformation(savedState);

    SuperSamplerProcessor restored;
    restored.setStateInformation(savedState.getData(), static_cast<int>(savedState.getSize()));
    const auto restoredTuning = firstPlayerTuning(restored);
    require(restoredTuning.first == -24,
            "coarse tuning survives state round-trip");
    require(restoredTuning.second == 75,
            "fine tuning survives state round-trip");

    juce::ValueTree legacyPlugin("PluginState");
    juce::ValueTree legacySampler("SamplerState");
    juce::ValueTree legacyPlayer("Player");
    legacyPlayer.setProperty("id", 7, nullptr);
    legacyPlayer.setProperty("midiLow", 36, nullptr);
    legacyPlayer.setProperty("midiHigh", 60, nullptr);
    legacyPlayer.setProperty("gain", 1.0f, nullptr);
    legacyPlayer.setProperty("filePath", {}, nullptr);
    legacySampler.addChild(legacyPlayer, -1, nullptr);
    legacyPlugin.addChild(legacySampler, -1, nullptr);

    SuperSamplerProcessor legacyRestored;
    const auto legacyState = serialiseValueTree(legacyPlugin);
    legacyRestored.setStateInformation(legacyState.getData(), static_cast<int>(legacyState.getSize()));
    const auto legacyTuning = firstPlayerTuning(legacyRestored);
    require(legacyTuning.first == 0,
            "missing coarse tuning defaults to zero");
    require(legacyTuning.second == 0,
            "missing fine tuning defaults to zero");

    juce::ValueTree malformedPlugin("PluginState");
    juce::ValueTree malformedSampler("SamplerState");
    juce::ValueTree malformedPlayer("Player");
    malformedPlayer.setProperty("id", 8, nullptr);
    malformedPlayer.setProperty("midiLow", 36, nullptr);
    malformedPlayer.setProperty("midiHigh", 60, nullptr);
    malformedPlayer.setProperty("gain", 1.0f, nullptr);
    malformedPlayer.setProperty("coarseTuneSemitones", 999, nullptr);
    malformedPlayer.setProperty("fineTuneCents", -999, nullptr);
    malformedSampler.addChild(malformedPlayer, -1, nullptr);
    malformedPlugin.addChild(malformedSampler, -1, nullptr);

    SuperSamplerProcessor malformedRestored;
    const auto malformedState = serialiseValueTree(malformedPlugin);
    malformedRestored.setStateInformation(malformedState.getData(), static_cast<int>(malformedState.getSize()));
    const auto malformedTuning = firstPlayerTuning(malformedRestored);
    require(malformedTuning.first == 36 && malformedTuning.second == -99,
            "restored tuning values clamp to the supported range");

    const auto cells = restored.getUIBoxes({});
    require(cells.size() == 8, "sampler UI exposes eight columns");
    require(cells[5].size() > 1 && cells[6].size() > 1, "sampler UI exposes coarse and fine controls");
    require(cells[5][1].onAdjust != nullptr && cells[6][1].onInsert != nullptr,
            "sampler tuning cells support adjustment and numeric entry");
    cells[5][1].onInsert(999.0);
    auto uiTuning = firstPlayerTuning(restored);
    require(uiTuning.first == 36 && uiTuning.second == 75,
            "coarse numeric input clamps while preserving fine tuning");
    const auto refreshedCells = restored.getUIBoxes({});
    refreshedCells[6][1].onInsert(-999.0);
    uiTuning = firstPlayerTuning(restored);
    require(uiTuning.first == 36 && uiTuning.second == -99,
            "fine numeric input clamps while preserving coarse tuning");

    return 0;
}
