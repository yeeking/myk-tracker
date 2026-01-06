#pragma once

#include <JuceHeader.h>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>
#include "Segment14Geometry.h"
#include "Palette.h"

// OpenGL-backed renderer for the tracker grid and overlay.
class TrackerUIComponent
{
public:
    struct CellState
    {
        std::string text;
        juce::Colour fillColor { PaletteDefaults::cellFill };
        juce::Colour textColor { PaletteDefaults::cellText };
        juce::Colour glowColor { PaletteDefaults::cellGlow };
        juce::Colour outlineColor { PaletteDefaults::cellOutline };
        float glow = 0.0f;
        float depthScale = 1.0f;
        bool drawOutline = false;
    };

    using CellGrid = std::vector<std::vector<CellState>>;

    struct OverlayState
    {
        std::string text;
        juce::Colour color { PaletteDefaults::overlayText };
        juce::Colour glowColor { PaletteDefaults::overlayGlow };
        float glowStrength = 0.35f;
    };

    struct ZoomState
    {
        float zoomLevel = 1.0f;
    };

    struct DragState
    {
        float panX = 0.0f;
        float panY = 0.0f;
    };

    struct Style
    {
        juce::Colour background { TrackerPalette{}.background };
        juce::Colour lightColor { TrackerPalette{}.lightColor };
        juce::Colour defaultGlowColor { TrackerPalette{}.gridPlayhead };
        float ambientStrength = TrackerPalette{}.ambientStrength;
        juce::Vector3D<float> lightDirection { TrackerPalette{}.lightDirection };
    };

    explicit TrackerUIComponent(juce::OpenGLContext& context);
    ~TrackerUIComponent();

    void initOpenGL(int width, int height);
    void shutdownOpenGL();
    void updateUIState(const CellGrid& cells,
                       const OverlayState& overlay,
                       const ZoomState& zoomState,
                       const DragState& dragState,
                       const std::vector<float>* columnWidths = nullptr);
    void renderUI();

    void setViewportBounds(const juce::Rectangle<int>& bounds, int componentHeight, float renderingScale);
    void setStyle(const Style& style);
    void setCellSize(float width, float height);

private:
    struct ShaderAttributes
    {
        std::unique_ptr<juce::OpenGLShaderProgram::Attribute> position;
        std::unique_ptr<juce::OpenGLShaderProgram::Attribute> normal;
    };

    struct ShaderUniforms
    {
        std::unique_ptr<juce::OpenGLShaderProgram::Uniform> projectionMatrix;
        std::unique_ptr<juce::OpenGLShaderProgram::Uniform> viewMatrix;
        std::unique_ptr<juce::OpenGLShaderProgram::Uniform> modelMatrix;
        std::unique_ptr<juce::OpenGLShaderProgram::Uniform> cellColor;
        std::unique_ptr<juce::OpenGLShaderProgram::Uniform> cellGlow;
        std::unique_ptr<juce::OpenGLShaderProgram::Uniform> lightDirection;
        std::unique_ptr<juce::OpenGLShaderProgram::Uniform> lightColor;
        std::unique_ptr<juce::OpenGLShaderProgram::Uniform> ambientStrength;
        std::unique_ptr<juce::OpenGLShaderProgram::Uniform> glowColor;
    };

    struct TextShaderAttributes
    {
        std::unique_ptr<juce::OpenGLShaderProgram::Attribute> position;
    };

    struct TextShaderUniforms
    {
        std::unique_ptr<juce::OpenGLShaderProgram::Uniform> projectionMatrix;
        std::unique_ptr<juce::OpenGLShaderProgram::Uniform> viewMatrix;
        std::unique_ptr<juce::OpenGLShaderProgram::Uniform> modelMatrix;
        std::unique_ptr<juce::OpenGLShaderProgram::Uniform> textColor;
        std::unique_ptr<juce::OpenGLShaderProgram::Uniform> glowStrength;
        std::unique_ptr<juce::OpenGLShaderProgram::Uniform> glowColor;
    };

    struct TextMesh
    {
        GLuint vbo = 0;
        GLuint ibo = 0;
        GLsizei indexCount = 0;
    };

    struct ViewportState
    {
        juce::Rectangle<int> bounds;
        int componentHeight = 0;
        float renderingScale = 1.0f;
    };

    void configureViewport();
    void renderGrid(const juce::Matrix3D<float>& projectionMatrix,
                    const juce::Matrix3D<float>& viewMatrix,
                    float glowPulse);
    void renderCellText(const juce::Matrix3D<float>& projectionMatrix,
                        const juce::Matrix3D<float>& viewMatrix,
                        float glowPulse);
    void renderOverlayText(const juce::Matrix3D<float>& projectionMatrix,
                           const juce::Matrix3D<float>& viewMatrix);

    juce::Matrix3D<float> getProjectionMatrix(float aspectRatio) const;
    juce::Matrix3D<float> getViewMatrix() const;
    juce::Matrix3D<float> getModelMatrix(juce::Vector3D<float> position,
                                         juce::Vector3D<float> scale) const;
    void updateTextGeometryCache();
    TextMesh& ensureTextMesh(const std::string& text);
    void clearTextMeshCache();
    float getGlowPulse() const;

    juce::OpenGLContext* openGLContext = nullptr;
    Style style{};
    ViewportState viewport{};
    ZoomState zoomState{};
    DragState dragState{};
    OverlayState overlayState{};

    std::mutex stateMutex;
    CellGrid cellStates;
    std::vector<float> columnWidths;
    bool textGeometryDirty = false;

    std::unique_ptr<juce::OpenGLShaderProgram> shaderProgram;
    std::unique_ptr<ShaderAttributes> shaderAttributes;
    std::unique_ptr<ShaderUniforms> shaderUniforms;
    std::unique_ptr<juce::OpenGLShaderProgram> textShaderProgram;
    std::unique_ptr<TextShaderAttributes> textShaderAttributes;
    std::unique_ptr<TextShaderUniforms> textShaderUniforms;
    std::unordered_map<std::string, TextMesh> textMeshCache;
    Segment14Geometry::Params textGeomParams{};
    Segment14Geometry textGeometry{ textGeomParams };
    GLuint vertexBuffer = 0;
    GLuint indexBuffer = 0;
    GLuint frontEdgeIndexBuffer = 0;

    float cellWidth = 2.0f;
    float cellHeight = 1.0f;
    float cellDepth = 0.6f;
    float cellGap = 0.2f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TrackerUIComponent)
};
