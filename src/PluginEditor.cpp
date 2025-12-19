/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin editor.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "SequencerCommands.h"
#include "SimpleClock.h"

using namespace juce::gl;

namespace
{
struct Vertex
{
    float position[3];
    float colour[4];
};

const char* vertexShaderSource = R"(
    attribute vec3 position;
    attribute vec4 colour;
    uniform mat4 projectionMatrix;
    uniform mat4 viewMatrix;
    uniform mat4 modelMatrix;
    uniform vec4 cellColor;
    varying vec4 vColour;

    void main()
    {
        vColour = colour * cellColor;
        gl_Position = projectionMatrix * viewMatrix * modelMatrix * vec4(position, 1.0);
    }
)";

const char* fragmentShaderSource = R"(
    varying vec4 vColour;

    void main()
    {
        gl_FragColor = vColour;
    }
)";

const Vertex cubeVertices[] =
{
    { { -0.5f, -0.5f,  0.5f }, { 1.0f, 1.0f, 1.0f, 1.0f } },
    { {  0.5f, -0.5f,  0.5f }, { 1.0f, 1.0f, 1.0f, 1.0f } },
    { {  0.5f,  0.5f,  0.5f }, { 1.0f, 1.0f, 1.0f, 1.0f } },
    { { -0.5f,  0.5f,  0.5f }, { 1.0f, 1.0f, 1.0f, 1.0f } },
    { { -0.5f, -0.5f, -0.5f }, { 1.0f, 1.0f, 1.0f, 1.0f } },
    { {  0.5f, -0.5f, -0.5f }, { 1.0f, 1.0f, 1.0f, 1.0f } },
    { {  0.5f,  0.5f, -0.5f }, { 1.0f, 1.0f, 1.0f, 1.0f } },
    { { -0.5f,  0.5f, -0.5f }, { 1.0f, 1.0f, 1.0f, 1.0f } }
};

const GLuint cubeIndices[] =
{
    0, 1, 2, 2, 3, 0, // front
    1, 5, 6, 6, 2, 1, // right
    5, 4, 7, 7, 6, 5, // back
    4, 0, 3, 3, 7, 4, // left
    3, 2, 6, 6, 7, 3, // top
    4, 5, 1, 1, 0, 4  // bottom
};
const GLsizei cubeIndexCount = static_cast<GLsizei>(sizeof(cubeIndices) / sizeof(cubeIndices[0]));

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
    openGLContext.setRenderer(this);
    openGLContext.setContinuousRepainting(false);
    openGLContext.attachTo(*this);
    // Make sure that before the constructor has finished, you've set the
    // editor's size to whatever you need it to be.
    setSize (1024, 768);
    addAndMakeVisible(controlPanelTable);
    
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
        shaderAttributes->colour = std::make_unique<juce::OpenGLShaderProgram::Attribute>(*shaderProgram, "colour");

        shaderUniforms = std::make_unique<ShaderUniforms>();
        shaderUniforms->projectionMatrix = std::make_unique<juce::OpenGLShaderProgram::Uniform>(*shaderProgram, "projectionMatrix");
        shaderUniforms->viewMatrix = std::make_unique<juce::OpenGLShaderProgram::Uniform>(*shaderProgram, "viewMatrix");
        shaderUniforms->modelMatrix = std::make_unique<juce::OpenGLShaderProgram::Uniform>(*shaderProgram, "modelMatrix");
        shaderUniforms->cellColor = std::make_unique<juce::OpenGLShaderProgram::Uniform>(*shaderProgram, "cellColor");
    }
    else
    {
        jassertfalse;
        shaderProgram.reset();
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

    const float renderingScale = openGLContext.getRenderingScale();
    const int viewportWidth = juce::roundToInt(seqViewBounds.getWidth() * renderingScale);
    const int viewportHeight = juce::roundToInt(seqViewBounds.getHeight() * renderingScale);

    if (viewportWidth <= 0 || viewportHeight <= 0)
        return;

    const int viewportX = juce::roundToInt(seqViewBounds.getX() * renderingScale);
    const int viewportY = juce::roundToInt((getHeight() - seqViewBounds.getBottom()) * renderingScale);

    glEnable(GL_SCISSOR_TEST);
    glScissor(viewportX, viewportY, viewportWidth, viewportHeight);
    glViewport(viewportX, viewportY, viewportWidth, viewportHeight);

    glClearColor(0.02f, 0.02f, 0.02f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

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

    if (shaderAttributes->colour != nullptr)
    {
        openGLContext.extensions.glVertexAttribPointer(shaderAttributes->colour->attributeID,
                                                       4,
                                                       GL_FLOAT,
                                                       GL_FALSE,
                                                       stride,
                                                       reinterpret_cast<GLvoid*>(offsetof(Vertex, colour)));
        openGLContext.extensions.glEnableVertexAttribArray(shaderAttributes->colour->attributeID);
    }

    {
        std::lock_guard<std::mutex> lock(cellStateMutex);
        if (visibleCols > 0 && visibleRows > 0 && !cellStates.empty())
        {
            const float cellWidth = 1.0f;
            const float cellHeight = 1.0f;
            const float cellDepth = 0.3f;
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
                    const float depthScale = getCellDepthScale(cell);
                    const float depth = cellDepth * depthScale;

                    const auto position = juce::Vector3D<float>(startX + static_cast<float>(col) * stepX,
                                                                startY - static_cast<float>(row) * stepY,
                                                                depth * 0.5f);
                    const auto scale = juce::Vector3D<float>(cellWidth, cellHeight, depth);
                    const auto modelMatrix = getModelMatrix(position, scale);
                    const auto colour = getCellColour(cell);

                    if (shaderUniforms->modelMatrix != nullptr)
                        shaderUniforms->modelMatrix->setMatrix4(modelMatrix.mat, 1, GL_FALSE);

                    if (shaderUniforms->cellColor != nullptr)
                        shaderUniforms->cellColor->set(colour.getFloatRed(),
                                                       colour.getFloatGreen(),
                                                       colour.getFloatBlue(),
                                                       colour.getFloatAlpha());

                    glDrawElements(GL_TRIANGLES, cubeIndexCount, GL_UNSIGNED_INT, nullptr);
                }
            }
        }
    }

    if (shaderAttributes->position != nullptr)
        openGLContext.extensions.glDisableVertexAttribArray(shaderAttributes->position->attributeID);

    if (shaderAttributes->colour != nullptr)
        openGLContext.extensions.glDisableVertexAttribArray(shaderAttributes->colour->attributeID);

    openGLContext.extensions.glUseProgram(0);
    glDisable(GL_SCISSOR_TEST);

    waitingForPaint = false;
}

void PluginEditor::openGLContextClosing()
{
    shaderAttributes.reset();
    shaderUniforms.reset();
    shaderProgram.reset();

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

    std::vector<std::vector<CellVisualState>> nextStates;
    nextStates.reserve(nextEndCol - nextStartCol);

    for (size_t col = nextStartCol; col < nextEndCol; ++col)
    {
        std::vector<CellVisualState> columnStates;
        columnStates.reserve(nextEndRow - nextStartRow);

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

            columnStates.push_back(CellVisualState{hasNote, isHighlighted, isSelected, isArmed});
        }

        nextStates.push_back(std::move(columnStates));
    }

    {
        std::lock_guard<std::mutex> lock(cellStateMutex);
        cellStates = std::move(nextStates);
        visibleCols = nextEndCol - nextStartCol;
        visibleRows = nextEndRow - nextStartRow;
        startCol = nextStartCol;
        startRow = nextStartRow;
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
    const float cameraDistance = 20.0f;
    return juce::Matrix3D<float>::fromTranslation({0.0f, 0.0f, -cameraDistance});
}

juce::Matrix3D<float> PluginEditor::getModelMatrix(juce::Vector3D<float> position, juce::Vector3D<float> scale) const
{
    return juce::Matrix3D<float>::fromTranslation(position) * makeScaleMatrix(scale);
}

juce::Colour PluginEditor::getCellColour(const CellVisualState& cell) const
{
    if (cell.isSelected)
        return juce::Colour::fromFloatRGBA(0.4f, 1.0f, 0.4f, 1.0f);
    if (cell.isActivePlayhead)
        return juce::Colour::fromFloatRGBA(1.0f, 0.6f, 0.1f, 1.0f);
    if (cell.isArmed)
        return juce::Colour::fromFloatRGBA(0.9f, 0.2f, 0.2f, 0.9f);
    if (cell.hasNote)
        return juce::Colour::fromFloatRGBA(0.2f, 0.7f, 0.9f, 0.9f);

    return juce::Colour::fromFloatRGBA(0.2f, 0.2f, 0.2f, 0.35f);
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

