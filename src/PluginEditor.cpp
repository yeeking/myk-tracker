/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin editor.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "SequencerCommands.h"
// #include "SimpleClock.h"
#include <algorithm>
#include <cmath>

namespace
{
std::string sanitizeLabel(const juce::String& input, size_t maxLen)
{
    auto trimmed = input.trim();
    if (trimmed.isEmpty())
        return {};

    auto upper = trimmed.toUpperCase();
    std::string out;
    out.reserve(static_cast<size_t>(upper.length()));
    for (auto ch : upper)
    {
        if (out.size() >= maxLen)
            break;
        if (ch == '\n' || ch == '\r')
            continue;
        if (ch < 32 || ch > 126)
            continue;
        out.push_back(static_cast<char>(ch));
    }
    return out;
}

std::string formatGain(float gain)
{
    juce::String text = juce::String(gain, 2);
    return sanitizeLabel(text, 6);
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
    uiComponent(openGLContext),
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

    samplerPalette.background = juce::Colour(0xFF03060B);
    samplerPalette.cellIdle = juce::Colour(0xFF141A22);
    samplerPalette.cellSelected = juce::Colour(0xFF00E8FF);
    samplerPalette.cellAccent = juce::Colour(0xFF1E2F3D);
    samplerPalette.cellDisabled = juce::Colour(0xFF0C1118);
    samplerPalette.textPrimary = juce::Colour(0xFF4EF2C2);
    samplerPalette.textMuted = juce::Colour(0xFF6B7C8F);
    samplerPalette.glowActive = juce::Colour(0xFFFF5533);
    samplerPalette.lightColor = juce::Colour(0xFFEAF6FF);
    samplerPalette.ambientStrength = 0.32f;
    samplerPalette.lightDirection = { 0.2f, 0.45f, 1.0f };

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

PluginEditor::~PluginEditor()
{
  stopTimer();
  openGLContext.detach();
}

void PluginEditor::newOpenGLContextCreated()
{
    uiComponent.initOpenGL(getWidth(), getHeight());
}

void PluginEditor::renderOpenGL()
{
    uiComponent.setViewportBounds(seqViewBounds, getHeight(), openGLContext.getRenderingScale());
    uiComponent.renderUI();
    waitingForPaint = false;
}

void PluginEditor::openGLContextClosing()
{
    uiComponent.shutdownOpenGL();
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
    // controlPanelTable.setBounds(0, 0, 0, 0);
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
      if (bpmInt != lastHudBpm)
      {
          overlayState.text = "@BPM " + std::to_string(bpmInt);
          lastHudBpm = bpmInt;
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



void PluginEditor::prepareSequenceView()
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
    updateCellStates(grid,
                     rowsInUI - 1,
                     6,
                     seqEditor->getCurrentSequence(),
                     seqEditor->getCurrentSeqParam(),
                     std::vector<std::pair<int, int>>(),
                     true,
                     Sequencer::notArmed);
}

void PluginEditor::prepareMachineConfigView()
{
    samplerViewActive = false;
    samplerColumnWidths.clear();

    const auto* sequence = sequencer->getSequence(seqEditor->getCurrentSequence());
    const auto machineType = static_cast<CommandType>(static_cast<std::size_t>(sequence->getMachineType()));
    const int machineId = static_cast<int>(sequence->getMachineId());

    if (machineType == CommandType::Sampler)
    {
        samplerViewActive = true;
        activeSamplerIndex = audioProcessor.getSamplerCount() > 0
            ? static_cast<std::size_t>(machineId) % audioProcessor.getSamplerCount()
            : 0;
        samplerColumnWidths = { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 2.0f };

        TrackerUIComponent::Style style;
        style.background = samplerPalette.background;
        style.lightColor = samplerPalette.lightColor;
        style.defaultGlowColor = samplerPalette.glowActive;
        style.ambientStrength = samplerPalette.ambientStrength;
        style.lightDirection = samplerPalette.lightDirection;
        uiComponent.setStyle(style);
        uiComponent.setCellSize(1.2f, 1.1f);

        const auto samplerState = audioProcessor.getSamplerState(activeSamplerIndex);
        if (samplerState.isObject())
            refreshSamplerFromState(samplerState);
        else
        {
            samplerPlayers.clear();
            rebuildSamplerCells();
        }
        overlayState.text = "SAMPLER ID " + std::to_string(machineId);
        overlayState.color = samplerPalette.textPrimary;
        overlayState.glowColor = samplerPalette.glowActive;
        overlayState.glowStrength = 0.35f;
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
        overlayState.text = "CHANNEL " + std::to_string(machineId);
    else if (machineType == CommandType::Log)
        overlayState.text = "CHECK CONSOLE";
    else
        overlayState.text = "MACHINE";
}

void PluginEditor::prepareControlPanelView()
{
    // std::vector<std::vector<std::string>> grid = trackerController->getControlPanelAsGridOfStrings();
    // controlPanelTable.updateData(grid, 
    //     1, 12, 
    //     0, 0, // todo - pull these from the editor which keeps track of this 
    //     std::vector<std::pair<int, int>>(), false); 
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

            const float previousGlow = reuseGlow ? playheadGlow[displayCol][displayRow] : 0.0f;
            // decay by a constant 
            // const float glowValue = isHighlighted ? 1.0f : std::max(0.0f, previousGlow - glowDecayStep);
            // decay using a scalar 
            const float glowValue = isHighlighted ? 1.0f : std::max(0.0f, previousGlow * glowDecayScalar);

            CellVisualFlags flags;
            flags.hasNote = hasNote;
            flags.isActivePlayhead = isHighlighted;
            flags.isSelected = isSelected;
            flags.isArmed = isArmed;

            auto cell = makeDefaultCell();
            cell.text = cellValue;
            cell.fillColor = getCellColour(flags);
            cell.textColor = getTextColour(flags);
            cell.glowColor = palette.gridPlayhead;
            cell.glow = glowValue;
            cell.depthScale = getCellDepthScale(flags);
            cell.drawOutline = hasNote;
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

TrackerUIComponent::CellState PluginEditor::makeDefaultCell() const
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
juce::Colour PluginEditor::getCellColour(const CellVisualFlags& cell) const
{
    if (cell.isSelected && cell.hasNote)
        return juce::Colours::red.withAlpha(0.6f);
    if (cell.isSelected)
        return palette.gridSelected;
    if (cell.isArmed)
        return palette.statusOk;

    return palette.gridEmpty;
}

juce::Colour PluginEditor::getTextColour(const CellVisualFlags& cell) const
{
    if (cell.isSelected)
        return palette.gridSelected;
    if (cell.isArmed)
        return palette.statusOk;
    if (cell.hasNote)
        return palette.gridNote;
    return palette.textPrimary;
}

float PluginEditor::getCellDepthScale(const CellVisualFlags& cell) const
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

void PluginEditor::refreshSamplerFromState(const juce::var& payload)
{
    if (! payload.isObject())
        return;

    const auto playersVar = payload.getProperty("players", juce::var());
    if (! playersVar.isArray())
        return;

    const auto* playersArray = playersVar.getArray();
    if (playersArray == nullptr)
        return;

    std::vector<SamplerPlayerState> nextPlayers;
    nextPlayers.reserve(static_cast<std::size_t>(playersArray->size()));
    for (const auto& entry : *playersArray)
    {
        auto* playerObj = entry.getDynamicObject();
        if (playerObj == nullptr)
            continue;

        SamplerPlayerState st;
        st.id = static_cast<int>(playerObj->getProperty("id"));
        st.midiLow = static_cast<int>(playerObj->getProperty("midiLow"));
        st.midiHigh = static_cast<int>(playerObj->getProperty("midiHigh"));
        st.gain = static_cast<float>(double(playerObj->getProperty("gain")));
        st.isPlaying = static_cast<bool>(playerObj->getProperty("isPlaying"));
        st.status = playerObj->getProperty("status").toString();
        st.fileName = playerObj->getProperty("fileName").toString();
        nextPlayers.push_back(st);
    }

    samplerPlayers = std::move(nextPlayers);
    rebuildSamplerCells();
}

void PluginEditor::rebuildSamplerCells()
{
    const size_t rows = samplerPlayers.size() + 1;
    const size_t cols = 6;

    if (rows == 0 || cols == 0)
    {
        cellStates.assign(1, std::vector<TrackerUIComponent::CellState>(1, makeDefaultCell()));
        return;
    }

    samplerCursorRow = std::min(samplerCursorRow, rows - 1);
    samplerCursorCol = std::min(samplerCursorCol, cols - 1);
    if (samplerCursorRow == 0)
        samplerCursorCol = 0;

    samplerCellVisualStates.assign(cols, std::vector<SamplerCellVisualState>(rows));
    samplerCellInfo.assign(cols, std::vector<SamplerCellInfo>(rows));
    cellStates.assign(cols, std::vector<TrackerUIComponent::CellState>(rows));

    for (size_t col = 0; col < cols; ++col)
    {
        for (size_t row = 0; row < rows; ++row)
        {
            SamplerCellInfo info{};
            std::string text;
            if (row == 0)
            {
                if (col == 0)
                {
                    info.action = SamplerAction::Add;
                    info.playerIndex = -1;
                    text = "ADD";
                }
                else
                {
                    info.action = SamplerAction::None;
                }
            }
            else
            {
                const auto& player = samplerPlayers[row - 1];
                info.playerIndex = static_cast<int>(row - 1);
                switch (col)
                {
                    case 0:
                        info.action = SamplerAction::Load;
                        text = "LOAD";
                        break;
                    case 1:
                        info.action = SamplerAction::Trigger;
                        text = player.isPlaying ? "PLAY" : "TRIG";
                        break;
                    case 2:
                        info.action = SamplerAction::Low;
                        text = sanitizeLabel(juce::String(player.midiLow), 4);
                        break;
                    case 3:
                        info.action = SamplerAction::High;
                        text = sanitizeLabel(juce::String(player.midiHigh), 4);
                        break;
                    case 4:
                        info.action = SamplerAction::Gain;
                        text = formatGain(player.gain);
                        break;
                    case 5:
                        info.action = SamplerAction::Waveform;
                        text = sanitizeLabel(player.fileName.isNotEmpty() ? player.fileName : player.status, 18);
                        break;
                    default:
                        info.action = SamplerAction::None;
                        break;
                }
            }

            samplerCellInfo[col][row] = info;
            auto& visual = samplerCellVisualStates[col][row];
            visual.isSelected = (row == samplerCursorRow && col == samplerCursorCol);
            visual.isEditing = (samplerEditMode && visual.isSelected);
            visual.isActive = (info.action == SamplerAction::Trigger && row > 0 && samplerPlayers[row - 1].isPlaying);
            visual.isDisabled = (info.action == SamplerAction::None);
            visual.glow = visual.isActive ? 1.0f : 0.0f;

            TrackerUIComponent::CellState cell;
            cell.text = text;
            cell.fillColor = getSamplerCellColour(visual, info);
            cell.textColor = getSamplerTextColour(visual, info);
            cell.glowColor = samplerPalette.glowActive;
            cell.glow = visual.glow;
            cell.depthScale = getSamplerCellDepthScale(visual);
            cell.drawOutline = visual.isSelected;
            cell.outlineColor = samplerPalette.cellSelected;
            cellStates[col][row] = cell;
        }
    }
}

void PluginEditor::handleSamplerAction(const SamplerCellInfo& info)
{
    if (info.action == SamplerAction::None)
        return;

    if (info.action == SamplerAction::Add)
    {
        audioProcessor.samplerAddPlayer(activeSamplerIndex);
        return;
    }

    if (info.playerIndex < 0 || info.playerIndex >= static_cast<int>(samplerPlayers.size()))
        return;

    const int playerId = samplerPlayers[static_cast<size_t>(info.playerIndex)].id;
    switch (info.action)
    {
        case SamplerAction::Load:
            audioProcessor.samplerRequestLoad(activeSamplerIndex, playerId);
            break;
        case SamplerAction::Trigger:
            audioProcessor.samplerTrigger(activeSamplerIndex, playerId);
            break;
        case SamplerAction::Low:
        case SamplerAction::High:
        case SamplerAction::Gain:
            samplerEditMode = true;
            samplerEditAction = info.action;
            samplerEditPlayerIndex = info.playerIndex;
            break;
        default:
            break;
    }
}

void PluginEditor::adjustSamplerEditValue(int direction)
{
    if (samplerEditPlayerIndex < 0 || samplerEditPlayerIndex >= static_cast<int>(samplerPlayers.size()))
        return;

    const auto player = samplerPlayers[static_cast<size_t>(samplerEditPlayerIndex)];
    if (samplerEditAction == SamplerAction::Low)
    {
        const int low = juce::jlimit(0, 127, player.midiLow + direction);
        audioProcessor.samplerSetRange(activeSamplerIndex, player.id, low, player.midiHigh);
    }
    else if (samplerEditAction == SamplerAction::High)
    {
        const int high = juce::jlimit(0, 127, player.midiHigh + direction);
        audioProcessor.samplerSetRange(activeSamplerIndex, player.id, player.midiLow, high);
    }
    else if (samplerEditAction == SamplerAction::Gain)
    {
        const float gain = juce::jlimit(0.0f, 2.0f, player.gain + direction * 0.05f);
        audioProcessor.samplerSetGain(activeSamplerIndex, player.id, gain);
    }
}

void PluginEditor::moveSamplerCursor(int deltaRow, int deltaCol)
{
    if (cellStates.empty() || cellStates[0].empty())
        return;

    const int maxRow = static_cast<int>(cellStates[0].size()) - 1;
    const int maxCol = static_cast<int>(cellStates.size()) - 1;

    int nextRow = juce::jlimit(0, maxRow, static_cast<int>(samplerCursorRow) + deltaRow);
    int nextCol = juce::jlimit(0, maxCol, static_cast<int>(samplerCursorCol) + deltaCol);

    if (nextRow == 0)
        nextCol = 0;

    samplerCursorRow = static_cast<size_t>(nextRow);
    samplerCursorCol = static_cast<size_t>(nextCol);
    rebuildSamplerCells();
}

juce::Colour PluginEditor::getSamplerCellColour(const SamplerCellVisualState& cell, const SamplerCellInfo& info) const
{
    if (cell.isDisabled)
        return samplerPalette.cellDisabled;
    if (cell.isEditing)
        return samplerPalette.cellSelected;
    if (cell.isSelected)
        return samplerPalette.cellSelected;
    if (cell.isActive)
        return samplerPalette.cellAccent;
    if (info.action == SamplerAction::Waveform)
        return samplerPalette.cellIdle.brighter(0.2f);
    return samplerPalette.cellIdle;
}

juce::Colour PluginEditor::getSamplerTextColour(const SamplerCellVisualState& cell, const SamplerCellInfo& info) const
{
    if (cell.isSelected)
        return samplerPalette.cellSelected;
    if (cell.isActive)
        return samplerPalette.glowActive;
    if (info.action == SamplerAction::Waveform)
        return samplerPalette.textMuted;
    return samplerPalette.textPrimary;
}

float PluginEditor::getSamplerCellDepthScale(const SamplerCellVisualState& cell) const
{
    if (cell.isEditing)
        return 1.05f;
    if (cell.isSelected)
        return 1.02f;
    return 1.0f;
}


bool PluginEditor::keyPressed(const juce::KeyPress& key, juce::Component* originatingComponent)
{
    if (seqEditor->getEditMode() == SequencerEditorMode::machineConfig)
    {
        if (key.isKeyCode(juce::KeyPress::returnKey))
        {
            samplerEditMode = false;
            samplerEditAction = SamplerAction::None;
            samplerEditPlayerIndex = -1;
            seqEditor->enterAtCursor();
            return true;
        }

        const auto* sequence = sequencer->getSequence(seqEditor->getCurrentSequence());
        const auto machineType = static_cast<CommandType>(static_cast<std::size_t>(sequence->getMachineType()));
        if (machineType == CommandType::Sampler)
        {
            if (samplerEditMode)
            {
                if (key == juce::KeyPress::escapeKey)
                {
                    samplerEditMode = false;
                    samplerEditAction = SamplerAction::None;
                    samplerEditPlayerIndex = -1;
                    return true;
                }
                if (key.getKeyCode() == juce::KeyPress::leftKey || key.getKeyCode() == juce::KeyPress::downKey)
                {
                    adjustSamplerEditValue(-1);
                    return true;
                }
                if (key.getKeyCode() == juce::KeyPress::rightKey || key.getKeyCode() == juce::KeyPress::upKey)
                {
                    adjustSamplerEditValue(1);
                    return true;
                }
                return true;
            }

            const char samplerKey = static_cast<char>(key.getTextCharacter());
            if (samplerKey == '=')
            {
                audioProcessor.samplerAddPlayer(activeSamplerIndex);
                return true;
            }
            if (samplerKey == '-')
            {
                if (! samplerPlayers.empty())
                {
                    size_t playerIndex = samplerPlayers.size() - 1;
                    if (samplerCursorRow > 0)
                    {
                        const size_t candidate = samplerCursorRow - 1;
                        if (candidate < samplerPlayers.size())
                            playerIndex = candidate;
                    }
                    audioProcessor.samplerRemovePlayer(activeSamplerIndex, samplerPlayers[playerIndex].id);
                }
                return true;
            }

            if (key.getKeyCode() == juce::KeyPress::leftKey)
            {
                moveSamplerCursor(0, -1);
                return true;
            }
            if (key.getKeyCode() == juce::KeyPress::rightKey)
            {
                moveSamplerCursor(0, 1);
                return true;
            }
            if (key.getKeyCode() == juce::KeyPress::upKey)
            {
                moveSamplerCursor(-1, 0);
                return true;
            }
            if (key.getKeyCode() == juce::KeyPress::downKey)
            {
                moveSamplerCursor(1, 0);
                return true;
            }
            if (key.getKeyCode() == juce::KeyPress::spaceKey)
            {
                if (samplerCursorCol < samplerCellInfo.size() && samplerCursorRow < samplerCellInfo[samplerCursorCol].size())
                    handleSamplerAction(samplerCellInfo[samplerCursorCol][samplerCursorRow]);
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
            samplerEditMode = false;
            samplerEditAction = SamplerAction::None;
            samplerEditPlayerIndex = -1;
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

bool PluginEditor::keyStateChanged(bool isKeyDown, juce::Component* originatingComponent)
{
  return false; 
}
