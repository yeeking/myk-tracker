#pragma once

#include <JuceHeader.h>

// Color palette for sampler-focused UI screens.
struct SamplerPalette
{
    juce::Colour background { 0xFF03060B };
    juce::Colour cellIdle { 0xFF141A22 };
    juce::Colour cellSelected { 0xFF00E8FF };
    juce::Colour cellAccent { 0xFF1E2F3D };
    juce::Colour cellDisabled { 0xFF0C1118 };
    juce::Colour textPrimary { 0xFF4EF2C2 };
    juce::Colour textMuted { 0xFF6B7C8F };
    juce::Colour glowActive { 0xFFFF5533 };
    juce::Colour lightColor { 0xFFEAF6FF };
    float glowDecayScalar = 0.4f;
    float ambientStrength = 0.32f;
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
    juce::Colour gridPlayhead { 0xFFFF3B2F };
    /** cursor colour when cell is empty */
    juce::Colour gridSelected { 0xFF29E0FF };
    /** cursor colour when cell is not empty */
    juce::Colour gridWithContentSelected { 0xBB29E0FF };
    
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
inline const juce::Colour cellFill = juce::Colours::black;
inline const juce::Colour cellText = juce::Colours::white;
inline const juce::Colour cellGlow = juce::Colours::black;
inline const juce::Colour cellOutline = juce::Colours::black;
inline const juce::Colour overlayText = juce::Colours::white;
inline const juce::Colour overlayGlow = juce::Colours::white;
inline const juce::Colour errorRed = juce::Colours::white;
}
