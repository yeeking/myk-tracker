#include <JuceHeader.h>
#include <cmath>
#include <cstdlib>
#include <iostream>

#include "machines/ModulationMatrixMachine.h"
#include "machines/DelayFxMachine.h"

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
}

int main()
{
    ModulationMatrixMachine matrix;
    matrix.setInstalledDestinations(1u | 2u | 4u | 8u);
    matrix.prepareToPlay(48000.0, 64);
    auto cells = matrix.getUIBoxes({});

    require(cells.size() == 8 && cells[0].size() == 10, "matrix has four source and four destination columns");
    require(cells[2][1].kind == UIBox::Kind::TrackerCell && !cells[2][1].isDisabled && cells[2][1].onAdjust,
            "LFO coarse rate cell is cursor-landable and adjustable");
    require(cells[4][1].kind == UIBox::Kind::TrackerCell && !cells[4][1].isDisabled && cells[4][1].onAdjust && cells[4][1].onInsert,
            "route cell is cursor-landable and supports bracket and numeric editing");

    cells[2][1].onAdjust(1);
    cells = matrix.getUIBoxes({});
    require(cells[2][1].text == "1", "bracket adjustment changes LFO coarse rate");
    cells[4][1].onInsert(75.0);
    cells = matrix.getUIBoxes({});
    require(cells[4][1].text == "75", "numeric insertion changes route depth");
    require(matrix.clearCell(1, 4), "Backspace-style clear accepts a matrix route cell");
    cells = matrix.getUIBoxes({});
    require(cells[4][1].text == "0", "Backspace-style clear resets a matrix send to zero");
    cells[4][1].onInsert(75.0);
    require(matrix.navigateRight(), "right navigation scrolls the destination window");
    require(matrix.navigateLeft(), "left navigation scrolls the destination window back");

    // Coarse zero is the deliberately slow range: Fine 1 must make a
    // roughly 0.01 Hz LFO rather than leaving the oscillator stopped.
    ModulationMatrixMachine slowLfo;
    slowLfo.setInstalledDestinations(1u);
    slowLfo.prepareToPlay(48000.0, 48000);
    auto slowCells = slowLfo.getUIBoxes({});
    slowCells[3][1].onInsert(1.0);   // LFO1 FINE, with CRS still zero.
    slowCells[4][1].onInsert(100.0); // LFO1 -> W F1.
    slowLfo.processModulationBlock(48000, true, false, nullptr, 0, {});
    const auto& slowView = slowLfo.getBlockView();
    const float oneSecondValue = slowView.get(ModulationDestination::waveFrequency, 47999);
    require(oneSecondValue > 0.05f && oneSecondValue < 0.08f,
            "CRS 0 / FINE 1 produces an approximately 0.01 Hz LFO");

    // PAN is a bipolar final-stack destination: a full-depth LFO route must
    // retain the source's complete -1..+1 excursion for left-to-right panning.
    while (matrix.navigateRight()) {}
    cells = matrix.getUIBoxes({});
    require(cells[7][0].text == "PAN", "PAN is the final always-visible matrix destination");
    cells[7][2].onInsert(100.0); // LFO2 -> PAN
    cells[2][2].onInsert(127.0);
    matrix.processModulationBlock(256, true, false, nullptr, 0, {});
    const auto& panView = matrix.getBlockView();
    float panLow = 1.0f, panHigh = -1.0f;
    for (int sample = 0; sample < panView.numSamples; ++sample)
    {
        const float value = panView.get(ModulationDestination::stackPan, sample);
        panLow = std::min(panLow, value);
        panHigh = std::max(panHigh, value);
    }
    require(panLow < -0.5f && panHigh > 0.5f,
            "full-depth LFO PAN routing preserves bipolar left-to-right movement");
    while (matrix.navigateLeft()) {}
    cells = matrix.getUIBoxes({});
    cells[4][1].onInsert(0.0); // isolate the following NOTE pulse assertion

    // NOTE occupies the former CLK4 row and is a one-sample impulse, even for a chord.
    cells = matrix.getUIBoxes({});
    require(cells[0][5].text == "NOTE" && cells[0][6].text == "CLK1"
                && cells[0][8].text == "CLK3" && cells[0][9].text == "PITCH",
            "source layout contains NOTE, three clocks, and PITCH");
    cells[4][5].onInsert(100.0); // NOTE -> W F1
    std::vector<MachineScheduledNote> chord {
        { 3, 60, 100, 1 }, { 3, 64, 100, 1 }
    };
    matrix.processModulationBlock(16, true, false, nullptr, 0, chord);
    const auto& view = matrix.getBlockView();
    require(std::abs(view.get(ModulationDestination::waveFrequency, 2)) < 0.0001f
                && view.get(ModulationDestination::waveFrequency, 3) > 0.99f
                && std::abs(view.get(ModulationDestination::waveFrequency, 4)) < 0.0001f,
            "NOTE creates one sample-accurate pulse for a chord");

    // The former phase columns are now LFO-frequency destinations; trigger routes are binary.
    for (int i = 0; i < 4; ++i) require(matrix.navigateRight(), "destination window reaches LFO rate destinations");
    cells = matrix.getUIBoxes({});
    require(cells[7][0].text == "L1 F", "LFO rate destination replaces phase destination");
    cells[7][5].onInsert(-50.0);
    cells = matrix.getUIBoxes({});
    require(cells[7][5].text == "1", "NOTE to LFO rate is canonical binary reset routing");

    juce::MemoryBlock state;
    matrix.getStateInformation(state);
    ModulationMatrixMachine restored;
    restored.setInstalledDestinations(1u | 2u | 4u | 8u);
    restored.prepareToPlay(48000.0, 64);
    restored.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    for (int i = 0; i < 4; ++i) restored.navigateRight();
    cells = restored.getUIBoxes({});
    require(cells[7][5].text == "1", "new matrix state preserves NOTE reset route");

    // Persisted machine state is external data: invalid UTF-8 must be ignored
    // safely instead of reaching JUCE String::fromUTF8's Debug assertion.
    const unsigned char invalidUtf8[] = { 0xff, 0xfe };
    restored.setStateInformation(invalidUtf8, static_cast<int>(sizeof(invalidUtf8)));
    cells = restored.getUIBoxes({});
    require(cells[7][5].text == "1", "invalid matrix state leaves existing settings intact");

    while (restored.navigateRight()) {}
    cells = restored.getUIBoxes({});
    require(cells[6][0].text == "D TIME" && cells[7][0].text == "PAN",
            "installed delay exposes D TIME before the always-visible PAN destination");
    cells[6][1].onInsert(100.0);
    restored.processModulationBlock(64, true, false, nullptr, 0, {});
    require(restored.getBlockView().isActive(ModulationDestination::delayTime),
            "delay-time routes publish an audio-rate modulation buffer");

    DelayFxMachine delayUi;
    auto delayCells = delayUi.getUIBoxes({});
    delayCells[1][0].onAdjust(1);
    delayCells = delayUi.getUIBoxes({});
    require(delayCells[1][0].text == "MS" && delayCells[1][2].text == "250",
            "delay can enter free-millisecond mode");
    delayCells[1][2].onAdjust(1);
    delayCells = delayUi.getUIBoxes({});
    require(delayCells[1][2].text == "251", "delay milliseconds adjust in one-ms steps");

    const auto configureDelay = [](DelayFxMachine& delay, int mode)
    {
        const auto json = juce::String("{\"version\":1,\"mode\":")
            + juce::String(mode)
            + ",\"syncTicks\":1,\"delayMs\":100,\"feedback\":0,\"mix\":1}";
        delay.prepareToPlay(1000.0, 256);
        delay.setSecondsPerTick(0.1);
        delay.setStateInformation(json.toRawUTF8(), json.getNumBytesAsUTF8());
    };
    std::array<float, 256> halfTimeModulation {};
    halfTimeModulation.fill(-0.5f);
    ModulationBlockView delayModulation;
    delayModulation.numSamples = static_cast<int>(halfTimeModulation.size());
    delayModulation.values[static_cast<std::size_t>(ModulationDestination::delayTime)] = halfTimeModulation.data();

    DelayFxMachine freeDelay;
    configureDelay(freeDelay, 1);
    freeDelay.setModulationBlock(&delayModulation);
    juce::AudioBuffer<float> freeBuffer(1, 256);
    freeBuffer.clear(); freeBuffer.setSample(0, 0, 1.0f);
    freeDelay.processAudioBuffer(freeBuffer);
    require(freeBuffer.getSample(0, 50) > 0.99f && std::abs(freeBuffer.getSample(0, 100)) < 0.0001f,
            "minus-fifty-percent D TIME modulation halves a free-ms delay");

    DelayFxMachine syncedDelay;
    configureDelay(syncedDelay, 0);
    syncedDelay.setModulationBlock(&delayModulation);
    juce::AudioBuffer<float> syncBuffer(1, 256);
    syncBuffer.clear(); syncBuffer.setSample(0, 0, 1.0f);
    syncedDelay.processAudioBuffer(syncBuffer);
    require(syncBuffer.getSample(0, 100) > 0.99f && std::abs(syncBuffer.getSample(0, 50)) < 0.0001f,
            "D TIME modulation is ignored in tracker-step sync mode");
    return 0;
}
