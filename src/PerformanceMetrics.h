#pragma once

#include <array>
#include <atomic>
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <vector>

/** Opt-in audio-path timing for the standalone performance benchmark.
    Timing probes compile to no-ops in normal tracker builds. */
class PerformanceMetrics
{
public:
    enum class Stage : std::size_t { callback, sequencing, modulation, sampler, wavetable, distortion, delay, channel, filter, aux1, aux2, scopeMeter, master, count };
    struct StageSummary { std::uint64_t calls = 0; double meanUs = 0.0; double maxUs = 0.0; };
    struct Snapshot
    {
        std::array<StageSummary, static_cast<std::size_t>(Stage::count)> stages {};
        double p95CallbackUs = 0.0, p99CallbackUs = 0.0, maxCallbackUs = 0.0;
        std::size_t callbackSamples = 0;
    };

    class Scope
    {
    public:
        Scope(PerformanceMetrics& ownerIn, Stage stageIn) noexcept
#if defined(MYK_TRACKER_PERFORMANCE_INSTRUMENTATION)
            : owner(ownerIn), stage(stageIn), active(owner.enabled.load(std::memory_order_relaxed)) { if (active) started = Clock::now(); }
#else
            { (void) ownerIn; (void) stageIn; }
#endif
        ~Scope() noexcept { stop(); }
        void stop() noexcept
        {
#if defined(MYK_TRACKER_PERFORMANCE_INSTRUMENTATION)
            if (active) { owner.record(stage, static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - started).count())); active = false; }
#endif
        }
    private:
        using Clock = std::chrono::steady_clock;
#if defined(MYK_TRACKER_PERFORMANCE_INSTRUMENTATION)
        PerformanceMetrics& owner; Stage stage; bool active = false; Clock::time_point started {};
#endif
    };

    void setEnabled(bool shouldEnable) noexcept
    {
#if defined(MYK_TRACKER_PERFORMANCE_INSTRUMENTATION)
        enabled.store(shouldEnable, std::memory_order_release);
#else
        (void) shouldEnable;
#endif
    }
    bool isEnabled() const noexcept
    {
#if defined(MYK_TRACKER_PERFORMANCE_INSTRUMENTATION)
        return enabled.load(std::memory_order_acquire);
#else
        return false;
#endif
    }
    void reset() noexcept
    {
#if defined(MYK_TRACKER_PERFORMANCE_INSTRUMENTATION)
        for (auto& item : totals) item.store(0, std::memory_order_relaxed);
        for (auto& item : maxima) item.store(0, std::memory_order_relaxed);
        for (auto& item : calls) item.store(0, std::memory_order_relaxed);
        callbackWriteIndex.store(0, std::memory_order_relaxed);
#endif
    }
    Snapshot snapshot() const
    {
        Snapshot result;
#if defined(MYK_TRACKER_PERFORMANCE_INSTRUMENTATION)
        for (std::size_t index = 0; index < result.stages.size(); ++index)
        {
            const auto count = calls[index].load(std::memory_order_relaxed);
            result.stages[index] = { count, count == 0 ? 0.0 : static_cast<double>(totals[index].load(std::memory_order_relaxed)) / static_cast<double>(count) / 1000.0, static_cast<double>(maxima[index].load(std::memory_order_relaxed)) / 1000.0 };
        }
        const auto count = std::min<std::size_t>(callbackWriteIndex.load(std::memory_order_relaxed), callbackSamples.size());
        std::vector<std::uint64_t> sorted; sorted.reserve(count);
        for (std::size_t index = 0; index < count; ++index) sorted.push_back(callbackSamples[index].load(std::memory_order_relaxed));
        std::sort(sorted.begin(), sorted.end()); result.callbackSamples = sorted.size();
        if (!sorted.empty())
        {
            const auto percentile = [&sorted](double p) { return static_cast<double>(sorted[static_cast<std::size_t>(p * static_cast<double>(sorted.size() - 1))]) / 1000.0; };
            result.p95CallbackUs = percentile(0.95); result.p99CallbackUs = percentile(0.99); result.maxCallbackUs = static_cast<double>(sorted.back()) / 1000.0;
        }
#endif
        return result;
    }
private:
    void record(Stage stage, std::uint64_t elapsedNs) noexcept
    {
#if defined(MYK_TRACKER_PERFORMANCE_INSTRUMENTATION)
        const auto index = static_cast<std::size_t>(stage); totals[index].fetch_add(elapsedNs, std::memory_order_relaxed); calls[index].fetch_add(1, std::memory_order_relaxed);
        auto currentMax = maxima[index].load(std::memory_order_relaxed);
        while (currentMax < elapsedNs && !maxima[index].compare_exchange_weak(currentMax, elapsedNs, std::memory_order_relaxed)) {}
        if (stage == Stage::callback) callbackSamples[callbackWriteIndex.fetch_add(1, std::memory_order_relaxed) % callbackSamples.size()].store(elapsedNs, std::memory_order_relaxed);
#else
        (void) stage; (void) elapsedNs;
#endif
    }
    std::atomic<bool> enabled { false };
    std::array<std::atomic<std::uint64_t>, static_cast<std::size_t>(Stage::count)> totals {}, maxima {}, calls {};
    std::array<std::atomic<std::uint64_t>, 8192> callbackSamples {};
    std::atomic<std::size_t> callbackWriteIndex { 0 };
};
