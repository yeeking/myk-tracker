#pragma once

#include <array>
#include <atomic>
#include <vector>

#include "../MachineInterface.h"
#include "../ModulationTypes.h"

class ModulationMatrixMachine final : public MachineInterface
{
public:
    enum class Waveform : int { sine, triangle, saw, square, sampleHold, count };
    struct UiPreviewState
    {
        std::array<int, 4> waveforms {};
        std::array<float, 4> lfoValues {};
        bool active = false;

        bool operator==(const UiPreviewState& other) const
        {
            return waveforms == other.waveforms && lfoValues == other.lfoValues && active == other.active;
        }
        bool operator!=(const UiPreviewState& other) const { return !(*this == other); }
    };
    ModulationMatrixMachine();
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override {}
    std::vector<std::vector<UIBox>> getUIBoxes(const MachineUiContext&) override;
    bool handleIncomingNote(unsigned short, unsigned short, unsigned short, MachineNoteEvent&) override { return false; }
    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;
    bool navigateLeft() override;
    bool navigateRight() override;
    bool clearCell(int row, int col) override;

    void setInstalledDestinations(unsigned int mask) noexcept { installedDestinationMask.store(mask, std::memory_order_release); }
    void resetClockDividers() noexcept;
    void processModulationBlock(int samples, bool enabled, bool transportPlaying,
                                const ModulationClockTick* ticks, std::size_t tickCount,
                                const std::vector<MachineScheduledNote>& notes);
    const ModulationBlockView& getBlockView() const noexcept { return blockView; }
    static float pitchRatio(float normalised, float octaveRange) noexcept;
    UiPreviewState getUiPreviewState() const noexcept;
    float sampleWaveformForUi(int waveform, float phase) const noexcept;

private:
    static constexpr int kTableSize = 2048;
    struct Route { unsigned char source = 0; float depth = 0.0f; };
    std::array<std::atomic<int>, 4> waveforms;
    std::array<std::atomic<int>, 4> coarse;
    std::array<std::atomic<int>, 4> fine;
    std::array<std::atomic<int>, 3> dividers;
    std::array<std::array<std::atomic<int>, kModulationDestinationCount>, kModulationSourceCount> depths;
    std::atomic<unsigned int> installedDestinationMask { 0 };
    std::atomic<unsigned long long> revision { 1 };
    unsigned long long compiledRevision = 0;
    std::array<std::array<Route, kModulationSourceCount>, kModulationDestinationCount> compiledRoutes {};
    std::array<int, kModulationDestinationCount> compiledRouteCount {};
    std::array<std::vector<float>, kModulationDestinationCount> buffers;
    ModulationBlockView blockView;
    std::array<double, 4> phases {};
    std::array<float, 4> heldValues {};
    std::array<unsigned int, 4> randomStates {{ 0x1234u, 0x2345u, 0x3456u, 0x4567u }};
    std::array<int, 3> dividerCounters {};
    std::array<std::atomic<float>, 4> latestLfoValues {};
    std::atomic<bool> matrixActive { false };
    int lastNote = -1;
    int destinationWindowStart = 0;
    double currentSampleRate = 44100.0;
    std::array<float, 128> rateTable {};

    void touch() noexcept { revision.fetch_add(1, std::memory_order_release); }
    void compileRoutes();
    float lfoRate(int index) const noexcept;
    float sourceValue(ModulationSource source, ModulationDestination destination,
                      const std::array<float, 4>& lfos,
                      const std::array<float, 3>& clocks, bool notePulse) const noexcept;
    float routeSum(ModulationDestination destination, const std::array<float, 4>& lfos,
                   const std::array<float, 3>& clocks, bool notePulse,
                   bool includePulseSources) const noexcept;
    static bool isPulseSource(ModulationSource source) noexcept;
    static bool isLfoFrequencyDestination(ModulationDestination destination) noexcept;
    static int sourceForUiRow(int row) noexcept;
    std::vector<ModulationDestination> visibleDestinations() const;
    static const char* destinationLabel(ModulationDestination destination);
};
