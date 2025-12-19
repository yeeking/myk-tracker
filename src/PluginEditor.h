/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin editor.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include <mutex>
#include "PluginProcessor.h"
#include "StringTable.h"
#include "Sequencer.h"
#include "SequencerEditor.h"
#include "TrackerController.h"
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

    // KeyListener overrides
    using juce::Component::keyPressed;
    bool keyPressed(const juce::KeyPress& key, juce::Component* originatingComponent) override;
    using juce::Component::keyStateChanged;
    bool keyStateChanged(bool isKeyDown, juce::Component* originatingComponent) override;
    /** next time we draw, call update on the sequencer's string representation */
    // void updateStringOnNextDraw();
    long framesDrawn; 
private:
    struct CellVisualState
    {
        bool hasNote = false;
        bool isActivePlayhead = false;
        bool isSelected = false;
        bool isArmed = false;
    };

    struct ShaderAttributes
    {
        std::unique_ptr<juce::OpenGLShaderProgram::Attribute> position;
        std::unique_ptr<juce::OpenGLShaderProgram::Attribute> colour;
    };

    struct ShaderUniforms
    {
        std::unique_ptr<juce::OpenGLShaderProgram::Uniform> projectionMatrix;
        std::unique_ptr<juce::OpenGLShaderProgram::Uniform> viewMatrix;
        std::unique_ptr<juce::OpenGLShaderProgram::Uniform> modelMatrix;
        std::unique_ptr<juce::OpenGLShaderProgram::Uniform> cellColor;
    };

    struct TextShaderAttributes
    {
        std::unique_ptr<juce::OpenGLShaderProgram::Attribute> position;
        std::unique_ptr<juce::OpenGLShaderProgram::Attribute> texCoord;
    };

    struct TextShaderUniforms
    {
        std::unique_ptr<juce::OpenGLShaderProgram::Uniform> projectionMatrix;
        std::unique_ptr<juce::OpenGLShaderProgram::Uniform> viewMatrix;
        std::unique_ptr<juce::OpenGLShaderProgram::Uniform> modelMatrix;
        std::unique_ptr<juce::OpenGLShaderProgram::Uniform> uvRect;
        std::unique_ptr<juce::OpenGLShaderProgram::Uniform> textTexture;
    };

    PluginProcessor& audioProcessor;

    StringTable controlPanelTable;
    Sequencer* sequencer; 
    SequencerEditor* seqEditor;
    TrackerController* trackerController; 
    juce::OpenGLContext openGLContext;
    std::unique_ptr<juce::OpenGLShaderProgram> shaderProgram;
    std::unique_ptr<ShaderAttributes> shaderAttributes;
    std::unique_ptr<ShaderUniforms> shaderUniforms;
    std::unique_ptr<juce::OpenGLShaderProgram> textShaderProgram;
    std::unique_ptr<TextShaderAttributes> textShaderAttributes;
    std::unique_ptr<TextShaderUniforms> textShaderUniforms;
    juce::OpenGLTexture textTexture;
    GLuint vertexBuffer = 0;
    GLuint indexBuffer = 0;
    GLuint textVertexBuffer = 0;

    size_t rowsInUI;
    juce::Rectangle<int> seqViewBounds;
    
    void prepareControlPanelView();
    void prepareSequenceView();
    void prepareStepView();
    void prepareSeqConfigView();
    void updateCellStates(const std::vector<std::vector<std::string>>& data,
                          size_t rowsToDisplay,
                          size_t colsToDisplay,
                          size_t cursorCol,
                          size_t cursorRow,
                          const std::vector<std::pair<int, int>>& highlightCells,
                          bool showCursor,
                          size_t armedSeq);

    juce::Matrix3D<float> getProjectionMatrix(float aspectRatio) const;
    juce::Matrix3D<float> getViewMatrix() const;
    juce::Matrix3D<float> getModelMatrix(juce::Vector3D<float> position, juce::Vector3D<float> scale) const;
    juce::Colour getCellColour(const CellVisualState& cell) const;
    float getCellDepthScale(const CellVisualState& cell) const;
    void updateTextAtlasImage();

    std::mutex cellStateMutex;
    std::vector<std::vector<CellVisualState>> cellStates;
    std::vector<std::vector<std::string>> visibleText;
    size_t visibleCols = 0;
    size_t visibleRows = 0;
    size_t startCol = 0;
    size_t startRow = 0;
    size_t lastStartCol = 0;
    size_t lastStartRow = 0;
    bool textAtlasDirty = false;
    bool textAtlasUploadPending = false;
    int textAtlasWidth = 0;
    int textAtlasHeight = 0;
    int cellPixelWidth = 0;
    int cellPixelHeight = 0;
    juce::Image textAtlasImage;

    bool waitingForPaint;
    bool updateSeqStrOnNextDraw;
    // This reference is provided as a quick way for your editor to
    // access the processor object that created it.

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginEditor)
};
