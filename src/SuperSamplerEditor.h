#pragma once

#include "SuperSamplerProcessor.h"
#include "TrackerUIComponent.h"
#include "Palette.h"
#include "UIBox.h"
#include <JuceHeader.h>
#include <mutex>
#include <vector>

// Dedicated sampler UI for the standalone sampler processor.
class SuperSamplerEditor final : public juce::AudioProcessorEditor,
                           public juce::OpenGLRenderer,
                           public juce::Timer,
                           public juce::KeyListener
{
public:
    explicit SuperSamplerEditor (SuperSamplerProcessor&);
    ~SuperSamplerEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    void timerCallback() override;

    void newOpenGLContextCreated() override;
    void renderOpenGL() override;
    void openGLContextClosing() override;
    void mouseWheelMove (const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) override;
    void mouseDown (const juce::MouseEvent& event) override;
    void mouseDrag (const juce::MouseEvent& event) override;

    using juce::Component::keyPressed;
    bool keyPressed (const juce::KeyPress& key, juce::Component* originatingComponent) override;

    void updateUIFromProcessor(const juce::var& payload);

private:
    enum class Action
    {
        None,
        Add,
        Load,
        Trigger,
        Low,
        High,
        Gain,
        Waveform
    };

    struct PlayerUIState
    {
        int id = 0;
        int midiLow = 36;
        int midiHigh = 60;
        float gain = 1.0f;
        bool isPlaying = false;
        juce::String status;
        juce::String fileName;
        juce::String filePath;
        std::vector<float> waveformPoints;
    };

    struct CellVisualState
    {
        bool isSelected = false;
        bool isEditing = false;
        bool isActive = false;
        bool isDisabled = false;
        float glow = 0.0f;
    };

    struct CellInfo
    {
        Action action = Action::None;
        int playerIndex = -1;
    };

    SuperSamplerProcessor& processorRef;

    juce::OpenGLContext openGLContext;
    TrackerUIComponent uiComponent;

    SamplerPalette palette{};

    std::vector<PlayerUIState> players;
    std::vector<std::vector<CellVisualState>> cellVisualStates;
    TrackerUIComponent::CellGrid cellStates;
    std::vector<std::vector<CellInfo>> cellInfo;

    std::mutex uiMutex;
    std::mutex stateMutex;
    juce::var pendingPayload;
    bool pendingPayloadReady = false;

    size_t cursorRow = 0;
    size_t cursorCol = 0;
    bool editMode = false;
    Action editAction = Action::None;
    int editPlayerIndex = -1;

    float zoomLevel = 1.0f;
    float panOffsetX = 0.0f;
    float panOffsetY = 0.0f;
    juce::Point<int> lastDragPosition;
    juce::Rectangle<int> gridBounds;
    std::vector<float> columnWidthScales;

    void refreshFromProcessor();
    void refreshFromPayload(const juce::var& payload);
    void rebuildCellLayout();
    void adjustZoom(float delta);

    void handleAction(const CellInfo& info);
    void adjustEditValue(int direction);
    void moveCursor(int deltaRow, int deltaCol);

    juce::Colour getCellColour(const UIBox& cell) const;
    juce::Colour getTextColour(const UIBox& cell) const;
    float getCellDepthScale(const UIBox& cell) const;
};
