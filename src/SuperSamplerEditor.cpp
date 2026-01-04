#include "SuperSamplerEditor.h"
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

SuperSamplerEditor::SuperSamplerEditor (SuperSamplerProcessor& p)
    : AudioProcessorEditor (&p),
      processorRef (p),
      uiComponent(openGLContext)
{
    palette.background = juce::Colour(0xFF03060B);
    palette.cellIdle = juce::Colour(0xFF141A22);
    palette.cellSelected = juce::Colour(0xFF00E8FF);
    palette.cellAccent = juce::Colour(0xFF1E2F3D);
    palette.cellDisabled = juce::Colour(0xFF0C1118);
    palette.textPrimary = juce::Colour(0xFF4EF2C2);
    palette.textMuted = juce::Colour(0xFF6B7C8F);
    palette.glowActive = juce::Colour(0xFFFF5533);
    palette.lightColor = juce::Colour(0xFFEAF6FF);

    TrackerUIComponent::Style style;
    style.background = palette.background;
    style.lightColor = palette.lightColor;
    style.defaultGlowColor = palette.glowActive;
    style.ambientStrength = palette.ambientStrength;
    style.lightDirection = palette.lightDirection;
    uiComponent.setStyle(style);
    uiComponent.setCellSize(1.2f, 1.1f);

    columnWidthScales = { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 2.0f };

    openGLContext.setRenderer(this);
    openGLContext.setContinuousRepainting(false);
    openGLContext.setComponentPaintingEnabled(false);
    openGLContext.attachTo(*this);

    setSize (980, 640);
    gridBounds = getLocalBounds();

    addKeyListener(this);
    setWantsKeyboardFocus(true);

    startTimerHz(30);
    refreshFromProcessor();
}

SuperSamplerEditor::~SuperSamplerEditor()
{
    stopTimer();
    openGLContext.detach();
}

void SuperSamplerEditor::paint (juce::Graphics& g)
{
    g.fillAll(palette.background);
}

void SuperSamplerEditor::resized()
{
    gridBounds = getLocalBounds();
}

void SuperSamplerEditor::timerCallback()
{
    refreshFromProcessor();

    TrackerUIComponent::OverlayState overlay;
    overlay.text = "supersampler";
    overlay.color = palette.textPrimary;
    overlay.glowColor = palette.glowActive;
    overlay.glowStrength = 0.35f;

    TrackerUIComponent::ZoomState zoomState;
    zoomState.zoomLevel = zoomLevel;

    TrackerUIComponent::DragState dragState;
    dragState.panX = panOffsetX;
    dragState.panY = panOffsetY;

    {
        const std::lock_guard<std::mutex> lock(uiMutex);
        uiComponent.updateUIState(cellStates, overlay, zoomState, dragState);
    }

    openGLContext.triggerRepaint();
}

void SuperSamplerEditor::updateUIFromProcessor(const juce::var& payload)
{
    const std::lock_guard<std::mutex> lock(stateMutex);
    pendingPayload = payload;
    pendingPayloadReady = true;
}

void SuperSamplerEditor::adjustZoom(float delta)
{
    const float minZoom = 0.5f;
    const float maxZoom = 2.5f;
    zoomLevel = juce::jlimit(minZoom, maxZoom, zoomLevel + delta);
}

void SuperSamplerEditor::mouseWheelMove (const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel)
{
    if (!getLocalBounds().contains(event.getPosition()))
        return;

    const float zoomDelta = wheel.deltaY * 0.4f;
    if (std::abs(zoomDelta) < 0.0001f)
        return;

    const std::lock_guard<std::mutex> lock(uiMutex);
    adjustZoom(zoomDelta);
}

void SuperSamplerEditor::mouseDown (const juce::MouseEvent& event)
{
    if (!getLocalBounds().contains(event.getPosition()))
        return;

    lastDragPosition = event.getPosition();
}

void SuperSamplerEditor::mouseDrag (const juce::MouseEvent& event)
{
    if (!getLocalBounds().contains(event.getPosition()))
        return;

    const auto currentPos = event.getPosition();
    const auto delta = currentPos - lastDragPosition;
    lastDragPosition = currentPos;

    const std::lock_guard<std::mutex> lock(uiMutex);
    const float panScale = 0.02f / zoomLevel;
    panOffsetX += static_cast<float>(delta.x) * panScale;
    panOffsetY -= static_cast<float>(delta.y) * panScale;
}

void SuperSamplerEditor::refreshFromProcessor()
{
    juce::var payload;
    {
        const std::lock_guard<std::mutex> lock(stateMutex);
        if (pendingPayloadReady)
        {
            payload = pendingPayload;
            pendingPayloadReady = false;
        }
    }

    if (payload.isVoid())
        payload = processorRef.getSamplerState();

    refreshFromPayload(payload);
}

void SuperSamplerEditor::refreshFromPayload(const juce::var& payload)
{
    auto* obj = payload.getDynamicObject();
    if (obj == nullptr)
        return;

    auto playersVar = obj->getProperty("players");
    const auto* playersArray = playersVar.getArray();
    if (playersArray == nullptr)
        return;

    std::vector<PlayerUIState> nextPlayers;
    nextPlayers.reserve(static_cast<size_t>(playersArray->size()));

    for (const auto& entry : *playersArray)
    {
        auto* playerObj = entry.getDynamicObject();
        if (playerObj == nullptr)
            continue;

        PlayerUIState st;
        st.id = static_cast<int>(playerObj->getProperty("id"));
        st.midiLow = static_cast<int>(playerObj->getProperty("midiLow"));
        st.midiHigh = static_cast<int>(playerObj->getProperty("midiHigh"));
        st.gain = static_cast<float>(double(playerObj->getProperty("gain")));
        st.isPlaying = static_cast<bool>(playerObj->getProperty("isPlaying"));
        st.status = playerObj->getProperty("status").toString();
        st.fileName = playerObj->getProperty("fileName").toString();
        st.filePath = playerObj->getProperty("filePath").toString();
        nextPlayers.push_back(st);
    }

    {
        const std::lock_guard<std::mutex> lock(uiMutex);
        players = std::move(nextPlayers);
        rebuildCellLayout();
    }
}

void SuperSamplerEditor::rebuildCellLayout()
{
    const size_t rows = players.size() + 1;
    const size_t cols = 6;

    cursorRow = std::min(cursorRow, rows - 1);
    cursorCol = std::min(cursorCol, cols - 1);
    if (cursorRow == 0)
        cursorCol = 0;

    cellVisualStates.assign(cols, std::vector<CellVisualState>(rows));
    cellStates.assign(cols, std::vector<TrackerUIComponent::CellState>(rows));
    cellInfo.assign(cols, std::vector<CellInfo>(rows));

    for (size_t col = 0; col < cols; ++col)
    {
        for (size_t row = 0; row < rows; ++row)
        {
            CellInfo info{};
            std::string text;
            if (row == 0)
            {
                if (col == 0)
                {
                    info.action = Action::Add;
                    info.playerIndex = -1;
                    text = "ADD";
                }
                else
                {
                    info.action = Action::None;
                }
            }
            else
            {
                const auto& player = players[row - 1];
                info.playerIndex = static_cast<int>(row - 1);
                switch (col)
                {
                    case 0:
                        info.action = Action::Load;
                        text = "LOAD";
                        break;
                    case 1:
                        info.action = Action::Trigger;
                        text = player.isPlaying ? "PLAY" : "TRIG";
                        break;
                    case 2:
                        info.action = Action::Low;
                        text = sanitizeLabel(juce::String(player.midiLow), 4);
                        break;
                    case 3:
                        info.action = Action::High;
                        text = sanitizeLabel(juce::String(player.midiHigh), 4);
                        break;
                    case 4:
                        info.action = Action::Gain;
                        text = formatGain(player.gain);
                        break;
                    case 5:
                        info.action = Action::Waveform;
                        text = sanitizeLabel(player.fileName.isNotEmpty() ? player.fileName : player.status, 18);
                        break;
                    default:
                        info.action = Action::None;
                        break;
                }
            }

            cellInfo[col][row] = info;
            auto& visual = cellVisualStates[col][row];
            visual.isSelected = (row == cursorRow && col == cursorCol);
            visual.isEditing = (editMode && visual.isSelected);
            visual.isActive = (info.action == Action::Trigger && row > 0 && players[row - 1].isPlaying);
            visual.isDisabled = (info.action == Action::None);
            visual.glow = visual.isActive ? 1.0f : 0.0f;

            TrackerUIComponent::CellState cell;
            cell.text = text;
            cell.fillColor = getCellColour(visual, info);
            cell.textColor = getTextColour(visual, info);
            cell.glowColor = palette.glowActive;
            cell.glow = visual.glow;
            cell.depthScale = getCellDepthScale(visual);
            cell.drawOutline = visual.isSelected;
            cell.outlineColor = palette.cellSelected;
            cellStates[col][row] = cell;
        }
    }
}

void SuperSamplerEditor::newOpenGLContextCreated()
{
    uiComponent.initOpenGL(getWidth(), getHeight());
}

void SuperSamplerEditor::renderOpenGL()
{
    uiComponent.setViewportBounds(gridBounds, getHeight(), openGLContext.getRenderingScale());
    uiComponent.renderUI();
}

void SuperSamplerEditor::openGLContextClosing()
{
    uiComponent.shutdownOpenGL();
}

bool SuperSamplerEditor::keyPressed (const juce::KeyPress& key, juce::Component*)
{
    if (editMode)
    {
        if (key == juce::KeyPress::escapeKey || key == juce::KeyPress::returnKey)
        {
            editMode = false;
            editAction = Action::None;
            editPlayerIndex = -1;
            return true;
        }

        if (key.getKeyCode() == juce::KeyPress::leftKey || key.getKeyCode() == juce::KeyPress::downKey)
        {
            adjustEditValue(-1);
            return true;
        }
        if (key.getKeyCode() == juce::KeyPress::rightKey || key.getKeyCode() == juce::KeyPress::upKey)
        {
            adjustEditValue(1);
            return true;
        }
        return false;
    }

    if (key.getKeyCode() == juce::KeyPress::leftKey)
    {
        moveCursor(0, -1);
        return true;
    }
    if (key.getKeyCode() == juce::KeyPress::rightKey)
    {
        moveCursor(0, 1);
        return true;
    }
    if (key.getKeyCode() == juce::KeyPress::upKey)
    {
        moveCursor(-1, 0);
        return true;
    }
    if (key.getKeyCode() == juce::KeyPress::downKey)
    {
        moveCursor(1, 0);
        return true;
    }

    if (key == juce::KeyPress::returnKey || key.getKeyCode() == juce::KeyPress::spaceKey)
    {
        if (cursorCol < cellInfo.size() && cursorRow < cellInfo[cursorCol].size())
            handleAction(cellInfo[cursorCol][cursorRow]);
        return true;
    }

    return false;
}

void SuperSamplerEditor::handleAction(const CellInfo& info)
{
    if (info.action == Action::None)
        return;

    if (info.action == Action::Add)
    {
        processorRef.addSamplePlayerFromWeb();
        return;
    }

    int playerId = -1;
    {
        const std::lock_guard<std::mutex> lock(uiMutex);
        if (info.playerIndex < 0 || info.playerIndex >= static_cast<int>(players.size()))
            return;
        playerId = players[(size_t)info.playerIndex].id;
    }

    switch (info.action)
    {
        case Action::None:
        case Action::Add:
        case Action::Waveform:
            break;
        case Action::Load:
            processorRef.requestSampleLoadFromWeb(playerId);
            break;
        case Action::Trigger:
            processorRef.triggerFromWeb(playerId);
            break;
        case Action::Low:
        case Action::High:
        case Action::Gain:
            editMode = true;
            editAction = info.action;
            editPlayerIndex = info.playerIndex;
            break;
        default:
            break;
    }
}

void SuperSamplerEditor::adjustEditValue(int direction)
{
    PlayerUIState player;
    {
        const std::lock_guard<std::mutex> lock(uiMutex);
        if (editPlayerIndex < 0 || editPlayerIndex >= static_cast<int>(players.size()))
            return;
        player = players[(size_t)editPlayerIndex];
    }
    if (editAction == Action::Low)
    {
        const int low = juce::jlimit(0, 127, player.midiLow + direction);
        processorRef.setSampleRangeFromWeb(player.id, low, player.midiHigh);
    }
    else if (editAction == Action::High)
    {
        const int high = juce::jlimit(0, 127, player.midiHigh + direction);
        processorRef.setSampleRangeFromWeb(player.id, player.midiLow, high);
    }
    else if (editAction == Action::Gain)
    {
        const float gain = juce::jlimit(0.0f, 2.0f, player.gain + direction * 0.05f);
        processorRef.setGainFromUI(player.id, gain);
    }
}

void SuperSamplerEditor::moveCursor(int deltaRow, int deltaCol)
{
    const std::lock_guard<std::mutex> lock(uiMutex);

    if (cellStates.empty() || cellStates[0].empty())
        return;

    const int maxRow = static_cast<int>(cellStates[0].size()) - 1;
    const int maxCol = static_cast<int>(cellStates.size()) - 1;

    int nextRow = juce::jlimit(0, maxRow, static_cast<int>(cursorRow) + deltaRow);
    int nextCol = juce::jlimit(0, maxCol, static_cast<int>(cursorCol) + deltaCol);

    if (nextRow == 0)
        nextCol = 0;

    cursorRow = static_cast<size_t>(nextRow);
    cursorCol = static_cast<size_t>(nextCol);
    rebuildCellLayout();
}

juce::Colour SuperSamplerEditor::getCellColour(const CellVisualState& cell, const CellInfo& info) const
{
    if (cell.isDisabled)
        return palette.cellDisabled;
    if (cell.isEditing)
        return palette.cellSelected;
    if (cell.isSelected)
        return palette.cellSelected;
    if (cell.isActive)
        return palette.cellAccent;
    if (info.action == Action::Waveform)
        return palette.cellIdle.brighter(0.2f);
    return palette.cellIdle;
}

juce::Colour SuperSamplerEditor::getTextColour(const CellVisualState& cell, const CellInfo& info) const
{
    if (cell.isSelected)
        return palette.cellSelected;
    if (cell.isActive)
        return palette.glowActive;
    if (info.action == Action::Waveform)
        return palette.textMuted;
    return palette.textPrimary;
}

float SuperSamplerEditor::getCellDepthScale(const CellVisualState& cell) const
{
    if (cell.isEditing)
        return 1.05f;
    if (cell.isSelected)
        return 1.02f;
    return 1.0f;
}
