#include "TrackerUIComponent.h"
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

TrackerUIComponent::TrackerUIComponent(juce::OpenGLContext& context)
    : openGLContext(&context)
{
    textGeomParams.cellW = 1.0f;
    textGeomParams.cellH = 1.6f;
    textGeomParams.thickness = 0.14f;
    textGeomParams.inset = 0.12f;
    textGeomParams.gap = 0.06f;
    textGeomParams.advance = 1.12f;
    textGeomParams.includeDot = true;
    textGeometry.setParams(textGeomParams);
}

TrackerUIComponent::~TrackerUIComponent()
{
    shutdownOpenGL();
}

void TrackerUIComponent::initOpenGL(int width, int height)
{
    if (openGLContext == nullptr)
        return;

    viewport.bounds = juce::Rectangle<int>(0, 0, width, height);
    viewport.componentHeight = height;

    shaderProgram = std::make_unique<juce::OpenGLShaderProgram>(*openGLContext);
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

    textShaderProgram = std::make_unique<juce::OpenGLShaderProgram>(*openGLContext);
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

    openGLContext->extensions.glGenBuffers(1, &vertexBuffer);
    openGLContext->extensions.glBindBuffer(GL_ARRAY_BUFFER, vertexBuffer);
    openGLContext->extensions.glBufferData(GL_ARRAY_BUFFER,
                                          sizeof(cubeVertices),
                                          cubeVertices,
                                          GL_STATIC_DRAW);

    openGLContext->extensions.glGenBuffers(1, &indexBuffer);
    openGLContext->extensions.glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, indexBuffer);
    openGLContext->extensions.glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                                          sizeof(cubeIndices),
                                          cubeIndices,
                                          GL_STATIC_DRAW);

    openGLContext->extensions.glGenBuffers(1, &frontEdgeIndexBuffer);
    openGLContext->extensions.glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, frontEdgeIndexBuffer);
    openGLContext->extensions.glBufferData(GL_ELEMENT_ARRAY_BUFFER,
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

void TrackerUIComponent::shutdownOpenGL()
{
    if (openGLContext == nullptr)
        return;

    shaderAttributes.reset();
    shaderUniforms.reset();
    shaderProgram.reset();
    textShaderAttributes.reset();
    textShaderUniforms.reset();
    textShaderProgram.reset();
    clearTextMeshCache();

    if (vertexBuffer != 0)
    {
        openGLContext->extensions.glDeleteBuffers(1, &vertexBuffer);
        vertexBuffer = 0;
    }

    if (indexBuffer != 0)
    {
        openGLContext->extensions.glDeleteBuffers(1, &indexBuffer);
        indexBuffer = 0;
    }

    if (frontEdgeIndexBuffer != 0)
    {
        openGLContext->extensions.glDeleteBuffers(1, &frontEdgeIndexBuffer);
        frontEdgeIndexBuffer = 0;
    }
}

void TrackerUIComponent::setViewportBounds(const juce::Rectangle<int>& bounds,
                                           int componentHeight,
                                           float renderingScale)
{
    viewport.bounds = bounds;
    viewport.componentHeight = componentHeight;
    viewport.renderingScale = renderingScale;
}

void TrackerUIComponent::setStyle(const Style& newStyle)
{
    style = newStyle;
}

void TrackerUIComponent::setCellSize(float width, float height)
{
    cellWidth = width;
    cellHeight = height;
}

void TrackerUIComponent::updateUIState(const CellGrid& cells,
                                       const OverlayState& overlay,
                                       const ZoomState& zoom,
                                       const DragState& drag,
                                       const std::vector<float>* newColumnWidths)
{
    std::lock_guard<std::mutex> lock(stateMutex);
    overlayState = overlay;
    zoomState = zoom;
    dragState = drag;
    if (newColumnWidths != nullptr)
        columnWidths = *newColumnWidths;
    else
        columnWidths.clear();

    const size_t cols = cells.size();
    const size_t rows = (cols > 0) ? cells[0].size() : 0;
    cellStates.resize(cols);
    for (size_t col = 0; col < cols; ++col)
        cellStates[col].resize(rows);

    for (size_t col = 0; col < cols; ++col)
        for (size_t row = 0; row < rows; ++row)
            cellStates[col][row] = cells[col][row];

    textGeometryDirty = true;
}

void TrackerUIComponent::renderUI()
{
    if (openGLContext == nullptr || shaderProgram == nullptr)
        return;

    if (viewport.bounds.isEmpty())
        return;

    configureViewport();

    glClearColor(style.background.getFloatRed(),
                 style.background.getFloatGreen(),
                 style.background.getFloatBlue(),
                 style.background.getFloatAlpha());
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDepthMask(GL_TRUE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);

    const float aspectRatio = static_cast<float>(viewport.bounds.getWidth())
        / static_cast<float>(viewport.bounds.getHeight());
    const auto projectionMatrix = getProjectionMatrix(aspectRatio);
    const auto viewMatrix = getViewMatrix();
    const float glowPulse = getGlowPulse();

    openGLContext->extensions.glUseProgram(shaderProgram->getProgramID());
    renderGrid(projectionMatrix, viewMatrix, glowPulse);

    if (textShaderProgram != nullptr)
    {
        updateTextGeometryCache();
        openGLContext->extensions.glUseProgram(textShaderProgram->getProgramID());
        if (textShaderAttributes != nullptr && textShaderAttributes->position != nullptr)
            openGLContext->extensions.glEnableVertexAttribArray(textShaderAttributes->position->attributeID);
        renderCellText(projectionMatrix, viewMatrix, glowPulse);
        renderOverlayText(projectionMatrix, viewMatrix);
        if (textShaderAttributes != nullptr && textShaderAttributes->position != nullptr)
            openGLContext->extensions.glDisableVertexAttribArray(textShaderAttributes->position->attributeID);
    }

    if (shaderAttributes != nullptr && shaderAttributes->position != nullptr)
        openGLContext->extensions.glDisableVertexAttribArray(shaderAttributes->position->attributeID);
    if (shaderAttributes != nullptr && shaderAttributes->normal != nullptr)
        openGLContext->extensions.glDisableVertexAttribArray(shaderAttributes->normal->attributeID);

    openGLContext->extensions.glUseProgram(0);
    glDisable(GL_SCISSOR_TEST);
}

void TrackerUIComponent::configureViewport()
{
    const float scale = viewport.renderingScale > 0.0f ? viewport.renderingScale : 1.0f;
    const int viewportWidth = juce::roundToInt(viewport.bounds.getWidth() * scale);
    const int viewportHeight = juce::roundToInt(viewport.bounds.getHeight() * scale);
    if (viewportWidth <= 0 || viewportHeight <= 0)
        return;

    const int viewportX = juce::roundToInt(viewport.bounds.getX() * scale);
    const int viewportY = juce::roundToInt((viewport.componentHeight - viewport.bounds.getBottom()) * scale);

    glEnable(GL_SCISSOR_TEST);
    glScissor(viewportX, viewportY, viewportWidth, viewportHeight);
    glViewport(viewportX, viewportY, viewportWidth, viewportHeight);
}

void TrackerUIComponent::renderGrid(const juce::Matrix3D<float>& projectionMatrix,
                                    const juce::Matrix3D<float>& viewMatrix,
                                    float glowPulse)
{
    if (shaderUniforms == nullptr || shaderAttributes == nullptr)
        return;

    if (shaderUniforms->projectionMatrix != nullptr)
        shaderUniforms->projectionMatrix->setMatrix4(projectionMatrix.mat, 1, GL_FALSE);
    if (shaderUniforms->viewMatrix != nullptr)
        shaderUniforms->viewMatrix->setMatrix4(viewMatrix.mat, 1, GL_FALSE);

    openGLContext->extensions.glBindBuffer(GL_ARRAY_BUFFER, vertexBuffer);
    openGLContext->extensions.glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, indexBuffer);

    const GLsizei stride = sizeof(Vertex);
    if (shaderAttributes->position != nullptr)
    {
        openGLContext->extensions.glVertexAttribPointer(shaderAttributes->position->attributeID,
                                                       3,
                                                       GL_FLOAT,
                                                       GL_FALSE,
                                                       stride,
                                                       reinterpret_cast<GLvoid*>(offsetof(Vertex, position)));
        openGLContext->extensions.glEnableVertexAttribArray(shaderAttributes->position->attributeID);
    }

    if (shaderAttributes->normal != nullptr)
    {
        openGLContext->extensions.glVertexAttribPointer(shaderAttributes->normal->attributeID,
                                                       3,
                                                       GL_FLOAT,
                                                       GL_FALSE,
                                                       stride,
                                                       reinterpret_cast<GLvoid*>(offsetof(Vertex, normal)));
        openGLContext->extensions.glEnableVertexAttribArray(shaderAttributes->normal->attributeID);
    }

    struct OutlineEntry
    {
        juce::Matrix3D<float> modelMatrix;
        juce::Colour color;
    };

    std::vector<OutlineEntry> outlines;

    CellGrid cellsCopy;
    std::vector<float> columnWidthsCopy;
    {
        std::lock_guard<std::mutex> lock(stateMutex);
        cellsCopy = cellStates;
        columnWidthsCopy = columnWidths;
    }

    if (cellsCopy.empty())
        return;

    const size_t visibleCols = cellsCopy.size();
    const size_t visibleRows = cellsCopy[0].size();
    if (visibleCols == 0 || visibleRows == 0)
        return;

    outlines.reserve(visibleCols * visibleRows);

    const float stepY = cellHeight + cellGap;
    std::vector<float> widthScales(visibleCols, 1.0f);
    if (!columnWidthsCopy.empty())
    {
        for (size_t col = 0; col < visibleCols; ++col)
        {
            if (col < columnWidthsCopy.size() && columnWidthsCopy[col] > 0.0f)
                widthScales[col] = columnWidthsCopy[col];
        }
    }
    float gridWidth = 0.0f;
    for (size_t col = 0; col < visibleCols; ++col)
    {
        gridWidth += cellWidth * widthScales[col];
        if (col + 1 < visibleCols)
            gridWidth += cellGap;
    }
    const float gridHeight = stepY * static_cast<float>(visibleRows);
    const float startX = -gridWidth * 0.5f;
    const float startY = gridHeight * 0.5f - stepY * 0.5f;

    float cursorX = startX;
    for (size_t col = 0; col < visibleCols; ++col)
    {
        const float widthScale = widthScales[col];
        const float width = cellWidth * widthScale;
        const float centerX = cursorX + width * 0.5f;
        const float cursorOffset = width + cellGap;
        for (size_t row = 0; row < visibleRows; ++row)
        {
            const auto& cell = cellsCopy[col][row];
            const float depthScale = cell.depthScale;
            const float depth = cellDepth * depthScale;

            const auto position = juce::Vector3D<float>(centerX,
                                                        startY - static_cast<float>(row) * stepY,
                                                        depth * 0.5f);
            const auto scale = juce::Vector3D<float>(width, cellHeight, depth);
            const auto modelMatrix = getModelMatrix(position, scale);
            const auto colour = cell.fillColor;
            const float glow = cell.glow * glowPulse;
            const auto glowColor = cell.glowColor;

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
                shaderUniforms->lightDirection->set(style.lightDirection.x,
                                                    style.lightDirection.y,
                                                    style.lightDirection.z);

            if (shaderUniforms->lightColor != nullptr)
                shaderUniforms->lightColor->set(style.lightColor.getFloatRed(),
                                                style.lightColor.getFloatGreen(),
                                                style.lightColor.getFloatBlue());

            if (shaderUniforms->ambientStrength != nullptr)
                shaderUniforms->ambientStrength->set(style.ambientStrength);

            if (shaderUniforms->glowColor != nullptr)
                shaderUniforms->glowColor->set(glowColor.getFloatRed(),
                                               glowColor.getFloatGreen(),
                                               glowColor.getFloatBlue());

            glDrawElements(GL_TRIANGLES, cubeIndexCount, GL_UNSIGNED_INT, nullptr);

            if (cell.drawOutline)
                outlines.push_back(OutlineEntry{ modelMatrix, cell.outlineColor });
        }
        cursorX += cursorOffset;
    }

    if (!outlines.empty())
    {
        glDepthMask(GL_FALSE);
        glEnable(GL_POLYGON_OFFSET_LINE);
        glPolygonOffset(-2.0f, -2.0f);
        glLineWidth(1.8f);
        openGLContext->extensions.glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, frontEdgeIndexBuffer);

        if (shaderUniforms->ambientStrength != nullptr)
            shaderUniforms->ambientStrength->set(1.0f);

        if (shaderUniforms->lightColor != nullptr)
            shaderUniforms->lightColor->set(0.0f, 0.0f, 0.0f);

        if (shaderUniforms->cellGlow != nullptr)
            shaderUniforms->cellGlow->set(1.0f);

        for (const auto& outline : outlines)
        {
            if (shaderUniforms->modelMatrix != nullptr)
                shaderUniforms->modelMatrix->setMatrix4(outline.modelMatrix.mat, 1, GL_FALSE);

            if (shaderUniforms->cellColor != nullptr)
                shaderUniforms->cellColor->set(outline.color.getFloatRed(),
                                               outline.color.getFloatGreen(),
                                               outline.color.getFloatBlue(),
                                               1.0f);

            if (shaderUniforms->glowColor != nullptr)
                shaderUniforms->glowColor->set(outline.color.getFloatRed(),
                                               outline.color.getFloatGreen(),
                                               outline.color.getFloatBlue());

            glDrawElements(GL_LINES, frontFaceEdgeIndexCount, GL_UNSIGNED_INT, nullptr);
        }

        openGLContext->extensions.glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, indexBuffer);
        glDisable(GL_POLYGON_OFFSET_LINE);
        glDepthMask(GL_TRUE);
    }
}

void TrackerUIComponent::renderCellText(const juce::Matrix3D<float>& projectionMatrix,
                                        const juce::Matrix3D<float>& viewMatrix,
                                        float glowPulse)
{
    if (textShaderUniforms == nullptr || textShaderAttributes == nullptr)
        return;

    if (textShaderUniforms->projectionMatrix != nullptr)
        textShaderUniforms->projectionMatrix->setMatrix4(projectionMatrix.mat, 1, GL_FALSE);

    if (textShaderUniforms->viewMatrix != nullptr)
        textShaderUniforms->viewMatrix->setMatrix4(viewMatrix.mat, 1, GL_FALSE);

    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    CellGrid cellsCopy;
    std::vector<float> columnWidthsCopy;
    {
        std::lock_guard<std::mutex> lock(stateMutex);
        cellsCopy = cellStates;
        columnWidthsCopy = columnWidths;
    }

    if (cellsCopy.empty())
        return;

    const size_t visibleCols = cellsCopy.size();
    const size_t visibleRows = cellsCopy[0].size();
    if (visibleCols == 0 || visibleRows == 0)
        return;

    std::vector<float> widthScales(visibleCols, 1.0f);
    if (!columnWidthsCopy.empty())
    {
        for (size_t col = 0; col < visibleCols; ++col)
        {
            if (col < columnWidthsCopy.size() && columnWidthsCopy[col] > 0.0f)
                widthScales[col] = columnWidthsCopy[col];
        }
    }

    const float textDepthOffset = 0.02f;
    const float stepY = cellHeight + cellGap;
    float gridWidth = 0.0f;
    for (size_t col = 0; col < visibleCols; ++col)
    {
        gridWidth += cellWidth * widthScales[col];
        if (col + 1 < visibleCols)
            gridWidth += cellGap;
    }
    const float gridHeight = stepY * static_cast<float>(visibleRows);
    const float startX = -gridWidth * 0.5f;
    const float startY = gridHeight * 0.5f - stepY * 0.5f;
    const float targetHeight = cellHeight * 0.6f;
    const float padXScale = 0.08f;

    float cursorX = startX;
    for (size_t col = 0; col < visibleCols; ++col)
    {
        const float widthScale = widthScales[col];
        const float width = cellWidth * widthScale;
        const float centerX = cursorX + width * 0.5f;
        const float cursorOffset = width + cellGap;
        for (size_t row = 0; row < visibleRows; ++row)
        {
            const auto& cell = cellsCopy[col][row];
            if (cell.text.empty())
                continue;

            auto meshIt = textMeshCache.find(cell.text);
            if (meshIt == textMeshCache.end() || meshIt->second.indexCount == 0)
                continue;
            const auto& mesh = meshIt->second;

            const float depth = cellDepth * cell.depthScale;
            const float cellCenterX = centerX;
            const float cellCenterY = startY - static_cast<float>(row) * stepY;
            const float textZ = depth + textDepthOffset;

            const float textWidth = textGeomParams.advance * static_cast<float>(cell.text.size());
            const float targetWidth = width * 0.9f;
            const float textWidthScale = (textWidth > 0.0f) ? (targetWidth / textWidth) : 1.0f;
            const float heightScale = targetHeight / textGeomParams.cellH;
            const float scale = std::min(textWidthScale, heightScale);
            const float textHeightScaled = textGeomParams.cellH * scale;
            const float baseX = cellCenterX - width * 0.5f + width * padXScale;
            const float baseY = cellCenterY - textHeightScaled * 0.5f;

            const auto position = juce::Vector3D<float>(baseX, baseY, textZ);
            const auto scaleVec = juce::Vector3D<float>(scale, scale, 1.0f);
            const auto modelMatrix = getModelMatrix(position, scaleVec);

            if (textShaderUniforms->textColor != nullptr)
                textShaderUniforms->textColor->set(cell.textColor.getFloatRed(),
                                                   cell.textColor.getFloatGreen(),
                                                   cell.textColor.getFloatBlue(),
                                                   cell.textColor.getFloatAlpha());

            const float glowStrength = cell.glow * glowPulse;
            if (textShaderUniforms->glowStrength != nullptr)
                textShaderUniforms->glowStrength->set(glowStrength);

            const auto glowColor = cell.glowColor;
            if (textShaderUniforms->glowColor != nullptr)
                textShaderUniforms->glowColor->set(glowColor.getFloatRed(),
                                                   glowColor.getFloatGreen(),
                                                   glowColor.getFloatBlue());

            if (textShaderUniforms->modelMatrix != nullptr)
                textShaderUniforms->modelMatrix->setMatrix4(modelMatrix.mat, 1, GL_FALSE);

            openGLContext->extensions.glBindBuffer(GL_ARRAY_BUFFER, mesh.vbo);
            openGLContext->extensions.glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh.ibo);

            if (textShaderAttributes->position != nullptr)
            {
                openGLContext->extensions.glVertexAttribPointer(textShaderAttributes->position->attributeID,
                                                               3,
                                                               GL_FLOAT,
                                                               GL_FALSE,
                                                               sizeof(Segment14Geometry::Vertex),
                                                               reinterpret_cast<GLvoid*>(offsetof(Segment14Geometry::Vertex, x)));
            }

            glDrawElements(GL_TRIANGLES, mesh.indexCount, GL_UNSIGNED_INT, nullptr);
        }
        cursorX += cursorOffset;
    }

    glDepthMask(GL_TRUE);
    openGLContext->extensions.glBindBuffer(GL_ARRAY_BUFFER, 0);
    openGLContext->extensions.glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
}

void TrackerUIComponent::renderOverlayText(const juce::Matrix3D<float>& projectionMatrix,
                                           const juce::Matrix3D<float>& viewMatrix)
{
    if (textShaderUniforms == nullptr || textShaderAttributes == nullptr)
        return;

    OverlayState overlayCopy;
    ZoomState zoomCopy;
    DragState dragCopy;
    {
        std::lock_guard<std::mutex> lock(stateMutex);
        overlayCopy = overlayState;
        zoomCopy = zoomState;
        dragCopy = dragState;
    }

    if (overlayCopy.text.empty())
        return;

    auto meshIt = textMeshCache.find(overlayCopy.text);
    if (meshIt == textMeshCache.end() || meshIt->second.indexCount == 0)
        return;

    const float aspectRatio = static_cast<float>(viewport.bounds.getWidth())
        / static_cast<float>(viewport.bounds.getHeight());
    const float nearPlane = 6.0f;
    const float frustumHeight = 3.0f;
    const float frustumWidth = frustumHeight * aspectRatio;
    const float hudDistance = 8.0f;
    const float hudHalfWidth = frustumWidth * (hudDistance / nearPlane);
    const float hudHalfHeight = frustumHeight * (hudDistance / nearPlane);
    const float padding = hudHalfHeight * 0.12f;
    const float targetHeight = hudHalfHeight * 0.18f;
    const float targetWidth = hudHalfWidth * 0.9f;

    const float textWidth = textGeomParams.advance * static_cast<float>(overlayCopy.text.size());
    const float widthScale = (textWidth > 0.0f) ? (targetWidth / textWidth) : 1.0f;
    const float heightScale = targetHeight / textGeomParams.cellH;
    const float scale = std::min(widthScale, heightScale);
    const float textHeightScaled = textGeomParams.cellH * scale;

    const float baseDistance = 20.0f;
    const float cameraDistance = baseDistance / zoomCopy.zoomLevel;
    const float viewX = -hudHalfWidth + padding;
    const float viewY = hudHalfHeight - padding - textHeightScaled;
    const float worldX = -dragCopy.panX + viewX;
    const float worldY = -dragCopy.panY + viewY;
    const float worldZ = cameraDistance - hudDistance;

    const auto position = juce::Vector3D<float>(worldX, worldY, worldZ);
    const auto scaleVec = juce::Vector3D<float>(scale, scale, 1.0f);
    const auto modelMatrix = getModelMatrix(position, scaleVec);

    if (textShaderUniforms->projectionMatrix != nullptr)
        textShaderUniforms->projectionMatrix->setMatrix4(projectionMatrix.mat, 1, GL_FALSE);
    if (textShaderUniforms->viewMatrix != nullptr)
        textShaderUniforms->viewMatrix->setMatrix4(viewMatrix.mat, 1, GL_FALSE);

    if (textShaderUniforms->textColor != nullptr)
        textShaderUniforms->textColor->set(overlayCopy.color.getFloatRed(),
                                           overlayCopy.color.getFloatGreen(),
                                           overlayCopy.color.getFloatBlue(),
                                           overlayCopy.color.getFloatAlpha());

    if (textShaderUniforms->glowStrength != nullptr)
        textShaderUniforms->glowStrength->set(overlayCopy.glowStrength);

    if (textShaderUniforms->glowColor != nullptr)
        textShaderUniforms->glowColor->set(overlayCopy.glowColor.getFloatRed(),
                                           overlayCopy.glowColor.getFloatGreen(),
                                           overlayCopy.glowColor.getFloatBlue());

    if (textShaderUniforms->modelMatrix != nullptr)
        textShaderUniforms->modelMatrix->setMatrix4(modelMatrix.mat, 1, GL_FALSE);

    glDisable(GL_DEPTH_TEST);
    const auto& mesh = meshIt->second;
    openGLContext->extensions.glBindBuffer(GL_ARRAY_BUFFER, mesh.vbo);
    openGLContext->extensions.glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh.ibo);

    if (textShaderAttributes->position != nullptr)
    {
        openGLContext->extensions.glVertexAttribPointer(textShaderAttributes->position->attributeID,
                                                       3,
                                                       GL_FLOAT,
                                                       GL_FALSE,
                                                       sizeof(Segment14Geometry::Vertex),
                                                       reinterpret_cast<GLvoid*>(offsetof(Segment14Geometry::Vertex, x)));
    }

    glDrawElements(GL_TRIANGLES, mesh.indexCount, GL_UNSIGNED_INT, nullptr);
    glEnable(GL_DEPTH_TEST);
}

juce::Matrix3D<float> TrackerUIComponent::getProjectionMatrix(float aspectRatio) const
{
    const float nearPlane = 6.0f;
    const float farPlane = 100.0f;
    const float frustumHeight = 3.0f;
    const float frustumWidth = frustumHeight * aspectRatio;

    return juce::Matrix3D<float>::fromFrustum(-frustumWidth, frustumWidth,
                                              -frustumHeight, frustumHeight,
                                              nearPlane, farPlane);
}

juce::Matrix3D<float> TrackerUIComponent::getViewMatrix() const
{
    const float baseDistance = 20.0f;
    const float cameraDistance = baseDistance / zoomState.zoomLevel;
    return juce::Matrix3D<float>::fromTranslation({ dragState.panX, dragState.panY, -cameraDistance });
}

juce::Matrix3D<float> TrackerUIComponent::getModelMatrix(juce::Vector3D<float> position,
                                                         juce::Vector3D<float> scale) const
{
    return juce::Matrix3D<float>::fromTranslation(position) * makeScaleMatrix(scale);
}

float TrackerUIComponent::getGlowPulse() const
{
    const double timeSeconds = juce::Time::getMillisecondCounterHiRes() / 1000.0;
    return 0.75f + 0.25f * std::sin(static_cast<float>(timeSeconds) * 6.0f);
}

void TrackerUIComponent::updateTextGeometryCache()
{
    CellGrid cellsCopy;
    OverlayState overlayCopy;
    {
        std::lock_guard<std::mutex> lock(stateMutex);
        if (!textGeometryDirty)
            return;
        cellsCopy = cellStates;
        overlayCopy = overlayState;
        textGeometryDirty = false;
    }

    std::unordered_set<std::string> uniqueStrings;
    for (const auto& column : cellsCopy)
        for (const auto& cell : column)
            if (!cell.text.empty())
                uniqueStrings.insert(cell.text);

    if (!overlayCopy.text.empty())
        uniqueStrings.insert(overlayCopy.text);

    for (const auto& text : uniqueStrings)
        ensureTextMesh(text);
}

TrackerUIComponent::TextMesh& TrackerUIComponent::ensureTextMesh(const std::string& text)
{
    auto it = textMeshCache.find(text);
    if (it != textMeshCache.end())
        return it->second;

    TextMesh meshInfo{};
    const auto mesh = textGeometry.buildStringMesh(text);
    meshInfo.indexCount = static_cast<GLsizei>(mesh.indices.size());

    if (meshInfo.indexCount > 0 && openGLContext != nullptr)
    {
        openGLContext->extensions.glGenBuffers(1, &meshInfo.vbo);
        openGLContext->extensions.glBindBuffer(GL_ARRAY_BUFFER, meshInfo.vbo);
        openGLContext->extensions.glBufferData(GL_ARRAY_BUFFER,
                                              static_cast<GLsizeiptr>(mesh.vertices.size() * sizeof(Segment14Geometry::Vertex)),
                                              mesh.vertices.data(),
                                              GL_STATIC_DRAW);

        openGLContext->extensions.glGenBuffers(1, &meshInfo.ibo);
        openGLContext->extensions.glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, meshInfo.ibo);
        openGLContext->extensions.glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                                              static_cast<GLsizeiptr>(mesh.indices.size() * sizeof(uint32_t)),
                                              mesh.indices.data(),
                                              GL_STATIC_DRAW);
    }

    auto [inserted, _] = textMeshCache.emplace(text, meshInfo);
    return inserted->second;
}

void TrackerUIComponent::clearTextMeshCache()
{
    if (openGLContext == nullptr)
        return;

    for (auto& entry : textMeshCache)
    {
        if (entry.second.vbo != 0)
            openGLContext->extensions.glDeleteBuffers(1, &entry.second.vbo);
        if (entry.second.ibo != 0)
            openGLContext->extensions.glDeleteBuffers(1, &entry.second.ibo);
    }
    textMeshCache.clear();
}
