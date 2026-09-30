/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin editor.

  ==============================================================================
*/

#include "TrackerMainProcessor.h"
#include "TrackerControlService.h"
#include "TrackerMainUI.h"
#include "SequencerCommands.h"
// #include "SimpleClock.h"
#include <algorithm>
#include <cmath>
#include <utility>

#if JucePlugin_Build_Standalone
#include "standalone/TrackerStandaloneHost.h"
#endif

namespace
{
#if JucePlugin_Build_Standalone
void showStandaloneAudioMidiSettings()
{
    juce::MessageManager::callAsync([]()
    {
        if (auto* holder = tracker::standalone::StandalonePluginHolder::getInstance())
            holder->showAudioSettingsDialog();
    });
}
#endif

std::string describeStackCursorAction(const std::string& cellText)
{
    if (cellText == "ADD") return "add";
    if (cellText == "DEL") return "delete";
    if (cellText == "UP") return "move up";
    if (cellText == "DOWN") return "move down";
    if (cellText == "ON") return "enabled";
    if (cellText == "OFF") return "disabled";
    if (cellText == "SAMPLER") return "sampler";
    if (cellText == "WAVE") return "wavetable synth";
    if (cellText == "DIST") return "distortion";
    if (cellText == "DELAY") return "delay";
    if (cellText == "CHSTR") return "channel strip";
    if (cellText == "MIDI") return "midi";
    if (cellText.empty()) return "stack";
    return juce::String(cellText).toLowerCase().toStdString();
}

std::string makeSequenceTitle(std::size_t sequenceIndex, std::size_t stepIndex, std::size_t stepCount, int bpmInt)
{
    return "Sequence [" + std::to_string(sequenceIndex + 1)
        + "-" + std::to_string(stepIndex + 1)
        + "/" + std::to_string(stepCount) + "] "
        + std::to_string(bpmInt) + " BPM";
}

std::string makeStepTitle(std::size_t sequenceIndex, std::size_t stepIndex, std::size_t stepCount)
{
    return "Step [" + std::to_string(sequenceIndex + 1)
        + "-" + std::to_string(stepIndex + 1)
        + "/" + std::to_string(stepCount) + "]";
}

std::vector<std::vector<std::string>> gridFromSnapshot(const juce::var& value)
{
    std::vector<std::vector<std::string>> grid;
    if (!value.isArray()) return grid;
    grid.reserve(static_cast<size_t>(value.getArray()->size()));
    for (const auto& sourceColumn : *value.getArray())
    {
        std::vector<std::string> column;
        if (sourceColumn.isArray())
            for (const auto& cell : *sourceColumn.getArray()) column.push_back(cell.toString().toStdString());
        grid.push_back(std::move(column));
    }
    return grid;
}

juce::Colour getSeqConfigRowColour(std::size_t row, const TrackerPalette& p)
{
    switch (row)
    {
        case Sequence::sendConfig:
            return p.seqConfigRouting;
        case Sequence::headCountConfig:
        case Sequence::headConfig:
            return p.seqConfigTopology;
        case Sequence::tpsConfig:
        case Sequence::modeConfig:
        case Sequence::rhythmConfig:
            return p.seqConfigTiming;
        case Sequence::polyphonyConfig:
        case Sequence::probabilityConfig:
        default:
            return p.seqConfigVariation;
    }
}

juce::Colour getSeqConfigCellFill(std::size_t row,
                                  std::size_t col,
                                  std::size_t cursorRow,
                                  std::size_t cursorCol,
                                  bool disabled,
                                  const TrackerPalette& p)
{
    juce::Colour fill = getSeqConfigRowColour(row, p);
    if (row == cursorRow)
        fill = fill.interpolatedWith(p.gridNote, 0.16f);
    if (col == cursorCol)
        fill = fill.interpolatedWith(juce::Colour(0xFF2990FF), 0.14f);
    if (disabled)
        fill = fill.darker(0.45f);
    return fill;
}

juce::Colour getSeqConfigCellText(std::size_t row,
                                  std::size_t col,
                                  std::size_t cursorRow,
                                  std::size_t cursorCol,
                                  bool disabled,
                                  const TrackerPalette& p)
{
    if (disabled)
        return p.seqConfigTextDisabled;
    if (row == cursorRow || col == cursorCol)
        return p.gridNote;
    return p.seqConfigTextMuted;
}

std::string makeSequenceConfigTitle(std::size_t sequenceIndex,
                                    std::size_t paramIndex,
                                    std::size_t configHead,
                                    std::size_t headCount,
                                    const std::vector<Parameter>& seqConfigSpecs,
                                    std::size_t startCol,
                                    std::size_t visibleCols,
                                    std::size_t totalSequences)
{
    const auto param = paramIndex < seqConfigSpecs.size()
        ? seqConfigSpecs[paramIndex].shortName
        : "?";
    const auto viewEnd = std::min(totalSequences, startCol + visibleCols);
    return "Sequence Config [" + std::to_string(sequenceIndex + 1) + "] "
        + param
        + " HEAD " + std::to_string(configHead + 1) + "/" + std::to_string(headCount)
        + " VIEW " + std::to_string(startCol + 1) + "-" + std::to_string(viewEnd)
        + "/" + std::to_string(totalSequences);
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
  currentView = audioProcessor.getControlService().getViewSnapshot();
  for (const auto& zoomCommand : audioProcessor.consumePendingZoomCommands())
  {
      adjustZoomAroundPoint(zoomCommand.delta,
                            { zoomCommand.normalizedX, zoomCommand.normalizedY });
  }
  prepareControlPanelView();  
  // check what to draw based on the state of the 
  // editor
  SequencerEditorMode editMode = SequencerEditorMode::selectingSeqAndStep;
  const auto snapshotUi = currentView.state.getProperty("ui", juce::var());
  const auto snapshotMode = snapshotUi.getProperty("mode", "sequence").toString();
  if (snapshotMode == "song") editMode = SequencerEditorMode::arrangingSong;
  else if (snapshotMode == "step") editMode = SequencerEditorMode::editingStep;
  else if (snapshotMode == "config") editMode = SequencerEditorMode::configuringSequence;
  else if (snapshotMode == "machine") editMode = SequencerEditorMode::machineConfig;
  else if (snapshotMode == "mixer") editMode = SequencerEditorMode::mixer;
  else if (snapshotMode == "reset") editMode = SequencerEditorMode::resetConfirmation;
  switch(editMode){

      case SequencerEditorMode::arrangingSong:
      {
          prepareSongView();
          break;
      }

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
      case SequencerEditorMode::mixer:
      {
          prepareMixerView();
          break;
      }
      case SequencerEditorMode::resetConfirmation:
      {
          prepareResetConfirmationView();
          break;
      }
  }
    if (editMode != SequencerEditorMode::machineConfig
        && editMode != SequencerEditorMode::arrangingSong
        && editMode != SequencerEditorMode::resetConfirmation)
    {
      const int bpmInt = static_cast<int>(std::lround(audioProcessor.getBPM()));
      std::string hudTitle;
      audioProcessor.withAudioThreadExclusive([&]()
      {
          const auto sequenceIndex = seqEditor->getCurrentSequence();
          const auto stepIndex = seqEditor->getCurrentStep();
          const auto stepCount = audioProcessor.getSequencer()->howManySteps(sequenceIndex);
          if (editMode == SequencerEditorMode::selectingSeqAndStep)
              hudTitle = makeSequenceTitle(sequenceIndex, stepIndex, stepCount, bpmInt);
          else if (editMode == SequencerEditorMode::editingStep)
              hudTitle = makeStepTitle(sequenceIndex, stepIndex, stepCount);
          else if (editMode == SequencerEditorMode::configuringSequence)
          {
              auto* sequencer = audioProcessor.getSequencer();
              auto* sequence = sequencer->getSequence(sequenceIndex);
              const auto headCount = sequence != nullptr ? sequence->getReadHeadCount() : 1u;
              hudTitle = makeSequenceConfigTitle(sequenceIndex,
                                                 seqEditor->getCurrentSeqParam(),
                                                 seqEditor->getCurrentConfigHead(),
                                                 headCount,
                                                 sequencer->getSeqConfigSpecs(),
                                                 startCol,
                                                 visibleCols,
                                                 sequencer->howManySequences());
          }
      });

      if (overlayState.text != hudTitle)
          overlayState.text = hudTitle;
      lastHudBpm = bpmInt;
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
  struct HeadCell { int sequence; int step; int head; };
  std::vector<HeadCell> headCells;
  std::vector<std::vector<std::string>> grid;
  size_t currentSequence = 0;
  size_t currentStep = 0;
  size_t armedSequence = SequencerAbs::notArmed;
  bool isPlaying = false;
  const auto ui = currentView.state.getProperty("ui", juce::var());
  currentSequence = static_cast<size_t>(static_cast<int>(ui.getProperty("currentSequence", 0)));
  currentStep = static_cast<size_t>(static_cast<int>(ui.getProperty("currentStep", 0)));
  armedSequence = static_cast<size_t>(static_cast<int>(ui.getProperty("armedSequence", static_cast<int>(SequencerAbs::notArmed))));
  isPlaying = static_cast<bool>(ui.getProperty("isPlaying", false));
  grid = gridFromSnapshot(ui.getProperty("sequenceGrid", juce::var()));
  const auto snapshotPlayheads = ui.getProperty("playHeads", juce::var());
  if (snapshotPlayheads.isArray())
      for (const auto& item : *snapshotPlayheads.getArray())
      {
          const int sequence = static_cast<int>(item.getProperty("sequence", 0));
          const int step = static_cast<int>(item.getProperty("step", 0));
          playHeads.emplace_back(sequence, step);
          headCells.push_back({ sequence, step, static_cast<int>(item.getProperty("head", 0)) });
      }
  const auto selection = ui.getProperty("sequenceSelection", juce::var());
  if (selection.getDynamicObject() != nullptr
      && static_cast<std::size_t>(static_cast<int>(selection.getProperty("sequence", -1))) == currentSequence)
  {
      const int start = juce::jmax(0, static_cast<int>(selection.getProperty("startStep", 0)));
      const int end = juce::jmax(start, static_cast<int>(selection.getProperty("endStep", start)));
      for (int step = start; step <= end; ++step)
          playHeads.emplace_back(static_cast<int>(currentSequence), step);
  }
  style.glowPulseEnabled = !isPlaying;
  auto boxes = buildBoxesFromGrid(grid,
                                        currentSequence,
                                        currentStep,
                                        playHeads,
                                        true,
                                        armedSequence);
  const std::array<juce::Colour, 3> headColours { juce::Colours::red, juce::Colours::cyan, juce::Colours::magenta };
  for (const auto& headCell : headCells)
  {
      if (headCell.sequence < 0 || headCell.step < 0
          || static_cast<std::size_t>(headCell.sequence) >= boxes.size()
          || static_cast<std::size_t>(headCell.step) >= boxes[static_cast<std::size_t>(headCell.sequence)].size())
          continue;
      auto& box = boxes[static_cast<std::size_t>(headCell.sequence)][static_cast<std::size_t>(headCell.step)];
      auto colour = headColours[static_cast<std::size_t>(juce::jlimit(0, 2, headCell.head))];
      if (box.useCustomFillColour) colour = juce::Colour(box.customFillArgb).interpolatedWith(colour, 0.5f);
      box.useCustomFillColour = true;
      box.customFillArgb = colour.getARGB();
  }
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
    const auto ui = currentView.state.getProperty("ui", juce::var());
    currentSequence = static_cast<size_t>(static_cast<int>(ui.getProperty("currentSequence", 0)));
    currentStep = static_cast<size_t>(static_cast<int>(ui.getProperty("currentStep", 0)));
    currentStepCol = static_cast<size_t>(static_cast<int>(ui.getProperty("currentStepCol", 0)));
    currentStepRow = static_cast<size_t>(static_cast<int>(ui.getProperty("currentStepRow", 0)));
    isPlaying = static_cast<bool>(ui.getProperty("isPlaying", false));
    grid = gridFromSnapshot(ui.getProperty("stepGrid", juce::var()));
    const auto snapshotPlayheads = ui.getProperty("playHeads", juce::var());
    if (snapshotPlayheads.isArray())
        for (const auto& item : *snapshotPlayheads.getArray())
            if (static_cast<size_t>(static_cast<int>(item.getProperty("sequence", -1))) == currentSequence
                && static_cast<size_t>(static_cast<int>(item.getProperty("step", -1))) == currentStep)
                for (size_t col = 0; col < grid.size(); ++col) playHeads.emplace_back(static_cast<int>(col), 0);
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
        grid = audioProcessor.getSequencer()->getSequenceConfigsAsGridOfStrings(seqEditor->getCurrentConfigHead());
    });

    std::vector<std::vector<UIBox>> boxes(grid.size(), std::vector<UIBox>(Sequence::configCount));

    for (std::size_t col = 0; col < grid.size(); ++col)
    {
        for (std::size_t row = 0; row < std::min(Sequence::configCount, grid[col].size()); ++row)
        {
            auto& box = boxes[col][row];
            box.kind = UIBox::Kind::TrackerCell;
            box.text = grid[col][row];
            // Config cells are form fields, not note cells: keep them flat and
            // outline-free while using row/column tints for orientation.
            box.hasNote = false;
            if (row == Sequence::polyphonyConfig && grid[col][Sequence::modeConfig] != "MODE:" + std::string(Sequence::readModeName(SequenceReadMode::randomChord)))
                box.isDisabled = true;

            box.useCustomFillColour = true;
            box.customFillArgb = getSeqConfigCellFill(row,
                                                      col,
                                                      currentSeqParam,
                                                      currentSequence,
                                                      box.isDisabled,
                                                      palette).getARGB();
            box.useCustomTextColour = true;
            box.customTextArgb = getSeqConfigCellText(row,
                                                      col,
                                                      currentSeqParam,
                                                      currentSequence,
                                                      box.isDisabled,
                                                      palette).getARGB();
        }
    }

    if (!boxes.empty() && currentSequence < boxes.size() && currentSeqParam < boxes[currentSequence].size())
        boxes[currentSequence][currentSeqParam].isSelected = true;

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
    std::string selectedStackAction;
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
        for (const auto& column : machineBoxes)
            for (const auto& cell : column)
                if (cell.isSelected)
                    selectedStackAction = describeStackCursorAction(cell.text);
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
        overlayState.text = "Stack [" + std::to_string(machineId) + "] machine [sampler]";
        overlayState.color = samplerPalette.textPrimary;
        overlayState.glowColor = samplerPalette.glowActive;
        overlayState.glowStrength = 0.35f;
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
        overlayState.text = "Stack [" + std::to_string(machineId) + "] machine [wavetable synth]";
        overlayState.color = palette.textPrimary;
        overlayState.glowColor = palette.gridPlayhead;
        overlayState.glowStrength = 0.25f;
        return;
    }
    if (detailType.has_value() && detailType.value() == CommandType::DistortionFx)
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
        overlayState.text = "Stack [" + std::to_string(machineId) + "] machine [distortion]";
        overlayState.color = palette.textPrimary;
        overlayState.glowColor = palette.gridPlayhead;
        overlayState.glowStrength = 0.25f;
        return;
    }
    if (detailType.has_value() && detailType.value() == CommandType::DelayFx)
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
        overlayState.text = "Stack [" + std::to_string(machineId) + "] machine [delay]";
        overlayState.color = palette.textPrimary;
        overlayState.glowColor = palette.gridPlayhead;
        overlayState.glowStrength = 0.25f;
        return;
    }
    if (detailType.has_value() && detailType.value() == CommandType::ChannelStripFx)
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
        overlayState.text = "Stack [" + std::to_string(machineId) + "] machine [channel strip]";
        overlayState.color = palette.textPrimary;
        overlayState.glowColor = palette.gridPlayhead;
        overlayState.glowStrength = 0.25f;
        return;
    }
    if (detailType.has_value() && (detailType.value() == CommandType::AuxSend1Fx || detailType.value() == CommandType::AuxSend2Fx))
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
        overlayState.text = detailType.value() == CommandType::AuxSend1Fx
            ? "Shared aux [1] reverb"
            : "Shared aux [2] reverb";
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
        overlayState.text = "Stack [" + std::to_string(machineId) + "] machine [midi]";
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
    overlayState.text = "Stack [" + std::to_string(machineId) + "] [" + selectedStackAction + "]";
}

void TrackerMainUI::prepareMixerView()
{
    samplerViewActive = false;
    customMachineColumnWidthsActive = false;
    samplerColumnWidths.clear();
    constexpr std::size_t rows = 11;
    const auto stackCount = audioProcessor.getMachineStackCount();
    std::vector<bool> muted(stackCount), solo(stackCount);
    std::vector<float> meters(stackCount), gains(stackCount);
    audioProcessor.withAudioThreadExclusive([&]
    {
        for (std::size_t stack = 0; stack < stackCount; ++stack)
        {
            muted[stack] = audioProcessor.isStackMuted(stack);
            solo[stack] = audioProcessor.isStackSolo(stack);
            meters[stack] = audioProcessor.getStackMeterLevel(stack);
            gains[stack] = audioProcessor.getStackGainDb(stack);
        }
    });
    std::vector<std::vector<UIBox>> boxes(stackCount, std::vector<UIBox>(rows));
    for (std::size_t stack = 0; stack < stackCount; ++stack)
    {
        boxes[stack][0].text = "STK:" + std::to_string(stack);
        boxes[stack][1].text = muted[stack] ? "MUTE:ON" : "MUTE:OFF";
        boxes[stack][2].text = solo[stack] ? "SOLO:ON" : "SOLO:OFF";
        const float meter = meters[stack];
        const float gain = gains[stack];
        const int lit = juce::jlimit(0, 8, static_cast<int>(std::ceil(meter * 8.0f)));
        const int handle = juce::jlimit(0, 7, static_cast<int>(std::lround((gain + 48.0f) / 54.0f * 7.0f)));
        for (std::size_t row = 0; row < rows; ++row) boxes[stack][row].kind = UIBox::Kind::TrackerCell;
        for (int band = 0; band < 8; ++band)
        {
            auto& cell = boxes[stack][3 + static_cast<std::size_t>(7 - band)];
            if (band < lit) cell.isHighlighted = true;
            if (band == handle) cell.text = "====";
        }
    }
    const auto stack = seqEditor->getMixerStack();
    const auto row = seqEditor->getMixerRow();
    if (stack < boxes.size() && row < rows) boxes[stack][row].isSelected = true;
    updateCellStates(boxes, rows, stackCount);
}

void TrackerMainUI::prepareControlPanelView()
{
    // std::vector<std::vector<std::string>> grid = trackerController->getControlPanelAsGridOfStrings();
    // controlPanelTable.updateData(grid, 
    //     1, 12, 
    //     0, 0, // todo - pull these from the editor which keeps track of this 
    //     std::vector<std::pair<int, int>>(), false); 
}

void TrackerMainUI::prepareSongView()
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

    std::size_t rowCount = 1;
    std::size_t currentSongRow = 0;
    std::size_t currentSongCol = 0;
    std::size_t playbackSongRow = 0;
    SongPlayMode playMode = SongPlayMode::sequence;
    audioProcessor.withAudioThreadExclusive([&]()
    {
        rowCount += audioProcessor.getSongRowCount();
        currentSongRow = seqEditor->getCurrentSongRow();
        currentSongCol = seqEditor->getCurrentSongCol();
        playbackSongRow = audioProcessor.getCurrentPlaybackSongRow();
        playMode = audioProcessor.getSongPlayMode();
    });

    std::vector<std::vector<UIBox>> boxes(4, std::vector<UIBox>(rowCount));

    boxes[0][0].kind = UIBox::Kind::TrackerCell;
    boxes[0][0].text = "PLAY SONG";
    boxes[0][0].isHighlighted = playMode == SongPlayMode::song;
    boxes[1][0].kind = UIBox::Kind::TrackerCell;
    boxes[1][0].text = "PLAY SEQ";
    boxes[1][0].isHighlighted = playMode == SongPlayMode::sequence;
    boxes[2][0].kind = UIBox::Kind::None;
    boxes[2][0].isDisabled = true;
    boxes[3][0].kind = UIBox::Kind::None;
    boxes[3][0].isDisabled = true;

    audioProcessor.withAudioThreadExclusive([&]()
    {
        for (std::size_t row = 0; row < audioProcessor.getSongRowCount(); ++row)
        {
            const std::size_t displayRow = row + 1;
            boxes[0][displayRow].kind = UIBox::Kind::TrackerCell;
            boxes[0][displayRow].text = "SET " + std::to_string(audioProcessor.getSongRowSequenceSetId(row) + 1);
            boxes[1][displayRow].kind = UIBox::Kind::TrackerCell;
            boxes[1][displayRow].text = "BEAT " + std::to_string(audioProcessor.getSongRowBeatCount(row));
            boxes[2][displayRow].kind = UIBox::Kind::TrackerCell;
            boxes[2][displayRow].text = "EDIT";
            boxes[3][displayRow].kind = UIBox::Kind::TrackerCell;
            boxes[3][displayRow].text = "DEL";

            if (playMode == SongPlayMode::song && row == playbackSongRow)
            {
                boxes[0][displayRow].isHighlighted = true;
                boxes[1][displayRow].isHighlighted = true;
                boxes[2][displayRow].isHighlighted = true;
                boxes[3][displayRow].isHighlighted = true;
            }
        }
    });

    currentSongRow = std::min(currentSongRow, rowCount - 1);
    currentSongCol = std::min(currentSongCol, static_cast<std::size_t>(currentSongRow == 0 ? 1 : 3));
    boxes[currentSongCol][currentSongRow].isSelected = true;

    updateCellStates(boxes, rowCount, 4);
    overlayState.text = "Song";
    overlayState.color = palette.textPrimary;
    overlayState.glowColor = palette.gridPlayhead;
    overlayState.glowStrength = 0.25f;
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
    if (key.getModifiers().isCtrlDown())
    {
        const auto keyCode = key.getKeyCode();
        if (keyCode == 's' || keyCode == 'S')
        {
            chooseStateSaveDirectory();
            return true;
        }
        if (keyCode == 'l' || keyCode == 'L')
        {
            chooseStateFileToLoad();
            return true;
        }
    }
    auto arguments = juce::var(new juce::DynamicObject());
    arguments.getDynamicObject()->setProperty("action", "key");
    arguments.getDynamicObject()->setProperty("keyCode", key.getKeyCode());
    arguments.getDynamicObject()->setProperty("text", juce::String::charToString(key.getTextCharacter()));
    arguments.getDynamicObject()->setProperty("ctrl", key.getModifiers().isCtrlDown());
    arguments.getDynamicObject()->setProperty("shift", key.getModifiers().isShiftDown());
    TrackerControlService::Command command;
    command.kind = TrackerControlService::CommandKind::uiAction;
    command.arguments = arguments;
    return audioProcessor.getControlService().execute(command).ok;
#if 0 // Kept temporarily as a behaviour reference while the view migration lands.
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

            if (keyCode == 'p' || keyCode == 'P')
            {
                showStandaloneAudioMidiSettings();
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
        else if (keyCode == '5')
        {
            handled = seqEditor->enterMachineDetailFromAnywhere();
        }
        else if (keyCode >= '1' && keyCode <= '7')
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
            handled = seqEditor->machineHandleTextBackspace();
            if (!handled)
            {
                seqEditor->resetAtCursor();
                handled = true;
            }
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
#endif
}

juce::File TrackerMainUI::initialStateDirectory() const
{
    if (lastStateDirectory.isDirectory())
        return lastStateDirectory;

    const auto documents = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory);
    return documents.isDirectory() ? documents : juce::File::getSpecialLocation(juce::File::userHomeDirectory);
}

void TrackerMainUI::showStateFileError(const juce::String& message) const
{
    juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                                           "MYK Tracker state",
                                           message,
                                           "OK");
}

void TrackerMainUI::chooseStateSaveDirectory()
{
    if (stateFileChooser != nullptr)
        return;

    stateFileChooser = std::make_unique<juce::FileChooser>("Choose a folder for the tracker state",
                                                             initialStateDirectory());
    juce::Component::SafePointer<TrackerMainUI> safeThis(this);
    stateFileChooser->launchAsync(juce::FileBrowserComponent::openMode
                                      | juce::FileBrowserComponent::canSelectDirectories,
        [safeThis] (const juce::FileChooser& chooser)
        {
            if (safeThis == nullptr)
                return;

            const auto directory = chooser.getResult();
            safeThis->stateFileChooser.reset();
            if (!directory.isDirectory())
                return; // User cancelled the directory browser.

            safeThis->lastStateDirectory = directory;
            const auto timestamp = juce::Time::getCurrentTime().formatted("%Y-%m-%d_%H-%M-%S");
            const auto file = directory.getNonexistentChildFile("MYK-Tracker-" + timestamp, ".myktracker");
            juce::MemoryBlock state;
            safeThis->audioProcessor.getStateInformation(state);
            if (state.getSize() == 0 || !file.replaceWithData(state.getData(), state.getSize()))
            {
                safeThis->showStateFileError("Could not save the tracker state to:\n" + file.getFullPathName());
                return;
            }

            juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::InfoIcon,
                                                   "MYK Tracker state saved",
                                                   file.getFullPathName(),
                                                   "OK");
        });
}

void TrackerMainUI::chooseStateFileToLoad()
{
    if (stateFileChooser != nullptr)
        return;

    stateFileChooser = std::make_unique<juce::FileChooser>("Load tracker state",
                                                             initialStateDirectory(),
                                                             "*.myktracker");
    juce::Component::SafePointer<TrackerMainUI> safeThis(this);
    stateFileChooser->launchAsync(juce::FileBrowserComponent::openMode
                                      | juce::FileBrowserComponent::canSelectFiles,
        [safeThis] (const juce::FileChooser& chooser)
        {
            if (safeThis == nullptr)
                return;

            const auto file = chooser.getResult();
            safeThis->stateFileChooser.reset();
            if (!file.existsAsFile())
                return; // User cancelled the file browser.

            juce::MemoryBlock state;
            if (!file.loadFileAsData(state) || state.getSize() == 0)
            {
                safeThis->showStateFileError("Could not read tracker state from:\n" + file.getFullPathName());
                return;
            }

            safeThis->audioProcessor.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
            safeThis->lastStateDirectory = file.getParentDirectory();
            juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::InfoIcon,
                                                   "MYK Tracker state loaded",
                                                   file.getFullPathName(),
                                                   "OK");
        });
}

bool TrackerMainUI::keyStateChanged(bool isKeyDown, juce::Component* originatingComponent)
{
  juce::ignoreUnused(isKeyDown);
  juce::ignoreUnused(originatingComponent);
  return false; 
}
