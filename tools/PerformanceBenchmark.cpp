#include "TrackerMainProcessor.h"

#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>

namespace
{
constexpr int kBlocks = 4096;
constexpr int kWarmupBlocks = 512;
constexpr int kBlockSize = 256;

const char* stageName(PerformanceMetrics::Stage stage)
{
    static constexpr const char* names[] = { "callback", "sequencing", "modulation", "sampler", "wavetable", "drum", "distortion", "delay", "channel", "filter", "aux1", "aux2", "scope_meter", "master" };
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

void configureScene(TrackerMainProcessor& processor, bool stress, bool effects, bool modulation)
{
    for (std::size_t stack = 0; stack < processor.getMachineStackCount(); ++stack)
    {
        if (stack == 0 || stress)
        {
            addAllMachines(processor, stack);
            if (!effects)
            {
                const auto types = processor.getMachineStackTypes(stack);
                for (std::size_t slot = 0; slot < types.size(); ++slot)
                    if (types[slot] != CommandType::WavetableSynth)
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
                }
            }
        }
        else
        {
            setEnabled(processor, stack, false);
        }
    }
}

void scheduleNotes(TrackerMainProcessor& processor, bool stress, int block)
{
    if (block % 32 != 0)
        return;
    const auto stackCount = stress ? processor.getMachineStackCount() : 1u;
    for (std::size_t stack = 0; stack < stackCount; ++stack)
        for (int voice = 0; voice < (stress ? 8 : 3); ++voice)
            processor.sendMessageToMachine(CommandType::WavetableSynth, static_cast<unsigned short>(stack),
                                           static_cast<unsigned short>(48 + (voice * 4) + static_cast<int>(stack % 3)), 100, 64);
}

struct Result { juce::String scenario; double sampleRate = 0.0; PerformanceMetrics::Snapshot metrics; };

Result runScenario(const juce::String& name, double sampleRate, bool stress, bool effects, bool modulation)
{
    TrackerMainProcessor processor;
    processor.setInternalClockEnabled(true);
    processor.prepareToPlay(sampleRate, kBlockSize);
    configureScene(processor, stress, effects, modulation);
    juce::AudioBuffer<float> buffer(2, kBlockSize);
    juce::MidiBuffer midi;
    for (int block = 0; block < kWarmupBlocks; ++block)
    {
        buffer.clear(); scheduleNotes(processor, stress, block); processor.processBlock(buffer, midi);
    }
    auto& metrics = processor.getPerformanceMetrics();
    metrics.reset(); metrics.setEnabled(true);
    for (int block = 0; block < kBlocks; ++block)
    {
        buffer.clear(); scheduleNotes(processor, stress, block); processor.processBlock(buffer, midi);
    }
    metrics.setEnabled(false);
    return { name, sampleRate, metrics.snapshot() };
}

void writeResult(std::ostream& out, const Result& result, bool csv)
{
    const auto& callback = result.metrics.stages[static_cast<std::size_t>(PerformanceMetrics::Stage::callback)];
    const double deadlineUs = 256.0 / result.sampleRate * 1000000.0;
    if (csv)
    {
        for (std::size_t i = 0; i < result.metrics.stages.size(); ++i)
            out << result.scenario << ',' << result.sampleRate << ',' << stageName(static_cast<PerformanceMetrics::Stage>(i)) << ','
                << result.metrics.stages[i].calls << ',' << result.metrics.stages[i].meanUs << ',' << result.metrics.stages[i].maxUs << ','
                << result.metrics.p95CallbackUs << ',' << result.metrics.p99CallbackUs << ',' << result.metrics.maxCallbackUs << ',' << deadlineUs << ',' << (callback.meanUs / deadlineUs * 100.0) << '\n';
        return;
    }
    out << "## " << result.scenario << " — " << result.sampleRate << " Hz\n\n";
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
        << ",\"callback\":{\"meanUs\":" << callback.meanUs << ",\"p95Us\":" << result.metrics.p95CallbackUs
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
    csv << "scenario,sample_rate,stage,calls,mean_us,max_us,callback_p95_us,callback_p99_us,callback_max_us,deadline_us,mean_deadline_percent\n";
    report << "# MYK Tracker Dynamic Audio Benchmark\n\nThis is a deterministic offline audio-callback benchmark. It measures CPU time, not real device xruns or OpenGL rendering.\n\n";
    json << "{\n \"blockSize\":" << kBlockSize << ",\n \"results\":[\n";
    int resultIndex = 0;
    for (const auto rate : { 44100.0, 48000.0 })
        for (const auto& scenario : { std::tuple<const char*, bool, bool, bool>{ "wave_only", false, false, false }, { "typical_effects", false, true, false }, { "typical_modulation", false, true, true }, { "stress_all_stacks", true, true, true } })
        {
            const auto result = runScenario(std::get<0>(scenario), rate, std::get<1>(scenario), std::get<2>(scenario), std::get<3>(scenario));
            writeResult(csv, result, true); writeResult(report, result, false);
            writeJsonResult(json, result, resultIndex++ < 7);
            std::cout << result.scenario << " at " << rate << " Hz complete\n";
        }
    json << " ]\n}\n";
    return 0;
}
