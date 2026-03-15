#pragma once

#include <JuceHeader.h>

// Shared cursor highlight colours used across tracker and sampler editing modes.
struct CursorPalette
{
    /** fill colour for the currently selected cell */
    juce::Colour fill { 0xBB2990FF };
    /** text colour inside the currently selected cell */
    juce::Colour text { 0xFF29E0FF };
};

// Color palette for sampler-focused UI screens.
struct SamplerPalette
{
    /** UI background behind sampler-machine cells */
    juce::Colour background { 0xFF03060B };
    /** fill colour for idle sampler cells */
    juce::Colour cellIdle { 0xFF141A22 };
    /** fill colour for active sampler action cells */
    juce::Colour cellAccent { 0xFF1E2F3D };
    /** fill colour for disabled sampler cells */
    juce::Colour cellDisabled { 0xFF0C1118 };
    /** primary text colour for sampler labels and values */
    juce::Colour textPrimary { 0xFF4EF2C2 };
    /** subdued text colour for secondary sampler information */
    juce::Colour textMuted { 0xFF6B7C8F };
    /** glow colour for active sampler controls */
    juce::Colour glowActive { 0xFFFF5533 };
    /** key light colour for sampler cell shading */
    juce::Colour lightColor { 0xFFEAF6FF };
    /** per-frame decay factor for sampler glow */
    float glowDecayScalar = 0.4f;
    /** strength of ambient lighting in sampler cell shading */
    float ambientStrength = 0.32f;
    /** direction of the key light in sampler cell shading */
    juce::Vector3D<float> lightDirection { 0.2f, 0.45f, 1.0f };
};

// Color palette for the tracker UI screens.
// todo: add brief comments describing which bit of the cell painting
// the color relates to
struct TrackerPalette
{
    /** UI background behind all cells */
    juce::Colour background { 0xFF02040A };
    /** fill colour for empty cells */
    juce::Colour gridEmpty { 0xFF1B2024 };
    /** cell outline colour when cell has content */
    juce::Colour gridNote { 0xFF00F6FF };
    /** glow colour for playhead-highlighted cells */
    // juce::Colour gridPlayhead { 0xFFFF3B2F };
    /** playhead highlight colour; hex literals use 0xAARRGGBB channel order */
    juce::Colour gridPlayhead { 0xFFFF0000 };
    
    /** primary cell text colour */
    juce::Colour textPrimary { 0xFF3DE6C0 };
    /** warning/emphasis text colour */
    juce::Colour textWarning { 0xFFFF5A3C };
    // juce::Colour textWarning { 0xFFFFFFFF };
    /** backing tint behind text */
    juce::Colour textBackground { juce::Colours::transparentBlack };
    /** highlight colour for armed/ok states */
    juce::Colour statusOk { 0xFF19FF6A };
    /** outer border accent for UI panels */
    juce::Colour borderNeon { 0xFF0F5F4B };
    /** key light colour for 3D cell shading */
    juce::Colour lightColor { 0xFFDDF6E8 };
    /** per-frame decay factor for playhead glow */
    float glowDecayScalar = 0.8f;
    /** strength of ambient lighting in 3D shading */
    float ambientStrength = 0.32f;
    /** direction of the key light in 3D shading */
    juce::Vector3D<float> lightDirection { 0.2f, 0.45f, 1.0f };
};

namespace PaletteDefaults
{
inline const CursorPalette cursor;
inline const juce::Colour cellFill = juce::Colours::black;
inline const juce::Colour cellText = juce::Colours::white;
inline const juce::Colour cellGlow = juce::Colours::black;
inline const juce::Colour cellOutline = juce::Colours::black;
inline const juce::Colour overlayText = juce::Colours::white;
inline const juce::Colour overlayGlow = juce::Colours::white;
}
