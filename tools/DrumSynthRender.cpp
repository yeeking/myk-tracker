#include <JuceHeader.h>

#include "machines/DrumSynthMachine.h"

#include <cstdlib>
#include <iostream>
#include <vector>

namespace
{
int noteForVoice(const juce::String& voice)
{
    if (voice == "kick") return 72;
    if (voice == "snare") return 73;
    if (voice == "hat") return 74;
    if (voice == "openhat") return 86;
    if (voice == "crash") return 75;
    if (voice == "ride") return 76;
    if (voice == "tom") return 77;
    if (voice == "clap") return 78;
    return -1;
}

void usage()
{
    std::cerr << "Usage: drum-synth-render --output <wav> [--preset 808|909]"
                 " [--voice kick|snare|hat|openhat|crash|ride|tom|clap] [--velocity 1..127]"
                 " [--rate Hz] [--seconds duration] [--block-size samples]"
                 " [--repeat-ms interval] [--count hits]"
                 " [--set voice.CONTROL=value]...\n";
}

int labelColumnForVoice(juce::String voice)
{
    voice = voice.toLowerCase();
    if (voice == "kick") return 0;
    if (voice == "snare") return 2;
    if (voice == "hat" || voice == "openhat") return 4;
    if (voice == "crash") return 6;
    if (voice == "ride") return 8;
    if (voice == "tom") return 10;
    if (voice == "clap") return 12;
    return -1;
}

bool applyOverride(DrumSynthMachine& synth, const juce::String& specification)
{
    const int dot = specification.indexOfChar('.');
    const int equals = specification.indexOfChar('=');
    if (dot <= 0 || equals <= dot + 1)
        return false;

    const int labelColumn = labelColumnForVoice(specification.substring(0, dot));
    if (labelColumn < 0)
        return false;
    const auto label = specification.substring(dot + 1, equals).trim().toUpperCase();
    const auto valueText = specification.substring(equals + 1).trim();
    const auto valueUtf8 = valueText.toStdString();
    char* end = nullptr;
    const double value = std::strtod(valueUtf8.c_str(), &end);
    if (end == valueUtf8.c_str() || *end != '\0' || !std::isfinite(value))
        return false;

    auto cells = synth.getUIBoxes({});
    for (std::size_t row = 0; row < cells[static_cast<std::size_t>(labelColumn)].size(); ++row)
    {
        if (juce::String(cells[static_cast<std::size_t>(labelColumn)][row].text).toUpperCase() != label)
            continue;
        auto& control = cells[static_cast<std::size_t>(labelColumn + 1)][row];
        if (control.onInsert == nullptr)
            return false;
        control.onInsert(value);
        return true;
    }
    return false;
}
}

int main(int argc, char* argv[])
{
    juce::String outputPath;
    juce::String voice = "snare";
    int preset = 1;
    int velocity = 120;
    double sampleRate = 44100.0;
    double seconds = 8.0;
    int blockSize = 512;
    double repeatMilliseconds = 0.0;
    int repeatCount = 1;
    std::vector<juce::String> overrides;
    for (int index = 1; index < argc; ++index)
    {
        const juce::String argument(argv[index]);
        if (argument == "--output" && index + 1 < argc) outputPath = argv[++index];
        else if (argument == "--voice" && index + 1 < argc) voice = juce::String(argv[++index]).toLowerCase();
        else if (argument == "--preset" && index + 1 < argc) preset = std::atoi(argv[++index]);
        else if (argument == "--velocity" && index + 1 < argc) velocity = std::atoi(argv[++index]);
        else if (argument == "--rate" && index + 1 < argc) sampleRate = std::atof(argv[++index]);
        else if (argument == "--seconds" && index + 1 < argc) seconds = std::atof(argv[++index]);
        else if (argument == "--block-size" && index + 1 < argc) blockSize = std::atoi(argv[++index]);
        else if (argument == "--repeat-ms" && index + 1 < argc) repeatMilliseconds = std::atof(argv[++index]);
        else if (argument == "--count" && index + 1 < argc) repeatCount = std::atoi(argv[++index]);
        else if (argument == "--set" && index + 1 < argc) overrides.emplace_back(argv[++index]);
        else { usage(); return 2; }
    }

    const int note = noteForVoice(voice);
    if (outputPath.isEmpty() || note < 0 || (preset != 808 && preset != 909)
        || velocity < 1 || velocity > 127 || sampleRate < 8000.0 || seconds <= 0.0
        || blockSize <= 0 || repeatMilliseconds < 0.0 || repeatCount < 1
        || (repeatCount > 1 && repeatMilliseconds <= 0.0))
    {
        usage();
        return 2;
    }

    const int repeatSamples = static_cast<int>(std::lround(sampleRate * repeatMilliseconds * 0.001));
    const int requestedSamples = static_cast<int>(std::ceil(sampleRate * seconds));
    const int requiredSamples = repeatCount > 1 ? repeatSamples * (repeatCount - 1) + static_cast<int>(sampleRate) : 0;
    const int samples = std::max(requestedSamples, requiredSamples);
    DrumSynthMachine synth;
    synth.prepareToPlay(sampleRate, blockSize);
    synth.applyKitPreset(preset == 909 ? DrumSynthMachine::KitPreset::tr909 : DrumSynthMachine::KitPreset::tr808);
    for (const auto& overrideValue : overrides)
        if (!applyOverride(synth, overrideValue))
        {
            std::cerr << "Invalid --set value: " << overrideValue << '\n';
            return 2;
        }

    juce::AudioBuffer<float> output(1, samples);
    output.clear();
    juce::MidiBuffer midi;
    std::vector<MachineScheduledNote> blockNotes;
    for (int offset = 0; offset < samples; offset += blockSize)
    {
        const int count = std::min(blockSize, samples - offset);
        juce::AudioBuffer<float> block(1, count);
        block.clear();
        blockNotes.clear();
        for (int hit = 0; hit < repeatCount; ++hit)
        {
            const int hitOffset = hit * repeatSamples;
            if (hitOffset >= offset && hitOffset < offset + count)
                blockNotes.push_back({ hitOffset - offset, static_cast<unsigned short>(note),
                                       static_cast<unsigned short>(velocity), 8.0, false });
        }
        synth.scheduleBlockNotes(blockNotes);
        synth.processBlock(block, midi);
        output.copyFrom(0, offset, block, 0, 0, count);
    }

    const juce::File destination(outputPath);
    destination.getParentDirectory().createDirectory();
    auto stream = std::unique_ptr<juce::FileOutputStream>(destination.createOutputStream());
    if (stream == nullptr || !stream->openedOk())
    {
        std::cerr << "Could not open " << outputPath << " for writing\n";
        return 1;
    }
    juce::WavAudioFormat wav;
    auto writer = std::unique_ptr<juce::AudioFormatWriter>(wav.createWriterFor(stream.release(), sampleRate, 1, 24, {}, 0));
    if (writer == nullptr || !writer->writeFromAudioSampleBuffer(output, 0, samples))
    {
        std::cerr << "Could not write " << outputPath << "\n";
        return 1;
    }
    return 0;
}
