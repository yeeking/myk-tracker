/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin editor.

  ==============================================================================
*/

#include "TrackerMainProcessor.h"
#include "TrackerMainUI.h"
#include "SequencerCommands.h"
// #include "SimpleClock.h"
#include <algorithm>
#include <cmath>

//==============================================================================
TrackerMainUI::TrackerMainUI (TrackerMainProcessor& p)
    : AudioProcessorEditor (&p),
    framesDrawn{0},
    audioProcessor (p),
    sequencer{p.getSequencer()},
    seqEditor{p.getSequenceEditor()},
    trackerController{p.getTrackerController()},
    uiComponent(openGLContext),
    rowsInUI{9},
    waitingForPaint{false},
    updateSeqStrOnNextDraw{false}
{
    TrackerUIComponent::Style style;
    style.background = palette.background;
    style.lightColor = palette.lightColor;
    style.defaultGlowColor = palette.gridPlayhead;
    style.ambientStrength = palette.ambientStrength;
    style.lightDirection = palette.lightDirection;
    uiComponent.setStyle(style);
    uiComponent.setCellSize(cellWidth, cellHeight);

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

TrackerMainUI::~TrackerMainUI()
{
  stopTimer();
  openGLContext.detach();
}

void TrackerMainUI::newOpenGLContextCreated()
{
    uiComponent.initOpenGL(getWidth(), getHeight());
}

void TrackerMainUI::renderOpenGL()
{
    uiComponent.setViewportBounds(seqViewBounds, getHeight(), openGLContext.getRenderingScale());
    uiComponent.renderUI();
    waitingForPaint = false;
}

void TrackerMainUI::openGLContextClosing()
{
    uiComponent.shutdownOpenGL();
}

//==============================================================================
void TrackerMainUI::paint (juce::Graphics& g)
{
    juce::ignoreUnused(g);
    waitingForPaint = false; 
}

void TrackerMainUI::resized()
{
    // This is generally where you'll want topip install tf-keras lay out the positions of any
    // subcomponents in your editor..
    // controlPanelTable.setBounds(0, 0, 0, 0);
    seqViewBounds = getLocalBounds();
   
}


void TrackerMainUI::timerCallback ()
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
      case SequencerEditorMode::machineConfig:
      {
          prepareMachineConfigView();
          break;
      }
  }
    if (seqEditor->getEditMode() != SequencerEditorMode::machineConfig)
    {
      const double bpmValue = audioProcessor.getBPM();
      const int bpmInt = static_cast<int>(std::lround(bpmValue));
      const bool internalClock = audioProcessor.isInternalClockEnabled();
      if (bpmInt != lastHudBpm || internalClock != lastHudInternalClock)
      {
          overlayState.text = "@BPM " + std::to_string(bpmInt) + (internalClock ? " INT" : " HOST");
          lastHudBpm = bpmInt;
          lastHudInternalClock = internalClock;
      }
  }

  overlayState.color = palette.textPrimary;
  overlayState.glowColor = palette.gridPlayhead;
  overlayState.glowStrength = 0.35f;

  TrackerUIComponent::ZoomState zoomState;
  zoomState.zoomLevel = zoomLevel;
  TrackerUIComponent::DragState dragState;
  dragState.panX = panOffsetX;
  dragState.panY = panOffsetY;
  uiComponent.updateUIState(cellStates,
                            overlayState,
                            zoomState,
                            dragState,
                            samplerViewActive ? &samplerColumnWidths : nullptr);

  waitingForPaint = true; 
  if (updateSeqStrOnNextDraw || framesDrawn % 60 == 0){
    sequencer->updateSeqStringGrid();
    updateSeqStrOnNextDraw = false; 
  }
  openGLContext.triggerRepaint();
}



void TrackerMainUI::prepareSequenceView()
{
  samplerViewActive = false;
  samplerColumnWidths.clear();
  TrackerUIComponent::Style style;
  style.background = palette.background;
  style.lightColor = palette.lightColor;
  style.defaultGlowColor = palette.gridPlayhead;
  style.ambientStrength = palette.ambientStrength;
  style.lightDirection = palette.lightDirection;
  uiComponent.setStyle(style);
  uiComponent.setCellSize(cellWidth, cellHeight);
  // sequencer->tick();
  std::vector<std::pair<int, int>> playHeads;
  for (size_t col=0;col<audioProcessor.getSequencer()->howManySequences(); ++col){  
    std::pair<int, int> colRow = {col, audioProcessor.getSequencer()->getCurrentStep(col)};
    playHeads.push_back(std::move(colRow));
  }
  const auto boxes = buildBoxesFromGrid(audioProcessor.getSequencer()->getSequenceAsGridOfStrings(),
                                        seqEditor->getCurrentSequence(),
                                        seqEditor->getCurrentStep(),
                                        playHeads,
                                        true,
                                        seqEditor->getArmedSequence());
  updateCellStates(boxes, rowsInUI - 1, 6);
}
void TrackerMainUI::prepareStepView()
{
    samplerViewActive = false;
    samplerColumnWidths.clear();
    TrackerUIComponent::Style style;
    style.background = palette.background;
    style.lightColor = palette.lightColor;
    style.defaultGlowColor = palette.gridPlayhead;
    style.ambientStrength = palette.ambientStrength;
    style.lightDirection = palette.lightDirection;
    uiComponent.setStyle(style);
    uiComponent.setCellSize(cellWidth, cellHeight);
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
    const auto boxes = buildBoxesFromGrid(grid,
                                          seqEditor->getCurrentStepCol(),
                                          seqEditor->getCurrentStepRow(),
                                          playHeads,
                                          true,
                                          Sequencer::notArmed);
    updateCellStates(boxes, rowsInUI - 1, 6);
}
void TrackerMainUI::prepareSeqConfigView()
{
    samplerViewActive = false;
    samplerColumnWidths.clear();
    TrackerUIComponent::Style style;
    style.background = palette.background;
    style.lightColor = palette.lightColor;
    style.defaultGlowColor = palette.gridPlayhead;
    style.ambientStrength = palette.ambientStrength;
    style.lightDirection = palette.lightDirection;
    uiComponent.setStyle(style);
    uiComponent.setCellSize(cellWidth, cellHeight);
    std::vector<std::vector<std::string>> grid = sequencer->getSequenceConfigsAsGridOfStrings();
    const auto boxes = buildBoxesFromGrid(grid,
                                          seqEditor->getCurrentSequence(),
                                          seqEditor->getCurrentSeqParam(),
                                          std::vector<std::pair<int, int>>(),
                                          true,
                                          Sequencer::notArmed);
    updateCellStates(boxes, rowsInUI - 1, 6);
}

void TrackerMainUI::prepareMachineConfigView()
{
    samplerViewActive = false;
    samplerColumnWidths.clear();

    const auto* sequence = sequencer->getSequence(seqEditor->getCurrentSequence());
    const auto machineType = static_cast<CommandType>(static_cast<std::size_t>(sequence->getMachineType()));
    const int machineId = static_cast<int>(sequence->getMachineId());

    if (machineType == CommandType::Sampler)
    {
        samplerViewActive = true;
        samplerColumnWidths = { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 2.0f };

        TrackerUIComponent::Style style;
        style.background = samplerPalette.background;
        style.lightColor = samplerPalette.lightColor;
        style.defaultGlowColor = samplerPalette.glowActive;
        style.ambientStrength = samplerPalette.ambientStrength;
        style.lightDirection = samplerPalette.lightDirection;
        uiComponent.setStyle(style);
        uiComponent.setCellSize(1.2f, 1.1f);

        seqEditor->refreshMachineStateForCurrentSequence();
        const auto& boxes = seqEditor->getMachineCells();
        const size_t rows = boxes.empty() ? 1 : boxes[0].size();
        const size_t cols = boxes.empty() ? 1 : boxes.size();
        updateCellStates(boxes, rows, cols);
        overlayState.text = "SAMPLER ID " + std::to_string(machineId)
                            + (audioProcessor.isInternalClockEnabled() ? " INT" : " HOST");
        overlayState.color = samplerPalette.textPrimary;
        overlayState.glowColor = samplerPalette.glowActive;
        overlayState.glowStrength = 0.35f;
        return;
    }
    if (machineType == CommandType::Arpeggiator)
    {
        TrackerUIComponent::Style style;
        style.background = palette.background;
        style.lightColor = palette.lightColor;
        style.defaultGlowColor = palette.gridPlayhead;
        style.ambientStrength = palette.ambientStrength;
        style.lightDirection = palette.lightDirection;
        uiComponent.setStyle(style);
        uiComponent.setCellSize(cellWidth, cellHeight);

        seqEditor->refreshMachineStateForCurrentSequence();
        const auto& boxes = seqEditor->getMachineCells();
        const size_t rows = boxes.empty() ? 1 : boxes[0].size();
        const size_t cols = boxes.empty() ? 1 : boxes.size();
        updateCellStates(boxes, rows, cols);
        overlayState.text = "ARP ID " + std::to_string(machineId)
                            + (audioProcessor.isInternalClockEnabled() ? " INT" : " HOST");
        overlayState.color = palette.textPrimary;
        overlayState.glowColor = palette.gridPlayhead;
        overlayState.glowStrength = 0.25f;
        return;
    }

    TrackerUIComponent::Style style;
    style.background = palette.background;
    style.lightColor = palette.lightColor;
    style.defaultGlowColor = palette.gridPlayhead;
    style.ambientStrength = palette.ambientStrength;
    style.lightDirection = palette.lightDirection;
    uiComponent.setStyle(style);
    uiComponent.setCellSize(cellWidth, cellHeight);

    cellStates.assign(1, std::vector<TrackerUIComponent::CellState>(1, makeDefaultCell()));
    if (machineType == CommandType::MidiNote)
        overlayState.text = "CHANNEL " + std::to_string(machineId)
                            + (audioProcessor.isInternalClockEnabled() ? " INT" : " HOST");
    else if (machineType == CommandType::Log)
        overlayState.text = std::string("CHECK CONSOLE") + (audioProcessor.isInternalClockEnabled() ? " INT" : " HOST");
    else
        overlayState.text = std::string("MACHINE") + (audioProcessor.isInternalClockEnabled() ? " INT" : " HOST");
}

void TrackerMainUI::prepareControlPanelView()
{
    // std::vector<std::vector<std::string>> grid = trackerController->getControlPanelAsGridOfStrings();
    // controlPanelTable.updateData(grid, 
    //     1, 12, 
    //     0, 0, // todo - pull these from the editor which keeps track of this 
    //     std::vector<std::pair<int, int>>(), false); 
}

std::vector<std::vector<UIBox>> TrackerMainUI::buildBoxesFromGrid(const std::vector<std::vector<std::string>>& data,
                                                                 size_t cursorCol,
                                                                 size_t cursorRow,
                                                                 const std::vector<std::pair<int, int>>& highlightCells,
                                                                 bool showCursor,
                                                                 size_t armedSeq) const
{
    std::vector<std::vector<UIBox>> boxes;
    const size_t cols = data.size();
    const size_t rows = (cols > 0) ? data[0].size() : 0;
    if (cols == 0 || rows == 0)
        return boxes;

    if (cursorCol >= cols) cursorCol = cols - 1;
    if (cursorRow >= rows) cursorRow = rows - 1;

    boxes.assign(cols, std::vector<UIBox>(rows));
    for (size_t col = 0; col < cols; ++col)
    {
        for (size_t row = 0; row < rows; ++row)
        {
            UIBox box;
            box.kind = UIBox::Kind::TrackerCell;
            box.text = data[col][row];
            box.hasNote = !box.text.empty() && box.text != "----" && box.text != "-";
            box.isSelected = showCursor && col == cursorCol && row == cursorRow;
            box.isArmed = (armedSeq != Sequencer::notArmed) && col == armedSeq;
            for (const auto& cell : highlightCells)
            {
                if (static_cast<size_t>(cell.first) == col && static_cast<size_t>(cell.second) == row)
                {
                    box.isHighlighted = true;
                    break;
                }
            }
            boxes[col][row] = std::move(box);
        }
    }

    return boxes;
}

void TrackerMainUI::updateCellStates(const std::vector<std::vector<UIBox>>& boxes,
                                    size_t rowsToDisplay,
                                    size_t colsToDisplay)
{
    if (rowsToDisplay == 0 || colsToDisplay == 0)
        return;

    const size_t maxCols = boxes.size();
    const size_t maxRows = (maxCols > 0) ? boxes[0].size() : 0;
    if (maxCols == 0 || maxRows == 0)
    {
        cellStates.assign(colsToDisplay, std::vector<TrackerUIComponent::CellState>(rowsToDisplay, makeDefaultCell()));
        playheadGlow.assign(colsToDisplay, std::vector<float>(rowsToDisplay, 0.0f));
        visibleCols = colsToDisplay;
        visibleRows = rowsToDisplay;
        startCol = 0;
        startRow = 0;
        lastStartCol = 0;
        lastStartRow = 0;
        return;
    }

    size_t cursorCol = 0;
    size_t cursorRow = 0;
    bool foundCursor = false;
    for (size_t col = 0; col < maxCols && !foundCursor; ++col)
    {
        for (size_t row = 0; row < maxRows; ++row)
        {
            if (boxes[col][row].isSelected)
            {
                cursorCol = col;
                cursorRow = row;
                foundCursor = true;
                break;
            }
        }
    }
    if (!foundCursor)
    {
        cursorCol = std::min(cursorCol, maxCols - 1);
        cursorRow = std::min(cursorRow, maxRows - 1);
    }

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

    const bool reuseGlow = (startCol == nextStartCol && startRow == nextStartRow);

    cellStates.resize(colsToDisplay);
    playheadGlow.resize(colsToDisplay);
    for (size_t col = 0; col < colsToDisplay; ++col)
    {
        cellStates[col].resize(rowsToDisplay, makeDefaultCell());
        playheadGlow[col].resize(rowsToDisplay, 0.0f);
    }
    // const float glowDecayStep = 0.1f;
    // const float glowDecayScalar = 0.8f;
    

    for (size_t displayCol = 0; displayCol < colsToDisplay; ++displayCol)
    {
        const size_t col = nextStartCol + displayCol;
        const bool colInRange = col < maxCols;

        for (size_t displayRow = 0; displayRow < rowsToDisplay; ++displayRow)
        {
            const size_t row = nextStartRow + displayRow;
            const bool rowInRange = row < maxRows;

            UIBox box;
            if (colInRange && rowInRange)
                box = boxes[col][row];

            const float previousGlow = reuseGlow ? playheadGlow[displayCol][displayRow] : 0.0f;
            const float glowDecayScalar = samplerViewActive
                ? samplerPalette.glowDecayScalar
                : palette.glowDecayScalar;
            const float glowValue = samplerViewActive
                ? box.glow
                : (box.isHighlighted ? 1.0f : std::max(0.0f, previousGlow * glowDecayScalar));

            auto cell = makeDefaultCell();
            cell.text = box.text;
            cell.fillColor = samplerViewActive ? getSamplerCellColour(box) : getCellColour(box);
            cell.textColor = samplerViewActive ? getSamplerTextColour(box) : getTextColour(box);
            cell.glowColor = samplerViewActive ? samplerPalette.glowActive : palette.gridPlayhead;
            cell.glow = glowValue;
            cell.depthScale = samplerViewActive ? getSamplerCellDepthScale(box) : getCellDepthScale(box);
            cell.drawOutline = samplerViewActive ? box.isSelected : box.hasNote;
            cell.outlineColor = palette.gridNote;

            cellStates[displayCol][displayRow] = cell;
            playheadGlow[displayCol][displayRow] = glowValue;
        }
    }

    visibleCols = colsToDisplay;
    visibleRows = rowsToDisplay;
    startCol = nextStartCol;
    startRow = nextStartRow;
    lastStartCol = nextStartCol;
    lastStartRow = nextStartRow;
}

TrackerUIComponent::CellState TrackerMainUI::makeDefaultCell() const
{
    TrackerUIComponent::CellState cell;
    cell.fillColor = palette.gridEmpty;
    cell.textColor = palette.textPrimary;
    cell.glowColor = palette.gridPlayhead;
    cell.outlineColor = palette.gridNote;
    cell.depthScale = 1.0f;
    return cell;
}

/** select colour based on cell state  */
juce::Colour TrackerMainUI::getCellColour(const UIBox& cell) const
{
    if (cell.isSelected && cell.hasNote)
        return PaletteDefaults::errorRed.withAlpha(0.6f);
    if (cell.isSelected)
        return palette.gridSelected;
    if (cell.isArmed)
        return palette.statusOk;

    return palette.gridEmpty;
}

juce::Colour TrackerMainUI::getTextColour(const UIBox& cell) const
{
    if (cell.isSelected)
        return palette.gridSelected;
    if (cell.isArmed)
        return palette.statusOk;
    if (cell.hasNote)
        return palette.gridNote;
    return palette.textPrimary;
}

float TrackerMainUI::getCellDepthScale(const UIBox& cell) const
{
    float scale = 1.0f;
    if (cell.hasNote) scale = 1.3f;
    if (cell.isHighlighted) scale = 1.8f;
    if (cell.isSelected) scale = 1.6f;
    if (cell.isArmed) scale = 1.2f;
    return scale;
}

void TrackerMainUI::adjustZoom(float delta)
{
    const float minZoom = 0.5f;
    const float maxZoom = 2.5f;
    zoomLevel = juce::jlimit(minZoom, maxZoom, zoomLevel + delta);
}

void TrackerMainUI::mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel)
{
    if (!seqViewBounds.contains(event.getPosition()))
        return;

    const float zoomDelta = wheel.deltaY * 0.4f;
    if (std::abs(zoomDelta) > 0.0001f)
        adjustZoom(zoomDelta);
}

void TrackerMainUI::moveUp(float amount)
{
    panOffsetY += amount;
}

void TrackerMainUI::moveDown(float amount)
{
    panOffsetY -= amount;
}

void TrackerMainUI::moveLeft(float amount)
{
    panOffsetX += amount;
}

void TrackerMainUI::moveRight(float amount)
{
    panOffsetX -= amount;
}

void TrackerMainUI::mouseDown(const juce::MouseEvent& event)
{
    if (!seqViewBounds.contains(event.getPosition()))
        return;

    lastDragPosition = event.getPosition();
}

void TrackerMainUI::mouseDrag(const juce::MouseEvent& event)
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

juce::Colour TrackerMainUI::getSamplerCellColour(const UIBox& cell) const
{
    if (cell.isDisabled)
        return samplerPalette.cellDisabled;
    if (cell.isEditing)
        return PaletteDefaults::errorRed.withAlpha(0.6f);
    if (cell.isSelected)
        return PaletteDefaults::errorRed.withAlpha(0.6f);
    if (cell.kind == UIBox::Kind::SamplerAction && cell.isActive)
        return samplerPalette.cellAccent;
    if (cell.kind == UIBox::Kind::SamplerWaveform)
        return samplerPalette.cellIdle.brighter(0.2f);
    return samplerPalette.cellIdle;
}

juce::Colour TrackerMainUI::getSamplerTextColour(const UIBox& cell) const
{
    if (cell.isSelected)
        return palette.gridSelected;
    if (cell.kind == UIBox::Kind::SamplerAction && cell.isActive)
        return samplerPalette.glowActive;
    if (cell.kind == UIBox::Kind::SamplerWaveform)
        return samplerPalette.textMuted;
    return samplerPalette.textPrimary;
}

float TrackerMainUI::getSamplerCellDepthScale(const UIBox& cell) const
{
    if (cell.isEditing)
        return 1.05f;
    if (cell.isSelected)
        return 1.02f;
    return 1.0f;
}


bool TrackerMainUI::keyPressed(const juce::KeyPress& key, juce::Component* originatingComponent)
{
    juce::ignoreUnused(originatingComponent);
    // 
    // space key always stops and starts.
    if (key.isKeyCode(juce::KeyPress::spaceKey)){
        CommandProcessor::sendAllNotesOff();
        if (audioProcessor.getSequencer()->isPlaying()){
            audioProcessor.getSequencer()->stop();
        }
        else{
            audioProcessor.getSequencer()->rewindAtNextZero();
            audioProcessor.getSequencer()->play();

        }
        return true; 
    }
    if (key.getModifiers().isShiftDown())
    {
        const juce::juce_wchar ch = key.getTextCharacter();
        if (ch == 'C' || ch == 'c')
        {
            const bool enabled = audioProcessor.isInternalClockEnabled();
            audioProcessor.setInternalClockEnabled(!enabled);
            return true;
        }
    }

    // deal with with the user pressing a key when the sequencer is stopped. 
    if (!audioProcessor.getSequencer()->isPlaying())
    {
        const char ch = static_cast<char>(key.getTextCharacter());
        const std::map<char, double> key_to_note = MachineUtilsAbs::getKeyboardToMidiNotes(0);
        const auto it = key_to_note.find(ch);
        // this block of code deals
        if (it != key_to_note.end())
        {
            const size_t seqIndex = seqEditor->getCurrentSequence();
            const size_t stepIndex = seqEditor->getCurrentStep();
            const size_t rowIndex = seqEditor->getCurrentStepRow();
            if (Sequence* sequence = sequencer->getSequence(seqIndex))
            {
                auto data = sequence->getStepData(stepIndex);
                const size_t safeRow = rowIndex < data.size() ? rowIndex : 0;
                double note = it->second + (12 * seqEditor->getCurrentOctave());
                
                seqEditor->machineLearnNote(static_cast<int>(note));
                auto context = sequence->getReadOnlyContext();
                bool useDefaults = data.empty();
                if (data.empty())
                {
                    data.resize(1);
                    data[0].assign(Step::maxInd + 1, 0.0);
                }
                if (data[safeRow].size() < Step::maxInd + 1)
                {
                    data[safeRow].resize(Step::maxInd + 1, 0.0);
                }
                if (!useDefaults && data[safeRow][Step::noteInd] == 0.0)
                {
                    useDefaults = true;
                }
                if (useDefaults)
                {
                    data[safeRow][Step::cmdInd] = context.machineType;
                    const Command& cmd = CommandProcessor::getCommand(context.machineType);
                    for (std::size_t i = 0; i < cmd.parameters.size() && i < Step::maxInd; ++i)
                    {
                        data[safeRow][i + 1] = cmd.parameters[i].defaultValue;
                    }
                }
                data[safeRow][Step::noteInd] = note;
                data[safeRow][Step::probInd] = 1.0;
                context.triggerProbability = 1.0;

                CommandProcessor::executeCommand(data[safeRow][Step::cmdInd], &data[safeRow], &context);
            }

        }
    }// end dealing with user pressing a key when sequencer is stopped.

    if (seqEditor->getEditMode() == SequencerEditorMode::machineConfig)
    {
        // if (key.isKeyCode(juce::KeyPress::returnKey))
        // {
        //     seqEditor->enterAtCursor();
        //     return true;
        // }
        if (key == juce::KeyPress::escapeKey)
        {
            // escape goes back to previous edit mode
            seqEditor->enterAtCursor();
            return true;
        }

        const auto* sequence = sequencer->getSequence(seqEditor->getCurrentSequence());
        const auto machineType = static_cast<CommandType>(static_cast<std::size_t>(sequence->getMachineType()));
        if (machineType == CommandType::Sampler || machineType == CommandType::Arpeggiator)
        {
            const char samplerKey = static_cast<char>(key.getTextCharacter());
            if (machineType == CommandType::Sampler)
            {
                if (samplerKey == '=')
                {
                    seqEditor->machineAddEntry();
                    return true;
                }
                if (samplerKey == '-')
                {
                    seqEditor->machineRemoveEntry();
                    return true;
                }
            }
            if (samplerKey == '[')
            {
                seqEditor->decrementAtCursor();
                return true;
            }
            if (samplerKey == ']')
            {
                seqEditor->incrementAtCursor();
                return true;
            }

            if (key.getKeyCode() == juce::KeyPress::leftKey)
            {
                seqEditor->moveCursorLeft();
                return true;
            }
            if (key.getKeyCode() == juce::KeyPress::rightKey)
            {
                seqEditor->moveCursorRight();
                return true;
            }
            if (key.getKeyCode() == juce::KeyPress::upKey)
            {
                seqEditor->moveCursorUp();
                return true;
            }
            if (key.getKeyCode() == juce::KeyPress::downKey)
            {
                seqEditor->moveCursorDown();
                return true;
            }
            if (key.getKeyCode() == juce::KeyPress::returnKey)
            {
                // return will trigger the button
                // it its an add or play button
                seqEditor->cycleAtCursor();
                return true;
            }
        }
        return true;
    }

    switch (key.getTextCharacter())
    {
        case 'A':// arm a sequence for live MIDI input 
            seqEditor->setArmedSequence(seqEditor->getCurrentSequence());
            break; 
        case 'R':// re-e-wind
            CommandProcessor::sendAllNotesOff();
            audioProcessor.getSequencer()->rewindAtNextZero();
            break;
        // case ' ':
        //     CommandProcessor::sendAllNotesOff();
        //     if (audioProcessor.getSequencer()->isPlaying()){
        //         audioProcessor.getSequencer()->stop();
        //     }
        //     else{
        //         audioProcessor.getSequencer()->rewindAtNextZero();
        //         audioProcessor.getSequencer()->play();

        //     }
        //     break;
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
            seqEditor->gotoMachineConfigPage();
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
            std::map<char, double> key_to_note = MachineUtilsAbs::getKeyboardToMidiNotes(0);
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

bool TrackerMainUI::keyStateChanged(bool isKeyDown, juce::Component* originatingComponent)
{
  juce::ignoreUnused(isKeyDown);
  juce::ignoreUnused(originatingComponent);
  return false; 
}
