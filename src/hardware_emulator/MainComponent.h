#pragma once

#include <JuceHeader.h>

class HardwareEmulatorMainComponent final : public juce::Component
{
public:
    HardwareEmulatorMainComponent();
    ~HardwareEmulatorMainComponent() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    std::unique_ptr<juce::Component> touchSurface;
    std::unique_ptr<juce::Component> dial;
    std::unique_ptr<juce::Component> smallScreen;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(HardwareEmulatorMainComponent)
};
