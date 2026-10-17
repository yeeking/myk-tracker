#include <JuceHeader.h>
#include <cmath>
#include <cstdlib>
#include <iostream>

#include "machines/ModulationMatrixMachine.h"

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
    matrix.setInstalledDestinations(1u | 2u | 4u);
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
    require(matrix.navigateRight(), "right navigation scrolls the destination window");
    require(matrix.navigateLeft(), "left navigation scrolls the destination window back");

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
    restored.setInstalledDestinations(1u | 2u | 4u);
    restored.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    for (int i = 0; i < 4; ++i) restored.navigateRight();
    cells = restored.getUIBoxes({});
    require(cells[7][5].text == "1", "new matrix state preserves NOTE reset route");
    return 0;
}
