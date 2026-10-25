#include <JuceHeader.h>

#include "PortableTrackerState.h"
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

void requirePermissiveProjectVersionLoading()
{
    require(static_cast<std::size_t>(CommandType::MidiNote) == 0
                && static_cast<std::size_t>(CommandType::Sampler) == 2
                && static_cast<std::size_t>(CommandType::WavetableSynth) == 4
                && static_cast<std::size_t>(CommandType::DistortionFx) == 6
                && static_cast<std::size_t>(CommandType::FilterFx) == 11
                && static_cast<std::size_t>(CommandType::ModulationMatrix) == 15
                && static_cast<std::size_t>(CommandType::DrumSynth) == 16,
            "persisted command IDs retain their historical numeric values");

    TrackerMainProcessor source;
    source.setMasterGainDb(-12.0f);
    juce::MemoryBlock stateData;
    source.getStateInformation(stateData);

    TrackerMainProcessor currentRestored;
    currentRestored.setStateInformation(stateData.getData(), static_cast<int>(stateData.getSize()));
    require(std::abs(currentRestored.getMasterGainDb() + 12.0f) < 0.001f,
            "current tracker schema restores normally");

    const auto directory = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile("myk-project-load-test", "", false);
    require(directory.createDirectory().wasOk(), "create tracker loading test directory");
    const auto stateOnlyFile = directory.getChildFile("current-state.myktracker");
    require(stateOnlyFile.replaceWithData(stateData.getData(), stateData.getSize()),
            "write current state-only tracker file");
    TrackerMainProcessor stateOnlyRestored;
    juce::String error;
    require(stateOnlyRestored.loadTrackerStateFile(stateOnlyFile, error)
                && std::abs(stateOnlyRestored.getMasterGainDb() + 12.0f) < 0.001f,
            "current state-only tracker file loads");

    const auto portableFile = directory.getChildFile("current-portable.myktracker");
    require(source.writePortableStateFile(portableFile, error),
            "write current portable tracker file");
    TrackerMainProcessor portableRestored;
    require(portableRestored.loadTrackerStateFile(portableFile, error)
                && std::abs(portableRestored.getMasterGainDb() + 12.0f) < 0.001f,
            "current portable tracker file loads");

    juce::MemoryInputStream input(stateData, false);
    auto xml = juce::parseXML(input.readEntireStreamAsString());
    require(xml != nullptr, "processor state serializes as XML");
    auto stateTree = juce::ValueTree::fromXml(*xml);
    auto state = juce::JSON::fromString(stateTree.getProperty("json").toString());
    require(state.isObject(), "processor state contains tracker JSON");
    state.getDynamicObject()->removeProperty("stateVersion");
    state.getDynamicObject()->setProperty("masterGainDb", -30.0f);
    stateTree.setProperty("json", juce::JSON::toString(state), nullptr);

    juce::MemoryBlock unversionedState;
    juce::MemoryOutputStream output(unversionedState, false);
    stateTree.createXml()->writeTo(output);

    currentRestored.setMasterGainDb(-6.0f);
    currentRestored.setStateInformation(unversionedState.getData(), static_cast<int>(unversionedState.getSize()));
    require(std::abs(currentRestored.getMasterGainDb() + 30.0f) < 0.001f,
            "unversioned tracker state is interpreted as the current layout");

    const auto unversionedFile = directory.getChildFile("unversioned.myktracker");
    require(unversionedFile.replaceWithData(unversionedState.getData(), unversionedState.getSize()),
            "write unversioned tracker fixture");
    currentRestored.setMasterGainDb(-6.0f);
    error.clear();
    require(currentRestored.loadTrackerStateFile(unversionedFile, error)
                && std::abs(currentRestored.getMasterGainDb() + 30.0f) < 0.001f,
            "file loader accepts an unversioned project as the current layout");

    const auto unversionedPortableFile = directory.getChildFile("unversioned-portable.myktracker");
    require(portable_tracker_state::write(unversionedPortableFile, unversionedState, {}, error),
            "write unversioned portable tracker fixture");
    currentRestored.setMasterGainDb(-6.0f);
    require(currentRestored.loadTrackerStateFile(unversionedPortableFile, error)
                && std::abs(currentRestored.getMasterGainDb() + 30.0f) < 0.001f,
            "portable loader accepts an unversioned project as the current layout");

    state.getDynamicObject()->setProperty("stateVersion", 999);
    state.getDynamicObject()->setProperty("masterGainDb", -18.0f);
    stateTree.setProperty("json", juce::JSON::toString(state), nullptr);
    juce::MemoryBlock mismatchedState;
    juce::MemoryOutputStream mismatchedOutput(mismatchedState, false);
    stateTree.createXml()->writeTo(mismatchedOutput);
    currentRestored.setStateInformation(mismatchedState.getData(), static_cast<int>(mismatchedState.getSize()));
    require(std::abs(currentRestored.getMasterGainDb() + 18.0f) < 0.001f,
            "mismatched tracker version is interpreted as the current layout");
    require(directory.deleteRecursively(), "remove tracker loading test directory");
}
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;
    requirePermissiveProjectVersionLoading();

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
