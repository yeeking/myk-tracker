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
struct TrackerPalette
{
    juce::Colour background { 0xFF02040A };
    juce::Colour gridEmpty { 0xFF1B2024 };
    juce::Colour gridNote { 0xFF00F6FF };
    juce::Colour gridPlayhead { 0xFFFF3B2F };
    juce::Colour gridSelected { 0xFF29E0FF };
    juce::Colour textPrimary { 0xFF3DE6C0 };
    juce::Colour textWarning { 0xFFFF5A3C };
    juce::Colour textBackground { juce::Colours::transparentBlack };
    juce::Colour statusOk { 0xFF19FF6A };
    juce::Colour borderNeon { 0xFF0F5F4B };
    juce::Colour lightColor { 0xFFDDF6E8 };
    float glowDecayScalar = 0.8f;
    float ambientStrength = 0.32f;
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
inline const juce::Colour errorRed = juce::Colours::red;
}
