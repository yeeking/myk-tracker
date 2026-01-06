/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin editor.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
// #include "StringTable.h"
#include "Sequencer.h"
#include "SequencerEditor.h"
#include "TrackerController.h"
#include "TrackerUIComponent.h"
#include "Palette.h"
//==============================================================================
/**
*/
class PluginEditor  : public juce::AudioProcessorEditor,
                      public juce::OpenGLRenderer,
                      public juce::Timer,
                      public juce::KeyListener

{
public:
    PluginEditor (PluginProcessor&);
    ~PluginEditor() override;

    //==============================================================================
    void paint (juce::Graphics&) override;
    void resized() override;

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
    long framesDrawn; 
private:

// some variables to control the display style
    const float glowDecayScalar{0.8f};
    // render sizes for cells and 
    const float cellWidth{2.0f};
    const float cellHeight{1.0f};
    
    PluginProcessor& audioProcessor;

    // StringTable controlPanelTable;
    Sequencer* sequencer; 
    SequencerEditor* seqEditor;
    TrackerController* trackerController; 
    juce::OpenGLContext openGLContext;
    TrackerUIComponent uiComponent;

    size_t rowsInUI;
    juce::Rectangle<int> seqViewBounds;
    
    void prepareControlPanelView();
    void prepareSequenceView();
    void prepareStepView();
    void prepareSeqConfigView();
    void prepareMachineConfigView();
    void updateCellStates(const std::vector<std::vector<std::string>>& data,
                          size_t rowsToDisplay,
                          size_t colsToDisplay,
                          size_t cursorCol,
                          size_t cursorRow,
                          const std::vector<std::pair<int, int>>& highlightCells,
                          bool showCursor,
                          size_t armedSeq);

    struct CellVisualFlags
    {
        bool hasNote = false;
        bool isActivePlayhead = false;
        bool isSelected = false;
        bool isArmed = false;
    };

    TrackerUIComponent::CellState makeDefaultCell() const;
    juce::Colour getCellColour(const CellVisualFlags& cell) const;
    juce::Colour getTextColour(const CellVisualFlags& cell) const;
    float getCellDepthScale(const CellVisualFlags& cell) const;
    void adjustZoom(float delta);
    void moveUp(float amount);
    void moveDown(float amount);
    void moveLeft(float amount);
    void moveRight(float amount);
    void updateSamplerCellStates(const std::vector<std::vector<SamplerCell>>& cells);
    juce::Colour getSamplerCellColour(const SamplerCell& cell) const;
    juce::Colour getSamplerTextColour(const SamplerCell& cell) const;
    float getSamplerCellDepthScale(const SamplerCell& cell) const;

    TrackerUIComponent::CellGrid cellStates;
    std::vector<std::vector<float>> playheadGlow;
    size_t visibleCols = 0;
    size_t visibleRows = 0;
    size_t startCol = 0;
    size_t startRow = 0;
    size_t lastStartCol = 0;
    size_t lastStartRow = 0;
    TrackerUIComponent::OverlayState overlayState;
    int lastHudBpm = -1;
    float zoomLevel = 1.0f;
    juce::Point<int> lastDragPosition;
    float panOffsetX = 0.0f;
    float panOffsetY = 0.0f;
    TrackerPalette palette;
    SamplerPalette samplerPalette;
    std::vector<float> samplerColumnWidths;
    bool samplerViewActive = false;

    bool waitingForPaint;
    bool updateSeqStrOnNextDraw;
    // This reference is provided as a quick way for your editor to
    // access the processor object that created it.

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginEditor)
};
