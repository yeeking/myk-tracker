#include <JuceHeader.h>
#include "ModulationMatrixMachine.h"

#include <cmath>

namespace
{
constexpr float kPi = 3.14159265358979323846f;
constexpr float kPitchTableRange = 48.0f;
float wrap(float phase) noexcept { return phase - std::floor(phase); }
float sampleWave(ModulationMatrixMachine::Waveform waveform, double phase, float held) noexcept
{
    const float p = wrap(static_cast<float>(phase));
    switch (waveform)
    {
        case ModulationMatrixMachine::Waveform::sine: return std::sin(2.0f * kPi * p);
        case ModulationMatrixMachine::Waveform::triangle: return 1.0f - 4.0f * std::abs(p - 0.5f);
        case ModulationMatrixMachine::Waveform::saw: return 2.0f * p - 1.0f;
        case ModulationMatrixMachine::Waveform::square: return p < 0.5f ? 1.0f : -1.0f;
        case ModulationMatrixMachine::Waveform::sampleHold: return held;
        default: return 0.0f;
    }
}
float rateForCoarse(int value) noexcept { return value <= 0 ? 0.0f : 0.1f * std::pow(10000.0f, static_cast<float>(value - 1) / 126.0f); }
const std::array<float, 8193>& pitchTable()
{
    static const std::array<float, 8193> table = []
    {
        std::array<float, 8193> values {};
        for (std::size_t i = 0; i < values.size(); ++i)
        {
            const float semitones = -kPitchTableRange + 2.0f * kPitchTableRange * static_cast<float>(i) / static_cast<float>(values.size() - 1);
            values[i] = std::exp2(semitones / 12.0f);
        }
        return values;
    }();
    return table;
}
}

ModulationMatrixMachine::ModulationMatrixMachine()
{
    for (int i = 0; i < 4; ++i)
    {
        waveforms[static_cast<std::size_t>(i)] = 0; coarse[static_cast<std::size_t>(i)] = 0; fine[static_cast<std::size_t>(i)] = 0;
        heldValues[static_cast<std::size_t>(i)] = 0.0f; latestLfoValues[static_cast<std::size_t>(i)] = 0.0f;
    }
    for (auto& divider : dividers) divider = 1;
    for (auto& row : depths) for (auto& depth : row) depth = 0;
    for (int i = 0; i < 128; ++i) rateTable[static_cast<std::size_t>(i)] = rateForCoarse(i);
    (void) pitchTable();
}
void ModulationMatrixMachine::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
    for (auto& buffer : buffers) buffer.assign(static_cast<std::size_t>(juce::jmax(1, samplesPerBlock)), 0.0f);
    for (std::size_t i = 0; i < buffers.size(); ++i) blockView.values[i] = buffers[i].data();
}
void ModulationMatrixMachine::releaseResources() { for (auto& buffer : buffers) buffer.clear(); blockView = {}; }
void ModulationMatrixMachine::resetClockDividers() noexcept { dividerCounters.fill(0); }
float ModulationMatrixMachine::pitchRatio(float normalised, float octaveRange) noexcept
{
    const float semitones = juce::jlimit(-1.0f, 1.0f, normalised) * octaveRange * 12.0f;
    const float position = (semitones + kPitchTableRange) * (8192.0f / (2.0f * kPitchTableRange));
    const int index = juce::jlimit(0, 8191, static_cast<int>(position));
    const auto& table = pitchTable();
    return table[static_cast<std::size_t>(index)] + (table[static_cast<std::size_t>(index + 1)] - table[static_cast<std::size_t>(index)]) * (position - static_cast<float>(index));
}
float ModulationMatrixMachine::lfoRate(int index) const noexcept
{
    const int c = coarse[static_cast<std::size_t>(index)].load(std::memory_order_relaxed);
    const int f = fine[static_cast<std::size_t>(index)].load(std::memory_order_relaxed);
    if (c == 0 && f <= 0) return 0.0f;
    const int neighbour = juce::jlimit(0, 127, c + (f < 0 ? -1 : 1));
    return juce::jmax(0.0f, rateTable[static_cast<std::size_t>(c)] + (rateTable[static_cast<std::size_t>(neighbour)] - rateTable[static_cast<std::size_t>(c)]) * std::abs(f) / 100.0f);
}
void ModulationMatrixMachine::compileRoutes()
{
    for (std::size_t d = 0; d < kModulationDestinationCount; ++d)
    {
        int count = 0;
        for (std::size_t s = 0; s < kModulationSourceCount; ++s)
        {
            const int value = depths[s][d].load(std::memory_order_relaxed);
            if (value != 0) compiledRoutes[d][static_cast<std::size_t>(count++)] = { static_cast<unsigned char>(s), value / 100.0f };
        }
        compiledRouteCount[d] = count;
    }
    compiledRevision = revision.load(std::memory_order_acquire);
}
bool ModulationMatrixMachine::isPulseSource(ModulationSource source) noexcept
{
    return source == ModulationSource::clock1 || source == ModulationSource::clock2 || source == ModulationSource::clock3 || source == ModulationSource::noteTrigger;
}
bool ModulationMatrixMachine::isLfoFrequencyDestination(ModulationDestination destination) noexcept
{
    return destination >= ModulationDestination::lfo1Frequency && destination <= ModulationDestination::lfo4Frequency;
}
int ModulationMatrixMachine::sourceForUiRow(int row) noexcept
{
    if (row < 4) return row;
    if (row == 4) return static_cast<int>(ModulationSource::noteTrigger);
    if (row < 8) return static_cast<int>(ModulationSource::clock1) + row - 5;
    return static_cast<int>(ModulationSource::lastNote);
}
float ModulationMatrixMachine::sourceValue(ModulationSource source, ModulationDestination destination, const std::array<float, 4>& lfos, const std::array<float, 3>& clocks, bool notePulse) const noexcept
{
    const auto index = static_cast<std::size_t>(source);
    if (index < 4) return lfos[index];
    if (source == ModulationSource::clock1) return clocks[0];
    if (source == ModulationSource::clock2) return clocks[1];
    if (source == ModulationSource::clock3) return clocks[2];
    if (source == ModulationSource::noteTrigger) return notePulse ? 1.0f : 0.0f;
    if (lastNote < 0) return 0.0f;
    const float semitones = static_cast<float>(lastNote - 69);
    if (destination == ModulationDestination::waveFrequency || destination == ModulationDestination::waveSubFrequency) return juce::jlimit(-1.0f, 1.0f, semitones / 24.0f);
    if (destination == ModulationDestination::filterCutoff || isLfoFrequencyDestination(destination)) return juce::jlimit(-1.0f, 1.0f, semitones / 48.0f);
    return juce::jlimit(-1.0f, 1.0f, semitones / 69.0f);
}
float ModulationMatrixMachine::routeSum(ModulationDestination destination, const std::array<float, 4>& lfos, const std::array<float, 3>& clocks, bool notePulse, bool includePulseSources) const noexcept
{
    float sum = 0.0f;
    const auto d = static_cast<std::size_t>(destination);
    for (int i = 0; i < compiledRouteCount[d]; ++i)
    {
        const auto& route = compiledRoutes[d][static_cast<std::size_t>(i)];
        const auto source = static_cast<ModulationSource>(route.source);
        if (!includePulseSources && isPulseSource(source)) continue;
        sum += route.depth * sourceValue(source, destination, lfos, clocks, notePulse);
    }
    return juce::jlimit(-1.0f, 1.0f, sum);
}
void ModulationMatrixMachine::processModulationBlock(int samples, bool enabled, bool transportPlaying, const ModulationClockTick* ticks, std::size_t tickCount, const std::vector<MachineScheduledNote>& notes)
{
    if (revision.load(std::memory_order_acquire) != compiledRevision) compileRoutes();
    samples = juce::jmin(samples, buffers.empty() ? 0 : static_cast<int>(buffers[0].size()));
    blockView.numSamples = samples;
    matrixActive.store(enabled, std::memory_order_release);
    if (!enabled)
    {
        // Null destinations also cover consumers which read sample pointers directly.
        blockView.values.fill(nullptr);
        for (const auto& note : notes)
            if (note.sampleOffset < samples) lastNote = note.note;
        for (auto& value : latestLfoValues) value.store(0.0f, std::memory_order_relaxed);
        return;
    }
    for (std::size_t d = 0; d < buffers.size(); ++d)
        blockView.values[d] = isLfoFrequencyDestination(static_cast<ModulationDestination>(d))
            ? nullptr : buffers[d].data();

    std::array<float, 4> baseRates {};
    std::array<Waveform, 4> blockWaveforms {};
    std::array<int, 3> blockDividers {};
    for (std::size_t i = 0; i < baseRates.size(); ++i)
    {
        baseRates[i] = lfoRate(static_cast<int>(i));
        blockWaveforms[i] = static_cast<Waveform>(waveforms[i].load(std::memory_order_relaxed));
    }
    for (std::size_t i = 0; i < blockDividers.size(); ++i)
        blockDividers[i] = juce::jmax(1, dividers[i].load(std::memory_order_relaxed));

    std::array<float, 4> displayedValues {};
    std::size_t noteCursor = 0, tickCursor = 0;
    for (int sample = 0; sample < samples; ++sample)
    {
        bool notePulse = false;
        while (noteCursor < notes.size() && notes[noteCursor].sampleOffset <= sample) { lastNote = notes[noteCursor].note; notePulse = true; ++noteCursor; }
        std::array<float, 3> clocks {};
        while (tickCursor < tickCount && ticks[tickCursor].sampleOffset <= sample)
        {
            if (transportPlaying && enabled)
                for (int clock = 0; clock < 3; ++clock)
                    if ((++dividerCounters[static_cast<std::size_t>(clock)] % blockDividers[static_cast<std::size_t>(clock)]) == 0)
                        clocks[static_cast<std::size_t>(clock)] = 1.0f;
            ++tickCursor;
        }
        if (!enabled) continue;
        for (int target = 0; target < 4; ++target)
        {
            const auto destination = static_cast<ModulationDestination>(static_cast<int>(ModulationDestination::lfo1Frequency) + target);
            const auto d = static_cast<std::size_t>(destination);
            for (int routeIndex = 0; routeIndex < compiledRouteCount[d]; ++routeIndex)
            {
                const auto& route = compiledRoutes[d][static_cast<std::size_t>(routeIndex)];
                const auto source = static_cast<ModulationSource>(route.source);
                if (route.depth != 0.0f && isPulseSource(source) && sourceValue(source, destination, displayedValues, clocks, notePulse) != 0.0f)
                { phases[static_cast<std::size_t>(target)] = 0.0; break; }
            }
        }
        std::array<float, 4> lfos {};
        for (int i = 0; i < 4; ++i) lfos[static_cast<std::size_t>(i)] = sampleWave(blockWaveforms[static_cast<std::size_t>(i)], phases[static_cast<std::size_t>(i)], heldValues[static_cast<std::size_t>(i)]);
        displayedValues = lfos;
        for (int i = 0; i < 4; ++i)
        {
            const float baseRate = baseRates[static_cast<std::size_t>(i)];
            if (baseRate == 0.0f) continue;
            const auto destination = static_cast<ModulationDestination>(static_cast<int>(ModulationDestination::lfo1Frequency) + i);
            const double before = phases[static_cast<std::size_t>(i)];
            const double rate = juce::jlimit(0.1f, 1000.0f, baseRate * pitchRatio(routeSum(destination, lfos, clocks, notePulse, false), 4.0f));
            phases[static_cast<std::size_t>(i)] += rate / currentSampleRate;
            phases[static_cast<std::size_t>(i)] -= std::floor(phases[static_cast<std::size_t>(i)]);
            if (blockWaveforms[static_cast<std::size_t>(i)] == Waveform::sampleHold && phases[static_cast<std::size_t>(i)] < before)
            { auto& state = randomStates[static_cast<std::size_t>(i)]; state = state * 1664525u + 1013904223u; heldValues[static_cast<std::size_t>(i)] = static_cast<float>((state >> 8) & 0xffffu) / 32767.5f - 1.0f; }
        }
        for (std::size_t d = 0; d < kModulationDestinationCount; ++d)
            if (!isLfoFrequencyDestination(static_cast<ModulationDestination>(d))) buffers[d][static_cast<std::size_t>(sample)] = routeSum(static_cast<ModulationDestination>(d), lfos, clocks, notePulse, true);
    }
    for (int i = 0; i < 4; ++i) latestLfoValues[static_cast<std::size_t>(i)].store(enabled ? displayedValues[static_cast<std::size_t>(i)] : 0.0f, std::memory_order_relaxed);
}
ModulationMatrixMachine::UiPreviewState ModulationMatrixMachine::getUiPreviewState() const noexcept
{
    UiPreviewState state;
    for (int i = 0; i < 4; ++i) { state.waveforms[static_cast<std::size_t>(i)] = waveforms[static_cast<std::size_t>(i)].load(std::memory_order_relaxed); state.lfoValues[static_cast<std::size_t>(i)] = latestLfoValues[static_cast<std::size_t>(i)].load(std::memory_order_relaxed); }
    state.active = matrixActive.load(std::memory_order_acquire);
    return state;
}
float ModulationMatrixMachine::sampleWaveformForUi(int waveform, float phase) const noexcept
{
    if (waveform == static_cast<int>(Waveform::sampleHold))
    { static constexpr std::array<float, 8> steps {{ -0.72f, 0.28f, -0.12f, 0.84f, -0.45f, 0.56f, 0.06f, -0.91f }}; return steps[static_cast<std::size_t>(juce::jlimit(0, 7, static_cast<int>(wrap(phase) * 8.0f)))]; }
    return sampleWave(static_cast<Waveform>(juce::jlimit(0, 3, waveform)), phase, 0.0f);
}
const char* ModulationMatrixMachine::destinationLabel(ModulationDestination destination)
{ static constexpr const char* labels[] = { "W F1", "W SUB", "W POS", "S POS", "F CUT", "F RES", "F ATK", "L1 F", "L2 F", "L3 F", "L4 F", "S POS" }; return labels[static_cast<std::size_t>(destination)]; }
std::vector<ModulationDestination> ModulationMatrixMachine::visibleDestinations() const
{
    const auto mask = installedDestinationMask.load(std::memory_order_acquire); std::vector<ModulationDestination> result;
    const auto add = [&result](ModulationDestination d) { result.push_back(d); };
    if (mask & 1u) { add(ModulationDestination::waveFrequency); add(ModulationDestination::waveSubFrequency); add(ModulationDestination::wavePosition); add(ModulationDestination::waveSubPosition); }
    if (mask & 2u) { add(ModulationDestination::filterCutoff); add(ModulationDestination::filterResonance); add(ModulationDestination::filterAttack); }
    add(ModulationDestination::lfo1Frequency); add(ModulationDestination::lfo2Frequency); add(ModulationDestination::lfo3Frequency); add(ModulationDestination::lfo4Frequency);
    if (mask & 4u) add(ModulationDestination::samplerPosition); return result;
}

std::vector<std::vector<UIBox>> ModulationMatrixMachine::getUIBoxes(const MachineUiContext&)
{
    const auto destinations = visibleDestinations();
    destinationWindowStart = juce::jlimit(0, juce::jmax(0, static_cast<int>(destinations.size()) - 4), destinationWindowStart);
    std::vector<std::vector<UIBox>> boxes(8, std::vector<UIBox>(10));
    for (auto& column : boxes) for (auto& cell : column) cell.kind = UIBox::Kind::TrackerCell;

    const char* headings[] = { "SRC", "WAVE", "CRS", "FINE" };
    for (int col = 0; col < 4; ++col) { boxes[static_cast<std::size_t>(col)][0].text = headings[col]; boxes[static_cast<std::size_t>(col)][0].isDisabled = true; }
    for (int col = 0; col < 4; ++col)
    {
        const int index = destinationWindowStart + col;
        boxes[static_cast<std::size_t>(col + 4)][0].text = index < static_cast<int>(destinations.size()) ? destinationLabel(destinations[static_cast<std::size_t>(index)]) : "";
        boxes[static_cast<std::size_t>(col + 4)][0].isDisabled = true;
    }

    const auto preview = getUiPreviewState();
    for (int row = 0; row < 4; ++row)
    {
        auto& label = boxes[0][static_cast<std::size_t>(row + 1)];
        label.text = "LFO" + std::to_string(row + 1); label.isDisabled = true;
        label.glow = preview.active ? 0.12f + 0.30f * (preview.lfoValues[static_cast<std::size_t>(row)] + 1.0f) * 0.5f : 0.0f;
        label.useCustomGlowColour = true; label.customGlowArgb = 0xffff3030u;
        auto& waveform = boxes[1][static_cast<std::size_t>(row + 1)];
        waveform.onAdjust = [this, row](int amount) { waveforms[static_cast<std::size_t>(row)] = (waveforms[static_cast<std::size_t>(row)].load() + amount + 5) % 5; touch(); };
        waveform.onInsert = [this, row](double value) { waveforms[static_cast<std::size_t>(row)] = juce::jlimit(0, 4, static_cast<int>(std::lround(value))); touch(); };
        boxes[2][static_cast<std::size_t>(row + 1)].text = std::to_string(coarse[static_cast<std::size_t>(row)].load());
        boxes[2][static_cast<std::size_t>(row + 1)].onAdjust = [this, row](int amount) { coarse[static_cast<std::size_t>(row)] = juce::jlimit(0, 127, coarse[static_cast<std::size_t>(row)].load() + amount); touch(); };
        boxes[2][static_cast<std::size_t>(row + 1)].onInsert = [this, row](double value) { coarse[static_cast<std::size_t>(row)] = juce::jlimit(0, 127, static_cast<int>(std::lround(value))); touch(); };
        boxes[3][static_cast<std::size_t>(row + 1)].text = juce::String(fine[static_cast<std::size_t>(row)].load()).toStdString();
        boxes[3][static_cast<std::size_t>(row + 1)].onAdjust = [this, row](int amount) { fine[static_cast<std::size_t>(row)] = juce::jlimit(-99, 99, fine[static_cast<std::size_t>(row)].load() + amount); touch(); };
        boxes[3][static_cast<std::size_t>(row + 1)].onInsert = [this, row](double value) { fine[static_cast<std::size_t>(row)] = juce::jlimit(-99, 99, static_cast<int>(std::lround(value))); touch(); };
    }
    boxes[0][5].text = "NOTE"; boxes[0][5].isDisabled = true;
    boxes[1][5].text = "TRIG"; boxes[1][5].isDisabled = true; boxes[2][5].isDisabled = true; boxes[3][5].isDisabled = true;
    for (int row = 0; row < 3; ++row)
    {
        const int displayRow = row + 6;
        boxes[0][static_cast<std::size_t>(displayRow)].text = "CLK" + std::to_string(row + 1); boxes[0][static_cast<std::size_t>(displayRow)].isDisabled = true;
        boxes[1][static_cast<std::size_t>(displayRow)].text = "DIV"; boxes[1][static_cast<std::size_t>(displayRow)].isDisabled = true;
        boxes[2][static_cast<std::size_t>(displayRow)].text = std::to_string(dividers[static_cast<std::size_t>(row)].load());
        boxes[2][static_cast<std::size_t>(displayRow)].onAdjust = [this, row](int amount) { dividers[static_cast<std::size_t>(row)] = juce::jlimit(1, 16, dividers[static_cast<std::size_t>(row)].load() + amount); touch(); };
        boxes[2][static_cast<std::size_t>(displayRow)].onInsert = [this, row](double value) { dividers[static_cast<std::size_t>(row)] = juce::jlimit(1, 16, static_cast<int>(std::lround(value))); touch(); };
        boxes[3][static_cast<std::size_t>(displayRow)].isDisabled = true;
    }
    boxes[0][9].text = "PITCH"; boxes[0][9].isDisabled = true;
    boxes[1][9].text = lastNote < 0 ? "---" : juce::String(juce::MidiMessage::getMidiNoteInHertz(lastNote), 0).toStdString() + "HZ"; boxes[1][9].isDisabled = true;
    boxes[2][9].isDisabled = true; boxes[3][9].isDisabled = true;

    for (int column = 0; column < 4; ++column)
    {
        const int destinationIndex = destinationWindowStart + column;
        if (destinationIndex >= static_cast<int>(destinations.size()))
        { for (int row = 1; row < 10; ++row) boxes[static_cast<std::size_t>(column + 4)][static_cast<std::size_t>(row)].isDisabled = true; continue; }
        const auto destination = destinations[static_cast<std::size_t>(destinationIndex)];
        for (int row = 0; row < 9; ++row)
        {
            const auto source = static_cast<ModulationSource>(sourceForUiRow(row));
            const bool binary = isPulseSource(source) && isLfoFrequencyDestination(destination);
            auto& cell = boxes[static_cast<std::size_t>(column + 4)][static_cast<std::size_t>(row + 1)];
            const int current = depths[static_cast<std::size_t>(source)][static_cast<std::size_t>(destination)].load();
            cell.text = binary ? (current == 0 ? "0" : "1") : juce::String(current).toStdString();
            cell.onAdjust = [this, source, destination, binary](int amount)
            { auto& value = depths[static_cast<std::size_t>(source)][static_cast<std::size_t>(destination)]; value = binary ? (amount > 0 ? 1 : 0) : juce::jlimit(-100, 100, value.load() + amount); touch(); };
            cell.onInsert = [this, source, destination, binary](double amount)
            { depths[static_cast<std::size_t>(source)][static_cast<std::size_t>(destination)] = binary ? (amount == 0.0 ? 0 : 1) : juce::jlimit(-100, 100, static_cast<int>(std::lround(amount))); touch(); };
        }
    }
    return boxes;
}

bool ModulationMatrixMachine::navigateLeft() { if (destinationWindowStart <= 0) return false; --destinationWindowStart; return true; }
bool ModulationMatrixMachine::navigateRight() { if (destinationWindowStart + 4 >= static_cast<int>(visibleDestinations().size())) return false; ++destinationWindowStart; return true; }

void ModulationMatrixMachine::getStateInformation(juce::MemoryBlock& out)
{
    auto* root = new juce::DynamicObject(); root->setProperty("version", 2);
    const auto setArray = [root](const char* name, const auto& values) { juce::Array<juce::var> array; for (const auto& value : values) array.add(value.load()); root->setProperty(name, array); };
    setArray("wave", waveforms); setArray("coarse", coarse); setArray("fine", fine); setArray("div", dividers);
    juce::Array<juce::var> routes; for (const auto& row : depths) for (const auto& value : row) routes.add(value.load()); root->setProperty("routes", routes);
    const auto json = juce::JSON::toString(juce::var(root)); out.append(json.toRawUTF8(), static_cast<std::size_t>(json.getNumBytesAsUTF8()));
}
void ModulationMatrixMachine::setStateInformation(const void* data, int size)
{
    const auto parsed = juce::JSON::parse(juce::String::fromUTF8(static_cast<const char*>(data), size));
    const int version = static_cast<int>(parsed.getProperty("version", 0));
    for (auto& row : depths) for (auto& value : row) value = 0;
    const auto load = [&parsed](const char* name, auto& values, int low, int high) { if (const auto* array = parsed.getProperty(name, {}).getArray()) for (std::size_t i = 0; i < values.size() && i < static_cast<std::size_t>(array->size()); ++i) values[i] = juce::jlimit(low, high, static_cast<int>(array->getReference(static_cast<int>(i)))); };
    load("wave", waveforms, 0, 4); load("coarse", coarse, 0, 127); load("fine", fine, -99, 99); load("div", dividers, 1, 16);
    if (const auto* array = parsed.getProperty("routes", {}).getArray())
        for (std::size_t source = 0; source < kModulationSourceCount; ++source) for (std::size_t destination = 0; destination < kModulationDestinationCount; ++destination)
        {
            if (version < 2 && source == static_cast<std::size_t>(ModulationSource::noteTrigger)) continue;
            const auto index = source * kModulationDestinationCount + destination;
            if (index < static_cast<std::size_t>(array->size())) depths[source][destination] = juce::jlimit(-100, 100, static_cast<int>(array->getReference(static_cast<int>(index))));
        }
    for (const auto source : { ModulationSource::clock1, ModulationSource::clock2, ModulationSource::clock3, ModulationSource::noteTrigger })
        for (int target = 0; target < 4; ++target)
        { auto& value = depths[static_cast<std::size_t>(source)][static_cast<std::size_t>(ModulationDestination::lfo1Frequency) + static_cast<std::size_t>(target)]; value = value.load() == 0 ? 0 : 1; }
    touch();
}
