#pragma once

#include <algorithm>
#include <cstddef>
#include <vector>

namespace sequence_scope
{
constexpr std::size_t kPreviewBucketCount = 48;
constexpr std::size_t kPreviewPointCount = kPreviewBucketCount * 2;

/** Returns first-seen, valid stack indices so routed columns can share one
    scope snapshot and calibration pass. */
inline std::vector<std::size_t> collectUniqueStackIndices(const std::vector<int>& stackIds,
                                                          std::size_t stackCount)
{
    std::vector<std::size_t> result;
    result.reserve(stackIds.size());
    for (const int stackId : stackIds)
    {
        if (stackId < 0)
            continue;
        const auto index = static_cast<std::size_t>(stackId);
        if (index >= stackCount || std::find(result.begin(), result.end(), index) != result.end())
            continue;
        result.push_back(index);
    }
    return result;
}

/** Reduces a rolling scope to chronological min/max pairs. Keeping both extrema
    from each bucket preserves brief positive and negative peaks in a narrow UI
    cell without uploading the full audio-rate ring to the GPU. */
inline void reducePeakPreserving(const std::vector<float>& source, std::vector<float>& destination)
{
    destination.assign(kPreviewPointCount, 0.0f);
    if (source.empty())
        return;

    for (std::size_t bucket = 0; bucket < kPreviewBucketCount; ++bucket)
    {
        const std::size_t begin = bucket * source.size() / kPreviewBucketCount;
        const std::size_t end = (bucket + 1) * source.size() / kPreviewBucketCount;
        if (begin >= end)
        {
            destination[bucket * 2] = source[std::min(begin, source.size() - 1)];
            destination[bucket * 2 + 1] = destination[bucket * 2];
            continue;
        }

        auto minimum = source.begin() + static_cast<std::ptrdiff_t>(begin);
        auto maximum = minimum;
        for (auto it = minimum + 1; it != source.begin() + static_cast<std::ptrdiff_t>(end); ++it)
        {
            if (*it < *minimum) minimum = it;
            if (*it > *maximum) maximum = it;
        }

        const auto output = bucket * 2;
        if (minimum <= maximum)
        {
            destination[output] = *minimum;
            destination[output + 1] = *maximum;
        }
        else
        {
            destination[output] = *maximum;
            destination[output + 1] = *minimum;
        }
    }
}
}
