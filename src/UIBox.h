#pragma once

#include <cstdint>
#include <functional>
#include <string>

// Shared UI cell model with state and action callbacks.
struct UIBox
{
    /** Visual style / interaction type for the cell. */
    enum class Kind
    {
        /** Empty cell with no tracker rendering semantics. */
        None,
        /** Standard tracker/grid cell. */
        TrackerCell,
        /** Sampler action button cell. */
        SamplerAction,
        /** Sampler numeric/value cell. */
        SamplerValue,
        /** Sampler waveform preview cell. */
        SamplerWaveform
    };

    /** Text displayed inside the cell. */
    std::string text;
    /** Width multiplier used for wide cells. */
    float width = 1.0f;
    /** Explicit glow amount used by some machine pages. */
    float glow = 0.0f;
    /** True when the editor cursor is on this cell. */
    bool isSelected = false;
    /** True when the cell is currently being edited. */
    bool isEditing = false;
    /** True when the cell should render in a disabled state. */
    bool isDisabled = false;
    /** True when the cell represents an enabled/active toggle state. */
    bool isActive = false;
    /** True when the cell should emit a strong glow highlight. */
    bool isHighlighted = false;
    /** True when the parent track is armed for input. */
    bool isArmed = false;
    /** True when this selected Sequence-page cell accepts piano/chord entry. */
    bool isSequenceEntryArmedCursor = false;
    /** True when the cell belongs to a control-mode sequence column. */
    bool isControlModeSequence = false;
    /** True when the cell belongs to a soloed sequence or mixer channel. */
    bool isSoloChannel = false;
    /** True when the cell belongs to a muted mixer channel. */
    bool isMutedChannel = false;
    /** 1=half, 2=quarter live sequence note gate. */
    int noteLengthMode = 0;
    /** True when the cell represents note-bearing content. */
    bool hasNote = false;
    /** True when `customFillArgb` should override normal fill colours. */
    bool useCustomFillColour = false;
    /** True when `customTextArgb` should override normal text colours. */
    bool useCustomTextColour = false;
    /** True when `customGlowArgb` should override the normal tracker glow colour. */
    bool useCustomGlowColour = false;
    /** True when custom colours must remain visible under the cursor. */
    bool customColoursOverrideSelection = false;
    /** ARGB fill colour override. */
    std::uint32_t customFillArgb = 0;
    /** ARGB text colour override. */
    std::uint32_t customTextArgb = 0;
    /** ARGB glow colour override. */
    std::uint32_t customGlowArgb = 0;
    /** Draw independent left/right meter rails inside this cell. */
    bool drawStereoMeter = false;
    bool stereoMeterLeftActive = false;
    bool stereoMeterRightActive = false;
    std::uint32_t stereoMeterArgb = 0;
    /** True when `valueNorm` describes a parameter value for the fill bar. */
    bool hasValueScale = false;
    /** Normalised parameter value in [0,1] shown by the horizontal fill bar. */
    float valueNorm = 0.0f;
    /** Cell kind used by the renderer. */
    Kind kind = Kind::None;

    /** Callback fired when the user activates the cell. */
    std::function<void()> onActivate;
    /** Callback fired when the user adjusts the cell incrementally. */
    std::function<void(int)> onAdjust;
    /** Callback fired when the user inserts a numeric/note value. */
    std::function<void(double)> onInsert;
    /** Callback fired when the user previews the cell content. */
    std::function<void()> onPreview;
    /** Callback fired when the user resets the cell content. */
    std::function<void()> onReset;
};
