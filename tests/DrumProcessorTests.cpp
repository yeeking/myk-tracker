#include <JuceHeader.h>

#include "TrackerMainProcessor.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>

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

double energy(const juce::AudioBuffer<float>& buffer)
{
    double result = 0.0;
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
            result += std::abs(static_cast<double>(buffer.getSample(channel, sample)));
    return result;
}

std::size_t findSlot(const TrackerMainProcessor& processor, CommandType type)
{
    const auto types = processor.getMachineStackTypes(0);
    const auto found = std::find(types.begin(), types.end(), type);
    return found == types.end() ? types.size() : static_cast<std::size_t>(found - types.begin());
}
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;
    TrackerMainProcessor processor;
    processor.prepareToPlay(48000.0, 256);

    while (findSlot(processor, CommandType::DrumSynth) >= processor.getMachineStackTypes(0).size())
        processor.addMachineToStack(0);

    const auto types = processor.getMachineStackTypes(0);
    const auto drumSlot = findSlot(processor, CommandType::DrumSynth);
    require(drumSlot < types.size(), "DRUM is available in the machine stack cycle");
    for (std::size_t slot = 0; slot < types.size(); ++slot)
    {
        const bool shouldEnable = slot == drumSlot;
        if (processor.isMachineEnabledInStack(0, slot) != shouldEnable)
            processor.toggleMachineEnabledInStack(0, slot);
    }

    processor.sendTimedMessageToMachine(CommandType::DrumSynth, 0, 72, 120, 1.0, false);
    juce::AudioBuffer<float> directAudio(2, 256);
    juce::MidiBuffer midi;
    double directEnergy = 0.0;
    for (int block = 0; block < 20; ++block)
    {
        directAudio.clear();
        processor.processBlock(directAudio, midi);
        directEnergy += energy(directAudio);
    }
    require(directEnergy > 0.01, "processor stack renders directly triggered DRUM audio");

    auto* editor = processor.getSequenceEditor();
    require(editor != nullptr, "processor exposes its sequence editor");
    editor->gotoMachineConfigPage();
    editor->refreshMachineStateForCurrentSequence();
    editor->setMachineCursor(drumSlot + 2, 0);
    require(editor->enterSelectedMachineDetail(), "DRUM detail opens from its stack row");
    editor->moveCursorRight();
    const auto& drumCells = editor->getMachineCells();
    editor->moveCursorDown();
    editor->moveCursorDown();
    require(drumCells.size() > 3 && drumCells[3].size() > 2
                && drumCells[3][2].isSelected && drumCells[3][2].text == "808",
            "right/down navigation reaches the snare TYPE value below its enable and MIDI controls");
    editor->incrementAtCursor();
    editor->refreshMachineStateForCurrentSequence();
    require(editor->getMachineCells()[3][2].text == "909",
            "bracket adjustment changes the selected snare model");

    auto* sequencer = processor.getSequencer();
    require(sequencer != nullptr && sequencer->getSequence(0) != nullptr,
            "processor has a playable default sequence");
    auto* sequence = sequencer->getSequence(0);
    sequence->setMachineId(0);
    sequence->setLength(1);
    sequence->setStepData(0, {{ static_cast<double>(CommandType::MidiNote), 72.0, 120.0, 1.0 }});
    processor.setInternalClockEnabled(true);
    editor->togglePlayback();

    // Transport starts on the next locked beat boundary, rather than in the
    // keyboard handler. Drive enough callback blocks to reach that boundary.
    for (int block = 0; block < 256 && !processor.isPlaybackRunningSafely(); ++block)
    {
        directAudio.clear();
        processor.processBlock(directAudio, midi);
    }
    require(processor.isPlaybackRunningSafely(), "space-equivalent playback toggle starts transport");

    double sequencedEnergy = 0.0;
    for (int block = 0; block < 40; ++block)
    {
        directAudio.clear();
        processor.processBlock(directAudio, midi);
        sequencedEnergy += energy(directAudio);
    }
    require(sequencedEnergy > 0.01, "running sequencer triggers DRUM through the stack");
    editor->togglePlayback();
    require(!processor.isPlaybackRunningSafely(), "space-equivalent playback toggle stops transport");
    processor.releaseResources();
    return 0;
}
