#include "TrackerMainProcessor.h"

#include <array>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>

namespace
{
constexpr int kBlocks = 4096;
constexpr int kWarmupBlocks = 512;

const char* stageName(PerformanceMetrics::Stage stage)
{
    static constexpr const char* names[] = { "callback", "sequencing", "modulation", "sampler", "wavetable", "drum", "fm", "distortion", "delay", "channel", "filter", "aux1", "aux2", "scope_meter", "master" };
    return names[static_cast<std::size_t>(stage)];
}

void addAllMachines(TrackerMainProcessor& processor, std::size_t stack)
{
    for (std::size_t i = 0; i < kSlotCycleTypes.size(); ++i)
        processor.addMachineToStack(stack);
}

void setEnabled(TrackerMainProcessor& processor, std::size_t stack, bool enabled)
{
    const auto types = processor.getMachineStackTypes(stack);
    for (std::size_t slot = 0; slot < types.size(); ++slot)
        if (processor.isMachineEnabledInStack(stack, slot) != enabled)
            processor.toggleMachineEnabledInStack(stack, slot);
}

void configureScene(TrackerMainProcessor& processor, bool stress, bool effects, bool modulation, CommandType source)
{
    for (std::size_t stack = 0; stack < processor.getMachineStackCount(); ++stack)
    {
        if (stack == 0 || stress)
        {
            addAllMachines(processor, stack);
            const auto types = processor.getMachineStackTypes(stack);
            for (std::size_t slot = 0; slot < types.size(); ++slot)
            {
                const auto type = types[slot];
                const bool isOtherTerminal = (type == CommandType::Sampler || type == CommandType::WavetableSynth
                    || type == CommandType::DrumSynth || type == CommandType::FmSynth) && type != source;
                if (isOtherTerminal || (!effects && type != source))
                    processor.toggleMachineEnabledInStack(stack, slot);
            }
            if (modulation != processor.isModulationEnabledInStack(stack))
                processor.toggleModulationEnabledInStack(stack);
            if (modulation)
            {
                // LFO1 -> Wave F1: ensure this is a routed, audio-rate MOD
                // measurement rather than merely the bypass/event path.
                if (auto* matrix = dynamic_cast<ModulationMatrixMachine*>(processor.getMachine(CommandType::ModulationMatrix, stack)))
                {
                    auto cells = matrix->getUIBoxes({});
                    if (cells.size() > 4 && cells[2].size() > 1 && cells[4].size() > 1)
                    {
                        if (cells[2][1].onInsert) cells[2][1].onInsert(80.0);
                        if (cells[4][1].onInsert) cells[4][1].onInsert(75.0);
                    }
                    if (source == CommandType::FmSynth)
                    {
                        for (int page = 0; page < 32; ++page)
                        {
                            cells = matrix->getUIBoxes({});
                            bool found = false;
                            for (std::size_t column = 4; column < cells.size(); ++column)
                                if (cells[column][0].text == "FM IDX")
                                {
                                    if (cells[column][1].onInsert) cells[column][1].onInsert(75.0);
                                    found = true;
                                    break;
                                }
                            if (found || !matrix->navigateRight()) break;
                        }
                    }
                }
            }
            if (source == CommandType::FmSynth)
                if (auto* fm = dynamic_cast<FmSynthMachine*>(processor.getMachine(CommandType::FmSynth, stack)))
                {
                    fm->applyPreset(stress ? FmSynthMachine::Preset::noise : FmSynthMachine::Preset::cascade);
                    auto cells = fm->getUIBoxes({});
                    if (cells[1][2].onInsert) cells[1][2].onInsert(stress ? 8.0 : 4.0);
                    if (stress)
                        for (std::size_t column = 11; column < 15; ++column)
                            for (std::size_t row = 1; row < 5; ++row)
                                if (cells[column][row].onInsert) cells[column][row].onInsert(2.0);
                }
        }
        else
        {
            setEnabled(processor, stack, false);
        }
    }
}

void scheduleNotes(TrackerMainProcessor& processor, bool stress, CommandType source, int block)
{
    if (block % 32 != 0)
        return;
    const auto stackCount = stress ? processor.getMachineStackCount() : 1u;
    for (std::size_t stack = 0; stack < stackCount; ++stack)
        for (int voice = 0; voice < (stress ? 8 : 3); ++voice)
        {
            const auto note = source == CommandType::DrumSynth ? static_cast<unsigned short>(72 + (voice % 7))
                                                               : static_cast<unsigned short>(48 + (voice * 4) + static_cast<int>(stack % 3));
            processor.sendMessageToMachine(source, static_cast<unsigned short>(stack), note, 100, 64);
        }
}

struct Result { juce::String scenario; double sampleRate = 0.0; int blockSize = 0; PerformanceMetrics::Snapshot metrics; };

Result runScenario(const juce::String& name, double sampleRate, int blockSize,
                   bool stress, bool effects, bool modulation, CommandType source)
{
    TrackerMainProcessor processor;
    processor.setInternalClockEnabled(true);
    processor.prepareToPlay(sampleRate, blockSize);
    configureScene(processor, stress, effects, modulation, source);
    juce::AudioBuffer<float> buffer(2, blockSize);
    juce::MidiBuffer midi;
    for (int block = 0; block < kWarmupBlocks; ++block)
    {
        buffer.clear(); scheduleNotes(processor, stress, source, block); processor.processBlock(buffer, midi);
    }
    auto& metrics = processor.getPerformanceMetrics();
    metrics.reset(); metrics.setEnabled(true);
    for (int block = 0; block < kBlocks; ++block)
    {
        buffer.clear(); scheduleNotes(processor, stress, source, block); processor.processBlock(buffer, midi);
    }
    metrics.setEnabled(false);
    return { name, sampleRate, blockSize, metrics.snapshot() };
}

void writeResult(std::ostream& out, const Result& result, bool csv)
{
    const auto& callback = result.metrics.stages[static_cast<std::size_t>(PerformanceMetrics::Stage::callback)];
    const double deadlineUs = static_cast<double>(result.blockSize) / result.sampleRate * 1000000.0;
    if (csv)
    {
        for (std::size_t i = 0; i < result.metrics.stages.size(); ++i)
            out << result.scenario << ',' << result.sampleRate << ',' << result.blockSize << ',' << stageName(static_cast<PerformanceMetrics::Stage>(i)) << ','
                << result.metrics.stages[i].calls << ',' << result.metrics.stages[i].meanUs << ',' << result.metrics.stages[i].maxUs << ','
                << result.metrics.p95CallbackUs << ',' << result.metrics.p99CallbackUs << ',' << result.metrics.maxCallbackUs << ',' << deadlineUs << ',' << (callback.meanUs / deadlineUs * 100.0) << '\n';
        return;
    }
    out << "## " << result.scenario << " — " << result.sampleRate << " Hz / " << result.blockSize << " samples\n\n";
    out << "Callback mean " << std::fixed << std::setprecision(2) << callback.meanUs << " us; p95 " << result.metrics.p95CallbackUs
        << " us; p99 " << result.metrics.p99CallbackUs << " us; max " << result.metrics.maxCallbackUs
        << " us; deadline use " << (callback.meanUs / deadlineUs * 100.0) << "% of " << deadlineUs << " us.\n\n";
    out << "| Stage | Mean us | Max us | Calls |\n|---|---:|---:|---:|\n";
    for (std::size_t i = 1; i < result.metrics.stages.size(); ++i)
        out << '|' << stageName(static_cast<PerformanceMetrics::Stage>(i)) << '|' << result.metrics.stages[i].meanUs << '|' << result.metrics.stages[i].maxUs << '|' << result.metrics.stages[i].calls << "|\n";
    out << '\n';
}

void writeJsonResult(std::ostream& out, const Result& result, bool trailingComma)
{
    const auto& callback = result.metrics.stages[static_cast<std::size_t>(PerformanceMetrics::Stage::callback)];
    out << "  {\"scenario\":\"" << result.scenario << "\",\"sampleRate\":" << result.sampleRate
        << ",\"blockSize\":" << result.blockSize << ",\"callback\":{\"meanUs\":" << callback.meanUs << ",\"p95Us\":" << result.metrics.p95CallbackUs
        << ",\"p99Us\":" << result.metrics.p99CallbackUs << ",\"maxUs\":" << result.metrics.maxCallbackUs << "},\"stages\":{\n";
    for (std::size_t i = 1; i < result.metrics.stages.size(); ++i)
    {
        const auto& stage = result.metrics.stages[i];
        out << "    \"" << stageName(static_cast<PerformanceMetrics::Stage>(i)) << "\":{\"calls\":" << stage.calls
            << ",\"meanUs\":" << stage.meanUs << ",\"maxUs\":" << stage.maxUs << "}" << (i + 1 == result.metrics.stages.size() ? "\n" : ",\n");
    }
    out << "  }}" << (trailingComma ? ',' : ' ') << '\n';
}
}

int main(int argc, char* argv[])
{
    const juce::File outputDirectory = argc > 1 ? juce::File(argv[1]) : juce::File::getCurrentWorkingDirectory().getChildFile("performance-results");
    outputDirectory.createDirectory();
    std::ofstream csv(outputDirectory.getChildFile("audio-benchmark.csv").getFullPathName().toStdString());
    std::ofstream report(outputDirectory.getChildFile("audio-benchmark.md").getFullPathName().toStdString());
    std::ofstream json(outputDirectory.getChildFile("audio-benchmark.json").getFullPathName().toStdString());
    csv << "scenario,sample_rate,block_size,stage,calls,mean_us,max_us,callback_p95_us,callback_p99_us,callback_max_us,deadline_us,mean_deadline_percent\n";
    report << "# MYK Tracker Dynamic Audio Benchmark\n\nThis is a deterministic offline audio-callback benchmark. It measures CPU time, not real device xruns or OpenGL rendering.\n\n";
    json << "{\n \"results\":[\n";
    const std::array<std::tuple<const char*, bool, bool, bool, CommandType>, 9> scenarios {{
        { "wave_only", false, false, false, CommandType::WavetableSynth }, { "typical_effects", false, true, false, CommandType::WavetableSynth },
        { "typical_modulation", false, true, true, CommandType::WavetableSynth }, { "stress_all_stacks", true, true, true, CommandType::WavetableSynth },
        { "drum_only", false, false, false, CommandType::DrumSynth }, { "drum_typical_effects", false, true, false, CommandType::DrumSynth },
        { "drum_stress_all_stacks", true, true, true, CommandType::DrumSynth },
        { "fm_only", false, false, false, CommandType::FmSynth }, { "fm_stress_all_stacks", true, true, true, CommandType::FmSynth }
    }};
    struct ScenarioRun { const char* name; double rate; int blockSize; bool stress; bool effects; bool modulation; CommandType source; };
    std::vector<ScenarioRun> runs;
    for (const auto rate : { 44100.0, 48000.0 })
        for (const auto& scenario : scenarios)
            runs.push_back({ std::get<0>(scenario), rate, 256, std::get<1>(scenario), std::get<2>(scenario), std::get<3>(scenario), std::get<4>(scenario) });
    for (const auto rate : { 44100.0, 48000.0, 96000.0 })
        for (const int blockSize : { 64, 128, 512 })
        {
            runs.push_back({ "fm_only", rate, blockSize, false, false, false, CommandType::FmSynth });
            runs.push_back({ "fm_stress_all_stacks", rate, blockSize, true, true, true, CommandType::FmSynth });
        }
    for (std::size_t index = 0; index < runs.size(); ++index)
    {
        const auto& run = runs[index];
        const auto result = runScenario(run.name, run.rate, run.blockSize, run.stress, run.effects, run.modulation, run.source);
        writeResult(csv, result, true); writeResult(report, result, false);
        writeJsonResult(json, result, index + 1 < runs.size());
        std::cout << result.scenario << " at " << run.rate << " Hz / " << run.blockSize << " samples complete\n";
    }
    json << " ]\n}\n";
    return 0;
}
