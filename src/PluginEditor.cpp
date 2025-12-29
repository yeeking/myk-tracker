/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin editor.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "SequencerCommands.h"
#include "SimpleClock.h"
#include <cmath>

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
    attribute vec2 texCoord;
    uniform mat4 projectionMatrix;
    uniform mat4 viewMatrix;
    uniform mat4 modelMatrix;
    uniform vec4 uvRect;
    varying vec2 vTexCoord;

    void main()
    {
        vTexCoord = texCoord * uvRect.zw + uvRect.xy;
        gl_Position = projectionMatrix * viewMatrix * modelMatrix * vec4(position, 1.0);
    }
)";

const char* textFragmentShaderSource = R"(
    varying vec2 vTexCoord;
    uniform sampler2D textTexture;

    void main()
    {
        vec4 sampled = texture2D(textTexture, vTexCoord);
        gl_FragColor = sampled;
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

const float quadVertices[] =
{
    -0.5f, -0.5f, 0.0f,  0.0f, 0.0f,
     0.5f, -0.5f, 0.0f,  1.0f, 0.0f,
     0.5f,  0.5f, 0.0f,  1.0f, 1.0f,
    -0.5f,  0.5f, 0.0f,  0.0f, 1.0f
};
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
    audioProcessor (p), 
    sequencer{p.getSequencer()},  
    seqEditor{p.getSequenceEditor()}, 
    trackerController{p.getTrackerController()}, 
    rowsInUI{9}, waitingForPaint{false}, updateSeqStrOnNextDraw{false}, framesDrawn{0}
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
        textShaderAttributes->texCoord = std::make_unique<juce::OpenGLShaderProgram::Attribute>(*textShaderProgram, "texCoord");

        textShaderUniforms = std::make_unique<TextShaderUniforms>();
        textShaderUniforms->projectionMatrix = std::make_unique<juce::OpenGLShaderProgram::Uniform>(*textShaderProgram, "projectionMatrix");
        textShaderUniforms->viewMatrix = std::make_unique<juce::OpenGLShaderProgram::Uniform>(*textShaderProgram, "viewMatrix");
        textShaderUniforms->modelMatrix = std::make_unique<juce::OpenGLShaderProgram::Uniform>(*textShaderProgram, "modelMatrix");
        textShaderUniforms->uvRect = std::make_unique<juce::OpenGLShaderProgram::Uniform>(*textShaderProgram, "uvRect");
        textShaderUniforms->textTexture = std::make_unique<juce::OpenGLShaderProgram::Uniform>(*textShaderProgram, "textTexture");
    }
    else
    {
        jassertfalse;
        textShaderProgram.reset();
        return;
    }

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

    openGLContext.extensions.glGenBuffers(1, &textVertexBuffer);
    openGLContext.extensions.glBindBuffer(GL_ARRAY_BUFFER, textVertexBuffer);
    openGLContext.extensions.glBufferData(GL_ARRAY_BUFFER,
                                          sizeof(quadVertices),
                                          quadVertices,
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
            // Grid layout (same geometry spacing as the text overlay pass below).
            // const float cellWidth = 1.0f;
            // const float cellWidth = 2.0f;
            
            const float cellHeight = 1.0f;
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
                    // Cells with notes draw as a wireframe cube to show "note present".
                    const bool wireframe = cell.hasNote;

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

                    // Wireframe vs solid cube depending on note presence.
                    if (wireframe)
                    {
                        glDisable(GL_CULL_FACE);
                        glLineWidth(1.5f);
                        openGLContext.extensions.glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, edgeIndexBuffer);
                        glDrawElements(GL_LINES, cubeEdgeIndexCount, GL_UNSIGNED_INT, nullptr);
                        openGLContext.extensions.glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, indexBuffer);
                        glEnable(GL_CULL_FACE);
                    }
                    else
                    {
                        glDrawElements(GL_TRIANGLES, cubeIndexCount, GL_UNSIGNED_INT, nullptr);
                    }
                }
            }
        }
    }

    // Upload the text atlas image when it changes; atlas is generated in updateTextAtlasImage()
    // from visibleText (one string per cell) and the palette text colors.
    if (textShaderProgram != nullptr && textAtlasUploadPending && textAtlasImage.isValid())
    {
        textTexture.loadImage(textAtlasImage);
        textAtlasUploadPending = false;
    }

    // Text pass: draw one textured quad per cell using the atlas built from visibleText.
    if (textShaderProgram != nullptr && textTexture.getTextureID() != 0)
    {
        openGLContext.extensions.glUseProgram(textShaderProgram->getProgramID());

        if (textShaderUniforms->projectionMatrix != nullptr)
            textShaderUniforms->projectionMatrix->setMatrix4(projectionMatrix.mat, 1, GL_FALSE);

        if (textShaderUniforms->viewMatrix != nullptr)
            textShaderUniforms->viewMatrix->setMatrix4(viewMatrix.mat, 1, GL_FALSE);

        glActiveTexture(GL_TEXTURE0);
        textTexture.bind();
        if (textShaderUniforms->textTexture != nullptr)
            textShaderUniforms->textTexture->set(0);

        openGLContext.extensions.glBindBuffer(GL_ARRAY_BUFFER, textVertexBuffer);

        const GLsizei textStride = sizeof(float) * 5;
        if (textShaderAttributes->position != nullptr)
        {
            openGLContext.extensions.glVertexAttribPointer(textShaderAttributes->position->attributeID,
                                                           3,
                                                           GL_FLOAT,
                                                           GL_FALSE,
                                                           textStride,
                                                           reinterpret_cast<GLvoid*>(0));
            openGLContext.extensions.glEnableVertexAttribArray(textShaderAttributes->position->attributeID);
        }

        if (textShaderAttributes->texCoord != nullptr)
        {
            openGLContext.extensions.glVertexAttribPointer(textShaderAttributes->texCoord->attributeID,
                                                           2,
                                                           GL_FLOAT,
                                                           GL_FALSE,
                                                           textStride,
                                                           reinterpret_cast<GLvoid*>(sizeof(float) * 3));
            openGLContext.extensions.glEnableVertexAttribArray(textShaderAttributes->texCoord->attributeID);
        }

        {
            std::lock_guard<std::mutex> lock(cellStateMutex);
            if (visibleCols > 0 && visibleRows > 0 && !cellStates.empty()
                && textAtlasWidth > 0 && textAtlasHeight > 0
                && cellPixelWidth > 0 && cellPixelHeight > 0)
            {
                // Match the same grid layout as the 3D cells so text sits centered on each cube.
                // const float cellWidth = 1.0f;
                const float cellHeight = 1.0f;
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
                        // Per-cell UVs into the atlas image (each cell owns a fixed rect).
                        const float u0 = (static_cast<float>(col * cellPixelWidth)) / static_cast<float>(textAtlasWidth);
                        const float u1 = (static_cast<float>((col + 1) * cellPixelWidth)) / static_cast<float>(textAtlasWidth);
                        const float v0 = 1.0f - (static_cast<float>((row + 1) * cellPixelHeight)) / static_cast<float>(textAtlasHeight);
                        const float v1 = 1.0f - (static_cast<float>(row * cellPixelHeight)) / static_cast<float>(textAtlasHeight);

                        if (textShaderUniforms->uvRect != nullptr)
                            textShaderUniforms->uvRect->set(u0, v0, u1 - u0, v1 - v0);

                        const auto position = juce::Vector3D<float>(startX + static_cast<float>(col) * stepX,
                                                                    startY - static_cast<float>(row) * stepY,
                                                                    0.5f);
                        const auto scale = juce::Vector3D<float>(cellWidth * 0.9f, cellHeight * 0.9f, 1.0f);
                        const auto modelMatrix = getModelMatrix(position, scale);

                        if (textShaderUniforms->modelMatrix != nullptr)
                            textShaderUniforms->modelMatrix->setMatrix4(modelMatrix.mat, 1, GL_FALSE);

                        glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
                    }
                }
            }
        }

        if (textShaderAttributes->position != nullptr)
            openGLContext.extensions.glDisableVertexAttribArray(textShaderAttributes->position->attributeID);

        if (textShaderAttributes->texCoord != nullptr)
            openGLContext.extensions.glDisableVertexAttribArray(textShaderAttributes->texCoord->attributeID);

        textTexture.release();
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
    textTexture.release();

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

    if (textVertexBuffer != 0)
    {
        openGLContext.extensions.glDeleteBuffers(1, &textVertexBuffer);
        textVertexBuffer = 0;
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
    int cPanelHeight = getHeight() / static_cast<int>(rowsInUI);
    controlPanelTable.setBounds(0, 0, getWidth(), cPanelHeight);
    seqViewBounds = juce::Rectangle<int>(0, cPanelHeight, getWidth(), getHeight() - cPanelHeight);
   
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
  updateTextAtlasImage();
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
    if (data.empty() || data[0].empty())
        return;

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

    if (nextEndRow >= data[0].size()) nextEndRow = data[0].size();
    if (nextEndCol >= data.size()) nextEndCol = data.size();

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
    

    std::vector<std::vector<CellVisualState>> nextStates;
    std::vector<std::vector<std::string>> nextText;
    std::vector<std::vector<float>> nextGlow;
    nextStates.reserve(nextEndCol - nextStartCol);
    nextText.reserve(nextEndCol - nextStartCol);
    nextGlow.reserve(nextEndCol - nextStartCol);

    for (size_t col = nextStartCol; col < nextEndCol; ++col)
    {
        std::vector<CellVisualState> columnStates;
        std::vector<std::string> columnText;
        std::vector<float> columnGlow;
        columnStates.reserve(nextEndRow - nextStartRow);
        columnText.reserve(nextEndRow - nextStartRow);
        columnGlow.reserve(nextEndRow - nextStartRow);

        for (size_t row = nextStartRow; row < nextEndRow; ++row)
        {
            const auto& cellValue = data[col][row];
            const bool hasNote = !cellValue.empty() && cellValue != "----" && cellValue != "-";

            bool isHighlighted = false;
            for (const auto& cell : highlightCells)
            {
                if (static_cast<size_t>(cell.first) == col && static_cast<size_t>(cell.second) == row)
                {
                    isHighlighted = true;
                    break;
                }
            }

            const bool isSelected = showCursor && col == cursorCol && row == cursorRow;
            const bool isArmed = (armedSeq != Sequencer::notArmed) && col == armedSeq;

            float previousGlow = 0.0f;
            if (reuseGlow && (col - nextStartCol) < oldGlow.size())
            {
                const auto& oldColumn = oldGlow[col - nextStartCol];
                if ((row - nextStartRow) < oldColumn.size())
                    previousGlow = oldColumn[row - nextStartRow];
            }
            // decay by a constant 
            // const float glowValue = isHighlighted ? 1.0f : std::max(0.0f, previousGlow - glowDecayStep);
            // decay using a scalar 
            const float glowValue = isHighlighted ? 1.0f : std::max(0.0f, previousGlow * glowDecayScalar);
            
            columnStates.push_back(CellVisualState{hasNote, isHighlighted, isSelected, isArmed, glowValue});
            columnText.push_back(cellValue);
            columnGlow.push_back(glowValue);
        }

        nextStates.push_back(std::move(columnStates));
        nextText.push_back(std::move(columnText));
        nextGlow.push_back(std::move(columnGlow));
    }

    {
        std::lock_guard<std::mutex> lock(cellStateMutex);
        cellStates = std::move(nextStates);
        visibleText = std::move(nextText);
        playheadGlow = std::move(nextGlow);
        visibleCols = nextEndCol - nextStartCol;
        visibleRows = nextEndRow - nextStartRow;
        startCol = nextStartCol;
        startRow = nextStartRow;
        textAtlasDirty = true;
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
        return juce::Colours::red.withAlpha(0.4f);
    if (cell.isSelected)
        return palette.gridSelected;
    if (cell.isArmed)
        return palette.statusOk;
    if (cell.hasNote)
        return palette.gridNote;

    return palette.gridEmpty;
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

/** dynamically generate a bitmap of the text for the sequencer */
void PluginEditor::updateTextAtlasImage()
{
    std::vector<std::vector<std::string>> textCopy;
    size_t cols = 0;
    size_t rows = 0;
    juce::Rectangle<int> boundsCopy;

    {
        std::lock_guard<std::mutex> lock(cellStateMutex);
        if (!textAtlasDirty || visibleText.empty())
            return;
        textCopy = visibleText;
        cols = visibleCols;
        rows = visibleRows;
        boundsCopy = seqViewBounds;
    }

    if (boundsCopy.isEmpty() || cols == 0 || rows == 0)
        return;

    const int nextCellWidth = std::max(1, boundsCopy.getWidth() / static_cast<int>(cols));
    const int nextCellHeight = std::max(1, boundsCopy.getHeight() / static_cast<int>(rows));
    const int nextAtlasWidth = nextCellWidth * static_cast<int>(cols);
    const int nextAtlasHeight = nextCellHeight * static_cast<int>(rows);

    juce::Image nextImage(juce::Image::ARGB, nextAtlasWidth, nextAtlasHeight, true);
    juce::Graphics g(nextImage);
    g.fillAll(palette.textBackground);
    g.setColour(palette.textPrimary.withAlpha(0.9f));
    // g.setFont(juce::Font(static_cast<float>(nextCellHeight) * 0.35f, juce::Font::bold));
    g.setFont(juce::Font(static_cast<float>(nextCellHeight) * 0.75f, juce::Font::bold));
    

    for (size_t col = 0; col < cols; ++col)
    {
        for (size_t row = 0; row < rows; ++row)
        {
            const int x = static_cast<int>(col) * nextCellWidth;
            const int y = static_cast<int>(row) * nextCellHeight;
            const juce::Rectangle<int> cellBounds(x, y, nextCellWidth, nextCellHeight);
            
            g.drawText(textCopy[col][row], cellBounds, juce::Justification::centred, true);
        }
    }

    {
        std::lock_guard<std::mutex> lock(cellStateMutex);
        textAtlasImage = std::move(nextImage);
        textAtlasWidth = nextAtlasWidth;
        textAtlasHeight = nextAtlasHeight;
        cellPixelWidth = nextCellWidth;
        cellPixelHeight = nextCellHeight;
        textAtlasDirty = false;
        textAtlasUploadPending = true;
    }
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
