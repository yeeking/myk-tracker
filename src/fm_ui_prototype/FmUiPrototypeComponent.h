#pragma once

#include <JuceHeader.h>
#include "TrackerUIComponent.h"

/** A keyboard-only visual study for a future FM machine page.  This is an
    isolated executable: it models the EVOLVE patch only and never opens an
    audio device or touches tracker state. */
class FmUiPrototypeComponent final : public juce::Component,
                                     private juce::OpenGLRenderer,
                                     private juce::Timer
{
public:
    FmUiPrototypeComponent();
    ~FmUiPrototypeComponent() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress&) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;

private:
    enum class Concept { lanes, route, depth };

    void newOpenGLContextCreated() override;
    void renderOpenGL() override;
    void openGLContextClosing() override;
    void timerCallback() override;

    void rebuildView();
    void buildLanesView();
    void buildRouteView();
    void buildDepthView();
    void advanceConcept(int amount);
    void adjustZoom(float amount);
    void writeRequestedScreenshot();

    juce::OpenGLContext openGLContext;
    TrackerUIComponent renderer { openGLContext };
    TrackerUIComponent::CellGrid cells;
    std::vector<TrackerUIComponent::Trace> traces;
    std::vector<float> columnWidths;
    TrackerUIComponent::OverlayState overlay;
    TrackerUIComponent::ZoomState zoom;
    TrackerUIComponent::DragState drag;

    Concept activeConcept { Concept::lanes };
    int selectedOperator = 0;
    int selectedField = 0;
    int selectedBank = 0;
    float indexOffset = 0.0f;
    bool held = false;
    double heldSeconds = 0.0;
    juce::Point<int> lastDragPosition;
    juce::File screenshotOutput;
    bool screenshotWritten = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FmUiPrototypeComponent)
};
