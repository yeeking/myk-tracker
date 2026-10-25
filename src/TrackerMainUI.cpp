/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin editor.

  ==============================================================================
*/

#include "TrackerMainProcessor.h"
#include "TrackerControlService.h"
#include "TrackerMainUI.h"
#include "SequencerCommands.h"
#include "machines/CurvedAdsr.h"
#include "machines/ModulationMatrixMachine.h"
#include <algorithm>
#include <cmath>
#include <utility>

#if JucePlugin_Build_Standalone
#include "standalone/TrackerStandaloneHost.h"
#endif

namespace
{
#if JucePlugin_Build_Standalone
[[maybe_unused]] void showStandaloneAudioMidiSettings()
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
    if (cellText.empty()) return "stack";
    for (std::size_t typeValue = 0; typeValue < kCommandTypeCount; ++typeValue)
    {
        const auto traits = machineTraits(static_cast<CommandType>(typeValue));
        if (cellText == traits.shortLabel)
            return traits.longName;
    }
    return juce::String(cellText).toLowerCase().toStdString();
}

bool isSimpleMachineDetail(CommandType type)
{
    return type == CommandType::DrumSynth
        || type == CommandType::DistortionFx
        || type == CommandType::DelayFx
        || type == CommandType::FilterFx
        || type == CommandType::ChannelStripFx
        || type == CommandType::AuxSend1Fx
        || type == CommandType::AuxSend2Fx
        || type == CommandType::MidiNote;
}

std::string compactPageLabel(SequencerEditorMode mode)
{
    switch (mode)
    {
        case SequencerEditorMode::arrangingSong: return "SONG";
        case SequencerEditorMode::selectingSeqAndStep: return "SEQ";
        case SequencerEditorMode::editingStep: return "STEP";
        case SequencerEditorMode::configuringSequence: return "CONF";
        case SequencerEditorMode::machineConfig: return "MACH";
        case SequencerEditorMode::mixer: return "MIX";
        case SequencerEditorMode::audioConfig: return "AUD";
        case SequencerEditorMode::help: return "HELP";
        case SequencerEditorMode::resetConfirmation: return "CONF";
    }
    return "MYK";
}

int indexedSnapshotValue(const juce::var& values, int index, int fallback)
{
    if (index < 0 || !values.isArray())
        return fallback;

    const auto* array = values.getArray();
    if (array == nullptr || index >= array->size())
        return fallback;

    return static_cast<int>(array->getReference(index));
}

std::string formatSignedGain(float gainDb)
{
    const int roundedGain = static_cast<int>(std::lround(gainDb));
    return (roundedGain >= 0 ? "+" : "") + std::to_string(roundedGain);
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
        case Sequence::sequenceModeConfig:
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

}

//==============================================================================
TrackerMainUI::TrackerMainUI (TrackerMainProcessor& p)
    : AudioProcessorEditor (&p),
    framesDrawn{0},
    audioProcessor (p),
    seqEditor{p.getSequenceEditor()},
    uiComponent(openGLContext),
    rowsInUI{9},
    waitingForPaint{false}
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

    // Benchmark-only runtime override.  Normal launches retain the 25 FPS
    // update cadence; invalid values are clamped rather than allowing a busy timer.
    const int requestedFps = juce::SystemStats::getEnvironmentVariable("MYK_TRACKER_UI_FPS", "25").getIntValue();
    const int uiFps = juce::jlimit(1, 120, requestedFps > 0 ? requestedFps : 25);
    const auto reportPath = juce::SystemStats::getEnvironmentVariable("MYK_TRACKER_UI_PERF_REPORT", {});
    if (reportPath.isNotEmpty())
    {
        uiBenchmarkEnabled = true;
        uiBenchmarkReportFile = juce::File(reportPath);
    }
    startTimer(juce::jmax(1, 1000 / uiFps));

}

std::string TrackerMainUI::makeRightHudText(SequencerEditorMode mode)
{
    const auto ui = currentView.state.getProperty("ui", juce::var());
    const int sequenceIndex = juce::jmax(0, static_cast<int>(ui.getProperty("currentSequence", 0)));
    const int stepIndex = juce::jmax(0, static_cast<int>(ui.getProperty("currentStep", 0)));
    // The per-sequence routing list is authoritative for the Sequence cursor.
    // The generic machineId can briefly belong to a different detail context.
    const int machineId = juce::jmax(0, indexedSnapshotValue(
        ui.getProperty("sequenceMachineIds", juce::var()),
        sequenceIndex,
        static_cast<int>(ui.getProperty("machineId", 0))));
    const int sequenceLength = juce::jmax(0, indexedSnapshotValue(
        ui.getProperty("sequenceLengths", juce::var()), sequenceIndex, 0));

    const auto sequenceAndStep = [&]()
    {
        return std::to_string(sequenceIndex + 1) + "->" + std::to_string(machineId)
            + "\n" + std::to_string(stepIndex + 1) + "/" + std::to_string(sequenceLength);
    };
    const auto appendEntryStatus = [&ui](std::string text)
    {
        const auto label = ui.getProperty("chordHudLabel", "").toString().toStdString();
        const auto variation = static_cast<int>(ui.getProperty("chordHudVariation", 0));
        const auto count = static_cast<int>(ui.getProperty("chordHudVariationCount", 0));
        const auto expiry = static_cast<double>(ui.getProperty("chordHudExpiryMs", 0.0));
        if (!label.empty() && variation >= 0 && count > 0
            && juce::Time::getMillisecondCounterHiRes() < expiry)
            text += "\n" + label + " " + std::to_string(variation + 1) + "/" + std::to_string(count);

        const auto octaveLabel = ui.getProperty("octaveHudLabel", "").toString().toStdString();
        const auto octaveExpiry = static_cast<double>(ui.getProperty("octaveHudExpiryMs", 0.0));
        if (!octaveLabel.empty() && juce::Time::getMillisecondCounterHiRes() < octaveExpiry)
            text += "\n" + octaveLabel;
        return text;
    };

    switch (mode)
    {
        case SequencerEditorMode::selectingSeqAndStep:
            return appendEntryStatus("SEQ\n" + sequenceAndStep());

        case SequencerEditorMode::editingStep:
            return appendEntryStatus("STEP\n" + std::to_string(sequenceIndex + 1)
                + "\n" + std::to_string(stepIndex + 1) + "/" + std::to_string(sequenceLength));

        case SequencerEditorMode::configuringSequence:
            return appendEntryStatus("CONF\n" + std::to_string(sequenceIndex + 1) + "->" + std::to_string(machineId));

        case SequencerEditorMode::machineConfig:
            break;

        case SequencerEditorMode::arrangingSong:
        case SequencerEditorMode::mixer:
        case SequencerEditorMode::audioConfig:
        case SequencerEditorMode::help:
        case SequencerEditorMode::resetConfirmation:
            return appendEntryStatus(compactPageLabel(mode));
    }

    int selectedMachineId = machineId;
    std::optional<CommandType> detailType;
    std::optional<std::size_t> detailSlot;
    bool detailEnabled = false;
    audioProcessor.withAudioThreadExclusive([&]()
    {
        if (auto* sequencer = audioProcessor.getSequencer())
            if (auto* sequence = sequencer->getSequence(seqEditor->getCurrentSequence()))
                selectedMachineId = juce::jmax(0, static_cast<int>(sequence->getMachineId()));

        if (!seqEditor->isEditingMachineDetail())
            return;

        detailType = seqEditor->getFocusedMachineDetailType();
        detailSlot = seqEditor->getFocusedMachineDetailSlot();
        if (!detailType.has_value())
            return;

        const auto stackIndex = static_cast<std::size_t>(selectedMachineId);
        detailEnabled = detailType.value() == CommandType::ModulationMatrix
            ? audioProcessor.isModulationEnabledInStack(stackIndex)
            : detailSlot.has_value() && audioProcessor.isMachineEnabledInStack(stackIndex, detailSlot.value());
    });

    if (!detailType.has_value())
        return appendEntryStatus("MACH\n" + std::to_string(selectedMachineId));

    return appendEntryStatus("MACH\n" + std::to_string(selectedMachineId)
        + "\n" + machineTraits(detailType.value()).shortLabel
        + "\n" + (detailEnabled ? "ON" : "OFF"));
}

TrackerMainUI::~TrackerMainUI()
{
  stopTimer();
  if (portableSaveFuture.valid())
      portableSaveFuture.wait();
  writeUiBenchmarkReport();
  {
    const juce::ScopedLock lock(screenshotLock);
    if (pendingScreenshot != nullptr)
    {
      *pendingScreenshot->error = "ui_unavailable";
      pendingScreenshot->completed->signal();
      pendingScreenshot.reset();
    }
  }
  openGLContext.detach();
}

void TrackerMainUI::newOpenGLContextCreated()
{
    uiComponent.initOpenGL(getWidth(), getHeight());
}

void TrackerMainUI::renderOpenGL()
{
    const auto started = uiBenchmarkEnabled ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point {};
    uiComponent.setViewportBounds(seqViewBounds,
                                  getHeight(),
                                  static_cast<float>(openGLContext.getRenderingScale()));
    uiComponent.renderUI();
    waitingForPaint = false;

    std::shared_ptr<TrackerMainProcessor::UiScreenshotRequest> request;
    {
        const juce::ScopedLock lock(screenshotLock);
        request = pendingScreenshot;
        pendingScreenshot.reset();
    }
    if (request != nullptr)
        captureScreenshotFrame(request);
    if (uiBenchmarkEnabled)
    {
        const auto elapsed = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - started).count());
        recordUiBenchmarkSample(uiRenderTotalNs, uiRenderMaxNs, uiRenderCount, elapsed);
    }
}

juce::String TrackerMainUI::requestScreenshot(std::shared_ptr<TrackerMainProcessor::UiScreenshotRequest> request)
{
    {
        const juce::ScopedLock lock(screenshotLock);
        if (pendingScreenshot != nullptr)
        {
            *request->error = "screenshot_busy";
            request->completed->signal();
            return "screenshot_busy";
        }
        pendingScreenshot = std::move(request);
    }
    openGLContext.triggerRepaint();
    return {};
}

void TrackerMainUI::captureScreenshotFrame(const std::shared_ptr<TrackerMainProcessor::UiScreenshotRequest>& request)
{
    const auto fail = [request](const juce::String& code)
    {
        *request->error = code;
        request->completed->signal();
    };

    int viewport[4] { 0, 0, 0, 0 };
    juce::gl::glGetIntegerv(juce::gl::GL_VIEWPORT, viewport);
    const int width = viewport[2];
    const int height = viewport[3];
    if (width <= 0 || height <= 0)
    {
        fail("screenshot_empty");
        return;
    }

    std::vector<unsigned char> pixels(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u);
    juce::gl::glReadPixels(0, 0, width, height, juce::gl::GL_RGBA, juce::gl::GL_UNSIGNED_BYTE, pixels.data());
    if (juce::gl::glGetError() != juce::gl::GL_NO_ERROR)
    {
        fail("screenshot_failed");
        return;
    }

    juce::Image image(juce::Image::ARGB, width, height, true);
    for (int y = 0; y < height; ++y)
    {
        const auto* sourceRow = pixels.data() + (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) * 4u);
        for (int x = 0; x < width; ++x)
        {
            const auto* pixel = sourceRow + (static_cast<std::size_t>(x) * 4u);
            image.setPixelAt(x, height - 1 - y,
                             juce::Colour::fromRGBA(static_cast<juce::uint8>(pixel[0]),
                                                    static_cast<juce::uint8>(pixel[1]),
                                                    static_cast<juce::uint8>(pixel[2]),
                                                    static_cast<juce::uint8>(pixel[3])));
        }
    }

    *request->image = image;
    request->completed->signal();
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
    const auto started = uiBenchmarkEnabled ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point {};
    if (!isShowing())
        return;
    pollPortableSave();
    ++framesDrawn;

    if (waitingForPaint) {return;}// already waiting for a repaint
  currentView = audioProcessor.getControlService().getViewSnapshotForPage();
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
  else if (snapshotMode == "audio") editMode = SequencerEditorMode::audioConfig;
  else if (snapshotMode == "help") editMode = SequencerEditorMode::help;
  else if (snapshotMode == "reset") editMode = SequencerEditorMode::resetConfirmation;
  const bool isPlaying = static_cast<bool>(snapshotUi.getProperty("isPlaying", false));

  // Parameter-change pulses are meaningful only within the same logical
  // interface. Reusing a previous page's normalised values made every scaled
  // control look newly changed (and therefore glow red) on page entry.
  valuePulseContext = snapshotMode.toStdString();
  if (stateDialogMode != StateDialogMode::none)
  {
      valuePulseContext = "state-dialog/" + std::to_string(static_cast<int>(stateDialogMode));
  }
  else if (editMode == SequencerEditorMode::machineConfig)
  {
      valuePulseContext += "/sequence/" + std::to_string(seqEditor->getCurrentSequence());
      if (seqEditor->isEditingMachineDetail())
      {
          if (const auto detailType = seqEditor->getFocusedMachineDetailType())
              valuePulseContext += "/detail/" + std::to_string(static_cast<int>(*detailType));
          if (const auto detailSlot = seqEditor->getFocusedMachineDetailSlot())
              valuePulseContext += "/slot/" + std::to_string(*detailSlot);
      }
      else
      {
          valuePulseContext += "/stack";
      }
  }
  if (editMode != SequencerEditorMode::configuringSequence)
  {
      seqConfigTriggerCounts.clear();
      seqConfigTriggerFlash.clear();
      seqConfigTriggerBaselineValid = false;
      seqConfigTriggerSetIndex = std::size_t(-1);
  }
  // Pages with scope or waveform previews repopulate their traces each frame.
  currentTraces.clear();
  if (stateDialogMode != StateDialogMode::none)
  {
      prepareStateDialogView();
  }
  else switch(editMode){

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
      case SequencerEditorMode::audioConfig:
      {
          prepareAudioConfigView();
          break;
      }
      case SequencerEditorMode::help:
      {
          prepareHelpView();
          break;
      }
      case SequencerEditorMode::resetConfirmation:
      {
          prepareResetConfirmationView();
          break;
      }
  }
  // All pages share the same glow animation. Keep it alive while editing at
  // rest, but render a steady glow during playback so meters and active cells
  // do not throb in time with the UI animation.
  uiComponent.setGlowPulseEnabled(!isPlaying);
  overlayState.rightStrip = stateDialogMode == StateDialogMode::none
      && editMode != SequencerEditorMode::resetConfirmation;
  if (overlayState.rightStrip)
      overlayState.text = makeRightHudText(editMode);

  overlayState.color = palette.textPrimary;
  overlayState.glowColor = palette.gridPlayhead;
  overlayState.glowStrength = 0.35f;

  TrackerUIComponent::ZoomState zoomState;
  zoomState.zoomLevel = zoomLevel;
  TrackerUIComponent::DragState dragState;
  dragState.panX = panOffsetX;
  dragState.panY = panOffsetY;
  uiComponent.setTraces(currentTraces);
  uiComponent.updateUIState(cellStates,
                            overlayState,
                            zoomState,
                            dragState,
                            customMachineColumnWidthsActive ? &samplerColumnWidths : nullptr);

  waitingForPaint = true;
  // Edits raise the sequencer's string-update flag directly, so this
  // periodic request is only a safety net for out-of-band changes.
  if (framesDrawn % 60 == 0){
    audioProcessor.withAudioThreadExclusive([&]()
    {
        audioProcessor.getSequencer()->requestStrUpdate();
    });
  }
  openGLContext.triggerRepaint();
  if (uiBenchmarkEnabled)
  {
      const auto elapsed = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - started).count());
      const auto index = static_cast<std::size_t>(editMode);
      if (index < uiPageCount.size())
          recordUiBenchmarkSample(uiPageTotalNs[index], uiPageMaxNs[index], uiPageCount[index], elapsed);
  }
}

void TrackerMainUI::recordUiBenchmarkSample(std::atomic<std::uint64_t>& total,
                                            std::atomic<std::uint64_t>& maximum,
                                            std::atomic<std::uint64_t>& count,
                                            std::uint64_t elapsedNs) noexcept
{
    total.fetch_add(elapsedNs, std::memory_order_relaxed);
    count.fetch_add(1, std::memory_order_relaxed);
    auto current = maximum.load(std::memory_order_relaxed);
    while (current < elapsedNs && !maximum.compare_exchange_weak(current, elapsedNs, std::memory_order_relaxed)) {}
}

void TrackerMainUI::writeUiBenchmarkReport()
{
    if (!uiBenchmarkEnabled || uiBenchmarkReportFile == juce::File())
        return;
    static constexpr const char* names[] = { "song", "sequence", "config", "step", "machine", "mixer", "audio", "help", "reset" };
    juce::String text("{\n  \"pages\": {\n");
    for (std::size_t index = 0; index < uiPageCount.size(); ++index)
    {
        const auto count = uiPageCount[index].load(std::memory_order_relaxed);
        const auto total = uiPageTotalNs[index].load(std::memory_order_relaxed);
        const auto maximum = uiPageMaxNs[index].load(std::memory_order_relaxed);
        text << "    \"" << names[index] << "\": {\"frames\": " << static_cast<juce::int64>(count)
             << ", \"meanUs\": " << (count == 0 ? 0.0 : static_cast<double>(total) / static_cast<double>(count) / 1000.0)
             << ", \"maxUs\": " << static_cast<double>(maximum) / 1000.0 << "}" << (index + 1 == uiPageCount.size() ? "\n" : ",\n");
    }
    const auto renderCount = uiRenderCount.load(std::memory_order_relaxed);
    text << "  },\n  \"render\": {\"frames\": " << static_cast<juce::int64>(renderCount)
         << ", \"meanUs\": " << (renderCount == 0 ? 0.0 : static_cast<double>(uiRenderTotalNs.load(std::memory_order_relaxed)) / static_cast<double>(renderCount) / 1000.0)
         << ", \"maxUs\": " << static_cast<double>(uiRenderMaxNs.load(std::memory_order_relaxed)) / 1000.0 << "}\n}\n";
    uiBenchmarkReportFile.getParentDirectory().createDirectory();
    uiBenchmarkReportFile.replaceWithText(text);
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
  bool sequenceEntryArmed = false;
  const auto ui = currentView.state.getProperty("ui", juce::var());
  currentSequence = static_cast<size_t>(static_cast<int>(ui.getProperty("currentSequence", 0)));
  currentStep = static_cast<size_t>(static_cast<int>(ui.getProperty("currentStep", 0)));
  sequenceEntryArmed = static_cast<bool>(ui.getProperty("sequenceEntryArmed", false));
  grid = gridFromSnapshot(ui.getProperty("sequenceGrid", juce::var()));
  const auto stackAudibleStates = ui.getProperty("sequenceStackAudibleStates", juce::var());
  if (stackAudibleStates.isArray())
  {
      const auto& audibleArray = *stackAudibleStates.getArray();
      for (int sequenceIndex = 0; sequenceIndex < audibleArray.size()
           && static_cast<std::size_t>(sequenceIndex) < grid.size(); ++sequenceIndex)
      {
          if (static_cast<bool>(audibleArray[sequenceIndex]))
              continue;
          for (auto& cellText : grid[static_cast<std::size_t>(sequenceIndex)])
              cellText.clear();
      }
  }
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
  auto boxes = buildBoxesFromGrid(grid,
                                        currentSequence,
                                        currentStep,
                                        playHeads,
                                        true,
                                        sequenceEntryArmed);
  const auto controlModes = ui.getProperty("sequenceControlModes", juce::var());
  if (controlModes.isArray())
  {
      const auto& controlModeArray = *controlModes.getArray();
      for (int sequenceIndex = 0; sequenceIndex < controlModeArray.size()
           && static_cast<std::size_t>(sequenceIndex) < boxes.size(); ++sequenceIndex)
      {
          if (!static_cast<bool>(controlModeArray[sequenceIndex]))
              continue;
          for (auto& cell : boxes[static_cast<std::size_t>(sequenceIndex)])
              cell.isControlModeSequence = true;
      }
  }
  const auto soloStates = ui.getProperty(
      "sequenceEffectiveSoloStates",
      ui.getProperty("sequenceSoloStates", juce::var()));
  if (soloStates.isArray())
  {
      const auto& soloStateArray = *soloStates.getArray();
      for (int sequenceIndex = 0; sequenceIndex < soloStateArray.size()
           && static_cast<std::size_t>(sequenceIndex) < boxes.size(); ++sequenceIndex)
      {
          if (!static_cast<bool>(soloStateArray[sequenceIndex]))
              continue;
          for (auto& cell : boxes[static_cast<std::size_t>(sequenceIndex)])
              cell.isSoloChannel = true;
      }
  }
  const auto noteLengthModes = ui.getProperty("sequenceNoteLengthModes", juce::var());
  if (noteLengthModes.isArray())
      for (int i = 0; i < noteLengthModes.getArray()->size() && static_cast<std::size_t>(i) < boxes.size(); ++i)
          for (auto& cell : boxes[static_cast<std::size_t>(i)])
              cell.noteLengthMode = juce::jlimit(0, 2, static_cast<int>(noteLengthModes.getArray()->getReference(i)));
  std::vector<int> sequenceMachineIds;
  const auto machineIds = ui.getProperty("sequenceMachineIds", juce::var());
  if (machineIds.isArray())
  {
      sequenceMachineIds.reserve(static_cast<std::size_t>(machineIds.getArray()->size()));
      for (const auto& machineId : *machineIds.getArray())
          sequenceMachineIds.push_back(static_cast<int>(machineId));
  }
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

  // The sequence page reserves a fixed, non-interactive header cell above its
  // scrolling step window. Copy the column presentation state so solo/mute
  // outlines continue to read as one continuous sequence column.
  std::vector<UIBox> scopeHeaders(boxes.size());
  for (std::size_t sequenceIndex = 0; sequenceIndex < boxes.size(); ++sequenceIndex)
  {
      auto& header = scopeHeaders[sequenceIndex];
      header.kind = UIBox::Kind::TrackerCell;
      header.isDisabled = true;
      header.useCustomFillColour = true;
      header.customFillArgb = 0xFF0A0F14;
      if (!boxes[sequenceIndex].empty())
      {
          const auto& source = boxes[sequenceIndex].front();
          header.isControlModeSequence = source.isControlModeSequence;
          header.isSoloChannel = source.isSoloChannel;
          header.isMutedChannel = source.isMutedChannel;
          header.noteLengthMode = source.noteLengthMode;
      }
  }
  updateCellStates(boxes, rowsInUI, 6, &scopeHeaders);

  // Only visible sequence columns need a scope. A shared stack is copied and
  // calibrated once, then deliberately drawn in each routed sequence header.
  std::vector<int> visibleStackIds(visibleCols, -1);
  for (std::size_t displayCol = 0; displayCol < visibleCols; ++displayCol)
  {
      const auto sequenceIndex = startCol + displayCol;
      if (sequenceIndex < sequenceMachineIds.size())
          visibleStackIds[displayCol] = sequenceMachineIds[sequenceIndex];
  }
  const auto uniqueStackIndices = sequence_scope::collectUniqueStackIndices(
      visibleStackIds, audioProcessor.getMachineStackCount());
  if (!uniqueStackIndices.empty())
  {
      audioProcessor.copyStackScopes(uniqueStackIndices, sequenceScopeRawBuffers);
      sequenceScopePreviewBuffers.resize(uniqueStackIndices.size());
      for (std::size_t stack = 0; stack < uniqueStackIndices.size(); ++stack)
      {
          const float scale = updateScopeCalibration(uniqueStackIndices[stack], sequenceScopeRawBuffers[stack]);
          auto& preview = sequenceScopePreviewBuffers[stack];
          sequence_scope::reducePeakPreserving(sequenceScopeRawBuffers[stack], preview);
          for (auto& sample : preview)
              sample *= scale;
      }

      currentTraces.reserve(currentTraces.size() + visibleCols);
      for (std::size_t displayCol = 0; displayCol < visibleStackIds.size(); ++displayCol)
      {
          const int stackId = visibleStackIds[displayCol];
          if (stackId < 0)
              continue;
          const auto found = std::find(uniqueStackIndices.begin(), uniqueStackIndices.end(),
                                       static_cast<std::size_t>(stackId));
          if (found == uniqueStackIndices.end())
              continue;

          TrackerUIComponent::Trace scope;
          scope.colStart = static_cast<float>(displayCol);
          scope.colEnd = static_cast<float>(displayCol + 1);
          scope.rowTop = 0.0f;
          scope.rowBottom = 1.0f;
          scope.samples = sequenceScopePreviewBuffers[static_cast<std::size_t>(found - uniqueStackIndices.begin())];
          scope.color = juce::Colour(0xFF29E0FF);
          scope.z = 1.15f;
          scope.lineWidth = 2.0f;
          currentTraces.push_back(std::move(scope));
      }
  }
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
    std::vector<std::pair<int, int>> playHeads;
    std::vector<std::vector<std::string>> grid;
    size_t currentSequence = 0;
    size_t currentStep = 0;
    size_t currentStepCol = 0;
    size_t currentStepRow = 0;
    const auto ui = currentView.state.getProperty("ui", juce::var());
    currentSequence = static_cast<size_t>(static_cast<int>(ui.getProperty("currentSequence", 0)));
    currentStep = static_cast<size_t>(static_cast<int>(ui.getProperty("currentStep", 0)));
    currentStepCol = static_cast<size_t>(static_cast<int>(ui.getProperty("currentStepCol", 0)));
    currentStepRow = static_cast<size_t>(static_cast<int>(ui.getProperty("currentStepRow", 0)));
    grid = gridFromSnapshot(ui.getProperty("stepGrid", juce::var()));
    const auto snapshotPlayheads = ui.getProperty("playHeads", juce::var());
    if (snapshotPlayheads.isArray())
        for (const auto& item : *snapshotPlayheads.getArray())
            if (static_cast<size_t>(static_cast<int>(item.getProperty("sequence", -1))) == currentSequence
                && static_cast<size_t>(static_cast<int>(item.getProperty("step", -1))) == currentStep)
                for (size_t col = 0; col < grid.size(); ++col) playHeads.emplace_back(static_cast<int>(col), 0);
    const auto boxes = buildBoxesFromGrid(grid,
                                          currentStepCol,
                                          currentStepRow,
                                          playHeads,
                                          true,
                                          false);
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
    std::vector<std::uint64_t> triggerCounts;
    size_t currentSequence = 0;
    size_t currentSeqParam = 0;
    std::size_t sequenceSetIndex = 0;
    audioProcessor.withAudioThreadExclusive([&]()
    {
        currentSequence = seqEditor->getCurrentSequence();
        currentSeqParam = seqEditor->getCurrentSeqParam();
        sequenceSetIndex = audioProcessor.getViewedSequenceSetIndex();
        auto* sequencer = audioProcessor.getSequencer();
        grid = gridFromSnapshot(currentView.state.getProperty("ui", {}).getProperty("sequenceConfigs", {}));
        if (sequencer == nullptr)
        {
            grid.clear();
            triggerCounts.clear();
            return;
        }
        // Host state may have been restored since currentView was captured.
        // Never index the new sequence set using a stale grid's column count.
        if (grid.size() != sequencer->howManySequences())
            grid = sequencer->getSequenceConfigsAsGridOfStrings(seqEditor->getCurrentConfigHead());
        triggerCounts.resize(grid.size());
        const auto sequenceCount = std::min(grid.size(), sequencer->howManySequences());
        for (std::size_t col = 0; col < sequenceCount; ++col)
            if (auto* sequence = sequencer->getSequence(col))
                triggerCounts[col] = sequence->getTriggerEventCount();
    });

    const auto previousTriggerCounts = seqConfigTriggerCounts;
    const bool establishTriggerBaseline = !seqConfigTriggerBaselineValid
        || seqConfigTriggerSetIndex != sequenceSetIndex
        || previousTriggerCounts.size() != grid.size();
    if (establishTriggerBaseline)
    {
        seqConfigTriggerFlash.assign(grid.size(), 0.0f);
        seqConfigTriggerCounts = triggerCounts;
        seqConfigTriggerBaselineValid = true;
        seqConfigTriggerSetIndex = sequenceSetIndex;
    }
    else
    {
        if (seqConfigTriggerFlash.size() != grid.size())
            seqConfigTriggerFlash.assign(grid.size(), 0.0f);
        for (std::size_t col = 0; col < grid.size(); ++col)
        {
            seqConfigTriggerFlash[col] *= 0.80f;
            // Only an increase represents new trigger events; resets, stop, and
            // sequence-set switches must not produce a visual flash.
            if (triggerCounts[col] > previousTriggerCounts[col])
                seqConfigTriggerFlash[col] = 1.0f;
            seqConfigTriggerCounts[col] = triggerCounts[col];
        }
    }

    std::vector<std::vector<UIBox>> boxes(grid.size(), std::vector<UIBox>(Sequence::configCount));

    for (std::size_t col = 0; col < grid.size(); ++col)
    {
        const float triggerFlash = col < seqConfigTriggerFlash.size()
            ? seqConfigTriggerFlash[col]
            : 0.0f;
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

            juce::Colour fill = getSeqConfigCellFill(row,
                                                      col,
                                                      currentSeqParam,
                                                      currentSequence,
                                                      box.isDisabled,
                                                      palette);
            if (triggerFlash > 0.005f)
                fill = fill.interpolatedWith(juce::Colours::white, triggerFlash * 0.10f);
            box.useCustomFillColour = true;
            box.customFillArgb = fill.getARGB();
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
    std::optional<CommandType> detailType;
    std::string selectedStackAction;
    std::size_t scopeStackIndex = 0;
    bool needsScope = true;
    audioProcessor.withAudioThreadExclusive([&]()
    {
        auto* seq = audioProcessor.getSequencer();
        if (auto* sequence = seq->getSequence(seqEditor->getCurrentSequence()))
        {
            machineId = static_cast<int>(sequence->getMachineId());
        }
        scopeStackIndex = seqEditor->getActiveMachineIndex(CommandType::Sampler);
        detailType = seqEditor->getFocusedMachineDetailType();
        needsScope = !detailType.has_value() || detailType.value() != CommandType::Sampler;
        const auto& machineCells = seqEditor->getMachineCells();
        for (const auto& column : machineCells)
            for (const auto& cell : column)
                if (cell.isSelected)
                    selectedStackAction = describeStackCursorAction(cell.text);
    });
    // copyStackScope takes the audio lock itself, so snapshot outside the block above.
    std::vector<float> scopeSamples;
    if (needsScope)
        audioProcessor.copyStackScope(scopeStackIndex, scopeSamples);

    if (detailType.has_value() && detailType.value() == CommandType::Sampler)
    {
        const auto& machineBoxes = seqEditor->getMachineCells();
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
        if (!hasCustomWidths && samplerColumnWidths.size() == 10)
            samplerColumnWidths = { 1.0f, 2.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 2.0f };

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
        // Keep the sampler at the same compact height as the tracker pages.
        // updateCellStates follows the selected machine cell, so cursor
        // navigation scrolls long samplesets through this fixed row window.
        constexpr std::size_t kSamplerVisibleRows = 9;
        updateCellStates(machineBoxes,
                         browserActive ? std::min<std::size_t>(rows, 8)
                                       : std::min<std::size_t>(rows, kSamplerVisibleRows),
                         cols);
        overlayState.text = "Stack [" + std::to_string(machineId) + "] machine [sampler]";
        overlayState.color = samplerPalette.textPrimary;
        overlayState.glowColor = samplerPalette.glowActive;
        overlayState.glowStrength = 0.35f;
        return;
    }
    if (detailType.has_value() && detailType.value() == CommandType::WavetableSynth)
    {
        auto machineBoxes = seqEditor->getMachineCells();
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
        appendScopeBand(machineBoxes, rows, scopeStackIndex, scopeSamples);
        updateCellStates(machineBoxes, rows + 2, cols);

        // WAVE previews: each source row shows one cycle of its base waveform,
        // and the ENV columns carry the ADSR outline.
        if (auto* synth = dynamic_cast<WavetableSynthMachine*>(
                audioProcessor.getMachine(CommandType::WavetableSynth, static_cast<std::size_t>(machineId))))
        {
            const auto previewState = synth->getUiPreviewState();
            auto& previewCache = wavetablePreviewCaches[machineId];
            if (!previewCache.valid || previewCache.state != previewState)
            {
                previewCache.valid = true;
                previewCache.state = previewState;
                ++previewCache.revision;
                previewCache.traces.clear();
                previewCache.traces.reserve(static_cast<std::size_t>(
                    previewState.waveStepCount + previewState.subWaveStepCount + 1));

                constexpr int kGlyphSamples = 32;
                for (int step = 0; step < previewState.waveStepCount; ++step)
                {
                    const float rowTop = static_cast<float>(step + 5);
                    if (rowTop + 1.0f > static_cast<float>(rows))
                        break;
                    TrackerUIComponent::Trace glyph;
                    glyph.colStart = 0.0f;
                    glyph.colEnd = 1.0f;
                    glyph.rowTop = rowTop;
                    glyph.rowBottom = rowTop + 1.0f;
                    glyph.samples.resize(static_cast<std::size_t>(kGlyphSamples));
                    const int waveformIndex = previewState.waveforms[static_cast<std::size_t>(step)];
                    for (int i = 0; i < kGlyphSamples; ++i)
                        glyph.samples[static_cast<std::size_t>(i)] =
                            synth->sampleWaveformForUi(waveformIndex,
                                                      static_cast<double>(i) / static_cast<double>(kGlyphSamples - 1));
                    glyph.color = juce::Colour(0xFF29E0FF);
                    glyph.z = 1.15f;
                    glyph.lineWidth = 2.0f;
                    glyph.cacheId = "wavetable/" + std::to_string(machineId)
                        + "/main/" + std::to_string(step);
                    glyph.cacheRevision = previewCache.revision;
                    previewCache.traces.push_back(std::move(glyph));
                }

                // Sub-osc previews: one trace per sub wave step row, beside the
                // main oscillator's. The tint tends toward red as the sub's
                // octave offset drops.
                constexpr juce::uint32 subTints[] = {
                    0xFFFF5A5A, // -3
                    0xFFFF9E4A, // -2
                    0xFFE8E24A, // -1
                    0xFF8CE84A, //  0
                    0xFF4AE87A, // +1
                    0xFF3DE8C8, // +2
                    0xFF29A0FF  // +3
                };
                const juce::Colour subTint(subTints[static_cast<std::size_t>(
                    juce::jlimit(-3, 3, previewState.subOctaveOffset) + 3)]);
                for (int step = 0; step < previewState.subWaveStepCount; ++step)
                {
                    const float rowTop = static_cast<float>(step + 5);
                    if (rowTop + 1.0f > static_cast<float>(rows))
                        break;
                    TrackerUIComponent::Trace glyph;
                    glyph.colStart = 2.0f;
                    glyph.colEnd = 3.0f;
                    glyph.rowTop = rowTop;
                    glyph.rowBottom = rowTop + 1.0f;
                    glyph.samples.resize(static_cast<std::size_t>(kGlyphSamples));
                    const int waveformIndex = previewState.subWaveforms[static_cast<std::size_t>(step)];
                    for (int i = 0; i < kGlyphSamples; ++i)
                        glyph.samples[static_cast<std::size_t>(i)] =
                            synth->sampleWaveformForUi(waveformIndex,
                                                      static_cast<double>(i) / static_cast<double>(kGlyphSamples - 1));
                    glyph.color = subTint;
                    glyph.z = 1.15f;
                    glyph.lineWidth = 2.0f;
                    glyph.cacheId = "wavetable/" + std::to_string(machineId)
                        + "/sub/" + std::to_string(step);
                    glyph.cacheRevision = previewCache.revision;
                    previewCache.traces.push_back(std::move(glyph));
                }

                TrackerUIComponent::Trace envelope;
                envelope.colStart = 4.0f;
                envelope.colEnd = 6.0f;
                envelope.rowTop = 1.0f;
                envelope.rowBottom = 5.0f;
                constexpr int kEnvSamples = 48;
                envelope.samples.resize(static_cast<std::size_t>(kEnvSamples));
                CurvedAdsr::fillTraceSamples(envelope.samples,
                                             previewState.attack, previewState.decay,
                                             previewState.sustain, previewState.release,
                                             previewState.bend, previewState.maxAttack,
                                             previewState.maxDecay, previewState.maxRelease);
                envelope.color = juce::Colour(0xFFFFD21E);
                envelope.z = 1.15f;
                envelope.lineWidth = 2.0f;
                envelope.cacheId = "wavetable/" + std::to_string(machineId) + "/envelope";
                envelope.cacheRevision = previewCache.revision;
                previewCache.traces.push_back(std::move(envelope));
            }

            currentTraces.insert(currentTraces.end(),
                                 previewCache.traces.begin(), previewCache.traces.end());
        }

        overlayState.text = "Stack [" + std::to_string(machineId) + "] machine [wavetable synth]";
        overlayState.color = palette.textPrimary;
        overlayState.glowColor = palette.gridPlayhead;
        overlayState.glowStrength = 0.25f;
        return;
    }
    if (detailType.has_value() && detailType.value() == CommandType::ModulationMatrix)
    {
        const auto& machineBoxes = seqEditor->getMachineCells();
        TrackerUIComponent::Style style;
        style.background = palette.background;
        style.lightColor = palette.lightColor;
        style.defaultGlowColor = palette.gridPlayhead;
        style.ambientStrength = palette.ambientStrength;
        style.lightDirection = palette.lightDirection;
        uiComponent.setStyle(style);
        uiComponent.setCellSize(cellWidth, cellHeight);
        const std::size_t rows = machineBoxes.empty() ? 1 : machineBoxes[0].size();
        const std::size_t cols = machineBoxes.empty() ? 1 : machineBoxes.size();
        updateCellStates(machineBoxes, rows, cols);

        if (auto* matrix = dynamic_cast<ModulationMatrixMachine*>(
                audioProcessor.getMachine(CommandType::ModulationMatrix, static_cast<std::size_t>(machineId))))
        {
            const auto preview = matrix->getUiPreviewState();
            auto& cache = modulationPreviewCaches[machineId];
            if (!cache.valid || cache.waveforms != preview.waveforms)
            {
                cache.valid = true;
                cache.waveforms = preview.waveforms;
                ++cache.revision;
                cache.traces.clear();
                constexpr int samples = 32;
                for (int lfo = 0; lfo < 4; ++lfo)
                {
                    TrackerUIComponent::Trace trace;
                    trace.colStart = 1.0f;
                    trace.colEnd = 2.0f;
                    trace.rowTop = static_cast<float>(lfo + 1);
                    trace.rowBottom = static_cast<float>(lfo + 2);
                    trace.samples.resize(samples);
                    for (int sample = 0; sample < samples; ++sample)
                        trace.samples[static_cast<std::size_t>(sample)] = matrix->sampleWaveformForUi(
                            preview.waveforms[static_cast<std::size_t>(lfo)],
                            static_cast<float>(sample) / static_cast<float>(samples - 1));
                    trace.color = juce::Colour(0xff29e0ff);
                    trace.z = 1.15f;
                    trace.lineWidth = 2.0f;
                    trace.cacheId = "modulation/" + std::to_string(machineId) + "/lfo/" + std::to_string(lfo);
                    trace.cacheRevision = cache.revision;
                    cache.traces.push_back(std::move(trace));
                }
            }
            currentTraces.insert(currentTraces.end(), cache.traces.begin(), cache.traces.end());
        }
        overlayState.text = "Stack [" + std::to_string(machineId) + "] machine [modulation matrix]";
        overlayState.color = palette.textPrimary;
        overlayState.glowColor = palette.gridPlayhead;
        overlayState.glowStrength = 0.25f;
        return;
    }
    if (detailType.has_value() && isSimpleMachineDetail(detailType.value()))
    {
        auto machineBoxes = seqEditor->getMachineCells();
        TrackerUIComponent::Style style;
        style.background = palette.background;
        style.lightColor = palette.lightColor;
        style.defaultGlowColor = palette.gridPlayhead;
        style.ambientStrength = palette.ambientStrength;
        style.lightDirection = palette.lightDirection;
        uiComponent.setStyle(style);
        uiComponent.setCellSize(cellWidth, cellHeight);

        const std::size_t baseRows = machineBoxes.empty() ? 1 : machineBoxes[0].size();
        const std::size_t cols = machineBoxes.empty() ? 1 : machineBoxes.size();
        const auto detail = detailType.value();

        std::size_t rowsBeforeScope = baseRows;
        if (detail == CommandType::FilterFx)
        {
            auto& previewCache = filterEnvelopePreviewCaches[machineId];
            std::array<float, 8> settings {};
            if (auto* filter = dynamic_cast<FilterFxMachine*>(
                    audioProcessor.getMachine(CommandType::FilterFx, static_cast<std::size_t>(machineId))))
            {
                filter->getEnvelopeSettings(settings[0], settings[1], settings[2], settings[3],
                                            settings[4], settings[5], settings[6], settings[7]);
            }
            if (!previewCache.valid || previewCache.settings != settings)
            {
                previewCache.valid = true;
                previewCache.settings = settings;
                ++previewCache.revision;
                previewCache.samples.resize(48);
                CurvedAdsr::fillTraceSamples(previewCache.samples,
                                             settings[0], settings[1], settings[2], settings[3],
                                             settings[4], settings[5], settings[6], settings[7]);
            }
            appendEnvelopeBand(machineBoxes, baseRows, previewCache.samples,
                               "filter/" + std::to_string(machineId) + "/envelope",
                               previewCache.revision);
            rowsBeforeScope += 2;
        }

        appendScopeBand(machineBoxes, rowsBeforeScope, scopeStackIndex, scopeSamples);
        updateCellStates(machineBoxes, rowsBeforeScope + 2, cols);

        if (detail == CommandType::DrumSynth)
        {
            // Each drum owns a label/value column pair. Thin full-height
            // separators make those strips legible without boxing every cell.
            for (std::size_t boundary = 2; boundary < cols; boundary += 2)
            {
                TrackerUIComponent::Trace divider;
                divider.colStart = static_cast<float>(boundary) - 0.002f;
                divider.colEnd = static_cast<float>(boundary) + 0.002f;
                divider.rowTop = 0.0f;
                divider.rowBottom = static_cast<float>(baseRows);
                divider.samples = { -1.0f, 1.0f };
                divider.color = palette.sequenceEntryArmedCursor.withAlpha(0.72f);
                divider.z = 1.16f;
                divider.lineWidth = 2.0f;
                divider.cacheId = "drum-divider/" + std::to_string(boundary);
                divider.cacheRevision = 1;
                currentTraces.push_back(std::move(divider));
            }
        }

        if (detail == CommandType::AuxSend1Fx)
            overlayState.text = "Shared aux [1] reverb";
        else if (detail == CommandType::AuxSend2Fx)
            overlayState.text = "Shared aux [2] reverb";
        else
            overlayState.text = "Stack [" + std::to_string(machineId) + "] machine [" + machineTraits(detail).longName + "]";

        overlayState.color = palette.textPrimary;
        overlayState.glowColor = palette.gridPlayhead;
        overlayState.glowStrength = 0.25f;
        return;
    }

    auto machineBoxes = seqEditor->getMachineCells();
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

    // Draw the live oscilloscope over the scope band (last two grid rows).
    currentTraces.clear();
    if (rows >= 4 && cols >= 1 && !scopeSamples.empty())
    {
        // Auto-calibrate the zoom from the snapshot peak, then apply it; the
        // renderer clamps samples to -1..1, so overdriven input flattens.
        const float scopeScale = updateScopeCalibration(scopeStackIndex, scopeSamples);
        for (float& sample : scopeSamples)
            sample *= scopeScale;

        TrackerUIComponent::Trace scope;
        scope.colStart = 0.0f;
        scope.colEnd = static_cast<float>(cols);
        scope.rowTop = static_cast<float>(rows - 2);
        scope.rowBottom = static_cast<float>(rows);
        scope.samples = std::move(scopeSamples);
        scope.color = juce::Colour(0xFF29E0FF);
        scope.z = 1.15f;
        scope.lineWidth = 2.0f;
        currentTraces.push_back(std::move(scope));
    }

    overlayState.text = "Stack [" + std::to_string(machineId) + "] [" + selectedStackAction + "]";
}

float TrackerMainUI::updateScopeCalibration(std::size_t stackIndex,
                                            const std::vector<float>& samples)
{
    auto& calibration = scopeCalibrations[stackIndex];

    const auto nowMs = juce::Time::getMillisecondCounterHiRes();
    // Clamp dt so a long stall (e.g. debugger) cannot jump the scale.
    const float dtSeconds = juce::jlimit(0.0f, 0.25f,
                                         static_cast<float>((nowMs - calibration.lastFrameMs) / 1000.0));
    calibration.lastFrameMs = nowMs;

    float peak = 0.0f;
    for (const float sample : samples)
        peak = std::max(peak, std::abs(sample));

    // Fast attack / slow release, like an envelope follower, and only while
    // there is signal; below-noise silence holds the last zoom instead of
    // zooming in on noise.
    if (peak >= 0.01f)
    {
        const float neededScale = juce::jlimit(1.0f, 100.0f, 1.0f / peak);
        if (neededScale < calibration.target)
        {
            // A new loud peak needs more zoom-out: take it immediately so a
            // transient is not clipped for up to a second.
            calibration.target = neededScale;
            calibration.lastRecalMs = nowMs;
        }
        else if (calibration.lastRecalMs == 0 || nowMs - calibration.lastRecalMs >= 1000)
        {
            // A quieter signal only zooms in once it has held for a second.
            calibration.target = neededScale;
            calibration.lastRecalMs = nowMs;
        }
    }

    // Asymmetric ease toward the target: settle within a few frames when
    // zooming out for loud signal, but drift slowly into quiet signal so the
    // zoom-in does not pump on momentary gaps.
    const float timeConstant = (calibration.target < calibration.scale) ? 0.05f : 1.5f;
    calibration.scale += (calibration.target - calibration.scale)
        * (1.0f - std::exp(-dtSeconds / timeConstant));
    return calibration.scale;
}

void TrackerMainUI::appendScopeBand(std::vector<std::vector<UIBox>>& boxes,
                                    std::size_t rows,
                                    std::size_t stackIndex,
                                    std::vector<float>& scopeSamples)
{
    const std::size_t cols = boxes.size();
    if (cols == 0)
        return;
    for (auto& column : boxes)
        column.resize(rows + 2);

    // Dark band cells, matching the scope band built into the machine stack
    // table. They are disabled and carry no callbacks, so the cursor cannot
    // land on them; the editor's machineCells grid is left untouched.
    for (std::size_t col = 0; col < cols; ++col)
    {
        for (std::size_t bandRow = 0; bandRow < 2; ++bandRow)
        {
            UIBox& cell = boxes[col][rows + bandRow];
            cell.kind = UIBox::Kind::TrackerCell;
            cell.isDisabled = true;
            cell.useCustomFillColour = true;
            cell.customFillArgb = 0xFF0A0F14;
        }
    }
    boxes[0][rows].text = "OUT";

    if (rows >= 4 && cols >= 1 && !scopeSamples.empty())
    {
        const float scopeScale = updateScopeCalibration(stackIndex, scopeSamples);
        for (float& sample : scopeSamples)
            sample *= scopeScale;

        TrackerUIComponent::Trace scope;
        scope.colStart = 0.0f;
        scope.colEnd = static_cast<float>(cols);
        scope.rowTop = static_cast<float>(rows);
        scope.rowBottom = static_cast<float>(rows + 2);
        scope.samples = std::move(scopeSamples);
        scope.color = juce::Colour(0xFF29E0FF);
        scope.z = 1.15f;
        scope.lineWidth = 2.0f;
        currentTraces.push_back(std::move(scope));
    }
}

void TrackerMainUI::appendEnvelopeBand(std::vector<std::vector<UIBox>>& boxes,
                                       std::size_t rows,
                                       const std::vector<float>& envelopeSamples,
                                       const std::string& traceCacheId,
                                       std::uint64_t traceCacheRevision)
{
    const std::size_t cols = boxes.size();
    if (cols == 0)
        return;
    for (auto& column : boxes)
        column.resize(rows + 2);

    // The preview band uses the same dark, non-interactive style as the scope
    // band; only the trace itself carries the envelope shape.
    for (std::size_t col = 0; col < cols; ++col)
    {
        for (std::size_t bandRow = 0; bandRow < 2; ++bandRow)
        {
            UIBox& cell = boxes[col][rows + bandRow];
            cell.kind = UIBox::Kind::TrackerCell;
            cell.isDisabled = true;
            cell.useCustomFillColour = true;
            cell.customFillArgb = 0xFF0A0F14;
        }
    }
    boxes[0][rows].text = "ENV";

    if (!envelopeSamples.empty())
    {
        TrackerUIComponent::Trace envelope;
        envelope.colStart = 0.0f;
        envelope.colEnd = static_cast<float>(cols);
        envelope.rowTop = static_cast<float>(rows);
        envelope.rowBottom = static_cast<float>(rows + 2);
        envelope.samples = envelopeSamples;
        envelope.color = juce::Colour(0xFFFFD21E);
        envelope.z = 1.15f;
        envelope.lineWidth = 2.0f;
        envelope.cacheId = traceCacheId;
        envelope.cacheRevision = traceCacheRevision;
        currentTraces.push_back(std::move(envelope));
    }
}

void TrackerMainUI::prepareMixerView()
{
    samplerViewActive = false;
    customMachineColumnWidthsActive = false;
    samplerColumnWidths.clear();
    constexpr std::size_t rows = 12;
    const auto stackCount = audioProcessor.getMachineStackCount();
    std::vector<bool> muted(stackCount), solo(stackCount);
    std::vector<float> meters(stackCount), gains(stackCount), stackPans(stackCount);
    std::array<float, 2> masterMeters { 0.0f, 0.0f };
    std::array<std::array<float, 2>, 2> auxMeters {};
    std::array<float, 2> auxGains {}, auxPans {};
    std::array<bool, 2> auxMuted {};
    float masterGain = 0.0f;
    bool masterMuted = false;
    bool masterRecording = false;
    audioProcessor.withAudioThreadExclusive([&]
    {
        masterMeters[0] = audioProcessor.getMasterMeterLevel(0);
        masterMeters[1] = audioProcessor.getMasterMeterLevel(1);
        masterGain = audioProcessor.getMasterGainDb();
        masterMuted = audioProcessor.isMasterMuted();
        masterRecording = audioProcessor.isMasterRecording();
        for (std::size_t aux = 0; aux < auxMeters.size(); ++aux)
        {
            auxMeters[aux][0] = audioProcessor.getAuxReturnMeterLevel(aux, 0);
            auxMeters[aux][1] = audioProcessor.getAuxReturnMeterLevel(aux, 1);
            auxGains[aux] = audioProcessor.getAuxReturnGainDb(aux);
            auxPans[aux] = audioProcessor.getAuxReturnPan(aux);
            auxMuted[aux] = audioProcessor.isAuxReturnMuted(aux);
        }
        for (std::size_t stack = 0; stack < stackCount; ++stack)
        {
            muted[stack] = audioProcessor.isStackMuted(stack);
            solo[stack] = audioProcessor.isStackSolo(stack);
            meters[stack] = audioProcessor.getStackMeterLevel(stack);
            gains[stack] = audioProcessor.getStackGainDb(stack);
            stackPans[stack] = audioProcessor.getStackPan(stack);
        }
    });
    std::vector<std::vector<UIBox>> boxes(stackCount + 3, std::vector<UIBox>(rows));
    boxes[0][2].text = "R";
    boxes[0][2].isActive = masterRecording;
    boxes[0][2].isHighlighted = masterRecording;
    if (masterRecording)
    {
        boxes[0][2].useCustomFillColour = true;
        boxes[0][2].customFillArgb = juce::Colour(0xFFB00020).getARGB();
        boxes[0][2].useCustomTextColour = true;
        boxes[0][2].customTextArgb = juce::Colours::white.getARGB();
        boxes[0][2].customColoursOverrideSelection = true;
    }
    const std::array<int, 2> masterLit {
        juce::jlimit(0, 8, static_cast<int>(std::ceil(masterMeters[0] * 8.0f))),
        juce::jlimit(0, 8, static_cast<int>(std::ceil(masterMeters[1] * 8.0f)))
    };
    const auto faderBand = [](float gainDb)
    {
        return juce::jlimit(0, 7, static_cast<int>(std::lround((gainDb + 48.0f) / 54.0f * 7.0f)));
    };
    const auto brightenFaderCell = [this](UIBox& cell)
    {
        cell.useCustomFillColour = true;
        cell.customFillArgb = palette.gridEmpty.brighter(0.45f).getARGB();
    };
    const auto formatPan = [](float pan)
    {
        const int value = static_cast<int>(std::lround(std::abs(juce::jlimit(-1.0f, 1.0f, pan)) * 100.0f));
        return value == 0 ? std::string("PAN:C") : std::string("PAN:") + (pan < 0.0f ? "L" : "R") + std::to_string(value);
    };
    const auto fillMeterCell = [](UIBox& cell, juce::Colour colour)
    {
        cell.isHighlighted = true;
        cell.useCustomFillColour = true;
        cell.customFillArgb = colour.darker(0.55f).getARGB();
        cell.useCustomGlowColour = true;
        cell.customGlowArgb = colour.getARGB();
    };
    const int masterHandle = faderBand(masterGain);
    for (std::size_t row = 0; row < rows; ++row)
    {
        boxes[0][row].kind = UIBox::Kind::TrackerCell;
        boxes[0][row].isMutedChannel = masterMuted;
    }
    for (int band = 0; band < 8; ++band)
    {
        auto& cell = boxes[0][4 + static_cast<std::size_t>(7 - band)];
        if (band < std::max(masterLit[0], masterLit[1]))
            fillMeterCell(cell, juce::Colours::limegreen);
        if (band == masterHandle) brightenFaderCell(cell);
    }
    if (masterMuted)
    {
        boxes[0][0].useCustomFillColour = true;
        boxes[0][0].customFillArgb = palette.gridEmpty.darker(0.55f).getARGB();
    }
    for (std::size_t aux = 0; aux < auxMeters.size(); ++aux)
    {
        auto& column = boxes[aux + 1];
        column[0].text = "AUX:" + std::to_string(aux + 1);
        column[1].text = "GAIN:" + formatSignedGain(auxGains[aux]);
        column[2].text = auxMuted[aux] ? "MUTE:ON" : "MUTE:OFF";
        column[3].text = formatPan(auxPans[aux]);
        const std::array<int, 2> lit {
            juce::jlimit(0, 8, static_cast<int>(std::ceil(auxMeters[aux][0] * 8.0f))),
            juce::jlimit(0, 8, static_cast<int>(std::ceil(auxMeters[aux][1] * 8.0f)))
        };
        const int handle = faderBand(auxGains[aux]);
        for (std::size_t row = 0; row < rows; ++row)
        {
            column[row].kind = UIBox::Kind::TrackerCell;
            column[row].isMutedChannel = auxMuted[aux];
        }
        for (int band = 0; band < 8; ++band)
        {
            auto& cell = column[4 + static_cast<std::size_t>(7 - band)];
            if (band < std::max(lit[0], lit[1]))
                fillMeterCell(cell, juce::Colours::deepskyblue);
            if (band == handle) brightenFaderCell(cell);
        }
    }
    for (std::size_t stack = 0; stack < stackCount; ++stack)
    {
        auto& column = boxes[stack + 3];
        column[0].text = "STK:" + std::to_string(stack);
        column[1].text = muted[stack] ? "MUTE:ON" : "MUTE:OFF";
        column[2].text = solo[stack] ? "SOLO:ON" : "SOLO:OFF";
        column[3].text = formatPan(stackPans[stack]);
        const float meter = meters[stack];
        const float gain = gains[stack];
        const int lit = juce::jlimit(0, 8, static_cast<int>(std::ceil(meter * 8.0f)));
        const int handle = faderBand(gain);
        for (std::size_t row = 0; row < rows; ++row)
        {
            column[row].kind = UIBox::Kind::TrackerCell;
            column[row].isSoloChannel = solo[stack];
            column[row].isMutedChannel = muted[stack];
        }
        for (int band = 0; band < 8; ++band)
        {
            auto& cell = column[4 + static_cast<std::size_t>(7 - band)];
            if (band < lit) cell.isHighlighted = true;
            if (band == handle) brightenFaderCell(cell);
        }
    }
    const auto column = seqEditor->getMixerColumn();
    const auto row = seqEditor->getMixerRow();
    if (column < boxes.size() && row < rows) boxes[column][row].isSelected = true;
    updateCellStates(boxes, rows, stackCount + 3);
}

void TrackerMainUI::prepareControlPanelView()
{
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

    std::vector<std::vector<UIBox>> boxes(5, std::vector<UIBox>(rowCount));

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
    boxes[4][0].kind = UIBox::Kind::None;
    boxes[4][0].isDisabled = true;

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
            boxes[2][displayRow].text = "BPM " + std::to_string(audioProcessor.getSongRowBpm(row));
            boxes[3][displayRow].kind = UIBox::Kind::TrackerCell;
            boxes[3][displayRow].text = "EDIT";
            boxes[4][displayRow].kind = UIBox::Kind::TrackerCell;
            boxes[4][displayRow].text = "DEL";

            if (playMode == SongPlayMode::song && row == playbackSongRow)
            {
                boxes[0][displayRow].isHighlighted = true;
                boxes[1][displayRow].isHighlighted = true;
                boxes[2][displayRow].isHighlighted = true;
                boxes[3][displayRow].isHighlighted = true;
                boxes[4][displayRow].isHighlighted = true;
            }
        }
    });

    currentSongRow = std::min(currentSongRow, rowCount - 1);
    currentSongCol = std::min(currentSongCol, static_cast<std::size_t>(currentSongRow == 0 ? 1 : 4));
    boxes[currentSongCol][currentSongRow].isSelected = true;

    updateCellStates(boxes, rowCount, 5);
    overlayState.text = "Song";
    overlayState.color = palette.textPrimary;
    overlayState.glowColor = palette.gridPlayhead;
    overlayState.glowStrength = 0.25f;
}

void TrackerMainUI::prepareAudioConfigView()
{
    samplerViewActive = false;
    customMachineColumnWidthsActive = true;
    TrackerUIComponent::Style style;
    style.background = palette.background;
    style.lightColor = palette.lightColor;
    style.defaultGlowColor = palette.gridPlayhead;
    style.ambientStrength = palette.ambientStrength;
    style.lightDirection = palette.lightDirection;
    uiComponent.setStyle(style);
    uiComponent.setCellSize(cellWidth, cellHeight);
    auto boxes = seqEditor->getAudioConfigCells();
    samplerColumnWidths.assign(boxes.size(), 1.0f);
    for (std::size_t col = 0; col < boxes.size(); ++col)
        for (const auto& cell : boxes[col])
            samplerColumnWidths[col] = std::max(samplerColumnWidths[col], cell.width);
    updateCellStates(boxes, rowsInUI - 1, 4);
    overlayState.text = "AUDIO CONFIG";
    overlayState.color = palette.textPrimary;
    overlayState.glowColor = palette.gridPlayhead;
    overlayState.glowStrength = 0.35f;
}

void TrackerMainUI::prepareHelpView()
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

    const std::vector<std::array<std::string, 6>> helpRows = {
        {"SPACE", "PLAY/STOP", "ARROWS", "MOVE", "ENTER", "ACT/REC"},
        {"BACK", "RESET/EXIT", "TAB", "STEP", "[ ]", "ADJUST"},
        {"{ }", "STEP ALL", "", "", "", ""},
        {"+/-", "ROWS", "S+C", "CLOCK", "R", "REWIND"},
        {"0-7", "PAGES", "S+1-8", "TPS", "S+I/K", "INS/DEL"},
        {"C+H", "HELP", "S+A", "ARM", "S+H", "SHORT"},
        {"C+V", "PASTE", "C+=", "SHIFT +", "C+-", "SHIFT -"},
        {"S+S", "SOLO", "S+M", "MUTE", "S+E/G", "XPL/IMP"},
        {"Z X C", "CONTROL", "Q-P", "FAM/CYC", ", .", "OCTAVE"}
    };

    std::vector<std::vector<UIBox>> boxes(6, std::vector<UIBox>(helpRows.size()));
    for (std::size_t row = 0; row < helpRows.size(); ++row)
        for (std::size_t col = 0; col < boxes.size(); ++col)
        {
            boxes[col][row].kind = UIBox::Kind::TrackerCell;
            boxes[col][row].text = helpRows[row][col];
        }

    updateCellStates(boxes, rowsInUI - 1, 6);
    overlayState.text = "HELP";
    overlayState.color = palette.textPrimary;
    overlayState.glowColor = palette.gridPlayhead;
    overlayState.glowStrength = 0.35f;
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
                                                                   bool sequenceEntryArmed) const
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
            box.isSequenceEntryArmedCursor = sequenceEntryArmed && box.isSelected;
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
                                    size_t colsToDisplay,
                                    const std::vector<UIBox>* stickyHeaderCells)
{
    if (rowsToDisplay == 0 || colsToDisplay == 0)
        return;

    const bool sameValuePulseContext = valuePulseContext == lastValuePulseContext;
    lastValuePulseContext = valuePulseContext;

    const bool hasStickyHeader = stickyHeaderCells != nullptr;
    const size_t bodyRowsToDisplay = hasStickyHeader ? rowsToDisplay - 1 : rowsToDisplay;
    if (bodyRowsToDisplay == 0)
        return;

    const size_t maxCols = boxes.size();
    const size_t maxRows = (maxCols > 0) ? boxes[0].size() : 0;
    if (maxCols == 0 || maxRows == 0)
    {
        cellStates.assign(colsToDisplay, std::vector<TrackerUIComponent::CellState>(rowsToDisplay, makeDefaultCell()));
        playheadGlow.assign(colsToDisplay, std::vector<float>(rowsToDisplay, 0.0f));
        lastValueNorm.assign(colsToDisplay, std::vector<float>(rowsToDisplay, -1.0f));
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
    size_t nextEndRow = nextStartRow + bodyRowsToDisplay;

    if (cursorCol < nextStartCol) nextStartCol = cursorCol;
    if (cursorCol >= nextEndCol) nextStartCol = cursorCol - colsToDisplay + 1;
    if (cursorRow < nextStartRow) nextStartRow = cursorRow;
    if (cursorRow >= nextEndRow) nextStartRow = cursorRow - bodyRowsToDisplay + 1;

    // Scroll origins are intentionally shared while moving between related
    // sequence views, but a wider/shorter page may not have enough content to
    // support the previous page's origin.  Clamp the origin itself rather than
    // only the end: otherwise the leading cells disappear and the unused tail
    // of the display is filled with blanks until the cursor moves back.
    const size_t maxStartCol = maxCols > colsToDisplay ? maxCols - colsToDisplay : 0;
    const size_t maxStartRow = maxRows > bodyRowsToDisplay ? maxRows - bodyRowsToDisplay : 0;
    nextStartCol = std::min(nextStartCol, maxStartCol);
    nextStartRow = std::min(nextStartRow, maxStartRow);

    nextEndCol = nextStartCol + colsToDisplay;
    nextEndRow = nextStartRow + bodyRowsToDisplay;

    if (nextEndRow >= maxRows) nextEndRow = maxRows;
    if (nextEndCol >= maxCols) nextEndCol = maxCols;

    const bool reuseGlow = sameValuePulseContext
        && startCol == nextStartCol && startRow == nextStartRow;

    cellStates.resize(colsToDisplay);
    playheadGlow.resize(colsToDisplay);
    lastValueNorm.resize(colsToDisplay);
    for (size_t col = 0; col < colsToDisplay; ++col)
    {
        cellStates[col].resize(rowsToDisplay, makeDefaultCell());
        playheadGlow[col].resize(rowsToDisplay, 0.0f);
        lastValueNorm[col].resize(rowsToDisplay, -1.0f);
    }
    for (size_t displayCol = 0; displayCol < colsToDisplay; ++displayCol)
    {
        const size_t col = nextStartCol + displayCol;
        const bool colInRange = col < maxCols;

        for (size_t displayRow = 0; displayRow < rowsToDisplay; ++displayRow)
        {
            UIBox box;
            if (hasStickyHeader && displayRow == 0)
            {
                if (colInRange && col < stickyHeaderCells->size())
                    box = (*stickyHeaderCells)[col];
            }
            else
            {
                const size_t bodyRow = displayRow - (hasStickyHeader ? 1 : 0);
                const size_t row = nextStartRow + bodyRow;
                if (colInRange && row < maxRows)
                    box = boxes[col][row];
            }

            const float previousGlow = reuseGlow ? playheadGlow[displayCol][displayRow] : 0.0f;
            const float glowDecayScalar = samplerViewActive
                ? samplerPalette.glowDecayScalar
                : palette.glowDecayScalar;
            const float baseGlow = samplerViewActive
                ? box.glow
                : std::max(box.glow, box.isHighlighted ? 1.0f
                    : std::max(0.0f, previousGlow * glowDecayScalar));

            // Flash the cell when its normalised parameter value changed
            // since the last frame so adjustments are visible at a glance.
            float valuePulse = 0.0f;
            if (!samplerViewActive && box.hasValueScale && !box.isDisabled)
            {
                const float previousNorm = (reuseGlow && lastValueNorm.size() > displayCol
                    && lastValueNorm[displayCol].size() > displayRow)
                    ? lastValueNorm[displayCol][displayRow]
                    : box.valueNorm;
                if (std::abs(box.valueNorm - previousNorm) > 0.0001f)
                    valuePulse = 1.0f;
            }
            const float glowValue = std::max(baseGlow, valuePulse);

            auto cell = makeDefaultCell();
            cell.text = box.text;
            cell.fillColor = samplerViewActive ? getSamplerCellColour(box) : getCellColour(box);
            cell.textColor = samplerViewActive ? getSamplerTextColour(box) : getTextColour(box);
            cell.glowColor = samplerViewActive ? samplerPalette.glowActive
                : (box.useCustomGlowColour ? juce::Colour(box.customGlowArgb) : palette.gridPlayhead);
            cell.glow = glowValue;
            cell.depthScale = samplerViewActive ? getSamplerCellDepthScale(box) : getCellDepthScale(box);
            const bool shortened = box.noteLengthMode != 0;
            const auto shortenColour = box.noteLengthMode == 1 ? juce::Colour(0xff36dd68) : juce::Colour(0xffb96cff);
            const bool outerPriority = box.isSoloChannel || box.isMutedChannel;
            cell.drawOutline = samplerViewActive ? box.isSelected
                : (box.hasNote || outerPriority || shortened);
            cell.outlineColor = box.isSoloChannel ? juce::Colours::yellow
                : box.isMutedChannel ? juce::Colours::red
                : shortened ? shortenColour
                : (box.isControlModeSequence && box.hasNote)
                    ? palette.sequenceControlModeOutline : palette.gridNote;
            cell.outlineLineWidth = (box.isSoloChannel || box.isMutedChannel) ? 5.0f : 3.0f;
            cell.drawInsetOutline = !samplerViewActive && shortened && outerPriority;
            cell.insetOutlineColor = shortenColour;
            cell.insetOutlineLineWidth = 2.0f;

            cell.fillFraction = (!samplerViewActive && box.hasValueScale && !box.isDisabled)
                ? box.valueNorm
                : -1.0f;
            cell.barColour = palette.paramBarFill;
            cell.drawStereoMeter = box.drawStereoMeter;
            cell.stereoMeterLeftActive = box.stereoMeterLeftActive;
            cell.stereoMeterRightActive = box.stereoMeterRightActive;
            if (box.drawStereoMeter)
                cell.stereoMeterColour = juce::Colour(box.stereoMeterArgb);

            cellStates[displayCol][displayRow] = cell;
            playheadGlow[displayCol][displayRow] = glowValue;
            lastValueNorm[displayCol][displayRow] = box.hasValueScale ? box.valueNorm : -1.0f;
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
    // Parameter strength is shown by the horizontal fill bar (see
    // updateCellStates), not by the cell fill, so parameter cells keep the
    // normal selected/empty fills.
    if (cell.isSequenceEntryArmedCursor && !cell.customColoursOverrideSelection)
        return palette.sequenceEntryArmedCursor;
    if (cell.isSelected && !cell.customColoursOverrideSelection)
        return PaletteDefaults::cursor.fill;

    auto colour = cell.useCustomFillColour ? juce::Colour(cell.customFillArgb)
        : cell.isArmed ? palette.statusOk
        : cell.isControlModeSequence
            ? palette.gridEmpty.interpolatedWith(juce::Colours::white, 0.08f)
            : palette.gridEmpty;
    return colour;
}

juce::Colour TrackerMainUI::getTextColour(const UIBox& cell) const
{
    if (cell.isSequenceEntryArmedCursor && !cell.customColoursOverrideSelection)
        return palette.sequenceEntryArmedCursorText;
    if (cell.isSelected && !cell.customColoursOverrideSelection)
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
    if (stateDialogMode != StateDialogMode::none)
        return handleStateDialogKey(key);
    if (key.getModifiers().isCtrlDown())
    {
        const auto keyCode = key.getKeyCode();
        if (keyCode == 's' || keyCode == 'S')
        {
            openStateSaveDialog();
            return true;
        }
        if (keyCode == 'l' || keyCode == 'L')
        {
            openStateLoadDialog();
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
}


bool TrackerMainUI::keyStateChanged(bool isKeyDown, juce::Component* originatingComponent)
{
  juce::ignoreUnused(originatingComponent);
  if (isKeyDown)
    return false;

  // JUCE reports the global state change but not the released key. Poll only
  // our small musical-key set and let the editor release any audition keys
  // that are no longer down. This is also robust when focus is lost mid-note.
  static constexpr const char* auditionKeys = "z s x d c v g b h n j m q w e r t y u i o p";
  juce::String stillHeld;
  for (const char* key = auditionKeys; *key != '\0'; ++key)
    if (*key != ' ' && juce::KeyPress::isKeyCurrentlyDown(*key))
      stillHeld += juce::String::charToString(*key);

  auto arguments = juce::var(new juce::DynamicObject());
  arguments.getDynamicObject()->setProperty("action", "release_audition_keys");
  arguments.getDynamicObject()->setProperty("keys", stillHeld);
  TrackerControlService::Command command;
  command.kind = TrackerControlService::CommandKind::uiAction;
  command.arguments = arguments;
  return audioProcessor.getControlService().execute(command).ok;
}
