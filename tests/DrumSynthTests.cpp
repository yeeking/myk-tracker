#include <JuceHeader.h>

#include "machines/DrumSynthMachine.h"
#include "machines/MachineStateCodec.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <tuple>

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

double energy(const juce::AudioBuffer<float>& buffer, int begin = 0, int end = -1)
{
    if (end < 0) end = buffer.getNumSamples();
    double result = 0.0;
    for (int sample = begin; sample < end; ++sample)
        result += std::abs(static_cast<double>(buffer.getSample(0, sample)));
    return result;
}

double differenceEnergy(const juce::AudioBuffer<float>& first, const juce::AudioBuffer<float>& second)
{
    double result = 0.0;
    for (int sample = 0; sample < std::min(first.getNumSamples(), second.getNumSamples()); ++sample)
        result += std::abs(static_cast<double>(first.getSample(0, sample) - second.getSample(0, sample)));
    return result;
}

double peak(const juce::AudioBuffer<float>& buffer)
{
    double result = 0.0;
    for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
        result = std::max(result, std::abs(static_cast<double>(buffer.getSample(0, sample))));
    return result;
}

double maximumDelta(const juce::AudioBuffer<float>& buffer)
{
    double result = 0.0;
    for (int sample = 1; sample < buffer.getNumSamples(); ++sample)
        result = std::max(result, std::abs(static_cast<double>(buffer.getSample(0, sample)
                                                            - buffer.getSample(0, sample - 1))));
    return result;
}

void insertControl(DrumSynthMachine& synth, int labelColumn, const char* label, double value)
{
    auto cells = synth.getUIBoxes({});
    for (std::size_t row = 0; row < cells[static_cast<std::size_t>(labelColumn)].size(); ++row)
        if (cells[static_cast<std::size_t>(labelColumn)][row].text == label)
        {
            require(cells[static_cast<std::size_t>(labelColumn + 1)][row].onInsert != nullptr,
                    "numeric drum controls support direct insertion");
            cells[static_cast<std::size_t>(labelColumn + 1)][row].onInsert(value);
            return;
        }
    require(false, "requested drum control exists");
}

void setStripEnabled(DrumSynthMachine& synth, int labelColumn, bool enabled)
{
    auto cells = synth.getUIBoxes({});
    auto& toggle = cells[static_cast<std::size_t>(labelColumn + 1)][0];
    require(toggle.onInsert != nullptr, "each drum strip header exposes an enable toggle");
    toggle.onInsert(enabled ? 1.0 : 0.0);
}

juce::AudioBuffer<float> render(int note, int eventOffset = 0, int samples = 2048)
{
    DrumSynthMachine synth;
    synth.prepareToPlay(48000.0, samples);
    synth.setSecondsPerTick(0.1);
    const std::vector<MachineScheduledNote> events { { eventOffset, static_cast<unsigned short>(note), 110, 4.0, false } };
    synth.scheduleBlockNotes(events);
    juce::AudioBuffer<float> buffer(1, samples);
    buffer.clear();
    juce::MidiBuffer midi;
    synth.processBlock(buffer, midi);
    return buffer;
}

juce::AudioBuffer<float> renderAtVelocity(int note, unsigned short velocity, int samples = 2048)
{
    DrumSynthMachine synth;
    synth.prepareToPlay(48000.0, samples);
    synth.setSecondsPerTick(0.1);
    const std::vector<MachineScheduledNote> events { { 0, static_cast<unsigned short>(note), velocity, 4.0, false } };
    synth.scheduleBlockNotes(events);
    juce::AudioBuffer<float> buffer(1, samples);
    buffer.clear();
    juce::MidiBuffer midi;
    synth.processBlock(buffer, midi);
    return buffer;
}


juce::AudioBuffer<float> renderWithControl(int note, int labelColumn, const char* label, double value)
{
    DrumSynthMachine synth;
    synth.prepareToPlay(48000.0, 2048);
    synth.setSecondsPerTick(0.1);
    insertControl(synth, labelColumn, label, value);
    const std::vector<MachineScheduledNote> events { { 0, static_cast<unsigned short>(note), 110, 4.0, false } };
    synth.scheduleBlockNotes(events);
    juce::AudioBuffer<float> buffer(1, 2048);
    buffer.clear();
    juce::MidiBuffer midi;
    synth.processBlock(buffer, midi);
    return buffer;
}

std::string controlText(DrumSynthMachine& synth, int labelColumn, const char* label)
{
    auto cells = synth.getUIBoxes({});
    for (std::size_t row = 0; row < cells[static_cast<std::size_t>(labelColumn)].size(); ++row)
        if (cells[static_cast<std::size_t>(labelColumn)][row].text == label)
            return cells[static_cast<std::size_t>(labelColumn + 1)][row].text;
    require(false, "requested drum control exists");
    return {};
}

juce::AudioBuffer<float> renderTogether(std::initializer_list<int> notes, int samples = 48000, int repeatAt = -1)
{
    DrumSynthMachine synth;
    synth.prepareToPlay(48000.0, samples);
    synth.setSecondsPerTick(0.1);
    std::vector<MachineScheduledNote> events;
    for (const int note : notes)
    {
        events.push_back({ 0, static_cast<unsigned short>(note), 127, 8.0, false });
        if (repeatAt >= 0)
            events.push_back({ repeatAt, static_cast<unsigned short>(note), 127, 8.0, false });
    }
    synth.scheduleBlockNotes(events);
    juce::AudioBuffer<float> buffer(1, samples);
    buffer.clear();
    juce::MidiBuffer midi;
    synth.processBlock(buffer, midi);
    return buffer;
}

}

int main()
{
    using Drum = DrumSynthMachine::Drum;
    DrumSynthMachine noteMap;
    require(noteMap.drumForNote(72) == Drum::kick && noteMap.drumForNote(73) == Drum::snare
            && noteMap.drumForNote(74) == Drum::closedHat && noteMap.drumForNote(75) == Drum::crash
            && noteMap.drumForNote(76) == Drum::ride && noteMap.drumForNote(77) == Drum::tom
            && noteMap.drumForNote(78) == Drum::clap,
            "the seven drum strips default to consecutive MIDI responders 72 through 78");
    require(noteMap.drumForNote(86) == Drum::openHat && noteMap.drumForNote(36) == Drum::none,
            "the open hat remains available one octave above its configurable closed-hat responder");
    require(std::string(noteMap.describeNote(74)) == "CHAT"
            && std::string(noteMap.describeNote(86)) == "OHAT",
            "configurable hat articulations retain compact sequence labels");
    insertControl(noteMap, 2, "PSET", 2.0);
    auto presetCells = noteMap.getUIBoxes({});
    require(presetCells[3][8].text == "909"
            && noteMap.getKitPreset() == DrumSynthMachine::KitPreset::tr909,
            "the kit preset selects the 909 snare and its linked cymbal models");
    insertControl(noteMap, 4, "ODEC", 1200.0);
    presetCells = noteMap.getUIBoxes({});
    require(presetCells[3][8].text == "CUST"
            && noteMap.getKitPreset() == DrumSynthMachine::KitPreset::custom,
            "editing a preset-controlled cymbal parameter marks the kit custom");
    insertControl(noteMap, 2, "PSET", 1.0);
    presetCells = noteMap.getUIBoxes({});
    require(presetCells[3][8].text == "808" && std::stoi(presetCells[5][5].text) > 3000,
            "the 808 preset restores its long open-hat decay without changing the note map");
    insertControl(noteMap, 0, "NOTE", 36.0);
    require(noteMap.drumForNote(36) == Drum::kick && noteMap.drumForNote(72) == Drum::none,
            "each strip NOTE control immediately retargets its MIDI responder");

    for (const int note : { 72, 75, 73, 76, 74, 77, 78, 86 })
    {
        const auto audio = render(note);
        bool finite = true;
        for (int sample = 0; sample < audio.getNumSamples(); ++sample)
            finite = finite && std::isfinite(audio.getSample(0, sample));
        require(finite && energy(audio) > 0.0001, "every mapped articulation renders finite audio");
    }
    require(energy(render(40)) < 1.0e-12, "unmapped notes render silence");
    require(energy(renderAtVelocity(72, 32)) < energy(renderAtVelocity(72, 127)) * 0.5,
            "drum amplitude responds clearly to MIDI velocity");

    DrumSynthMachine disabledKick;
    disabledKick.prepareToPlay(48000.0, 2048);
    setStripEnabled(disabledKick, 0, false);
    const std::vector<MachineScheduledNote> disabledKickEvent { { 0, 72, 127, 4.0, false } };
    disabledKick.scheduleBlockNotes(disabledKickEvent);
    juce::AudioBuffer<float> disabledKickAudio(1, 2048);
    disabledKickAudio.clear();
    juce::MidiBuffer disabledMidi;
    disabledKick.processBlock(disabledKickAudio, disabledMidi);
    require(energy(disabledKickAudio) < 1.0e-12,
            "a disabled drum ignores its trigger and performs no audible synthesis");
    setStripEnabled(disabledKick, 0, true);
    disabledKick.scheduleBlockNotes(disabledKickEvent);
    disabledKickAudio.clear();
    disabledKick.processBlock(disabledKickAudio, disabledMidi);
    require(energy(disabledKickAudio) > 0.0001,
            "re-enabling a drum makes its preallocated engine immediately available");

    DrumSynthMachine disabledTail;
    disabledTail.prepareToPlay(48000.0, 2048);
    disabledTail.scheduleBlockNotes(disabledKickEvent);
    juce::AudioBuffer<float> tailAudio(1, 256);
    tailAudio.clear();
    disabledTail.processBlock(tailAudio, disabledMidi);
    require(energy(tailAudio) > 0.0001, "enabled drum starts its normal decay tail");
    setStripEnabled(disabledTail, 0, false);
    disabledTail.scheduleBlockNotes({});
    tailAudio.clear();
    disabledTail.processBlock(tailAudio, disabledMidi);
    require(energy(tailAudio) < 1.0e-12,
            "disabling an active drum discards its tail instead of continuing DSP work");
    const std::array<std::tuple<int, int, const char*>, 7> adventurousControls {{
        { 72, 0, "PUNCH" }, { 73, 2, "RING" }, { 74, 4, "METAL" },
        { 75, 6, "EDGE" }, { 76, 8, "BELL" }, { 77, 10, "BEND" }, { 78, 12, "ROOM" }
    }};
    for (const auto& [note, column, label] : adventurousControls)
    {
        const auto negative = renderWithControl(note, column, label, -10.0);
        const auto neutral = renderWithControl(note, column, label, 0.0);
        const auto positive = renderWithControl(note, column, label, 10.0);
        require(differenceEnergy(negative, neutral) > 0.25
                    && differenceEnergy(positive, neutral) > 0.25
                    && differenceEnergy(negative, positive) > 0.25,
                "each adventurous control creates strong timbral changes on both sides of neutral");
        if (!(std::isfinite(peak(negative)) && std::isfinite(peak(positive))
              && peak(negative) < 1.0 && peak(positive) < 1.0))
        {
            std::cerr << "FAILED: adventurous endpoint peak " << label << " "
                      << peak(negative) << " " << peak(positive) << '\n';
            return 1;
        }
    }

    const auto kickSnare = renderTogether({ 72, 73 });
    const auto kickSnareRetrigger = renderTogether({ 72, 73 }, 48000, 24000);
    require(std::isfinite(peak(kickSnare)) && peak(kickSnare) < 1.0
            && maximumDelta(kickSnare) < 0.5,
            "combined kick and snare output remains bounded and free of discontinuous bursts");
    require(std::isfinite(peak(kickSnareRetrigger)) && peak(kickSnareRetrigger) < 1.0
            && maximumDelta(kickSnareRetrigger) < 0.75,
            "combined kick and snare retriggers remain bounded");

    DrumSynthMachine::Biquad corruptedFilter;
    corruptedFilter.z1 = std::numeric_limits<float>::infinity();
    require(std::abs(corruptedFilter.process(0.25f)) < 1.0e-12f
            && std::abs(corruptedFilter.z1) < 1.0e-12f
            && std::abs(corruptedFilter.z2) < 1.0e-12f,
            "drum filters discard a corrupt recursive state instead of poisoning the stack");

    const auto delayed = render(72, 64, 256);
    require(energy(delayed, 0, 64) < 1.0e-12 && energy(delayed, 64, 256) > 0.0,
            "drum events begin at their in-block sample offset");

    DrumSynthMachine oneShot;
    oneShot.prepareToPlay(1000.0, 256);
    oneShot.setSecondsPerTick(0.1);
    insertControl(oneShot, 0, "DEC", 8000.0);
    const std::vector<MachineScheduledNote> shortKick { { 0, 72, 127, 0.5, false } };
    oneShot.scheduleBlockNotes(shortKick);
    juce::AudioBuffer<float> oneShotAudio(1, 256);
    oneShotAudio.clear();
    juce::MidiBuffer midi;
    oneShot.processBlock(oneShotAudio, midi);
    require(energy(oneShotAudio, 100, 256) > 0.01,
            "default one-shot mode ignores the stored note duration");

    DrumSynthMachine gated;
    gated.prepareToPlay(1000.0, 256);
    gated.setSecondsPerTick(0.1);
    insertControl(gated, 0, "DEC", 8000.0);
    insertControl(gated, 0, "MODE", 0.0);
    gated.scheduleBlockNotes(shortKick);
    juce::AudioBuffer<float> gatedAudio(1, 256);
    gatedAudio.clear();
    gated.processBlock(gatedAudio, midi);
    require(energy(gatedAudio, 0, 50) > 0.0 && energy(gatedAudio, 80, 256) < 0.0001,
            "GATE mode applies fractional tick duration through a short release");

    DrumSynthMachine noteOffModes;
    noteOffModes.prepareToPlay(1000.0, 256);
    noteOffModes.setSecondsPerTick(0.1);
    insertControl(noteOffModes, 2, "RING", 10.0);
    const std::vector<MachineScheduledNote> snareWithNoteOff {
        { 0, 73, 127, 8.0, false }, { 20, 73, 0, 0.0, true }
    };
    noteOffModes.scheduleBlockNotes(snareWithNoteOff);
    juce::AudioBuffer<float> oneShotNoteOffAudio(1, 256);
    oneShotNoteOffAudio.clear();
    noteOffModes.processBlock(oneShotNoteOffAudio, midi);
    require(energy(oneShotNoteOffAudio, 80, 256) > 0.01,
            "one-shot mode ignores explicit held-note release events");

    DrumSynthMachine gatedNoteOff;
    gatedNoteOff.prepareToPlay(1000.0, 256);
    gatedNoteOff.setSecondsPerTick(0.1);
    insertControl(gatedNoteOff, 2, "RING", 10.0);
    insertControl(gatedNoteOff, 2, "MODE", 0.0);
    gatedNoteOff.scheduleBlockNotes(snareWithNoteOff);
    juce::AudioBuffer<float> gatedNoteOffAudio(1, 256);
    gatedNoteOffAudio.clear();
    gatedNoteOff.processBlock(gatedNoteOffAudio, midi);
    require(energy(gatedNoteOffAudio, 80, 256) < 0.0001,
            "GATE mode responds to explicit held-note release events");

    DrumSynthMachine retriggered;
    retriggered.prepareToPlay(1000.0, 256);
    retriggered.setSecondsPerTick(0.1);
    insertControl(retriggered, 0, "DEC", 8000.0);
    insertControl(retriggered, 0, "MODE", 0.0);
    const std::vector<MachineScheduledNote> retriggeredKick {
        { 0, 72, 127, 10.0, false }, { 20, 72, 127, 0.5, false }
    };
    retriggered.scheduleBlockNotes(retriggeredKick);
    juce::AudioBuffer<float> retriggeredAudio(1, 256);
    retriggeredAudio.clear();
    retriggered.processBlock(retriggeredAudio, midi);
    require(energy(retriggeredAudio, 20, 50) > 0.0 && energy(retriggeredAudio, 100, 256) < 0.0001,
            "latest retrigger replaces the earlier gate deadline");

    DrumSynthMachine stateSource;
    insertControl(stateSource, 2, "PSET", 2.0);
    insertControl(stateSource, 6, "EDGE", 8.0);
    insertControl(stateSource, 8, "BELL", -7.0);
    insertControl(stateSource, 0, "PUNCH", 6.0);
    insertControl(stateSource, 4, "METAL", 9.0);
    insertControl(stateSource, 10, "BEND", -4.0);
    insertControl(stateSource, 12, "ROOM", 7.0);
    insertControl(stateSource, 12, "DENS", 80.0);
    insertControl(stateSource, 0, "TUNE", 95.0);
    insertControl(stateSource, 0, "NOTE", 90.0);
    insertControl(stateSource, 2, "NOTE", 91.0);
    setStripEnabled(stateSource, 0, false);
    setStripEnabled(stateSource, 6, false);
    for (const int column : { 0, 2, 4, 6, 8, 10, 12 })
        insertControl(stateSource, column, "MODE", 0.0);
    juce::MemoryBlock state;
    stateSource.getStateInformation(state);
    DrumSynthMachine restored;
    restored.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    auto cells = restored.getUIBoxes({});
    require(cells[1][0].text == "OFF" && cells[7][0].text == "OFF"
            && cells[3][0].text == "ON" && controlText(restored, 6, "EDGE") == "8"
            && controlText(restored, 8, "BELL") == "-7" && controlText(restored, 0, "PUNCH") == "6"
            && controlText(restored, 4, "METAL") == "9" && controlText(restored, 10, "BEND") == "-4"
            && controlText(restored, 12, "ROOM") == "7" && controlText(restored, 12, "DENS") == "80"
            && controlText(restored, 0, "TUNE") == "95" && controlText(restored, 0, "NOTE") == "90"
            && controlText(restored, 2, "NOTE") == "91" && cells[3][8].text == "CUST"
            && controlText(restored, 0, "MODE") == "GATE" && controlText(restored, 2, "MODE") == "GATE"
            && controlText(restored, 4, "MODE") == "GATE" && controlText(restored, 6, "MODE") == "GATE"
            && controlText(restored, 8, "MODE") == "GATE" && controlText(restored, 10, "MODE") == "GATE"
            && controlText(restored, 12, "MODE") == "GATE",
            "compact controls, responder notes, and trigger modes survive state round trip");

    juce::DynamicObject::Ptr malformed = new juce::DynamicObject();
    malformed->setProperty("version", 1);
    malformed->setProperty("kickTune", 1000);
    malformed->setProperty("kickTone", -10.0);
    malformed->setProperty("kickPunch", 99);
    malformed->setProperty("crashDecayMs", 99999);
    juce::MemoryBlock malformedState;
    writeMachineStateJson(malformedState, juce::var(malformed.get()));
    restored.setStateInformation(malformedState.getData(), static_cast<int>(malformedState.getSize()));
    cells = restored.getUIBoxes({});
    require(controlText(restored, 0, "TUNE") == "96" && controlText(restored, 0, "TONE") == "0"
            && controlText(restored, 0, "PUNCH") == "10" && controlText(restored, 6, "DEC") == "12000",
            "malformed drum state clamps to the adventurous but safe ranges");

    const int decayBeforeAdjustment = std::stoi(controlText(restored, 0, "DEC"));
    cells = restored.getUIBoxes({});
    cells[1][5].onAdjust(1);
    cells = restored.getUIBoxes({});
    require(decayBeforeAdjustment == 1200 && std::stoi(cells[1][5].text) > 1600,
            "kick decay adjustment uses a rapid exponential step");

    require(controlText(restored, 0, "WAVE") == "SIN", "kick waveform selector restores the persisted sine default");
    insertControl(restored, 0, "WAVE", 8.0);
    require(controlText(restored, 0, "WAVE") == "ODD5", "kick waveform selector exposes all nine waveform choices");

    DrumSynthMachine triggerCounter;
    triggerCounter.prepareToPlay(48000.0, 256);
    const std::vector<MachineScheduledNote> clapTrigger { { 0, 78, 127, 4.0, false } };
    triggerCounter.scheduleBlockNotes(clapTrigger);
    juce::AudioBuffer<float> triggerBuffer(1, 256);
    triggerBuffer.clear();
    triggerCounter.processBlock(triggerBuffer, midi);
    require(triggerCounter.getTriggerCounts()[6] == 1, "clap trigger counter reports successful audio-thread triggers");

    juce::DynamicObject::Ptr legacy = new juce::DynamicObject();
    legacy->setProperty("version", 3);
    juce::MemoryBlock legacyState;
    writeMachineStateJson(legacyState, juce::var(legacy.get()));
    DrumSynthMachine legacyRestored;
    legacyRestored.setStateInformation(legacyState.getData(), static_cast<int>(legacyState.getSize()));
    require(legacyRestored.drumForNote(78) == Drum::clap
                && legacyRestored.getUIBoxes({})[13][0].text == "OFF",
            "pre-clap drum state keeps formerly unmapped clap notes silent");

    require(cells.size() == 14 && cells[0].size() == 9,
            "drum UI uses seven compact two-column strips with Roland controls and adventurous extensions");
    return 0;
}
