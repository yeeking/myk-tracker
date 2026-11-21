#include <JuceHeader.h>

#include "machines/FmSynthMachine.h"

#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace
{
void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

juce::AudioBuffer<float> render(FmSynthMachine& synth, int note = 69, int velocity = 100,
                                double seconds = 0.5, double sampleRate = 48000.0)
{
    synth.prepareToPlay(sampleRate, 256);
    std::vector<MachineScheduledNote> notes { { 0, static_cast<unsigned short>(note), static_cast<unsigned short>(velocity), 0.0, false } };
    synth.scheduleBlockNotes(notes);
    juce::AudioBuffer<float> result(1, static_cast<int>(seconds * sampleRate));
    result.clear();
    juce::MidiBuffer midi;
    synth.processBlock(result, midi);
    return result;
}

float peak(const juce::AudioBuffer<float>& buffer)
{
    return buffer.getMagnitude(0, buffer.getNumSamples());
}

void insertControl(FmSynthMachine& synth, int labelColumn, const char* label, double value)
{
    auto boxes = synth.getControlBoxes({});
    for (std::size_t row = 0; row < boxes[static_cast<std::size_t>(labelColumn)].size(); ++row)
        if (boxes[static_cast<std::size_t>(labelColumn)][row].text == label)
        {
            auto& cell = boxes[static_cast<std::size_t>(labelColumn + 1)][row];
            require(static_cast<bool>(cell.onInsert), "FM control does not support direct insertion");
            cell.onInsert(value);
            return;
        }
    throw std::runtime_error(std::string("missing FM control: ") + label);
}

void adjustControl(FmSynthMachine& synth, int labelColumn, const char* label, int amount)
{
    auto boxes = synth.getControlBoxes({});
    for (std::size_t row = 0; row < boxes[static_cast<std::size_t>(labelColumn)].size(); ++row)
        if (boxes[static_cast<std::size_t>(labelColumn)][row].text == label)
        {
            auto& cell = boxes[static_cast<std::size_t>(labelColumn + 1)][row];
            require(static_cast<bool>(cell.onAdjust), "FM control does not support adjustment");
            cell.onAdjust(amount, kCoarseAdjustRangeFraction);
            return;
        }
    throw std::runtime_error(std::string("missing FM control: ") + label);
}

double toneAmplitude(const juce::AudioBuffer<float>& buffer, double sampleRate,
                     double frequency, double startSeconds, double durationSeconds)
{
    const int start = static_cast<int>(startSeconds * sampleRate);
    const int count = std::min(static_cast<int>(durationSeconds * sampleRate), buffer.getNumSamples() - start);
    double real = 0.0, imaginary = 0.0;
    for (int sample = 0; sample < count; ++sample)
    {
        const double phase = 2.0 * juce::MathConstants<double>::pi * frequency
            * static_cast<double>(sample) / sampleRate;
        const double value = buffer.getSample(0, start + sample);
        real += value * std::cos(phase);
        imaginary -= value * std::sin(phase);
    }
    return count > 0 ? 2.0 * std::sqrt(real * real + imaginary * imaginary) / static_cast<double>(count) : 0.0;
}

double rmsInWindow(const juce::AudioBuffer<float>& buffer, double sampleRate,
                   double startSeconds, double durationSeconds)
{
    const int start = static_cast<int>(startSeconds * sampleRate);
    const int count = std::min(static_cast<int>(durationSeconds * sampleRate), buffer.getNumSamples() - start);
    double energy = 0.0;
    for (int sample = 0; sample < count; ++sample)
    {
        const double value = buffer.getSample(0, start + sample);
        energy += value * value;
    }
    return count > 0 ? std::sqrt(energy / static_cast<double>(count)) : 0.0;
}

double spectralActivity(const juce::AudioBuffer<float>& buffer, double sampleRate,
                        double startSeconds, double durationSeconds)
{
    const int start = static_cast<int>(startSeconds * sampleRate);
    const int count = std::min(static_cast<int>(durationSeconds * sampleRate), buffer.getNumSamples() - start);
    double signalEnergy = 0.0, differenceEnergy = 0.0;
    float previous = buffer.getSample(0, start);
    for (int sample = 1; sample < count; ++sample)
    {
        const float value = buffer.getSample(0, start + sample);
        signalEnergy += static_cast<double>(value) * value;
        const double difference = static_cast<double>(value) - previous;
        differenceEnergy += difference * difference;
        previous = value;
    }
    return signalEnergy > 0.0 ? std::sqrt(differenceEnergy / signalEnergy) : 0.0;
}
}

int main()
{
    try
    {
        FmSynthMachine synth;
        auto boxes = synth.getUIBoxes({});
        require(boxes.size() == 12 && boxes.front().size() == 11, "FM visual grid dimensions changed");
        require(boxes[0][0].text == "OP1" && boxes[3][0].text == "OP2" && boxes[9][0].text == "OP4",
                "FM operator lanes are missing");
        auto logical = synth.getControlBoxes({});
        require(logical.size() == 15 && logical.front().size() == 22, "FM logical grid dimensions changed");
        require(logical[0][0].text == "PSET" && logical[2][0].text == "OP1" && logical[10][0].text == "FROM",
                "FM global/operator/matrix sections are missing");

        const std::array<FmSynthMachine::UiBank, 5> banks {
            FmSynthMachine::UiBank::core, FmSynthMachine::UiBank::envelope,
            FmSynthMachine::UiBank::scaling, FmSynthMachine::UiBank::route,
            FmSynthMachine::UiBank::global
        };
        for (auto bank : banks)
        {
            require(synth.getUiBank() == bank, "FM UI bank order changed");
            const auto cursor = synth.consumePreferredCursorPosition();
            if (bank != FmSynthMachine::UiBank::core)
                require(cursor.has_value(), "FM UI bank did not provide a preferred cursor");
            require(synth.cycleUiPanel(1), "FM UI bank did not cycle");
        }
        require(synth.getUiBank() == FmSynthMachine::UiBank::core, "FM UI bank did not wrap");

        synth.setUiEnvelopeMonitoring(true);
        render(synth, 60, 100, 0.02);
        FmSynthMachine::UiEnvelopeSnapshot snapshot;
        synth.getUiEnvelopeSnapshot(snapshot);
        bool foundLiveVoice = false;
        for (const auto& voice : snapshot.voices)
            if (voice.active)
            {
                foundLiveVoice = true;
                require(voice.midiNote == 60, "FM UI snapshot MIDI note is incorrect");
                for (int op = 0; op < 4; ++op)
                {
                    require(voice.stages[static_cast<std::size_t>(op)] >= 0
                                && voice.stages[static_cast<std::size_t>(op)] < 4,
                            "FM UI snapshot stage is invalid");
                    require(voice.progress[static_cast<std::size_t>(op)] >= 0.0f
                                && voice.progress[static_cast<std::size_t>(op)] <= 1.0f,
                            "FM UI snapshot progress is invalid");
                }
            }
        require(foundLiveVoice, "FM UI snapshot did not publish an active voice");
        synth.allNotesOff();
        synth.getUiEnvelopeSnapshot(snapshot);
        for (const auto& voice : snapshot.voices)
            require(!voice.active, "FM UI snapshot left a stale marker after all-notes-off");
        synth.setUiEnvelopeMonitoring(false);

        auto initial = render(synth);
        require(peak(initial) > 0.001f, "INIT preset did not render audio");
        for (int sample = 0; sample < initial.getNumSamples(); ++sample)
            require(std::isfinite(initial.getSample(0, sample)), "INIT emitted non-finite audio");

        // A simple sine pair uses matrix values as the Chowning modulation
        // index in radians. With c:m=4:1 there are no low-order reflected
        // components at the carrier, so J1(I)/J0(I) can be checked directly.
        synth.applyPreset(FmSynthMachine::Preset::init);
        insertControl(synth, 2, "CRS", 4.0); // carrier ratio
        insertControl(synth, 4, "CRS", 1.0); // modulator ratio
        for (const char* label : { "L1", "L2", "L3" })
            insertControl(synth, 4, label, 100.0);
        auto matrix = synth.getControlBoxes({});
        require(static_cast<bool>(matrix[11][2].onInsert), "OP2 to OP1 matrix route is editable");
        matrix[11][2].onInsert(1.0);
        matrix[11][2].onAdjust(1, kCoarseAdjustRangeFraction);
        require(synth.getControlBoxes({})[11][2].text == "1.32",
                "FM matrix coarse adjust moves 1% of the route range (quantum 0.01)");
        matrix = synth.getControlBoxes({});
        matrix[11][2].onInsert(1.0);
        matrix[11][2].onAdjust(1, kFineAdjustRangeFraction);
        require(synth.getControlBoxes({})[11][2].text == "1.03",
                "FM matrix fine adjust moves 0.1% of the route range (quantum 0.01)");
        for (const double index : { 0.5, 1.0, 2.0, 4.0 })
        {
            matrix[11][2].onInsert(index);
            auto theoretical = render(synth, 45, 127, 1.0); // A2=110 Hz, carrier=440 Hz
            const double carrier = toneAmplitude(theoretical, 48000.0, 440.0, 0.25, 0.5);
            const double upperFirstSideband = toneAmplitude(theoretical, 48000.0, 550.0, 0.25, 0.5);
            const double measuredRatio = upperFirstSideband / carrier;
            const double expectedRatio = std::abs(std::cyl_bessel_j(1, index) / std::cyl_bessel_j(0, index));
            require(std::abs(measuredRatio - expectedRatio) < 0.08 + expectedRatio * 0.05,
                    "simple sine FM sideband amplitude does not follow the Bessel prediction");
        }

        // Fixed-frequency carriers remain fixed as different MIDI pitches are
        // played, which is the basis of the formant/resonance technique.
        synth.applyAlgorithm(FmSynthMachine::Algorithm::additive);
        insertControl(synth, 4, "OUT", 0.0); insertControl(synth, 6, "OUT", 0.0); insertControl(synth, 8, "OUT", 0.0);
        adjustControl(synth, 2, "MODE", 1); insertControl(synth, 2, "FIX", 2000.0);
        auto fixedLow = render(synth, 48, 100, 0.75);
        auto fixedHigh = render(synth, 84, 100, 0.75);
        require(toneAmplitude(fixedLow, 48000.0, 2000.0, 0.2, 0.4) > 0.05
                    && toneAmplitude(fixedHigh, 48000.0, 2000.0, 0.2, 0.4) > 0.05,
                "fixed-frequency operator did not retain its resonance across notes");

        for (int algorithm = 0; algorithm < 8; ++algorithm)
        {
            synth.applyAlgorithm(static_cast<FmSynthMachine::Algorithm>(algorithm));
            auto audio = render(synth, 60 + algorithm, 110, 0.1);
            for (int sample = 0; sample < audio.getNumSamples(); ++sample)
                require(std::isfinite(audio.getSample(0, sample)), "algorithm emitted non-finite audio");
        }

        for (int preset = 0; preset < static_cast<int>(FmSynthMachine::Preset::custom); ++preset)
        {
            synth.applyPreset(static_cast<FmSynthMachine::Preset>(preset));
            auto audio = render(synth, 48 + preset * 3, 127, 0.1);
            require(peak(audio) < 8.0f, "factory preset exceeded bounded output");
        }

        synth.applyPreset(FmSynthMachine::Preset::evolve);
        constexpr double evolveRate = 24000.0;
        auto evolving = render(synth, 45, 110, 28.0, evolveRate);
        const double earlyActivity = spectralActivity(evolving, evolveRate, 1.0, 2.0);
        const double middleActivity = spectralActivity(evolving, evolveRate, 12.0, 2.0);
        const double lateActivity = spectralActivity(evolving, evolveRate, 24.0, 2.0);
        require(rmsInWindow(evolving, evolveRate, 1.0, 2.0) > 0.005
                    && rmsInWindow(evolving, evolveRate, 24.0, 2.0) > 0.005,
                "EVOLVE did not remain audible throughout a held note");
        require(std::abs(earlyActivity - middleActivity) > 0.03
                    && std::abs(middleActivity - lateActivity) > 0.03,
                "EVOLVE did not produce distinct early, middle and late spectra");

        juce::MemoryBlock evolvingState;
        synth.getStateInformation(evolvingState);
        FmSynthMachine evolvingRestored;
        evolvingRestored.setStateInformation(evolvingState.getData(), static_cast<int>(evolvingState.getSize()));
        require(evolvingRestored.getControlBoxes({})[1][0].text == "EVOLVE",
                "EVOLVE preset identity did not survive state round trip");

        const char legacyCustom[] = "{\"version\":1,\"preset\":12}";
        evolvingRestored.setStateInformation(legacyCustom, static_cast<int>(sizeof(legacyCustom) - 1));
        require(evolvingRestored.getControlBoxes({})[1][0].text == "CUST",
                "version-1 custom FM preset identity did not migrate safely");

        insertControl(synth, 0, "VOI", 8.0);
        insertControl(synth, 2, "CRS", 7.0);
        insertControl(synth, 2, "FINE", 25.0);
        insertControl(synth, 2, "FIX", 2000.0);
        insertControl(synth, 2, "DET", -12.5);
        insertControl(synth, 2, "VEL", 100.0);
        insertControl(synth, 2, "BP", 72.0);
        require(synth.getVoiceCount() == 8, "voice-count insertion failed");

        juce::MemoryBlock state;
        synth.getStateInformation(state);
        FmSynthMachine restored;
        restored.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
        require(restored.getVoiceCount() == 8, "FM state did not restore voice count");
        auto restoredAudio = render(restored, 72, 90, 0.1);
        require(peak(restoredAudio) > 0.0001f, "restored FM state was silent");

        const char malformed[] = "{\"version\":1,\"voices\":999,\"level\":1e999,\"matrix\":[1e999]}";
        restored.setStateInformation(malformed, static_cast<int>(sizeof(malformed) - 1));
        require(restored.getVoiceCount() == 8, "malformed FM voice count was not clamped");
        auto safeAudio = render(restored, 36, 127, 0.1, 96000.0);
        for (int sample = 0; sample < safeAudio.getNumSamples(); ++sample)
            require(std::isfinite(safeAudio.getSample(0, sample)), "malformed state produced non-finite output");

        std::cout << "FM synth tests passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "FM synth tests failed: " << error.what() << '\n';
        return 1;
    }
}
