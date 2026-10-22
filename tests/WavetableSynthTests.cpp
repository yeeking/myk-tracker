#include <JuceHeader.h>

#include "machines/CurvedAdsr.h"
#include "machines/MachineStateCodec.h"
#include "machines/WavetableSynthMachine.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <vector>

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

void setVoiceCount(WavetableSynthMachine& synth, double count)
{
    auto cells = synth.getUIBoxes({});
    require(cells.size() > 5 && cells[5].size() > 6 && cells[5][6].onInsert,
            "WAVE UI exposes numeric VOIC editing");
    cells[5][6].onInsert(count);
}

void setCyclerRate(WavetableSynthMachine& synth, double rate)
{
    auto cells = synth.getUIBoxes({});
    for (std::size_t col = 0; col + 1 < cells.size(); ++col)
        for (std::size_t row = 0; row < cells[col].size(); ++row)
            if (cells[col][row].text == "CYC")
            {
                require(cells[col + 1][row].onAdjust != nullptr, "CYC supports bracket adjustment");
                const int steps = static_cast<int>(std::lround(rate / 0.1));
                // Fresh test synths start at 1.0 CPS; use the real bracket
                // path to reach the requested zero-rate endpoint.
                for (int step = 10; step > steps; --step)
                    cells[col + 1][row].onAdjust(-1);
                return;
            }
    require(false, "WAVE UI contains the CYC control");
}

double absoluteEnergy(const juce::AudioBuffer<float>& buffer, int firstSample = 0)
{
    double energy = 0.0;
    for (int sample = firstSample; sample < buffer.getNumSamples(); ++sample)
        energy += std::abs(static_cast<double>(buffer.getSample(0, sample)));
    return energy;
}

juce::AudioBuffer<float> renderNotes(int voiceCount,
                                     const std::vector<MachineScheduledNote>& notes,
                                     int sampleCount = 4096)
{
    WavetableSynthMachine synth;
    synth.prepareToPlay(48000.0, sampleCount);
    setVoiceCount(synth, voiceCount);
    synth.scheduleBlockNotes(notes);
    juce::AudioBuffer<float> buffer(1, sampleCount);
    buffer.clear();
    juce::MidiBuffer midi;
    synth.processBlock(buffer, midi);
    return buffer;
}

juce::MemoryBlock stateWithVoiceCount(int voiceCount)
{
    juce::DynamicObject::Ptr root = new juce::DynamicObject();
    root->setProperty("version", 1);
    root->setProperty("voiceCount", voiceCount);
    juce::MemoryBlock state;
    writeMachineStateJson(state, juce::var(root.get()));
    return state;
}
}

int main()
{
    WavetableSynthMachine synth;
    require(synth.getVoiceCount() == 4, "new WAVE machines default to four voices");

    auto cells = synth.getUIBoxes({});
    require(cells[4][6].text == "VOIC" && cells[5][6].text == "4",
            "WAVE detail shows the VOIC control");
    require(cells[5][6].onAdjust && cells[5][6].onInsert,
            "VOIC supports brackets and numeric insertion");
    setCyclerRate(synth, 0.0);
    cells = synth.getUIBoxes({});
    bool zeroCyclerDisplayed = false;
    for (std::size_t col = 0; col + 1 < cells.size(); ++col)
        for (std::size_t row = 0; row < cells[col].size(); ++row)
            if (cells[col][row].text == "CYC")
                zeroCyclerDisplayed = cells[col + 1][row].text == "0.0";
    require(zeroCyclerDisplayed, "CYC displays zero-rate wavetable cycling");

    // Position modulation remains a full wrapped morph control when CPS is
    // stopped, including its negative half-cycle.
    WavetableSynthMachine modulatedMorph;
    modulatedMorph.prepareToPlay(48000.0, 128);
    setCyclerRate(modulatedMorph, 0.0);
    std::array<float, 128> morphValues {};
    for (std::size_t i = 0; i < morphValues.size(); ++i)
        morphValues[i] = -1.0f + 2.0f * static_cast<float>(i) / static_cast<float>(morphValues.size() - 1);
    ModulationBlockView morphBlock;
    morphBlock.values[static_cast<std::size_t>(ModulationDestination::wavePosition)] = morphValues.data();
    morphBlock.numSamples = static_cast<int>(morphValues.size());
    modulatedMorph.setModulationBlock(&morphBlock);
    modulatedMorph.scheduleBlockNotes({ { 0, 60, 100, 100 } });
    juce::AudioBuffer<float> morphAudio(1, 128);
    morphAudio.clear();
    juce::MidiBuffer morphMidi;
    modulatedMorph.processBlock(morphAudio, morphMidi);
    bool hasFiniteAudio = true;
    for (int i = 0; i < morphAudio.getNumSamples(); ++i)
        hasFiniteAudio = hasFiniteAudio && std::isfinite(morphAudio.getSample(0, i));
    require(hasFiniteAudio,
            "zero-CPS WAVE accepts bipolar position modulation without invalid step access");
    cells[5][6].onAdjust(-1);
    require(synth.getVoiceCount() == 3, "bracket adjustment changes voice count by one");
    setVoiceCount(synth, -100.0);
    require(synth.getVoiceCount() == 1, "voice count clamps at one");
    setVoiceCount(synth, 100.0);
    require(synth.getVoiceCount() == 8, "voice count clamps at eight");

    setVoiceCount(synth, 7.0);
    juce::MemoryBlock state;
    synth.getStateInformation(state);
    WavetableSynthMachine restored;
    restored.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    require(restored.getVoiceCount() == 7, "voice count survives state round trip");

    auto tooLowState = stateWithVoiceCount(-50);
    restored.setStateInformation(tooLowState.getData(), static_cast<int>(tooLowState.getSize()));
    require(restored.getVoiceCount() == 1, "restored voice count clamps low values");
    auto tooHighState = stateWithVoiceCount(50);
    restored.setStateInformation(tooHighState.getData(), static_cast<int>(tooHighState.getSize()));
    require(restored.getVoiceCount() == 8, "restored voice count clamps high values");

    // All events are phase-aligned and identical, so the eight-voice render
    // should carry twice the energy of the four-voice stealing render.
    std::vector<MachineScheduledNote> denseChord;
    for (int note = 0; note < 8; ++note)
        denseChord.push_back({ 0, 60, 100, 100 });
    const auto fourVoice = renderNotes(4, denseChord);
    const auto eightVoice = renderNotes(8, denseChord);
    const double fourEnergy = absoluteEnergy(fourVoice, 256);
    const double eightEnergy = absoluteEnergy(eightVoice, 256);
    require(fourEnergy > 0.0 && std::abs((eightEnergy / fourEnergy) - 2.0) < 0.001,
            "configured polyphony limits allocation while retaining voice stealing");

    // In mono, simultaneous events are consumed in stable order. The final
    // event must therefore produce exactly the same audio as that note alone.
    const std::vector<MachineScheduledNote> chord { { 0, 60, 100, 100 }, { 0, 67, 100, 100 } };
    const std::vector<MachineScheduledNote> lastOnly { { 0, 67, 100, 100 } };
    const auto monoChord = renderNotes(1, chord, 1024);
    const auto monoLast = renderNotes(1, lastOnly, 1024);
    double monoDifference = 0.0;
    for (int sample = 0; sample < monoChord.getNumSamples(); ++sample)
        monoDifference += std::abs(static_cast<double>(monoChord.getSample(0, sample)
            - monoLast.getSample(0, sample)));
    require(monoDifference < 0.000001, "mono chords deterministically use the last scheduled note");

    // A continuous retrigger must rise from the current envelope level rather
    // than falling back to zero like a normal polyphonic noteOn.
    CurvedAdsr envelope;
    envelope.setSampleRate(1000.0);
    CurvedAdsr::Parameters envelopeParameters;
    envelopeParameters.attack = 0.1f;
    envelopeParameters.decay = 0.1f;
    envelopeParameters.sustain = 0.5f;
    envelopeParameters.release = 0.2f;
    envelope.setParameters(envelopeParameters);
    envelope.noteOn();
    float beforeRetrigger = 0.0f;
    for (int sample = 0; sample < 50; ++sample)
        beforeRetrigger = envelope.getNextSample();
    envelope.retriggerFromCurrent();
    const float afterRetrigger = envelope.getNextSample();
    require(afterRetrigger >= beforeRetrigger && (afterRetrigger - beforeRetrigger) < 0.02f,
            "continuous envelope retrigger starts smoothly from its current level");

    // Retriggering an already sounding mono voice must retain useful output
    // immediately; a hard reset would collapse this short window near zero.
    WavetableSynthMachine mono;
    mono.prepareToPlay(48000.0, 3072);
    setVoiceCount(mono, 1.0);
    const std::vector<MachineScheduledNote> firstNote { { 0, 60, 127, 100 } };
    mono.scheduleBlockNotes(firstNote);
    juce::AudioBuffer<float> firstBlock(1, 3072);
    firstBlock.clear();
    juce::MidiBuffer midi;
    mono.processBlock(firstBlock, midi);
    const double precedingEnergy = absoluteEnergy(firstBlock, firstBlock.getNumSamples() - 128);
    const std::vector<MachineScheduledNote> retrigger { { 0, 72, 127, 100 } };
    mono.scheduleBlockNotes(retrigger);
    juce::AudioBuffer<float> retriggerBlock(1, 128);
    retriggerBlock.clear();
    mono.processBlock(retriggerBlock, midi);
    require(precedingEnergy > 0.0 && absoluteEnergy(retriggerBlock) > precedingEnergy * 0.25,
            "mono pitch changes retain oscillator/envelope continuity");

    // Velocity changes are ramped rather than stepped. A zero-velocity
    // retrigger therefore retains energy at the start and reaches silence by
    // the end of the two-millisecond ramp.
    const std::vector<MachineScheduledNote> zeroVelocity { { 0, 72, 0, 100 } };
    mono.scheduleBlockNotes(zeroVelocity);
    juce::AudioBuffer<float> velocityBlock(1, 128);
    velocityBlock.clear();
    mono.processBlock(velocityBlock, midi);
    double earlyVelocityEnergy = 0.0;
    double lateVelocityEnergy = 0.0;
    for (int sample = 0; sample < 32; ++sample)
        earlyVelocityEnergy += std::abs(static_cast<double>(velocityBlock.getSample(0, sample)));
    for (int sample = 112; sample < 128; ++sample)
        lateVelocityEnergy += std::abs(static_cast<double>(velocityBlock.getSample(0, sample)));
    require(earlyVelocityEnergy > 0.0 && lateVelocityEnergy < 0.000001,
            "mono velocity changes use the short smoothing ramp");

    // A retrigger replaces the previous gate duration. The one-tick second
    // note should therefore release and become silent long before the first
    // note's original hundred-tick duration would have elapsed.
    WavetableSynthMachine durationSynth;
    durationSynth.prepareToPlay(1000.0, 400);
    durationSynth.setSecondsPerTick(0.01);
    setVoiceCount(durationSynth, 1.0);
    const std::vector<MachineScheduledNote> longNote { { 0, 60, 127, 100 } };
    durationSynth.scheduleBlockNotes(longNote);
    juce::AudioBuffer<float> durationWarmup(1, 20);
    durationWarmup.clear();
    durationSynth.processBlock(durationWarmup, midi);
    const std::vector<MachineScheduledNote> shortReplacement { { 0, 67, 127, 1 } };
    durationSynth.scheduleBlockNotes(shortReplacement);
    juce::AudioBuffer<float> durationTail(1, 400);
    durationTail.clear();
    durationSynth.processBlock(durationTail, midi);
    require(absoluteEnergy(durationTail, 300) < 0.000001,
            "newest mono note duration replaces the previous gate duration");

    // Reducing polyphony while notes sound must retain an audible newest
    // voice and use release tails instead of hard-clearing the old pool.
    WavetableSynthMachine liveLimit;
    liveLimit.prepareToPlay(48000.0, 1024);
    const std::vector<MachineScheduledNote> liveChord {
        { 0, 48, 100, 100 }, { 0, 55, 100, 100 },
        { 0, 60, 100, 100 }, { 0, 67, 100, 100 }
    };
    liveLimit.scheduleBlockNotes(liveChord);
    juce::AudioBuffer<float> liveWarmup(1, 1024);
    liveWarmup.clear();
    liveLimit.processBlock(liveWarmup, midi);
    setVoiceCount(liveLimit, 1.0);
    require(liveLimit.getVoiceCount() == 1, "live polyphony reduction selects mono mode");
    const std::vector<MachineScheduledNote> noNotes;
    liveLimit.scheduleBlockNotes(noNotes);
    juce::AudioBuffer<float> liveRelease(1, 256);
    liveRelease.clear();
    liveLimit.processBlock(liveRelease, midi);
    require(absoluteEnergy(liveRelease) > 0.0,
            "live polyphony reduction preserves the newest voice and release tails");

    return 0;
}
