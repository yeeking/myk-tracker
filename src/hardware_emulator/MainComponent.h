#pragma once

#include <JuceHeader.h>

class HardwareEmulatorMainComponent final : public juce::Component
{
public:
    /** Creates the standalone hardware-emulator main component. */
    HardwareEmulatorMainComponent();
    /** Destroys the standalone hardware-emulator main component. */
    ~HardwareEmulatorMainComponent() override;

    /** Paints the emulator background and child controls. */
    void paint(juce::Graphics& g) override;
    /** Lays out the emulator child controls. */
    void resized() override;

private:
    /** Placeholder touch surface component. */
    std::unique_ptr<juce::Component> touchSurface;
    /** Placeholder dial component. */
    std::unique_ptr<juce::Component> dial;
    /** Placeholder display component. */
    std::unique_ptr<juce::Component> smallScreen;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(HardwareEmulatorMainComponent)
};
