#pragma once

#include <functional>
#include <string>

// Shared UI cell model with state and action callbacks.
struct UIBox
{
    enum class Kind
    {
        None,
        TrackerCell,
        SamplerAction,
        SamplerValue,
        SamplerWaveform
    };

    std::string text;
    float width = 1.0f;
    float glow = 0.0f;
    bool isSelected = false;
    bool isEditing = false;
    bool isDisabled = false;
    bool isActive = false;
    bool isHighlighted = false;
    bool isArmed = false;
    bool hasNote = false;
    Kind kind = Kind::None;

    std::function<void()> onActivate;
    std::function<void(int)> onAdjust;
    std::function<void(double)> onInsert;
    std::function<void()> onReset;
};
