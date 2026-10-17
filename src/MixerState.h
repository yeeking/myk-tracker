#pragma once

#include <algorithm>
#include <cmath>

namespace MixerState
{
constexpr float minimumMasterGainDb = -48.0f;
constexpr float maximumMasterGainDb = 6.0f;
constexpr float minimumPan = -1.0f;
constexpr float maximumPan = 1.0f;

inline float clampPan(float pan)
{
    if (!std::isfinite(pan))
        return 0.0f;
    return std::clamp(pan, minimumPan, maximumPan);
}

inline float clampMasterGainDb(float gainDb)
{
    if (!std::isfinite(gainDb))
        return 0.0f;
    return std::clamp(gainDb, minimumMasterGainDb, maximumMasterGainDb);
}

inline float masterGainToLinear(float gainDb)
{
    return std::pow(10.0f, clampMasterGainDb(gainDb) / 20.0f);
}

struct SequencePresentation
{
    bool stackAudible = true;
    bool soloOutline = false;
};

inline SequencePresentation getSequencePresentation(bool sequenceSolo,
                                                    bool stackMuted,
                                                    bool stackSolo,
                                                    bool anyStackSolo)
{
    const bool stackAudible = !stackMuted && (!anyStackSolo || stackSolo);
    const bool soloOutline = anyStackSolo ? stackSolo : (!stackMuted && sequenceSolo);
    return { stackAudible, soloOutline };
}
}
