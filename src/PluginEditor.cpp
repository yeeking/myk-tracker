/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin editor.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "SequencerCommands.h"
#include "SimpleClock.h"
#include <algorithm>
#include <cmath>
#include <unordered_set>

using namespace juce::gl;

namespace
{
struct Vertex
{
    float position[3];
    float normal[3];
};

const char* vertexShaderSource = R"(
    attribute vec3 position;
    attribute vec3 normal;
    uniform mat4 projectionMatrix;
    uniform mat4 viewMatrix;
    uniform mat4 modelMatrix;
    varying vec3 vNormal;

    void main()
    {
        vec4 worldNormal = modelMatrix * vec4(normal, 0.0);
        vNormal = normalize(worldNormal.xyz);
        gl_Position = projectionMatrix * viewMatrix * modelMatrix * vec4(position, 1.0);
    }
)";

const char* fragmentShaderSource = R"(
    varying vec3 vNormal;
    uniform vec4 cellColor;
    uniform float cellGlow;
    uniform vec3 lightDirection;
    uniform vec3 lightColor;
    uniform float ambientStrength;
    uniform vec3 glowColor;

    void main()
    {
        vec3 normal = normalize(vNormal);
        vec3 lightDir = normalize(lightDirection);
        float diff = max(dot(normal, lightDir), 0.0);
        vec3 litColor = cellColor.rgb * (ambientStrength + diff * lightColor);
        vec3 glow = glowColor * cellGlow;
        gl_FragColor = vec4(litColor + glow, cellColor.a);
    }
)";

const char* textVertexShaderSource = R"(
    attribute vec3 position;
    uniform mat4 projectionMatrix;
    uniform mat4 viewMatrix;
    uniform mat4 modelMatrix;

    void main()
    {
        gl_Position = projectionMatrix * viewMatrix * modelMatrix * vec4(position, 1.0);
    }
)";

const char* textFragmentShaderSource = R"(
    uniform vec4 textColor;
    uniform float glowStrength;
    uniform vec3 glowColor;

    void main()
    {
        vec3 color = textColor.rgb + glowColor * glowStrength;
        gl_FragColor = vec4(color, textColor.a);
    }
)";

const Vertex cubeVertices[] =
{
    // +Z
    { { -0.5f, -0.5f,  0.5f }, { 0.0f, 0.0f, 1.0f } },
    { {  0.5f, -0.5f,  0.5f }, { 0.0f, 0.0f, 1.0f } },
    { {  0.5f,  0.5f,  0.5f }, { 0.0f, 0.0f, 1.0f } },
    { { -0.5f,  0.5f,  0.5f }, { 0.0f, 0.0f, 1.0f } },
    // +X
    { {  0.5f, -0.5f,  0.5f }, { 1.0f, 0.0f, 0.0f } },
    { {  0.5f, -0.5f, -0.5f }, { 1.0f, 0.0f, 0.0f } },
    { {  0.5f,  0.5f, -0.5f }, { 1.0f, 0.0f, 0.0f } },
    { {  0.5f,  0.5f,  0.5f }, { 1.0f, 0.0f, 0.0f } },
    // -Z
    { {  0.5f, -0.5f, -0.5f }, { 0.0f, 0.0f, -1.0f } },
    { { -0.5f, -0.5f, -0.5f }, { 0.0f, 0.0f, -1.0f } },
    { { -0.5f,  0.5f, -0.5f }, { 0.0f, 0.0f, -1.0f } },
    { {  0.5f,  0.5f, -0.5f }, { 0.0f, 0.0f, -1.0f } },
    // -X
    { { -0.5f, -0.5f, -0.5f }, { -1.0f, 0.0f, 0.0f } },
    { { -0.5f, -0.5f,  0.5f }, { -1.0f, 0.0f, 0.0f } },
    { { -0.5f,  0.5f,  0.5f }, { -1.0f, 0.0f, 0.0f } },
    { { -0.5f,  0.5f, -0.5f }, { -1.0f, 0.0f, 0.0f } },
    // +Y
    { { -0.5f,  0.5f,  0.5f }, { 0.0f, 1.0f, 0.0f } },
    { {  0.5f,  0.5f,  0.5f }, { 0.0f, 1.0f, 0.0f } },
    { {  0.5f,  0.5f, -0.5f }, { 0.0f, 1.0f, 0.0f } },
    { { -0.5f,  0.5f, -0.5f }, { 0.0f, 1.0f, 0.0f } },
    // -Y
    { { -0.5f, -0.5f, -0.5f }, { 0.0f, -1.0f, 0.0f } },
    { {  0.5f, -0.5f, -0.5f }, { 0.0f, -1.0f, 0.0f } },
    { {  0.5f, -0.5f,  0.5f }, { 0.0f, -1.0f, 0.0f } },
    { { -0.5f, -0.5f,  0.5f }, { 0.0f, -1.0f, 0.0f } }
};

const GLuint cubeIndices[] =
{
    0, 1, 2, 2, 3, 0,       // +Z
    4, 5, 6, 6, 7, 4,       // +X
    8, 9, 10, 10, 11, 8,    // -Z
    12, 13, 14, 14, 15, 12, // -X
    16, 17, 18, 18, 19, 16, // +Y
    20, 21, 22, 22, 23, 20  // -Y
};
const GLsizei cubeIndexCount = static_cast<GLsizei>(sizeof(cubeIndices) / sizeof(cubeIndices[0]));

const GLuint cubeEdgeIndices[] =
{
    0, 1, 1, 2, 2, 3, 3, 0,       // front
    8, 9, 9, 10, 10, 11, 11, 8,   // back
    0, 13, 1, 4, 2, 7, 3, 16      // side connections
};
const GLsizei cubeEdgeIndexCount = static_cast<GLsizei>(sizeof(cubeEdgeIndices) / sizeof(cubeEdgeIndices[0]));

const GLuint frontFaceEdgeIndices[] =
{
    0, 1, 1, 2, 2, 3, 3, 0
};
const GLsizei frontFaceEdgeIndexCount = static_cast<GLsizei>(sizeof(frontFaceEdgeIndices) / sizeof(frontFaceEdgeIndices[0]));

juce::Matrix3D<float> makeScaleMatrix(juce::Vector3D<float> scale)
{
    return { scale.x, 0.0f, 0.0f, 0.0f,
             0.0f, scale.y, 0.0f, 0.0f,
             0.0f, 0.0f, scale.z, 0.0f,
             0.0f, 0.0f, 0.0f, 1.0f };
}
} // namespace

//==============================================================================
PluginEditor::PluginEditor (PluginProcessor& p)
    : AudioProcessorEditor (&p),
    framesDrawn{0},
    audioProcessor (p),
    sequencer{p.getSequencer()},
    seqEditor{p.getSequenceEditor()},
    trackerController{p.getTrackerController()},
    rowsInUI{9},
    waitingForPaint{false},
    updateSeqStrOnNextDraw{false}
{
    palette.background = juce::Colour(0xFF02040A);
    palette.gridEmpty = juce::Colour(0xFF1B2024);
    palette.gridNote = juce::Colour(0xFF00F6FF);
    palette.gridPlayhead = juce::Colour(0xFFFF3B2F);
    palette.gridSelected = juce::Colour(0xFF29E0FF);
    palette.textPrimary = juce::Colour(0xFF3DE6C0);
    palette.textWarning = juce::Colour(0xFFFF5A3C);
    palette.textBackground = juce::Colours::transparentBlack;
    palette.statusOk = juce::Colour(0xFF19FF6A);
    palette.borderNeon = juce::Colour(0xFF0F5F4B);
    palette.lightColor = juce::Colour(0xFFDDF6E8);
    palette.ambientStrength = 0.32f;
    palette.lightDirection = { 0.2f, 0.45f, 1.0f };
    textGeomParams.cellW = 1.0f;
    textGeomParams.cellH = 1.6f;
    textGeomParams.thickness = 0.14f;
    textGeomParams.inset = 0.12f;
    textGeomParams.gap = 0.06f;
    textGeomParams.advance = 1.12f;
    textGeomParams.includeDot = true;
    textGeometry.setParams(textGeomParams);

    openGLContext.setRenderer(this);
    openGLContext.setContinuousRepainting(false);
    openGLContext.setComponentPaintingEnabled(true);
    openGLContext.attachTo(*this);
    // Make sure that before the constructor has finished, you've set the
    // editor's size to whatever you need it to be.
    setSize (1024, 768);
    // addAndMakeVisible(controlPanelTable);
    
    // Add this editor as a key listener
    addKeyListener(this);
    setWantsKeyboardFocus(true);

    startTimer(1000 / 25);

}

PluginEditor::~PluginEditor()
{
  stopTimer();
  openGLContext.detach();
}

void PluginEditor::newOpenGLContextCreated()
{
    shaderProgram = std::make_unique<juce::OpenGLShaderProgram>(openGLContext);
    if (shaderProgram->addVertexShader(vertexShaderSource)
        && shaderProgram->addFragmentShader(fragmentShaderSource)
        && shaderProgram->link())
    {
        shaderAttributes = std::make_unique<ShaderAttributes>();
        shaderAttributes->position = std::make_unique<juce::OpenGLShaderProgram::Attribute>(*shaderProgram, "position");
        shaderAttributes->normal = std::make_unique<juce::OpenGLShaderProgram::Attribute>(*shaderProgram, "normal");

        shaderUniforms = std::make_unique<ShaderUniforms>();
        shaderUniforms->projectionMatrix = std::make_unique<juce::OpenGLShaderProgram::Uniform>(*shaderProgram, "projectionMatrix");
        shaderUniforms->viewMatrix = std::make_unique<juce::OpenGLShaderProgram::Uniform>(*shaderProgram, "viewMatrix");
        shaderUniforms->modelMatrix = std::make_unique<juce::OpenGLShaderProgram::Uniform>(*shaderProgram, "modelMatrix");
        shaderUniforms->cellColor = std::make_unique<juce::OpenGLShaderProgram::Uniform>(*shaderProgram, "cellColor");
        shaderUniforms->cellGlow = std::make_unique<juce::OpenGLShaderProgram::Uniform>(*shaderProgram, "cellGlow");
        shaderUniforms->lightDirection = std::make_unique<juce::OpenGLShaderProgram::Uniform>(*shaderProgram, "lightDirection");
        shaderUniforms->lightColor = std::make_unique<juce::OpenGLShaderProgram::Uniform>(*shaderProgram, "lightColor");
        shaderUniforms->ambientStrength = std::make_unique<juce::OpenGLShaderProgram::Uniform>(*shaderProgram, "ambientStrength");
        shaderUniforms->glowColor = std::make_unique<juce::OpenGLShaderProgram::Uniform>(*shaderProgram, "glowColor");
    }
    else
    {
        jassertfalse;
        shaderProgram.reset();
        return;
    }

    textShaderProgram = std::make_unique<juce::OpenGLShaderProgram>(openGLContext);
    if (textShaderProgram->addVertexShader(textVertexShaderSource)
        && textShaderProgram->addFragmentShader(textFragmentShaderSource)
        && textShaderProgram->link())
    {
        textShaderAttributes = std::make_unique<TextShaderAttributes>();
        textShaderAttributes->position = std::make_unique<juce::OpenGLShaderProgram::Attribute>(*textShaderProgram, "position");

        textShaderUniforms = std::make_unique<TextShaderUniforms>();
        textShaderUniforms->projectionMatrix = std::make_unique<juce::OpenGLShaderProgram::Uniform>(*textShaderProgram, "projectionMatrix");
        textShaderUniforms->viewMatrix = std::make_unique<juce::OpenGLShaderProgram::Uniform>(*textShaderProgram, "viewMatrix");
        textShaderUniforms->modelMatrix = std::make_unique<juce::OpenGLShaderProgram::Uniform>(*textShaderProgram, "modelMatrix");
        textShaderUniforms->textColor = std::make_unique<juce::OpenGLShaderProgram::Uniform>(*textShaderProgram, "textColor");
        textShaderUniforms->glowStrength = std::make_unique<juce::OpenGLShaderProgram::Uniform>(*textShaderProgram, "glowStrength");
        textShaderUniforms->glowColor = std::make_unique<juce::OpenGLShaderProgram::Uniform>(*textShaderProgram, "glowColor");
    }
    else
    {
        jassertfalse;
        textShaderProgram.reset();
        return;
    }
    textGeometryDirty = true;

    openGLContext.extensions.glGenBuffers(1, &vertexBuffer);
    openGLContext.extensions.glBindBuffer(GL_ARRAY_BUFFER, vertexBuffer);
    openGLContext.extensions.glBufferData(GL_ARRAY_BUFFER,
                                          sizeof(cubeVertices),
                                          cubeVertices,
                                          GL_STATIC_DRAW);

    openGLContext.extensions.glGenBuffers(1, &indexBuffer);
    openGLContext.extensions.glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, indexBuffer);
    openGLContext.extensions.glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                                          sizeof(cubeIndices),
                                          cubeIndices,
                                          GL_STATIC_DRAW);

    openGLContext.extensions.glGenBuffers(1, &edgeIndexBuffer);
    openGLContext.extensions.glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, edgeIndexBuffer);
    openGLContext.extensions.glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                                          sizeof(cubeEdgeIndices),
                                          cubeEdgeIndices,
                                          GL_STATIC_DRAW);

    openGLContext.extensions.glGenBuffers(1, &frontEdgeIndexBuffer);
    openGLContext.extensions.glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, frontEdgeIndexBuffer);
    openGLContext.extensions.glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                                          sizeof(frontFaceEdgeIndices),
                                          frontFaceEdgeIndices,
                                          GL_STATIC_DRAW);

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
}

void PluginEditor::renderOpenGL()
{
    if (shaderProgram == nullptr || seqViewBounds.isEmpty())
        return;

    // Convert the JUCE pixel bounds into an OpenGL viewport/scissor rectangle.
    const float renderingScale = openGLContext.getRenderingScale();
    const int viewportWidth = juce::roundToInt(seqViewBounds.getWidth() * renderingScale);
    const int viewportHeight = juce::roundToInt(seqViewBounds.getHeight() * renderingScale);

    if (viewportWidth <= 0 || viewportHeight <= 0)
        return;

    const int viewportX = juce::roundToInt(seqViewBounds.getX() * renderingScale);
    const int viewportY = juce::roundToInt((getHeight() - seqViewBounds.getBottom()) * renderingScale);

    // Constrain rendering to the sequencer view and clear to the palette background.
    glEnable(GL_SCISSOR_TEST);
    glScissor(viewportX, viewportY, viewportWidth, viewportHeight);
    glViewport(viewportX, viewportY, viewportWidth, viewportHeight);

    glClearColor(palette.background.getFloatRed(),
                 palette.background.getFloatGreen(),
                 palette.background.getFloatBlue(),
                 palette.background.getFloatAlpha());
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDepthMask(GL_TRUE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);

    // Main grid shader: cube geometry with lighting + glow uniforms.
    openGLContext.extensions.glUseProgram(shaderProgram->getProgramID());

    const float aspectRatio = static_cast<float>(viewportWidth) / static_cast<float>(viewportHeight);
    const auto projectionMatrix = getProjectionMatrix(aspectRatio);
    const auto viewMatrix = getViewMatrix();

    if (shaderUniforms->projectionMatrix != nullptr)
        shaderUniforms->projectionMatrix->setMatrix4(projectionMatrix.mat, 1, GL_FALSE);

    if (shaderUniforms->viewMatrix != nullptr)
        shaderUniforms->viewMatrix->setMatrix4(viewMatrix.mat, 1, GL_FALSE);

    openGLContext.extensions.glBindBuffer(GL_ARRAY_BUFFER, vertexBuffer);
    openGLContext.extensions.glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, indexBuffer);

    const GLsizei stride = sizeof(Vertex);
    if (shaderAttributes->position != nullptr)
    {
        openGLContext.extensions.glVertexAttribPointer(shaderAttributes->position->attributeID,
                                                       3,
                                                       GL_FLOAT,
                                                       GL_FALSE,
                                                       stride,
                                                       reinterpret_cast<GLvoid*>(offsetof(Vertex, position)));
        openGLContext.extensions.glEnableVertexAttribArray(shaderAttributes->position->attributeID);
    }

    if (shaderAttributes->normal != nullptr)
    {
        openGLContext.extensions.glVertexAttribPointer(shaderAttributes->normal->attributeID,
                                                       3,
                                                       GL_FLOAT,
                                                       GL_FALSE,
                                                       stride,
                                                       reinterpret_cast<GLvoid*>(offsetof(Vertex, normal)));
        openGLContext.extensions.glEnableVertexAttribArray(shaderAttributes->normal->attributeID);
    }

    {
        std::lock_guard<std::mutex> lock(cellStateMutex);
        if (visibleCols > 0 && visibleRows > 0 && !cellStates.empty())
        {
            struct NoteOutline
            {
                juce::Matrix3D<float> modelMatrix;
                float glow = 0.0f;
            };

            std::vector<NoteOutline> noteOutlines;
            noteOutlines.reserve(visibleCols * visibleRows);

            // Grid layout (same geometry spacing as the text overlay pass below).
            // const float cellWidth = 1.0f;
            // const float cellWidth = 2.0f;
            
            // const float cellHeight = 1.0f;// 
            const float cellDepth = 0.6f;
            const float cellGap = 0.2f;

            const float stepX = cellWidth + cellGap;
            const float stepY = cellHeight + cellGap;
            const float gridWidth = stepX * static_cast<float>(visibleCols);
            const float gridHeight = stepY * static_cast<float>(visibleRows);
            const float startX = -gridWidth * 0.5f + stepX * 0.5f;
            const float startY = gridHeight * 0.5f - stepY * 0.5f;

            for (size_t col = 0; col < visibleCols; ++col)
            {
                for (size_t row = 0; row < visibleRows; ++row)
                {
                    const auto& cell = cellStates[col][row];
                    // Depth/thickness, colour, glow, and wireframe are all derived from cell state.
                    const float depthScale = getCellDepthScale(cell);
                    const float depth = cellDepth * depthScale;

                    const auto position = juce::Vector3D<float>(startX + static_cast<float>(col) * stepX,
                                                                startY - static_cast<float>(row) * stepY,
                                                                depth * 0.5f);
                    const auto scale = juce::Vector3D<float>(cellWidth, cellHeight, depth);
                    const auto modelMatrix = getModelMatrix(position, scale);
                    // getCellColour maps selection/armed/note states to palette colors.
                    const auto colour = getCellColour(cell);
                    // playheadGlow is animated so the active playhead can pulse.
                    const float timeSeconds = static_cast<float>(framesDrawn) / 25.0f;
                    const float glowPulse = 0.75f + 0.25f * std::sin(timeSeconds * 6.0f);
                    const float glow = cell.playheadGlow * glowPulse;

                    if (shaderUniforms->modelMatrix != nullptr)
                        shaderUniforms->modelMatrix->setMatrix4(modelMatrix.mat, 1, GL_FALSE);

                    if (shaderUniforms->cellColor != nullptr)
                        shaderUniforms->cellColor->set(colour.getFloatRed(),
                                                       colour.getFloatGreen(),
                                                       colour.getFloatBlue(),
                                                       colour.getFloatAlpha());

                    if (shaderUniforms->cellGlow != nullptr)
                        shaderUniforms->cellGlow->set(glow);

                    if (shaderUniforms->lightDirection != nullptr)
                        shaderUniforms->lightDirection->set(palette.lightDirection.x,
                                                            palette.lightDirection.y,
                                                            palette.lightDirection.z);

                    if (shaderUniforms->lightColor != nullptr)
                        shaderUniforms->lightColor->set(palette.lightColor.getFloatRed(),
                                                        palette.lightColor.getFloatGreen(),
                                                        palette.lightColor.getFloatBlue());

                    if (shaderUniforms->ambientStrength != nullptr)
                        shaderUniforms->ambientStrength->set(palette.ambientStrength);

                    if (shaderUniforms->glowColor != nullptr)
                        shaderUniforms->glowColor->set(palette.gridPlayhead.getFloatRed(),
                                                       palette.gridPlayhead.getFloatGreen(),
                                                       palette.gridPlayhead.getFloatBlue());

                    glDrawElements(GL_TRIANGLES, cubeIndexCount, GL_UNSIGNED_INT, nullptr);

                    if (cell.hasNote)
                        noteOutlines.push_back(NoteOutline{modelMatrix, glow});
                }
            }

            if (!noteOutlines.empty())
            {
                glDepthMask(GL_FALSE);
                glEnable(GL_POLYGON_OFFSET_LINE);
                glPolygonOffset(-2.0f, -2.0f);
                glLineWidth(1.8f);
                openGLContext.extensions.glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, frontEdgeIndexBuffer);

                const auto outlineColour = palette.gridNote;
                if (shaderUniforms->cellColor != nullptr)
                    shaderUniforms->cellColor->set(outlineColour.getFloatRed(),
                                                   outlineColour.getFloatGreen(),
                                                   outlineColour.getFloatBlue(),
                                                   1.0f);

                if (shaderUniforms->ambientStrength != nullptr)
                    shaderUniforms->ambientStrength->set(1.0f);

                if (shaderUniforms->lightColor != nullptr)
                    shaderUniforms->lightColor->set(0.0f, 0.0f, 0.0f);

                if (shaderUniforms->glowColor != nullptr)
                    shaderUniforms->glowColor->set(outlineColour.getFloatRed(),
                                                   outlineColour.getFloatGreen(),
                                                   outlineColour.getFloatBlue());

                if (shaderUniforms->cellGlow != nullptr)
                    shaderUniforms->cellGlow->set(1.0f);

                for (const auto& outline : noteOutlines)
                {
                    if (shaderUniforms->modelMatrix != nullptr)
                        shaderUniforms->modelMatrix->setMatrix4(outline.modelMatrix.mat, 1, GL_FALSE);

                    glDrawElements(GL_LINES, frontFaceEdgeIndexCount, GL_UNSIGNED_INT, nullptr);
                }

                openGLContext.extensions.glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, indexBuffer);
                glDisable(GL_POLYGON_OFFSET_LINE);
                glDepthMask(GL_TRUE);
            }
        }
    }

    if (textShaderProgram != nullptr)
    {
        updateTextGeometryCache();
        openGLContext.extensions.glUseProgram(textShaderProgram->getProgramID());

        if (textShaderUniforms->projectionMatrix != nullptr)
            textShaderUniforms->projectionMatrix->setMatrix4(projectionMatrix.mat, 1, GL_FALSE);

        if (textShaderUniforms->viewMatrix != nullptr)
            textShaderUniforms->viewMatrix->setMatrix4(viewMatrix.mat, 1, GL_FALSE);

        glEnable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        if (textShaderAttributes->position != nullptr)
            openGLContext.extensions.glEnableVertexAttribArray(textShaderAttributes->position->attributeID);

        {
            std::lock_guard<std::mutex> lock(cellStateMutex);
            if (visibleCols > 0 && visibleRows > 0 && !cellStates.empty())
            {
                const float cellDepth = 0.6f;
                const float cellGap = 0.2f;
                const float textDepthOffset = 0.02f;
                const float stepX = cellWidth + cellGap;
                const float stepY = cellHeight + cellGap;
                const float gridWidth = stepX * static_cast<float>(visibleCols);
                const float gridHeight = stepY * static_cast<float>(visibleRows);
                const float startX = -gridWidth * 0.5f + stepX * 0.5f;
                const float startY = gridHeight * 0.5f - stepY * 0.5f;
                const float targetHeight = cellHeight * 0.6f;
                const float targetWidth = cellWidth * 0.9f;
                const float padX = cellWidth * 0.08f;

                for (size_t col = 0; col < visibleCols; ++col)
                {
                    for (size_t row = 0; row < visibleRows; ++row)
                    {
                        const auto& cell = cellStates[col][row];
                        const auto& text = visibleText[col][row];
                        if (text.empty())
                            continue;

                        auto meshIt = textMeshCache.find(text);
                        if (meshIt == textMeshCache.end() || meshIt->second.indexCount == 0)
                            continue;
                        const auto& mesh = meshIt->second;

                        const float depthScale = getCellDepthScale(cell);
                        const float depth = cellDepth * depthScale;
                        const float cellCenterX = startX + static_cast<float>(col) * stepX;
                        const float cellCenterY = startY - static_cast<float>(row) * stepY;
                        const float textZ = depth + textDepthOffset;

                        const float textWidth = textGeomParams.advance * static_cast<float>(text.size());
                        const float widthScale = (textWidth > 0.0f) ? (targetWidth / textWidth) : 1.0f;
                        const float heightScale = targetHeight / textGeomParams.cellH;
                        const float scale = std::min(widthScale, heightScale);
                        const float textHeightScaled = textGeomParams.cellH * scale;
                        const float baseX = cellCenterX - cellWidth * 0.5f + padX;
                        const float baseY = cellCenterY - textHeightScaled * 0.5f;

                        const auto position = juce::Vector3D<float>(baseX, baseY, textZ);
                        const auto scaleVec = juce::Vector3D<float>(scale, scale, 1.0f);
                        const auto modelMatrix = getModelMatrix(position, scaleVec);

                        const auto textColour = getTextColour(cell);
                        if (textShaderUniforms->textColor != nullptr)
                            textShaderUniforms->textColor->set(textColour.getFloatRed(),
                                                               textColour.getFloatGreen(),
                                                               textColour.getFloatBlue(),
                                                               textColour.getFloatAlpha());

                        const float timeSeconds = static_cast<float>(framesDrawn) / 25.0f;
                        const float glowPulse = 0.75f + 0.25f * std::sin(timeSeconds * 6.0f);
                        const float glowStrength = cell.playheadGlow * glowPulse;

                        if (textShaderUniforms->glowStrength != nullptr)
                            textShaderUniforms->glowStrength->set(glowStrength);

                        if (textShaderUniforms->glowColor != nullptr)
                            textShaderUniforms->glowColor->set(palette.gridPlayhead.getFloatRed(),
                                                               palette.gridPlayhead.getFloatGreen(),
                                                               palette.gridPlayhead.getFloatBlue());

                        if (textShaderUniforms->modelMatrix != nullptr)
                            textShaderUniforms->modelMatrix->setMatrix4(modelMatrix.mat, 1, GL_FALSE);

                        openGLContext.extensions.glBindBuffer(GL_ARRAY_BUFFER, mesh.vbo);
                        openGLContext.extensions.glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh.ibo);

                        if (textShaderAttributes->position != nullptr)
                        {
                            openGLContext.extensions.glVertexAttribPointer(textShaderAttributes->position->attributeID,
                                                                           3,
                                                                           GL_FLOAT,
                                                                           GL_FALSE,
                                                                           sizeof(Segment14Geometry::Vertex),
                                                                           reinterpret_cast<GLvoid*>(offsetof(Segment14Geometry::Vertex, x)));
                        }

                        glDrawElements(GL_TRIANGLES, mesh.indexCount, GL_UNSIGNED_INT, nullptr);
                    }
                }
            }
        }

        const double bpmValue = audioProcessor.getBPM();
        const int bpmInt = static_cast<int>(std::lround(bpmValue));
        if (bpmInt != lastHudBpm)
        {
            // hudBpmText = "@BPM " + std::to_string(bpmInt);
            hudBpmText = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";

            lastHudBpm = bpmInt;
            ensureTextMesh(hudBpmText);
        }

        if (!hudBpmText.empty())
        {
            auto meshIt = textMeshCache.find(hudBpmText);
            if (meshIt != textMeshCache.end() && meshIt->second.indexCount > 0)
            {
                const float nearPlane = 6.0f;
                const float frustumHeight = 3.0f;
                const float frustumWidth = frustumHeight * aspectRatio;
                const float hudDistance = 8.0f;
                const float hudHalfWidth = frustumWidth * (hudDistance / nearPlane);
                const float hudHalfHeight = frustumHeight * (hudDistance / nearPlane);
                const float padding = hudHalfHeight * 0.12f;
                const float targetHeight = hudHalfHeight * 0.18f;
                const float targetWidth = hudHalfWidth * 0.9f;

                const float textWidth = textGeomParams.advance * static_cast<float>(hudBpmText.size());
                const float widthScale = (textWidth > 0.0f) ? (targetWidth / textWidth) : 1.0f;
                const float heightScale = targetHeight / textGeomParams.cellH;
                const float scale = std::min(widthScale, heightScale);
                const float textHeightScaled = textGeomParams.cellH * scale;

                const float cameraDistance = 20.0f / zoomLevel;
                const float viewX = -hudHalfWidth + padding;
                const float viewY = hudHalfHeight - padding - textHeightScaled;
                const float worldX = -panOffsetX + viewX;
                const float worldY = -panOffsetY + viewY;
                const float worldZ = cameraDistance - hudDistance;

                const auto position = juce::Vector3D<float>(worldX, worldY, worldZ);
                const auto scaleVec = juce::Vector3D<float>(scale, scale, 1.0f);
                const auto modelMatrix = getModelMatrix(position, scaleVec);

                const auto& mesh = meshIt->second;
                if (textShaderUniforms->textColor != nullptr)
                    textShaderUniforms->textColor->set(palette.textPrimary.getFloatRed(),
                                                       palette.textPrimary.getFloatGreen(),
                                                       palette.textPrimary.getFloatBlue(),
                                                       1.0f);

                if (textShaderUniforms->glowStrength != nullptr)
                    textShaderUniforms->glowStrength->set(0.35f);

                if (textShaderUniforms->glowColor != nullptr)
                    textShaderUniforms->glowColor->set(palette.gridPlayhead.getFloatRed(),
                                                       palette.gridPlayhead.getFloatGreen(),
                                                       palette.gridPlayhead.getFloatBlue());

                if (textShaderUniforms->modelMatrix != nullptr)
                    textShaderUniforms->modelMatrix->setMatrix4(modelMatrix.mat, 1, GL_FALSE);

                glDisable(GL_DEPTH_TEST);
                openGLContext.extensions.glBindBuffer(GL_ARRAY_BUFFER, mesh.vbo);
                openGLContext.extensions.glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh.ibo);

                if (textShaderAttributes->position != nullptr)
                {
                    openGLContext.extensions.glVertexAttribPointer(textShaderAttributes->position->attributeID,
                                                                   3,
                                                                   GL_FLOAT,
                                                                   GL_FALSE,
                                                                   sizeof(Segment14Geometry::Vertex),
                                                                   reinterpret_cast<GLvoid*>(offsetof(Segment14Geometry::Vertex, x)));
                }

                glDrawElements(GL_TRIANGLES, mesh.indexCount, GL_UNSIGNED_INT, nullptr);
                glEnable(GL_DEPTH_TEST);
            }
        }

        if (textShaderAttributes->position != nullptr)
            openGLContext.extensions.glDisableVertexAttribArray(textShaderAttributes->position->attributeID);

        glDepthMask(GL_TRUE);
        openGLContext.extensions.glBindBuffer(GL_ARRAY_BUFFER, 0);
        openGLContext.extensions.glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    }

    if (shaderAttributes->position != nullptr)
        openGLContext.extensions.glDisableVertexAttribArray(shaderAttributes->position->attributeID);

    if (shaderAttributes->normal != nullptr)
        openGLContext.extensions.glDisableVertexAttribArray(shaderAttributes->normal->attributeID);

    openGLContext.extensions.glUseProgram(0);
    glDisable(GL_SCISSOR_TEST);

    waitingForPaint = false;
}

void PluginEditor::openGLContextClosing()
{
    shaderAttributes.reset();
    shaderUniforms.reset();
    shaderProgram.reset();
    textShaderAttributes.reset();
    textShaderUniforms.reset();
    textShaderProgram.reset();
    clearTextMeshCache();

    if (vertexBuffer != 0)
    {
        openGLContext.extensions.glDeleteBuffers(1, &vertexBuffer);
        vertexBuffer = 0;
    }

    if (indexBuffer != 0)
    {
        openGLContext.extensions.glDeleteBuffers(1, &indexBuffer);
        indexBuffer = 0;
    }

    if (edgeIndexBuffer != 0)
    {
        openGLContext.extensions.glDeleteBuffers(1, &edgeIndexBuffer);
        edgeIndexBuffer = 0;
    }

    if (frontEdgeIndexBuffer != 0)
    {
        openGLContext.extensions.glDeleteBuffers(1, &frontEdgeIndexBuffer);
        frontEdgeIndexBuffer = 0;
    }
}

//==============================================================================
void PluginEditor::paint (juce::Graphics& g)
{
    waitingForPaint = false; 
}

void PluginEditor::resized()
{
    // This is generally where you'll want topip install tf-keras lay out the positions of any
    // subcomponents in your editor..
    controlPanelTable.setBounds(0, 0, 0, 0);
    seqViewBounds = getLocalBounds();
   
}


void PluginEditor::timerCallback ()
{
    ++framesDrawn;

    if (waitingForPaint) {return;}// already waiting for a repaint
  prepareControlPanelView();  
  // check what to draw based on the state of the 
  // editor
  switch(seqEditor->getEditMode()){

      case SequencerEditorMode::selectingSeqAndStep:
      {
          prepareSequenceView();
          break;
      }
      case SequencerEditorMode::editingStep:
      {
          prepareStepView();
          break; 
      }
      case SequencerEditorMode::configuringSequence:
      {
          prepareSeqConfigView();
          break;
      }
  }
  waitingForPaint = true; 
  if (updateSeqStrOnNextDraw || framesDrawn % 60 == 0){
    sequencer->updateSeqStringGrid();
    updateSeqStrOnNextDraw = false; 
  }
  openGLContext.triggerRepaint();
}



void PluginEditor::prepareSequenceView()
{
  // sequencer->tick();
  std::vector<std::pair<int, int>> playHeads;
  for (size_t col=0;col<audioProcessor.getSequencer()->howManySequences(); ++col){  
    std::pair<int, int> colRow = {col, audioProcessor.getSequencer()->getCurrentStep(col)};
    playHeads.push_back(std::move(colRow));
  }
  updateCellStates(audioProcessor.getSequencer()->getSequenceAsGridOfStrings(),
                   rowsInUI - 1,
                   6,
                   seqEditor->getCurrentSequence(),
                   seqEditor->getCurrentStep(),
                   playHeads,
                   true,
                   seqEditor->getArmedSequence());
}
void PluginEditor::prepareStepView()
{
    // Step* step = sequencer->getStep(seqEditor->getCurrentSequence(), seqEditor->getCurrentStep());
    // std::vector<std::vector<std::string>> grid = step->toStringGrid();
    std::vector<std::pair<int, int>> playHeads;
    if (sequencer->getCurrentStep(seqEditor->getCurrentSequence()) == seqEditor->getCurrentStep()){
        int cols = static_cast<int>(sequencer->howManyStepDataCols(seqEditor->getCurrentSequence(), seqEditor->getCurrentStep()));
        for (int col=0;col<cols;++col){
            playHeads.push_back(std::pair(col, 0));
        }
    }
    std::vector<std::vector<std::string>> grid = sequencer->getStepAsGridOfStrings(seqEditor->getCurrentSequence(), seqEditor->getCurrentStep());
    updateCellStates(grid,
                     rowsInUI - 1,
                     6,
                     seqEditor->getCurrentStepCol(),
                     seqEditor->getCurrentStepRow(),
                     playHeads,
                     true,
                     Sequencer::notArmed);
}
void PluginEditor::prepareSeqConfigView()
{
    std::vector<std::vector<std::string>> grid = sequencer->getSequenceConfigsAsGridOfStrings();
    updateCellStates(grid,
                     rowsInUI - 1,
                     6,
                     seqEditor->getCurrentSequence(),
                     seqEditor->getCurrentSeqParam(),
                     std::vector<std::pair<int, int>>(),
                     true,
                     Sequencer::notArmed);
}

void PluginEditor::prepareControlPanelView()
{
    std::vector<std::vector<std::string>> grid = trackerController->getControlPanelAsGridOfStrings();
    controlPanelTable.updateData(grid, 
        1, 12, 
        0, 0, // todo - pull these from the editor which keeps track of this 
        std::vector<std::pair<int, int>>(), false); 
}

void PluginEditor::updateCellStates(const std::vector<std::vector<std::string>>& data,
                                    size_t rowsToDisplay,
                                    size_t colsToDisplay,
                                    size_t cursorCol,
                                    size_t cursorRow,
                                    const std::vector<std::pair<int, int>>& highlightCells,
                                    bool showCursor,
                                    size_t armedSeq)
{
    if (rowsToDisplay == 0 || colsToDisplay == 0)
        return;

    const size_t maxCols = data.size();
    const size_t maxRows = (maxCols > 0) ? data[0].size() : 0;
    if (maxCols == 0 || maxRows == 0)
    {
        std::lock_guard<std::mutex> lock(cellStateMutex);
        cellStates.assign(colsToDisplay, std::vector<CellVisualState>(rowsToDisplay, CellVisualState{}));
        visibleText.assign(colsToDisplay, std::vector<std::string>(rowsToDisplay, ""));
        playheadGlow.assign(colsToDisplay, std::vector<float>(rowsToDisplay, 0.0f));
        visibleCols = colsToDisplay;
        visibleRows = rowsToDisplay;
        startCol = 0;
        startRow = 0;
        lastStartCol = 0;
        lastStartRow = 0;
        textGeometryDirty = true;
        return;
    }

    // Clamp the cursor to the data bounds so view switches don't leave us with an invalid window.
    if (cursorCol >= maxCols) cursorCol = maxCols - 1;
    if (cursorRow >= maxRows) cursorRow = maxRows - 1;

    size_t nextStartCol = lastStartCol;
    size_t nextStartRow = lastStartRow;
    size_t nextEndCol = nextStartCol + colsToDisplay;
    size_t nextEndRow = nextStartRow + rowsToDisplay;

    if (cursorCol < nextStartCol) nextStartCol = cursorCol;
    if (cursorCol >= nextEndCol) nextStartCol = cursorCol - colsToDisplay + 1;
    if (cursorRow < nextStartRow) nextStartRow = cursorRow;
    if (cursorRow >= nextEndRow) nextStartRow = cursorRow - rowsToDisplay + 1;

    nextEndCol = nextStartCol + colsToDisplay;
    nextEndRow = nextStartRow + rowsToDisplay;

    if (nextEndRow >= maxRows) nextEndRow = maxRows;
    if (nextEndCol >= maxCols) nextEndCol = maxCols;

    lastStartCol = nextStartCol;
    lastStartRow = nextStartRow;

    std::vector<std::vector<float>> oldGlow;
    size_t oldStartCol = 0;
    size_t oldStartRow = 0;
    {
        std::lock_guard<std::mutex> lock(cellStateMutex);
        oldGlow = playheadGlow;
        oldStartCol = startCol;
        oldStartRow = startRow;
    }

    const bool reuseGlow = (oldStartCol == nextStartCol && oldStartRow == nextStartRow);
    // const float glowDecayStep = 0.1f;
    // const float glowDecayScalar = 0.8f;
    

    std::vector<std::vector<CellVisualState>> nextStates(colsToDisplay,
                                                         std::vector<CellVisualState>(rowsToDisplay));
    std::vector<std::vector<std::string>> nextText(colsToDisplay,
                                                   std::vector<std::string>(rowsToDisplay, ""));
    std::vector<std::vector<float>> nextGlow(colsToDisplay,
                                             std::vector<float>(rowsToDisplay, 0.0f));

    for (size_t displayCol = 0; displayCol < colsToDisplay; ++displayCol)
    {
        const size_t col = nextStartCol + displayCol;
        const bool colInRange = col < maxCols;

        for (size_t displayRow = 0; displayRow < rowsToDisplay; ++displayRow)
        {
            const size_t row = nextStartRow + displayRow;
            const bool rowInRange = row < maxRows;

            const std::string cellValue = (colInRange && rowInRange) ? data[col][row] : "";
            const bool hasNote = !cellValue.empty() && cellValue != "----" && cellValue != "-";

            bool isHighlighted = false;
            if (colInRange && rowInRange)
            {
                for (const auto& cell : highlightCells)
                {
                    if (static_cast<size_t>(cell.first) == col && static_cast<size_t>(cell.second) == row)
                    {
                        isHighlighted = true;
                        break;
                    }
                }
            }

            const bool isSelected = showCursor && col == cursorCol && row == cursorRow;
            const bool isArmed = (armedSeq != Sequencer::notArmed) && col == armedSeq;

            float previousGlow = 0.0f;
            if (reuseGlow && displayCol < oldGlow.size())
            {
                const auto& oldColumn = oldGlow[displayCol];
                if (displayRow < oldColumn.size())
                    previousGlow = oldColumn[displayRow];
            }
            // decay by a constant 
            // const float glowValue = isHighlighted ? 1.0f : std::max(0.0f, previousGlow - glowDecayStep);
            // decay using a scalar 
            const float glowValue = isHighlighted ? 1.0f : std::max(0.0f, previousGlow * glowDecayScalar);

            nextStates[displayCol][displayRow] = CellVisualState{hasNote, isHighlighted, isSelected, isArmed, glowValue};
            nextText[displayCol][displayRow] = cellValue;
            nextGlow[displayCol][displayRow] = glowValue;
        }
    }

    {
        std::lock_guard<std::mutex> lock(cellStateMutex);
        cellStates = std::move(nextStates);
        visibleText = std::move(nextText);
        playheadGlow = std::move(nextGlow);
        visibleCols = colsToDisplay;
        visibleRows = rowsToDisplay;
        startCol = nextStartCol;
        startRow = nextStartRow;
        textGeometryDirty = true;
    }
}

juce::Matrix3D<float> PluginEditor::getProjectionMatrix(float aspectRatio) const
{
    const float nearPlane = 6.0f;
    const float farPlane = 100.0f;
    const float frustumHeight = 3.0f;
    const float frustumWidth = frustumHeight * aspectRatio;

    return juce::Matrix3D<float>::fromFrustum(-frustumWidth, frustumWidth,
                                              -frustumHeight, frustumHeight,
                                              nearPlane, farPlane);
}

juce::Matrix3D<float> PluginEditor::getViewMatrix() const
{
    const float baseDistance = 20.0f;
    const float cameraDistance = baseDistance / zoomLevel;
    return juce::Matrix3D<float>::fromTranslation({panOffsetX, panOffsetY, -cameraDistance});
}

juce::Matrix3D<float> PluginEditor::getModelMatrix(juce::Vector3D<float> position, juce::Vector3D<float> scale) const
{
    return juce::Matrix3D<float>::fromTranslation(position) * makeScaleMatrix(scale);
}

/** select colour based on cell state  */
juce::Colour PluginEditor::getCellColour(const CellVisualState& cell) const
{
    if (cell.isSelected && cell.hasNote)
        return juce::Colours::red.withAlpha(0.6f);
    if (cell.isSelected)
        return palette.gridSelected;
    if (cell.isArmed)
        return palette.statusOk;

    return palette.gridEmpty;
}

juce::Colour PluginEditor::getTextColour(const CellVisualState& cell) const
{
    if (cell.isSelected)
        return palette.gridSelected;
    if (cell.isArmed)
        return palette.statusOk;
    if (cell.hasNote)
        return palette.gridNote;
    return palette.textPrimary;
}

float PluginEditor::getCellDepthScale(const CellVisualState& cell) const
{
    float scale = 1.0f;
    if (cell.hasNote) scale = 1.3f;
    if (cell.isActivePlayhead) scale = 1.8f;
    if (cell.isSelected) scale = 1.6f;
    if (cell.isArmed) scale = 1.2f;
    return scale;
}

void PluginEditor::adjustZoom(float delta)
{
    const float minZoom = 0.5f;
    const float maxZoom = 2.5f;
    zoomLevel = juce::jlimit(minZoom, maxZoom, zoomLevel + delta);
}

void PluginEditor::mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel)
{
    if (!seqViewBounds.contains(event.getPosition()))
        return;

    const float zoomDelta = wheel.deltaY * 0.4f;
    if (std::abs(zoomDelta) > 0.0001f)
        adjustZoom(zoomDelta);
}

void PluginEditor::moveUp(float amount)
{
    panOffsetY += amount;
}

void PluginEditor::moveDown(float amount)
{
    panOffsetY -= amount;
}

void PluginEditor::moveLeft(float amount)
{
    panOffsetX += amount;
}

void PluginEditor::moveRight(float amount)
{
    panOffsetX -= amount;
}

void PluginEditor::mouseDown(const juce::MouseEvent& event)
{
    if (!seqViewBounds.contains(event.getPosition()))
        return;

    lastDragPosition = event.getPosition();
}

void PluginEditor::mouseDrag(const juce::MouseEvent& event)
{
    if (!seqViewBounds.contains(event.getPosition()))
        return;

    const auto currentPos = event.getPosition();
    const auto delta = currentPos - lastDragPosition;
    lastDragPosition = currentPos;

    const float panScale = 0.02f / zoomLevel;
    panOffsetX += static_cast<float>(delta.x) * panScale;
    panOffsetY -= static_cast<float>(delta.y) * panScale;
}

void PluginEditor::updateTextGeometryCache()
{
    std::vector<std::vector<std::string>> textCopy;
    {
        std::lock_guard<std::mutex> lock(cellStateMutex);
        if (!textGeometryDirty || visibleText.empty())
            return;
        textCopy = visibleText;
        textGeometryDirty = false;
    }

    std::unordered_set<std::string> uniqueStrings;
    for (const auto& column : textCopy)
        for (const auto& text : column)
            if (!text.empty())
                uniqueStrings.insert(text);

    for (const auto& text : uniqueStrings)
        ensureTextMesh(text);
}

PluginEditor::TextMesh& PluginEditor::ensureTextMesh(const std::string& text)
{
    auto it = textMeshCache.find(text);
    if (it != textMeshCache.end())
        return it->second;

    TextMesh meshInfo{};
    const auto mesh = textGeometry.buildStringMesh(text);
    meshInfo.indexCount = static_cast<GLsizei>(mesh.indices.size());

    if (meshInfo.indexCount > 0)
    {
        openGLContext.extensions.glGenBuffers(1, &meshInfo.vbo);
        openGLContext.extensions.glBindBuffer(GL_ARRAY_BUFFER, meshInfo.vbo);
        openGLContext.extensions.glBufferData(GL_ARRAY_BUFFER,
                                              static_cast<GLsizeiptr>(mesh.vertices.size() * sizeof(Segment14Geometry::Vertex)),
                                              mesh.vertices.data(),
                                              GL_STATIC_DRAW);

        openGLContext.extensions.glGenBuffers(1, &meshInfo.ibo);
        openGLContext.extensions.glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, meshInfo.ibo);
        openGLContext.extensions.glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                                              static_cast<GLsizeiptr>(mesh.indices.size() * sizeof(uint32_t)),
                                              mesh.indices.data(),
                                              GL_STATIC_DRAW);
    }

    auto [inserted, _] = textMeshCache.emplace(text, meshInfo);
    return inserted->second;
}

void PluginEditor::clearTextMeshCache()
{
    for (auto& entry : textMeshCache)
    {
        if (entry.second.vbo != 0)
            openGLContext.extensions.glDeleteBuffers(1, &entry.second.vbo);
        if (entry.second.ibo != 0)
            openGLContext.extensions.glDeleteBuffers(1, &entry.second.ibo);
    }
    textMeshCache.clear();
}

bool PluginEditor::keyPressed(const juce::KeyPress& key, juce::Component* originatingComponent)
{

    switch (key.getTextCharacter())
    {
        case 'A':// arm a sequence for live MIDI input 
            seqEditor->setArmedSequence(seqEditor->getCurrentSequence());
            break; 
        case 'R':// re-e-wind
            CommandProcessor::sendAllNotesOff();
            audioProcessor.getSequencer()->rewindAtNextZero();
            break;
        case ' ':
            CommandProcessor::sendAllNotesOff();
            if (audioProcessor.getSequencer()->isPlaying()){
                audioProcessor.getSequencer()->stop();
            }
            else{
                audioProcessor.getSequencer()->rewindAtNextZero();
                audioProcessor.getSequencer()->play();

            }
            break;
        case '\t':
            seqEditor->nextStep();
            break;
        case '-':
           seqEditor->removeRow();
            break;

        case '=':
           seqEditor->addRow();
            break;

        case '_':
            trackerController->decrementBPM();
            break;

        case '+':
            trackerController->incrementBPM();
            break;

        case '[':
           seqEditor->decrementAtCursor();
            break;

        case ']':
           seqEditor->incrementAtCursor();
            break;

        case ',':
           seqEditor->decrementOctave();
           
           break;

        case '.':
           seqEditor->incrementOctave();
            break;

        case 'M':
            audioProcessor.getSequencer()->toggleSequenceMute(seqEditor->getCurrentSequence());
            // sequencer.toggleSequenceMute(seqEditor->getCurrentSequence());
            break;

        // case juce::KeyPress::deleteKey:
        //    seqEditor->resetAtCursor();
        //     CommandProcessor::sendAllNotesOff();
        //     break;

        case '\n':
           seqEditor->enterAtCursor();
            break;

        case 'S':
           seqEditor->gotoSequenceConfigPage();
            break;


        case 'p':
            // if (seqEditor->getEditMode() == SequencerEditor::EditingStep) {
            //     // sequencer.triggerStep(seqEditor->getCurrentSequence(),seqEditor->getCurrentStep(),seqEditor->getCurrentStepRow());
            // }
            break;

        default:
            char ch = static_cast<char>(key.getTextCharacter()); // sketch as converting wchar unicode to char... 
            std::map<char, double> key_to_note = MidiUtilsAbs::getKeyboardToMidiNotes(0);
            for (const std::pair<const char, double>& key_note : key_to_note)
            {
              if (ch == key_note.first){ 
                // key_note_match = true;
                seqEditor->enterStepData(key_note.second, Step::noteInd);
                break;// break the for loop
                
              }
            }
            // do the velocity controls
            for (int num=1;num<5;++num){
              if (ch == num + 48){
                seqEditor->enterStepData(num * (128/4), Step::velInd);
                break; 
              }
            }
            // for (int i=0;i<lenKeys.size();++i){
            //   if (lenKeys[i] == ch){
            //     editor.enterStepData(i+1, Step::lengthInd);
            //     break;
            //   }
            // }
            // Handle arrow and other non character keys
            if (key.isKeyCode(juce::KeyPress::backspaceKey)) {
                seqEditor->resetAtCursor();
                CommandProcessor::sendAllNotesOff();
                break;
            }
            if (key.isKeyCode(juce::KeyPress::returnKey)) {
               seqEditor->enterAtCursor();
                break;
            }
            if (key.isKeyCode(juce::KeyPress::upKey)) {
               seqEditor->moveCursorUp();
                break;
            }
            if (key.isKeyCode(juce::KeyPress::downKey)) {

               seqEditor->moveCursorDown();
                break;
            }
            if (key.isKeyCode(juce::KeyPress::leftKey)) {
               seqEditor->moveCursorLeft();
                break;
            }
            if (key.isKeyCode(juce::KeyPress::rightKey)) {
               seqEditor->moveCursorRight();
                break;
            }

    }
    audioProcessor.getSequencer()->updateSeqStringGrid();
    return true; // Key was handled
}

bool PluginEditor::keyStateChanged(bool isKeyDown, juce::Component* originatingComponent)
{
  return false; 
}
