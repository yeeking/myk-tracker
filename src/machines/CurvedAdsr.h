#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <vector>

#include <JuceHeader.h>

/** Sustain-preserving ADSR with an optional curve bend.

    A bend of 0 keeps the standard linear stages. Positive bend reaches each
    stage's target quickly and eases into it; negative bend lingers before
    arriving. The sustain level itself is never curved. */
class CurvedAdsr
{
public:
    struct Parameters
    {
        float attack = 0.0f;
        float decay = 0.0f;
        float sustain = 1.0f;
        float release = 0.0f;
        float bend = 0.0f;
    };

    CurvedAdsr()
    {
        rebuildLut(0.0f);
    }

    void setSampleRate(double sampleRate)
    {
        currentSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
        oneOverSampleRate = 1.0f / static_cast<float>(currentSampleRate);
    }

    void setParameters(const Parameters& parameters)
    {
        attackSeconds = sanitizePositive(parameters.attack);
        decaySeconds = sanitizePositive(parameters.decay);
        sustainLevel = juce::jlimit(0.0f, 1.0f, sanitizeFinite(parameters.sustain, 1.0f));
        releaseSeconds = sanitizePositive(parameters.release);
        const float newBend = juce::jlimit(-2.0f, 2.0f, sanitizeFinite(parameters.bend, 0.0f));
        if (newBend != cachedBend)
            rebuildLut(newBend);
    }

    void reset()
    {
        currentLevel = 0.0f;
        phaseTime = 0.0f;
        releaseStartLevel = 0.0f;
        phase = Phase::None;
    }

    void noteOn()
    {
        currentLevel = 0.0f;
        phaseTime = 0.0f;
        releaseStartLevel = 0.0f;
        phase = Phase::Attack;
    }

    void noteOff()
    {
        if (phase == Phase::Release)
            return;
        releaseStartLevel = currentLevel;
        phaseTime = 0.0f;
        phase = Phase::Release;
    }

    float getNextSample()
    {
        if (phase == Phase::None)
            return 0.0f;

        phaseTime += oneOverSampleRate;

        if (phase == Phase::Attack)
        {
            if (attackSeconds <= 0.0f)
            {
                currentLevel = 1.0f;
                phaseTime = 0.0f;
                phase = Phase::Decay;
                return getNextSample();
            }
            if (phaseTime >= attackSeconds)
            {
                currentLevel = 1.0f;
                phaseTime = 0.0f;
                phase = Phase::Decay;
                return currentLevel;
            }
            currentLevel = shape(phaseTime / attackSeconds);
        }
        else if (phase == Phase::Decay)
        {
            if (decaySeconds <= 0.0f)
            {
                currentLevel = sustainLevel;
                phaseTime = 0.0f;
                phase = Phase::Sustain;
                return getNextSample();
            }
            if (phaseTime >= decaySeconds)
            {
                currentLevel = sustainLevel;
                phaseTime = 0.0f;
                phase = Phase::Sustain;
                return currentLevel;
            }
            const float shaped = shape(phaseTime / decaySeconds);
            currentLevel = sustainLevel + (1.0f - sustainLevel) * (1.0f - shaped);
        }
        else if (phase == Phase::Sustain)
        {
            currentLevel = sustainLevel;
        }
        else if (phase == Phase::Release)
        {
            if (releaseSeconds <= 0.0f)
            {
                currentLevel = 0.0f;
                phase = Phase::None;
                return 0.0f;
            }
            if (phaseTime >= releaseSeconds)
            {
                currentLevel = 0.0f;
                phase = Phase::None;
                return 0.0f;
            }
            const float shaped = shape(phaseTime / releaseSeconds);
            currentLevel = releaseStartLevel * (1.0f - shaped);
        }

        return currentLevel;
    }

    bool isActive() const noexcept
    {
        return phase != Phase::None;
    }

    /** Maps a 0..1 stage progress through the selected bend curve. */
    static float shapeProgress(float progress, float bend)
    {
        const float safeProgress = juce::jlimit(0.0f, 1.0f, sanitizeFinite(progress, 0.0f));
        const float safeBend = juce::jlimit(-2.0f, 2.0f, sanitizeFinite(bend, 0.0f));
        if (safeBend == 0.0f)
            return safeProgress;
        const float exponent = 1.0f / (1.0f + 0.5f * safeBend);
        return static_cast<float>(std::pow(static_cast<double>(safeProgress), static_cast<double>(exponent)));
    }

    /** Fills a UI preview trace with the same curve math used in the audio path. */
    static void fillTraceSamples(std::vector<float>& samples,
                                 float attack, float decay, float sustain, float release, float bend,
                                 float maxAttack, float maxDecay, float maxRelease)
    {
        if (samples.empty())
            return;

        const float safeAttack = sanitizePositive(attack);
        const float safeDecay = sanitizePositive(decay);
        const float safeSustain = juce::jlimit(0.0f, 1.0f, sanitizeFinite(sustain, 1.0f));
        const float safeRelease = sanitizePositive(release);
        const float safeMaxAttack = sanitizePositive(maxAttack);
        const float safeMaxDecay = sanitizePositive(maxDecay);
        const float safeMaxRelease = sanitizePositive(maxRelease);

        const float attackSpan = safeMaxAttack > 0.0f ? safeAttack / safeMaxAttack : 0.0f;
        const float decaySpan = safeMaxDecay > 0.0f ? safeDecay / safeMaxDecay : 0.0f;
        const float releaseSpan = safeMaxRelease > 0.0f ? safeRelease / safeMaxRelease : 0.0f;
        constexpr float kPreviewSustainSpan = 0.35f;
        const float totalSpan = juce::jmax(0.001f, attackSpan + decaySpan + kPreviewSustainSpan + releaseSpan);

        for (std::size_t i = 0; i < samples.size(); ++i)
        {
            const float x = (samples.size() == 1)
                ? 0.0f
                : static_cast<float>(i) / static_cast<float>(samples.size() - 1) * totalSpan;
            float value;
            if (x <= attackSpan)
                value = shapeProgress(attackSpan > 0.0f ? x / attackSpan : 1.0f, bend);
            else if (x <= attackSpan + decaySpan)
            {
                const float shaped = shapeProgress(decaySpan > 0.0f ? (x - attackSpan) / decaySpan : 1.0f, bend);
                value = safeSustain + (1.0f - safeSustain) * (1.0f - shaped);
            }
            else if (x <= attackSpan + decaySpan + kPreviewSustainSpan)
                value = safeSustain;
            else
            {
                const float shaped = shapeProgress(releaseSpan > 0.0f
                    ? (x - attackSpan - decaySpan - kPreviewSustainSpan) / releaseSpan
                    : 1.0f, bend);
                value = safeSustain * (1.0f - shaped);
            }
            samples[i] = value * 2.0f - 1.0f;
        }
    }

private:
    enum class Phase
    {
        None,
        Attack,
        Decay,
        Sustain,
        Release
    };

    static float sanitizeFinite(float value, float fallback)
    {
        return std::isfinite(value) ? value : fallback;
    }

    static float sanitizePositive(float value)
    {
        return juce::jmax(0.0f, sanitizeFinite(value, 0.0f));
    }

    void rebuildLut(float bendValue)
    {
        const float safeBend = juce::jlimit(-2.0f, 2.0f, sanitizeFinite(bendValue, 0.0f));
        const float exponent = 1.0f / (1.0f + 0.5f * safeBend);
        for (std::size_t i = 0; i < kLutSize; ++i)
        {
            const float progress = static_cast<float>(i) / static_cast<float>(kLutSize - 1);
            lut[i] = (safeBend == 0.0f)
                ? progress
                : static_cast<float>(std::pow(static_cast<double>(progress), static_cast<double>(exponent)));
        }
        cachedBend = safeBend;
    }

    float shape(float progress) const
    {
        const float safeProgress = juce::jlimit(0.0f, 1.0f, sanitizeFinite(progress, 0.0f));
        if (cachedBend == 0.0f)
            return safeProgress;
        const float scaled = safeProgress * static_cast<float>(kLutSize - 1);
        const int index = juce::jmin(static_cast<int>(scaled), static_cast<int>(kLutSize - 2));
        const float fraction = scaled - static_cast<float>(index);
        return lut[static_cast<std::size_t>(index)]
            + (lut[static_cast<std::size_t>(index + 1)] - lut[static_cast<std::size_t>(index)]) * fraction;
    }

    static constexpr std::size_t kLutSize = 256;

    double currentSampleRate = 44100.0;
    float oneOverSampleRate = 1.0f / 44100.0f;
    float attackSeconds = 0.0f;
    float decaySeconds = 0.0f;
    float sustainLevel = 1.0f;
    float releaseSeconds = 0.0f;
    float cachedBend = 0.0f;
    float currentLevel = 0.0f;
    float phaseTime = 0.0f;
    float releaseStartLevel = 0.0f;
    Phase phase = Phase::None;
    std::array<float, kLutSize> lut {};
};
