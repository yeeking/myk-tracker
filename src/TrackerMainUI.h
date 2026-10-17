/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin editor.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include <array>
#include <cstdint>
#include <chrono>
#include <unordered_map>
#include "TrackerMainProcessor.h"
#include "TrackerControlService.h"
// #include "StringTable.h"
#include "SequencerEditor.h"
#include "SequenceScopeUtils.h"
#include "TrackerUIComponent.h"
#include "TrackerFileBrowser.h"
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
    float updateScopeCalibration(std::size_t stackIndex,
                                 const std::vector<float>& samples);
    void appendScopeBand(std::vector<std::vector<UIBox>>& boxes,
                         std::size_t rows,
                         std::size_t stackIndex,
                         std::vector<float>& scopeSamples);
    void appendEnvelopeBand(std::vector<std::vector<UIBox>>& boxes,
                            std::size_t rows,
                            const std::vector<float>& envelopeSamples,
                            const std::string& traceCacheId = {},
                            std::uint64_t traceCacheRevision = 0);
    void prepareMixerView();
    void prepareAudioConfigView();
    void prepareHelpView();
    void prepareResetConfirmationView();
    void updateCellStates(const std::vector<std::vector<UIBox>>& boxes,
                          size_t rowsToDisplay,
                          size_t colsToDisplay,
                          const std::vector<UIBox>* stickyHeaderCells = nullptr);

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
    void openStateSaveDialog();
    void openStateLoadDialog();
    void closeStateDialog();
    bool handleStateDialogKey(const juce::KeyPress& key);
    void prepareStateDialogView();
    void recordUiBenchmarkSample(std::atomic<std::uint64_t>& total, std::atomic<std::uint64_t>& maximum,
                                 std::atomic<std::uint64_t>& count, std::uint64_t elapsedNs) noexcept;
    void writeUiBenchmarkReport();
    void rebuildStateDialogBoxes();
    void activateStateDialogCell();
    void moveStateDialogCursor(int rowDelta, int colDelta);
    void beginStateBrowser(bool saving);
    void saveStateToSelectedFile();
    void loadSelectedStateFile(const juce::File& file);
    juce::File selectedStateSaveFile(juce::String& error) const;
    juce::File initialStateDirectory() const;
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
    float zoomLevel = 1.0f;
    juce::Point<int> lastDragPosition;
    float panOffsetX = 0.0f;
    float panOffsetY = 0.0f;
    TrackerPalette palette;
    SamplerPalette samplerPalette;
    std::vector<float> samplerColumnWidths;
    /** Polylines drawn over the current grid (machine-page scope band). */
    std::vector<TrackerUIComponent::Trace> currentTraces;
    struct WavetablePreviewCache
    {
        bool valid = false;
        WavetableSynthMachine::UiPreviewState state;
        std::vector<TrackerUIComponent::Trace> traces;
        std::uint64_t revision = 0;
    };
    struct EnvelopePreviewCache
    {
        bool valid = false;
        std::array<float, 8> settings {};
        std::vector<float> samples;
        std::uint64_t revision = 0;
    };
    struct ModulationPreviewCache
    {
        bool valid = false;
        std::array<int, 4> waveforms {};
        std::vector<TrackerUIComponent::Trace> traces;
        std::uint64_t revision = 0;
    };
    std::unordered_map<int, WavetablePreviewCache> wavetablePreviewCaches;
    std::unordered_map<int, EnvelopePreviewCache> filterEnvelopePreviewCaches;
    std::unordered_map<int, ModulationPreviewCache> modulationPreviewCaches;
    struct ScopeCalibration
    {
        float scale = 1.0f;
        float target = 1.0f;
        std::int64_t lastRecalMs = 0;
        std::int64_t lastFrameMs = 0;
    };
    /** One auto-calibration state per stack so Sequence-page mini-scopes do not
        influence each other's vertical scale. This is transient UI state. */
    std::unordered_map<std::size_t, ScopeCalibration> scopeCalibrations;
    /** Reused UI-thread buffers for a single batched scope snapshot. */
    std::vector<std::vector<float>> sequenceScopeRawBuffers;
    std::vector<std::vector<float>> sequenceScopePreviewBuffers;
    bool samplerViewActive = false;
    bool customMachineColumnWidthsActive = false;
    juce::File lastStateDirectory;
    enum class StateDialogMode { none, savePrompt, loadPrompt, saveBrowser, loadBrowser, overwriteConfirm, error };
    StateDialogMode stateDialogMode = StateDialogMode::none;
    TrackerFileBrowser stateFileBrowser;
    std::vector<std::vector<UIBox>> stateDialogBoxes;
    std::size_t stateDialogColumn = 0;
    std::size_t stateDialogRow = 0;
    juce::String stateSaveName;
    juce::String stateDialogError;
    juce::File pendingStateFile;
    bool stateDialogErrorFromSave = false;
    bool stateFilenameEditing = false;

    std::atomic<bool> waitingForPaint { false };
    bool uiBenchmarkEnabled = false;
    juce::File uiBenchmarkReportFile;
    std::array<std::atomic<std::uint64_t>, 9> uiPageTotalNs {};
    std::array<std::atomic<std::uint64_t>, 9> uiPageMaxNs {};
    std::array<std::atomic<std::uint64_t>, 9> uiPageCount {};
    std::atomic<std::uint64_t> uiRenderTotalNs { 0 }, uiRenderMaxNs { 0 }, uiRenderCount { 0 };
    juce::CriticalSection screenshotLock;
    std::shared_ptr<TrackerMainProcessor::UiScreenshotRequest> pendingScreenshot;
    // This reference is provided as a quick way for your editor to
    // access the processor object that created it.

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TrackerMainUI)
};
