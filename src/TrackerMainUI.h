/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin editor.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "TrackerMainProcessor.h"
#include "TrackerControlService.h"
// #include "StringTable.h"
#include "SequencerEditor.h"
#include "TrackerUIComponent.h"
#include "Palette.h"
#include "UIBox.h"
//==============================================================================
/**
*/
// Main plugin UI that renders the tracker and handles input.
class TrackerMainUI  : public juce::AudioProcessorEditor,
                      public juce::OpenGLRenderer,
                      public juce::Timer,
                      public juce::KeyListener

{
public:
    TrackerMainUI (TrackerMainProcessor&);
    ~TrackerMainUI() override;

    //==============================================================================
    void paint (juce::Graphics&) override;
    void resized() override;
    void parentHierarchyChanged() override;

    void timerCallback () override; 

    // OpenGLRenderer overrides
    void newOpenGLContextCreated() override;
    void renderOpenGL() override;
    void openGLContextClosing() override;
    void mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;

    // KeyListener overrides
    using juce::Component::keyPressed;
    bool keyPressed(const juce::KeyPress& key, juce::Component* originatingComponent) override;
    using juce::Component::keyStateChanged;
    bool keyStateChanged(bool isKeyDown, juce::Component* originatingComponent) override;
    /** next time we draw, call update on the sequencer's string representation */
    // void updateStringOnNextDraw();
    /** Arms a one-shot OpenGL capture.  The request is completed on the next render frame. */
    juce::String requestScreenshot(std::shared_ptr<TrackerMainProcessor::UiScreenshotRequest> request);
    long framesDrawn; 
private:

// some variables to control the display style
    // render sizes for cells and 
    const float cellWidth{2.0f};
    const float cellHeight{1.0f};
    
    TrackerMainProcessor& audioProcessor;
    TrackerControlService::TrackerViewSnapshot currentView;

    // StringTable controlPanelTable;
    SequencerEditor* seqEditor;
    juce::OpenGLContext openGLContext;
    TrackerUIComponent uiComponent;

    size_t rowsInUI;
    juce::Rectangle<int> seqViewBounds;
    
    void captureScreenshotFrame(const std::shared_ptr<TrackerMainProcessor::UiScreenshotRequest>& request);
    void prepareControlPanelView();
    void prepareSongView();
    void prepareSequenceView();
    void prepareStepView();
    void prepareSeqConfigView();
    void prepareMachineConfigView();
    void updateScopeCalibration(int machineId, std::size_t stackIndex,
                                const std::vector<float>& samples);
    void appendScopeBand(std::vector<std::vector<UIBox>>& boxes,
                         std::size_t rows,
                         int machineId,
                         std::size_t stackIndex,
                         std::vector<float>& scopeSamples);
    void prepareMixerView();
    void prepareResetConfirmationView();
    void updateCellStates(const std::vector<std::vector<UIBox>>& boxes,
                          size_t rowsToDisplay,
                          size_t colsToDisplay);

    TrackerUIComponent::CellState makeDefaultCell() const;
    juce::Colour getCellColour(const UIBox& cell) const;
    juce::Colour getTextColour(const UIBox& cell) const;
    float getCellDepthScale(const UIBox& cell) const;
    void adjustZoom(float delta);
    void adjustZoomAroundPoint(float delta, juce::Point<float> normalizedPoint);
    void moveUp(float amount);
    void moveDown(float amount);
    void moveLeft(float amount);
    void moveRight(float amount);
    void chooseStateSaveDirectory();
    void chooseStateFileToLoad();
    juce::File initialStateDirectory() const;
    void showStateFileError(const juce::String& message) const;
    std::vector<std::vector<UIBox>> buildBoxesFromGrid(const std::vector<std::vector<std::string>>& data,
                                                       size_t cursorCol,
                                                       size_t cursorRow,
                                                       const std::vector<std::pair<int, int>>& highlightCells,
                                                       bool showCursor,
                                                       size_t armedSeq) const;
    juce::Colour getSamplerCellColour(const UIBox& cell) const;
    juce::Colour getSamplerTextColour(const UIBox& cell) const;
    float getSamplerCellDepthScale(const UIBox& cell) const;

    TrackerUIComponent::CellGrid cellStates;
    std::vector<std::vector<float>> playheadGlow;
    /** Last frame's valueNorm per display cell, used for change pulses. */
    std::vector<std::vector<float>> lastValueNorm;
    std::vector<std::uint64_t> seqConfigTriggerCounts;
    std::vector<float> seqConfigTriggerFlash;
    bool seqConfigTriggerBaselineValid = false;
    std::size_t seqConfigTriggerSetIndex = std::size_t(-1);
    size_t visibleCols = 0;
    size_t visibleRows = 0;
    size_t startCol = 0;
    size_t startRow = 0;
    size_t lastStartCol = 0;
    size_t lastStartRow = 0;
    TrackerUIComponent::OverlayState overlayState;
    int lastHudBpm = -1;
    bool lastHudInternalClock = true;
    float zoomLevel = 1.0f;
    juce::Point<int> lastDragPosition;
    float panOffsetX = 0.0f;
    float panOffsetY = 0.0f;
    TrackerPalette palette;
    SamplerPalette samplerPalette;
    std::vector<float> samplerColumnWidths;
    /** Polylines drawn over the current grid (machine-page scope band). */
    std::vector<TrackerUIComponent::Trace> currentTraces;
    /** Scale applied to the live scope snapshot by the auto-calibration. */
    float scopeCalScale = 1.0f;
    /** Target scale the auto-calibration eases toward. */
    float scopeCalTarget = 1.0f;
    /** Hi-res ms of the last recalibration; 0 = never calibrated yet. */
    std::int64_t scopeCalLastRecalMs = 0;
    /** Hi-res ms of the previous machine-view frame (for the ease step). */
    std::int64_t scopeCalLastFrameMs = 0;
    /** Machine/stack the calibration state was last computed for. */
    int scopeCalMachineId = -1;
    std::size_t scopeCalStackIndex = 0;
    bool samplerViewActive = false;
    bool customMachineColumnWidthsActive = false;
    juce::File lastStateDirectory;
    std::unique_ptr<juce::FileChooser> stateFileChooser;

    bool waitingForPaint;
    juce::CriticalSection screenshotLock;
    std::shared_ptr<TrackerMainProcessor::UiScreenshotRequest> pendingScreenshot;
    // This reference is provided as a quick way for your editor to
    // access the processor object that created it.

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TrackerMainUI)
};
