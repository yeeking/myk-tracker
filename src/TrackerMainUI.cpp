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
#include <utility>

namespace
{
const char* getOverlayTitleForEditMode(SequencerEditorMode mode)
{
    switch (mode)
    {
        case SequencerEditorMode::selectingSeqAndStep:
            return "SEQUENCE";
        case SequencerEditorMode::editingStep:
            return "STEP";
        case SequencerEditorMode::configuringSequence:
            return "SEQ CONFIG";
        case SequencerEditorMode::machineConfig:
            return "MACHINE";
        case SequencerEditorMode::resetConfirmation:
            return "RESET TRACKER?";
    }

    return "";
}

std::string makeHudTitle(SequencerEditorMode mode, int bpmInt, bool internalClock)
{
    std::string title = getOverlayTitleForEditMode(mode);
    if (mode == SequencerEditorMode::machineConfig || mode == SequencerEditorMode::resetConfirmation)
        return title;

    title += " @";
    title += std::to_string(bpmInt);
    title += internalClock ? " INT" : " HOST";
    return title;
}
}

//==============================================================================
TrackerMainUI::TrackerMainUI (TrackerMainProcessor& p)
    : AudioProcessorEditor (&p),
    framesDrawn{0},
    audioProcessor (p),
    seqEditor{p.getSequenceEditor()},
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

#if JucePlugin_Build_Standalone
    seqEditor->setQuitConfirmationHandler([]()
    {
        juce::MessageManager::callAsync([]()
        {
            if (auto* app = juce::JUCEApplicationBase::getInstance())
                app->systemRequestedQuit();
        });
    });
#endif

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
    uiComponent.setViewportBounds(seqViewBounds,
                                  getHeight(),
                                  static_cast<float>(openGLContext.getRenderingScale()));
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

void TrackerMainUI::parentHierarchyChanged()
{
    AudioProcessorEditor::parentHierarchyChanged();

#if JucePlugin_Build_Standalone
    if (auto* window = dynamic_cast<juce::DocumentWindow*>(getTopLevelComponent()))
    {
        window->setUsingNativeTitleBar(false);
        window->setTitleBarHeight(0);
        juce::Desktop::getInstance().setKioskModeComponent(window, false);
    }
#endif
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
  for (const auto& zoomCommand : audioProcessor.consumePendingZoomCommands())
  {
      adjustZoomAroundPoint(zoomCommand.delta,
                            { zoomCommand.normalizedX, zoomCommand.normalizedY });
  }
  prepareControlPanelView();  
  // check what to draw based on the state of the 
  // editor
  SequencerEditorMode editMode = SequencerEditorMode::selectingSeqAndStep;
  audioProcessor.withAudioThreadExclusive([&]()
  {
      editMode = seqEditor->getEditMode();
  });
  switch(editMode){

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
      case SequencerEditorMode::resetConfirmation:
      {
          prepareResetConfirmationView();
          break;
      }
  }
  audioProcessor.withAudioThreadExclusive([&]()
  {
      audioProcessor.sendCurrentCellValueOverOscIfChanged();
  });
    if (editMode != SequencerEditorMode::machineConfig
        && editMode != SequencerEditorMode::resetConfirmation)
    {
      const double bpmValue = audioProcessor.getBPM();
      const int bpmInt = static_cast<int>(std::lround(bpmValue));
      const bool internalClock = audioProcessor.isInternalClockEnabled();
      const std::string hudTitle = makeHudTitle(editMode, bpmInt, internalClock);
      if (bpmInt != lastHudBpm
          || internalClock != lastHudInternalClock
          || overlayState.text != hudTitle)
      {
          overlayState.text = hudTitle;
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
                            customMachineColumnWidthsActive ? &samplerColumnWidths : nullptr);

  waitingForPaint = true; 
  if (updateSeqStrOnNextDraw || framesDrawn % 60 == 0){
    audioProcessor.withAudioThreadExclusive([&]()
    {
        audioProcessor.getSequencer()->requestStrUpdate();
    });
    updateSeqStrOnNextDraw = false; 
  }
  openGLContext.triggerRepaint();
}



void TrackerMainUI::prepareSequenceView()
{
  samplerViewActive = false;
  customMachineColumnWidthsActive = false;
  samplerColumnWidths.clear();
  TrackerUIComponent::Style style;
  style.background = palette.background;
  style.lightColor = palette.lightColor;
  style.defaultGlowColor = palette.gridPlayhead;
  style.ambientStrength = palette.ambientStrength;
  style.lightDirection = palette.lightDirection;
  uiComponent.setStyle(style);
  uiComponent.setCellSize(cellWidth, cellHeight);
  std::vector<std::pair<int, int>> playHeads;
  std::vector<std::vector<std::string>> grid;
  size_t currentSequence = 0;
  size_t currentStep = 0;
  size_t armedSequence = SequencerAbs::notArmed;
  bool isPlaying = false;
  audioProcessor.withAudioThreadExclusive([&]()
  {
      currentSequence = seqEditor->getCurrentSequence();
      currentStep = seqEditor->getCurrentStep();
      armedSequence = seqEditor->getArmedSequence();
      auto* seq = audioProcessor.getSequencer();
      isPlaying = seq->isPlaying();
      const size_t sequenceCount = seq->howManySequences();
      playHeads.clear();
      playHeads.reserve(sequenceCount);
      for (size_t col = 0; col < sequenceCount; ++col)
      {
          playHeads.emplace_back(static_cast<int>(col),
                                 static_cast<int>(seq->getCurrentStep(col)));
      }
      grid = seq->getSequenceAsGridOfStrings();
  });
  style.glowPulseEnabled = !isPlaying;
  const auto boxes = buildBoxesFromGrid(grid,
                                        currentSequence,
                                        currentStep,
                                        playHeads,
                                        true,
                                        armedSequence);
  updateCellStates(boxes, rowsInUI - 1, 6);
}
void TrackerMainUI::prepareStepView()
{
  samplerViewActive = false;
  customMachineColumnWidthsActive = false;
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
    std::vector<std::vector<std::string>> grid;
    size_t currentSequence = 0;
    size_t currentStep = 0;
    size_t currentStepCol = 0;
    size_t currentStepRow = 0;
    bool isPlaying = false;
    audioProcessor.withAudioThreadExclusive([&]()
    {
        currentSequence = seqEditor->getCurrentSequence();
        currentStep = seqEditor->getCurrentStep();
        currentStepCol = seqEditor->getCurrentStepCol();
        currentStepRow = seqEditor->getCurrentStepRow();
        auto* seq = audioProcessor.getSequencer();
        isPlaying = seq->isPlaying();
        if (seq->getCurrentStep(currentSequence) == currentStep)
        {
            const int cols = static_cast<int>(seq->howManyStepDataCols(currentSequence, currentStep));
            playHeads.clear();
            playHeads.reserve(static_cast<size_t>(cols));
            for (int col = 0; col < cols; ++col)
                playHeads.emplace_back(col, 0);
        }
        grid = seq->getStepAsGridOfStrings(currentSequence, currentStep);
    });
    style.glowPulseEnabled = !isPlaying;
    const auto boxes = buildBoxesFromGrid(grid,
                                          currentStepCol,
                                          currentStepRow,
                                          playHeads,
                                          true,
                                          Sequencer::notArmed);
    updateCellStates(boxes, rowsInUI - 1, 6);
}
void TrackerMainUI::prepareSeqConfigView()
{
  samplerViewActive = false;
  customMachineColumnWidthsActive = false;
  samplerColumnWidths.clear();
    TrackerUIComponent::Style style;
    style.background = palette.background;
    style.lightColor = palette.lightColor;
    style.defaultGlowColor = palette.gridPlayhead;
    style.ambientStrength = palette.ambientStrength;
    style.lightDirection = palette.lightDirection;
    uiComponent.setStyle(style);
    uiComponent.setCellSize(cellWidth, cellHeight);
    std::vector<std::vector<std::string>> grid;
    size_t currentSequence = 0;
    size_t currentSeqParam = 0;
    audioProcessor.withAudioThreadExclusive([&]()
    {
        currentSequence = seqEditor->getCurrentSequence();
        currentSeqParam = seqEditor->getCurrentSeqParam();
        grid = audioProcessor.getSequencer()->getSequenceConfigsAsGridOfStrings();
    });
    const auto boxes = buildBoxesFromGrid(grid,
                                          currentSequence,
                                          currentSeqParam,
                                          std::vector<std::pair<int, int>>(),
                                          true,
                                          Sequencer::notArmed);
    updateCellStates(boxes, rowsInUI - 1, 6);
}

void TrackerMainUI::prepareMachineConfigView()
{
    samplerViewActive = false;
    customMachineColumnWidthsActive = false;
    samplerColumnWidths.clear();

    int machineId = 0;
    std::vector<std::vector<UIBox>> machineBoxes;
    std::optional<CommandType> detailType;
    audioProcessor.withAudioThreadExclusive([&]()
    {
        auto* seq = audioProcessor.getSequencer();
        if (auto* sequence = seq->getSequence(seqEditor->getCurrentSequence()))
        {
            machineId = static_cast<int>(sequence->getMachineId());
        }
        seqEditor->refreshMachineStateForCurrentSequence();
        machineBoxes = seqEditor->getMachineCells();
        detailType = seqEditor->getFocusedMachineDetailType();
    });

    if (detailType.has_value() && detailType.value() == CommandType::Sampler)
    {
        samplerViewActive = true;
        customMachineColumnWidthsActive = true;
        bool browserActive = false;
        if (auto* sampler = dynamic_cast<SuperSamplerProcessor*>(audioProcessor.getMachine(CommandType::Sampler, static_cast<std::size_t>(machineId))))
            browserActive = sampler->isBrowsingFiles();
        samplerColumnWidths.clear();
        samplerColumnWidths.resize(machineBoxes.size(), 1.0f);
        bool hasCustomWidths = false;
        for (std::size_t col = 0; col < machineBoxes.size(); ++col)
        {
            float columnWidth = 1.0f;
            for (const auto& cell : machineBoxes[col])
                columnWidth = std::max(columnWidth, cell.width);
            samplerColumnWidths[col] = columnWidth;
            hasCustomWidths = hasCustomWidths || columnWidth > 1.0f;
        }
        if (!hasCustomWidths && samplerColumnWidths.size() == 6)
            samplerColumnWidths = { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 2.0f };

        TrackerUIComponent::Style style;
        style.background = samplerPalette.background;
        style.lightColor = samplerPalette.lightColor;
        style.defaultGlowColor = samplerPalette.glowActive;
        style.ambientStrength = samplerPalette.ambientStrength;
        style.lightDirection = samplerPalette.lightDirection;
        uiComponent.setStyle(style);
        uiComponent.setCellSize(1.2f, 1.1f);

        const size_t rows = machineBoxes.empty() ? 1 : machineBoxes[0].size();
        const size_t cols = machineBoxes.empty() ? 1 : machineBoxes.size();
        updateCellStates(machineBoxes, browserActive ? std::min<std::size_t>(rows, 8) : rows, cols);
        overlayState.text = "STACK " + std::to_string(machineId) + " SAMPLER"
                            + (audioProcessor.isInternalClockEnabled() ? " INT" : " HOST");
        overlayState.color = samplerPalette.textPrimary;
        overlayState.glowColor = samplerPalette.glowActive;
        overlayState.glowStrength = 0.35f;
        return;
    }
    if (detailType.has_value() && detailType.value() == CommandType::Arpeggiator)
    {
        TrackerUIComponent::Style style;
        style.background = palette.background;
        style.lightColor = palette.lightColor;
        style.defaultGlowColor = palette.gridPlayhead;
        style.ambientStrength = palette.ambientStrength;
        style.lightDirection = palette.lightDirection;
        uiComponent.setStyle(style);
        uiComponent.setCellSize(cellWidth, cellHeight);

        const size_t rows = machineBoxes.empty() ? 1 : machineBoxes[0].size();
        const size_t cols = machineBoxes.empty() ? 1 : machineBoxes.size();
        updateCellStates(machineBoxes, rows, cols);
        overlayState.text = "STACK " + std::to_string(machineId) + " ARP"
                            + (audioProcessor.isInternalClockEnabled() ? " INT" : " HOST");
        overlayState.color = palette.textPrimary;
        overlayState.glowColor = palette.gridPlayhead;
        overlayState.glowStrength = 0.25f;
        return;
    }
    if (detailType.has_value() && detailType.value() == CommandType::PolyArpeggiator)
    {
        TrackerUIComponent::Style style;
        style.background = palette.background;
        style.lightColor = palette.lightColor;
        style.defaultGlowColor = palette.gridPlayhead;
        style.ambientStrength = palette.ambientStrength;
        style.lightDirection = palette.lightDirection;
        uiComponent.setStyle(style);
        uiComponent.setCellSize(cellWidth, cellHeight);

        const size_t rows = machineBoxes.empty() ? 1 : machineBoxes[0].size();
        const size_t cols = machineBoxes.empty() ? 1 : machineBoxes.size();
        updateCellStates(machineBoxes, rows, cols);
        overlayState.text = "STACK " + std::to_string(machineId) + " POLY ARP"
                            + (audioProcessor.isInternalClockEnabled() ? " INT" : " HOST");
        overlayState.color = palette.textPrimary;
        overlayState.glowColor = palette.gridPlayhead;
        overlayState.glowStrength = 0.25f;
        return;
    }
    if (detailType.has_value() && detailType.value() == CommandType::WavetableSynth)
    {
        customMachineColumnWidthsActive = true;
        samplerColumnWidths.assign(machineBoxes.size(), 1.0f);
        for (std::size_t col = 0; col < machineBoxes.size(); ++col)
        {
            float columnWidth = 1.0f;
            for (const auto& cell : machineBoxes[col])
                columnWidth = std::max(columnWidth, cell.width);
            samplerColumnWidths[col] = columnWidth;
        }

        TrackerUIComponent::Style style;
        style.background = palette.background;
        style.lightColor = palette.lightColor;
        style.defaultGlowColor = palette.gridPlayhead;
        style.ambientStrength = palette.ambientStrength;
        style.lightDirection = palette.lightDirection;
        uiComponent.setStyle(style);
        uiComponent.setCellSize(cellWidth, cellHeight);

        const size_t rows = machineBoxes.empty() ? 1 : machineBoxes[0].size();
        const size_t cols = machineBoxes.empty() ? 1 : machineBoxes.size();
        updateCellStates(machineBoxes, rows, cols);
        overlayState.text = "STACK " + std::to_string(machineId) + " WAVE"
                            + (audioProcessor.isInternalClockEnabled() ? " INT" : " HOST");
        overlayState.color = palette.textPrimary;
        overlayState.glowColor = palette.gridPlayhead;
        overlayState.glowStrength = 0.25f;
        return;
    }
    if (detailType.has_value() && detailType.value() == CommandType::MidiNote)
    {
        TrackerUIComponent::Style style;
        style.background = palette.background;
        style.lightColor = palette.lightColor;
        style.defaultGlowColor = palette.gridPlayhead;
        style.ambientStrength = palette.ambientStrength;
        style.lightDirection = palette.lightDirection;
        uiComponent.setStyle(style);
        uiComponent.setCellSize(cellWidth, cellHeight);

        const size_t rows = machineBoxes.empty() ? 1 : machineBoxes[0].size();
        const size_t cols = machineBoxes.empty() ? 1 : machineBoxes.size();
        updateCellStates(machineBoxes, rows, cols);
        overlayState.text = "STACK " + std::to_string(machineId) + " MIDI"
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

    const size_t rows = machineBoxes.empty() ? 1 : machineBoxes[0].size();
    const size_t cols = machineBoxes.empty() ? 1 : machineBoxes.size();
    updateCellStates(machineBoxes, rows, cols);
    overlayState.text = "STACK " + std::to_string(machineId)
                        + (audioProcessor.isInternalClockEnabled() ? " INT" : " HOST");
}

void TrackerMainUI::prepareControlPanelView()
{
    // std::vector<std::vector<std::string>> grid = trackerController->getControlPanelAsGridOfStrings();
    // controlPanelTable.updateData(grid, 
    //     1, 12, 
    //     0, 0, // todo - pull these from the editor which keeps track of this 
    //     std::vector<std::pair<int, int>>(), false); 
}

void TrackerMainUI::prepareResetConfirmationView()
{
  samplerViewActive = false;
    customMachineColumnWidthsActive = false;
    samplerColumnWidths.clear();

    TrackerUIComponent::Style style;
    style.background = palette.background;
    style.lightColor = palette.lightColor;
    style.defaultGlowColor = palette.gridPlayhead;
    style.ambientStrength = palette.ambientStrength;
    style.lightDirection = palette.lightDirection;
    uiComponent.setStyle(style);
    uiComponent.setCellSize(cellWidth, cellHeight);

    const bool yesSelected = seqEditor->isResetConfirmationYesSelected();
    std::vector<std::vector<UIBox>> boxes(2, std::vector<UIBox>(1));

    boxes[0][0].kind = UIBox::Kind::TrackerCell;
    boxes[0][0].text = "YES";
    boxes[0][0].isSelected = yesSelected;

    boxes[1][0].kind = UIBox::Kind::TrackerCell;
    boxes[1][0].text = "NO";
    boxes[1][0].isSelected = !yesSelected;

    updateCellStates(boxes, 1, 2);
    overlayState.text = seqEditor->getConfirmationPrompt();
    overlayState.color = palette.textWarning;
    overlayState.glowColor = palette.gridPlayhead;
    overlayState.glowStrength = 0.45f;
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
    if (cell.isSelected)
        return PaletteDefaults::cursor.fill;
    if (cell.useCustomFillColour)
        return juce::Colour(cell.customFillArgb);
    if (cell.isArmed)
        return palette.statusOk;

    return palette.gridEmpty;
}

juce::Colour TrackerMainUI::getTextColour(const UIBox& cell) const
{
    if (cell.isSelected)
        return PaletteDefaults::cursor.text;
    if (cell.useCustomTextColour)
        return juce::Colour(cell.customTextArgb);
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

void TrackerMainUI::adjustZoomAroundPoint(float delta, juce::Point<float> normalizedPoint)
{
    const float minZoom = 0.5f;
    const float maxZoom = 2.5f;
    const float oldZoom = zoomLevel;
    const float newZoom = juce::jlimit(minZoom, maxZoom, zoomLevel + delta);

    if (std::abs(newZoom - oldZoom) < 0.0001f)
        return;

    const float aspectRatio = seqViewBounds.getHeight() > 0
        ? static_cast<float>(seqViewBounds.getWidth()) / static_cast<float>(seqViewBounds.getHeight())
        : 1.0f;
    const float nearPlane = 6.0f;
    const float frustumHalfHeight = 3.0f;
    const float frustumHalfWidth = frustumHalfHeight * aspectRatio;
    const float baseDistance = 20.0f;
    const float oldDistance = baseDistance / oldZoom;
    const float newDistance = baseDistance / newZoom;

    const float normalizedX = juce::jlimit(0.0f, 1.0f, normalizedPoint.x);
    const float normalizedY = juce::jlimit(0.0f, 1.0f, normalizedPoint.y);
    const float oldViewX = ((normalizedX * 2.0f) - 1.0f) * frustumHalfWidth * (oldDistance / nearPlane);
    const float newViewX = ((normalizedX * 2.0f) - 1.0f) * frustumHalfWidth * (newDistance / nearPlane);
    const float oldViewY = (1.0f - (normalizedY * 2.0f)) * frustumHalfHeight * (oldDistance / nearPlane);
    const float newViewY = (1.0f - (normalizedY * 2.0f)) * frustumHalfHeight * (newDistance / nearPlane);

    panOffsetX += (newViewX - oldViewX);
    panOffsetY += (newViewY - oldViewY);
    zoomLevel = newZoom;
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
        return PaletteDefaults::cursor.fill;
    if (cell.isSelected)
        return PaletteDefaults::cursor.fill;
    if (cell.kind == UIBox::Kind::SamplerAction && cell.isActive)
        return samplerPalette.cellAccent;
    if (cell.kind == UIBox::Kind::SamplerWaveform)
        return samplerPalette.cellIdle.brighter(0.2f);
    return samplerPalette.cellIdle;
}

juce::Colour TrackerMainUI::getSamplerTextColour(const UIBox& cell) const
{
    if (cell.isSelected)
        return PaletteDefaults::cursor.text;
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
    return audioProcessor.withAudioThreadExclusive([&]() -> bool
    {
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

        if (key.getModifiers().isCtrlDown())
        {
            const int keyCode = key.getKeyCode();
            if (keyCode == 'r' || keyCode == 'R')
            {
                DBG("Going for a reset");
                seqEditor->requestTrackerReset();
                audioProcessor.getSequencer()->requestStrUpdate();
                return true;
            }
#if JucePlugin_Build_Standalone
            if (keyCode == 'q' || keyCode == 'Q')
            {
                seqEditor->requestApplicationQuit();
                audioProcessor.getSequencer()->requestStrUpdate();
                return true;
            }
#endif
        }

        bool handled = false;
        const int keyCode = key.getKeyCode();
        const char ch = static_cast<char>(std::tolower(static_cast<unsigned char>(key.getTextCharacter())));
        const bool machineCapturesKeyboard = seqEditor->machineWantsExclusiveKeyboardInput();

        if (machineCapturesKeyboard && !key.getModifiers().isCtrlDown())
        {
            if (key.isKeyCode(juce::KeyPress::backspaceKey))
                return seqEditor->machineHandleTextBackspace();

            if (ch >= 32 && ch <= 126)
                return seqEditor->machineHandleTextInput(ch);
        }

        if (key.isKeyCode(juce::KeyPress::spaceKey))
        {
            seqEditor->togglePlayback();
            handled = true;
        }
        else if (keyCode == '4')
        {
            handled = seqEditor->enterMachineDetailFromAnywhere();
        }
        else if (keyCode >= '1' && keyCode <= '6')
        {
            handled = seqEditor->selectPageShortcut(keyCode - '0');
        }
        else if (seqEditor->handleChordKey(ch))
        {
            handled = true;
        }
        else if (seqEditor->handleNoteKey(ch))
        {
            handled = true;
        }
        else if (key.isKeyCode(juce::KeyPress::backspaceKey))
        {
            seqEditor->resetAtCursor();
            handled = true;
        }
        else if (key.isKeyCode(juce::KeyPress::escapeKey))
        {
            handled = seqEditor->dismissCurrentTransientUi();
        }
        else if (key.isKeyCode(juce::KeyPress::returnKey))
        {
            seqEditor->click();
            handled = true;
        }
        else if (key.isKeyCode(juce::KeyPress::upKey))
        {
            seqEditor->moveCursorUp();
            handled = true;
        }
        else if (key.isKeyCode(juce::KeyPress::pageUpKey))
        {
            if (seqEditor->getCurrentPage() == SequencerEditorPage::machine)
            {
                for (int i = 0; i < 6; ++i)
                    seqEditor->moveCursorUp();
                handled = true;
            }
        }
        else if (key.isKeyCode(juce::KeyPress::downKey))
        {
            seqEditor->moveCursorDown();
            handled = true;
        }
        else if (key.isKeyCode(juce::KeyPress::pageDownKey))
        {
            if (seqEditor->getCurrentPage() == SequencerEditorPage::machine)
            {
                for (int i = 0; i < 6; ++i)
                    seqEditor->moveCursorDown();
                handled = true;
            }
        }
        else if (key.isKeyCode(juce::KeyPress::leftKey))
        {
            seqEditor->moveCursorLeft();
            handled = true;
        }
        else if (key.isKeyCode(juce::KeyPress::rightKey))
        {
            seqEditor->moveCursorRight();
            handled = true;
        }
        else
        {
            switch (ch)
            {
                case 'q':
                    seqEditor->toggleMuteCurrentSequence();
                    handled = true;
                    break;
                case 'w':
                // toggle solo
                    // seqEditor->toggleMuteCurrentSequence();
                    handled = true;
                    break;
                    
                case 'e':
                    seqEditor->toggleArmCurrentSequence();
                    handled = true;
                    break;
                case 'r':
                    seqEditor->rewindTransport();
                    handled = true;
                    break;
                case '\t':
                    if (seqEditor->getCurrentPage() == SequencerEditorPage::machine && seqEditor->isEditingMachineDetail())
                        handled = seqEditor->cycleMachineDetailNext();
                    else
                    {
                        seqEditor->nextStep();
                        handled = true;
                    }
                    break;
                case '-':
                    seqEditor->removeRow();
                    handled = true;
                    break;
                case '=':
                    seqEditor->addRow();
                    handled = true;
                    break;
                case '_':
                {
                    const double bpm = audioProcessor.getBPM();
                    audioProcessor.setBPM(bpm <= 1.0 ? 1.0 : bpm - 1.0);
                    handled = true;
                    break;
                }
                case '+':
                {
                    audioProcessor.setBPM(audioProcessor.getBPM() + 1.0);
                    handled = true;
                    break;
                }
                case '[':
                    seqEditor->decrementAtCursor();
                    handled = true;
                    break;
                case ']':
                    seqEditor->incrementAtCursor();
                    handled = true;
                    break;
                case ',':
                    seqEditor->decrementOctave();
                    handled = true;
                    break;
                case '.':
                    seqEditor->incrementOctave();
                    handled = true;
                    break;
                default:
                    break;
            }
        }

        if (handled)
            audioProcessor.getSequencer()->requestStrUpdate();

        return handled;
    });
}

bool TrackerMainUI::keyStateChanged(bool isKeyDown, juce::Component* originatingComponent)
{
  juce::ignoreUnused(isKeyDown);
  juce::ignoreUnused(originatingComponent);
  return false; 
}
