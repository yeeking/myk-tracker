#pragma once

#include <cmath>
#include <functional>
#include <string>

#include <JuceHeader.h>

#include "UIBox.h"

/** Default tracker tick duration in seconds (120 BPM, 8 ticks per beat). */
inline constexpr double kDefaultTrackerSecondsPerTick = 60.0 / (120.0 * 8.0);

/** Substitutes a 44.1 kHz fallback when the host passes a non-positive sample rate. */
inline double sanitizedSampleRate(double sampleRate)
{
    return sampleRate > 0.0 ? sampleRate : 44100.0;
}

/** Formats a float parameter for compact tracker display. */
inline std::string formatFloat(float value, int decimals)
{
    return juce::String(value, decimals).toStdString();
}

/** Formats a dB gain value for compact tracker display. */
inline std::string formatDb(float value, int decimals)
{
    return juce::String(value, decimals).toStdString();
}

/** Formats a frequency in Hz, switching to a "k" suffix at 1 kHz and above. */
inline std::string formatHz(float hz)
{
    if (hz >= 1000.0f)
        return juce::String(hz / 1000.0f, 2).toStdString() + "k";
    return juce::String(hz, 0).toStdString();
}

/** Soft-clip transfer function shared by the distortion machines. */
inline float softClip(float input)
{
    return std::tanh(input);
}

/** Builds a plain text label cell. */
inline UIBox makeLabelCell(const std::string& text)
{
    UIBox cell;
    cell.kind = UIBox::Kind::TrackerCell;
    cell.text = text;
    return cell;
}

/** Builds a float parameter cell.
    The caller-supplied adjust callback owns the state update (locking and
    read-modify-write); this helper only handles presentation: text, fill
    scale, and linear normalisation over [min, max]. */
inline UIBox makeFloatCell(float currentValue, float minValue, float maxValue, int decimals,
                           const std::function<void(int)>& onAdjust)
{
    UIBox cell;
    cell.kind = UIBox::Kind::TrackerCell;
    cell.text = formatFloat(currentValue, decimals);
    cell.onAdjust = onAdjust;
    cell.hasValueScale = true;
    cell.valueNorm = maxValue > minValue
        ? juce::jlimit(0.0f, 1.0f, (currentValue - minValue) / (maxValue - minValue))
        : 0.0f;
    return cell;
}

/** Builds an on/off toggle cell. As with makeFloatCell, the adjust callback
    owns the state update; this helper only handles presentation. */
inline UIBox makeToggleCell(bool isOn, const std::string& offText, const std::string& onText,
                            const std::function<void(int)>& onAdjust)
{
    UIBox cell;
    cell.kind = UIBox::Kind::TrackerCell;
    cell.text = isOn ? onText : offText;
    cell.onAdjust = onAdjust;
    cell.hasValueScale = true;
    cell.valueNorm = isOn ? 1.0f : 0.0f;
    return cell;
}
