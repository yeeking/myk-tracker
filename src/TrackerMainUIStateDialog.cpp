#include "TrackerMainUI.h"
#include "PortableTrackerState.h"

#include <algorithm>
#include <chrono>
#include <utility>

namespace
{
bool isTrackerStateFile(const juce::File& file)
{
    return file.existsAsFile() && file.hasFileExtension("myktracker");
}

juce::String sanitizeStateFileName(juce::String name)
{
    name = name.trim();
    name = name.replaceCharacter('/', '_').replaceCharacter('\\', '_');
    return name;
}
}

juce::File TrackerMainUI::initialStateDirectory() const
{
    if (lastStateDirectory.isDirectory())
        return lastStateDirectory;

    const auto documents = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory);
    return documents.isDirectory() ? documents : juce::File::getSpecialLocation(juce::File::userHomeDirectory);
}

void TrackerMainUI::openStateSaveDialog()
{
    stateDialogMode = StateDialogMode::savePrompt;
    stateDialogColumn = stateDialogRow = 0;
    stateDialogError.clear();
    stateDialogErrorFromSave = false;
    pendingStateFile = juce::File();
    stateSaveIncludesAudio = false;
}

void TrackerMainUI::openStateLoadDialog()
{
    stateDialogMode = StateDialogMode::loadPrompt;
    stateDialogColumn = stateDialogRow = 0;
    stateDialogError.clear();
    stateDialogErrorFromSave = false;
    pendingStateFile = juce::File();
}

void TrackerMainUI::closeStateDialog()
{
    if (stateDialogMode == StateDialogMode::savingAudio)
        return;
    stateFileBrowser.close();
    stateDialogMode = StateDialogMode::none;
    stateDialogBoxes.clear();
    stateFilenameEditing = false;
    pendingStateFile = juce::File();
    grabKeyboardFocus();
}

void TrackerMainUI::beginStateBrowser(bool saving, bool includeAudio)
{
    stateFileBrowser.open(initialStateDirectory());
    stateDialogMode = saving ? StateDialogMode::saveBrowser : StateDialogMode::loadBrowser;
    stateDialogColumn = stateDialogRow = 0;
    stateFileEntryCursor = 0;
    stateFileListScroll = 0;
    stateFilenameEditing = false;
    stateSaveIncludesAudio = saving && includeAudio;
    if (saving)
    {
        const auto timestamp = juce::Time::getCurrentTime().formatted("%Y-%m-%d_%H-%M-%S");
        stateSaveName = "MYK-Tracker-" + timestamp + ".myktracker";
    }
}

juce::File TrackerMainUI::selectedStateSaveFile(juce::String& error) const
{
    auto name = sanitizeStateFileName(stateSaveName);
    if (name.isEmpty() || name == ".myktracker")
    {
        error = "ENTER A FILE NAME";
        return juce::File();
    }
    if (!name.endsWithIgnoreCase(".myktracker"))
        name += ".myktracker";
    return stateFileBrowser.getDirectory().getChildFile(name);
}

void TrackerMainUI::saveStateToSelectedFile()
{
    juce::String error;
    const auto file = selectedStateSaveFile(error);
    if (file.getFullPathName().isEmpty())
    {
        stateDialogError = error;
        stateDialogErrorFromSave = true;
        stateDialogMode = StateDialogMode::error;
        return;
    }
    if (file.existsAsFile())
    {
        pendingStateFile = file;
        stateDialogMode = StateDialogMode::overwriteConfirm;
        stateDialogColumn = 0;
        stateDialogRow = 0;
        return;
    }

    if (stateSaveIncludesAudio)
    {
        startPortableSave(file);
        return;
    }
    const bool saved = [&]()
        {
            juce::MemoryBlock state;
            audioProcessor.getStateInformation(state);
            return state.getSize() > 0 && file.replaceWithData(state.getData(), state.getSize());
        }();
    if (!saved)
    {
        stateDialogError = error.isNotEmpty() ? error : "COULD NOT SAVE " + file.getFileName();
        stateDialogErrorFromSave = true;
        stateDialogMode = StateDialogMode::error;
        return;
    }
    lastStateDirectory = file.getParentDirectory();
    closeStateDialog();
}

void TrackerMainUI::startPortableSave(const juce::File& file)
{
    juce::MemoryBlock state;
    std::vector<PortableSampleAsset> samples;
    juce::String error;
    if (!audioProcessor.capturePortableState(state, samples, error))
    {
        stateDialogError = error.isNotEmpty() ? error : "COULD NOT SAVE " + file.getFileName();
        stateDialogErrorFromSave = true;
        stateDialogMode = StateDialogMode::error;
        return;
    }

    pendingStateFile = file;
    stateDialogMode = StateDialogMode::savingAudio;
    portableSaveFuture = std::async(std::launch::async,
                                    [file, state = std::move(state), samples = std::move(samples)]() mutable
    {
        juce::String writeError;
        const bool ok = portable_tracker_state::write(file, state, samples, writeError);
        return std::make_pair(ok, writeError);
    });
}

void TrackerMainUI::pollPortableSave()
{
    if (stateDialogMode != StateDialogMode::savingAudio || !portableSaveFuture.valid()
        || portableSaveFuture.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready)
        return;

    const auto result = portableSaveFuture.get();
    if (result.first)
    {
        lastStateDirectory = pendingStateFile.getParentDirectory();
        // The saving state intentionally ignores Escape/normal close requests.
        // Leave it before performing the completion close.
        stateDialogMode = StateDialogMode::saveBrowser;
        closeStateDialog();
        return;
    }
    stateDialogError = result.second.isNotEmpty() ? result.second : "COULD NOT WRITE PORTABLE STATE";
    stateDialogErrorFromSave = true;
    stateDialogMode = StateDialogMode::error;
}

void TrackerMainUI::loadSelectedStateFile(const juce::File& file)
{
    juce::String error;
    if (!audioProcessor.loadTrackerStateFile(file, error))
    {
        stateDialogError = error.isNotEmpty() ? error : "COULD NOT LOAD " + file.getFileName();
        stateDialogErrorFromSave = false;
        stateDialogMode = StateDialogMode::error;
        return;
    }
    lastStateDirectory = file.getParentDirectory();
    closeStateDialog();
}

void TrackerMainUI::rebuildStateDialogBoxes()
{
    stateDialogBoxes.assign(2, {});
    const auto pushRow = [this](UIBox left, UIBox right = UIBox{})
    {
        left.kind = UIBox::Kind::TrackerCell;
        if (right.kind == UIBox::Kind::None && right.text.empty() && !right.onActivate)
            right.isDisabled = true;
        else
            right.kind = UIBox::Kind::TrackerCell;
        stateDialogBoxes[0].push_back(std::move(left));
        stateDialogBoxes[1].push_back(std::move(right));
    };
    const auto action = [] (const juce::String& text, std::function<void()> callback)
    {
        UIBox box;
        box.text = text.toStdString();
        box.onActivate = std::move(callback);
        return box;
    };

    if (stateDialogMode == StateDialogMode::savePrompt)
    {
        pushRow(action("WITH AUDIO", [this] { beginStateBrowser(true, true); }), action("STATE ONLY", [this] { beginStateBrowser(true, false); }));
        pushRow(action("CANCEL", [this] { closeStateDialog(); }));
    }
    else if (stateDialogMode == StateDialogMode::loadPrompt)
        pushRow(action("LOAD", [this] { beginStateBrowser(false); }), action("CANCEL", [this] { closeStateDialog(); }));
    else if (stateDialogMode == StateDialogMode::overwriteConfirm)
        pushRow(action("REPLACE", [this]
        {
            juce::String error;
            const auto file = pendingStateFile;
            pendingStateFile = juce::File();
            if (file.existsAsFile())
            {
                if (stateSaveIncludesAudio)
                {
                    startPortableSave(file);
                    return;
                }
                const bool saved = [&]()
                    {
                        juce::MemoryBlock state;
                        audioProcessor.getStateInformation(state);
                        return state.getSize() > 0 && file.replaceWithData(state.getData(), state.getSize());
                    }();
                if (saved)
                {
                    lastStateDirectory = file.getParentDirectory();
                    closeStateDialog();
                    return;
                }
            }
            stateDialogError = error.isNotEmpty() ? error : "COULD NOT REPLACE FILE";
            stateDialogErrorFromSave = true;
            stateDialogMode = StateDialogMode::error;
        }), action("CANCEL", [this] { pendingStateFile = juce::File(); stateDialogMode = StateDialogMode::saveBrowser; }));
    else if (stateDialogMode == StateDialogMode::error)
        pushRow(action("BACK", [this]
        {
            stateDialogMode = stateDialogErrorFromSave ? StateDialogMode::saveBrowser : StateDialogMode::loadBrowser;
            stateDialogError.clear();
            pendingStateFile = juce::File();
        }));
    else if (stateDialogMode == StateDialogMode::savingAudio)
    {
        UIBox working;
        working.text = "SAVING AUDIO";
        working.isDisabled = true;
        pushRow(std::move(working));
    }
    else
    {
        UIBox path = action(stateFileBrowser.getDirectory().getFullPathName(), [this] { closeStateDialog(); });
        path.width = 3.0f;
        pushRow(std::move(path), action("CANCEL", [this] { closeStateDialog(); }));
        if (stateDialogMode == StateDialogMode::saveBrowser)
        {
            UIBox name = action("NAME: " + stateSaveName + (stateFilenameEditing ? "_" : ""), [this] { stateFilenameEditing = !stateFilenameEditing; });
            name.width = 3.0f;
            pushRow(std::move(name), action("SAVE", [this] { saveStateToSelectedFile(); }));
        }
        else
        {
            UIBox find = action(stateFileBrowser.getSearchQuery().isEmpty() ? "FIND: _" : "FIND: " + stateFileBrowser.getSearchQuery(), [] {});
            find.width = 3.0f;
            pushRow(std::move(find));
        }
        UIBox up = action("../", [this]
        {
            stateFileBrowser.browseUp();
            if (stateDialogMode == StateDialogMode::saveBrowser)
                moveStateBrowserCursorToTopEntry();
            else
                stateDialogRow = 2;
        });
        up.width = 3.0f;
        pushRow(std::move(up));
        juce::String preferredFocusName;
        if (stateDialogMode == StateDialogMode::loadBrowser)
            preferredFocusName = stateFileBrowser.consumePreferredFocusName();
        else
            stateFileBrowser.consumePreferredFocusName();
        const auto entries = stateFileBrowser.getEntries(isTrackerStateFile);
        constexpr std::size_t maxVisibleFileEntries = 8;
        if (entries.empty())
        {
            stateFileEntryCursor = 0;
            stateFileListScroll = 0;
        }
        else
        {
            stateFileEntryCursor = std::min(stateFileEntryCursor, entries.size() - 1);
            const auto maxScroll = entries.size() > maxVisibleFileEntries
                ? entries.size() - maxVisibleFileEntries : 0;
            stateFileListScroll = std::min(stateFileListScroll, maxScroll);
            if (stateFileEntryCursor < stateFileListScroll)
                stateFileListScroll = stateFileEntryCursor;
            else if (stateFileEntryCursor >= stateFileListScroll + maxVisibleFileEntries)
                stateFileListScroll = stateFileEntryCursor - maxVisibleFileEntries + 1;
        }

        if (preferredFocusName.isNotEmpty())
            for (std::size_t entryIndex = 0; entryIndex < entries.size(); ++entryIndex)
                if (entries[entryIndex].isDirectory && entries[entryIndex].file.getFileName() == preferredFocusName)
                {
                    stateFileEntryCursor = entryIndex;
                    stateFileListScroll = entryIndex >= maxVisibleFileEntries
                        ? entryIndex - maxVisibleFileEntries + 1 : 0;
                    stateDialogRow = 3 + (stateFileEntryCursor - stateFileListScroll);
                    stateDialogColumn = 0;
                    break;
                }

        const auto lastVisibleEntry = std::min(entries.size(), stateFileListScroll + maxVisibleFileEntries);
        for (std::size_t entryIndex = stateFileListScroll; entryIndex < lastVisibleEntry; ++entryIndex)
        {
            const auto& entry = entries[entryIndex];
            UIBox name = action((entry.isDirectory ? "[D] " : "[F] ") + entry.file.getFileName(), [this, entry]
            {
                if (entry.isDirectory)
                {
                    stateFileBrowser.browseInto(entry.file);
                    if (stateDialogMode == StateDialogMode::saveBrowser)
                        moveStateBrowserCursorToTopEntry();
                }
                else if (stateDialogMode == StateDialogMode::saveBrowser)
                {
                    stateSaveName = entry.file.getFileName();
                    stateDialogRow = 1;
                    stateDialogColumn = 0;
                }
            });
            name.width = 3.0f;
            UIBox right;
            if (!entry.isDirectory && stateDialogMode == StateDialogMode::loadBrowser)
                right = action("LOAD", [this, entry] { loadSelectedStateFile(entry.file); });
            pushRow(std::move(name), std::move(right));
        }

        if (stateDialogRow >= 3 && !entries.empty())
            stateDialogRow = 3 + (stateFileEntryCursor - stateFileListScroll);
    }
    const auto rows = stateDialogBoxes[0].size();
    if (rows == 0) stateDialogRow = stateDialogColumn = 0;
    else stateDialogRow = std::min(stateDialogRow, rows - 1);
}

void TrackerMainUI::prepareStateDialogView()
{
    samplerViewActive = false;
    TrackerUIComponent::Style style;
    style.background = palette.background;
    style.lightColor = palette.lightColor;
    style.defaultGlowColor = palette.gridPlayhead;
    style.ambientStrength = palette.ambientStrength;
    style.lightDirection = palette.lightDirection;
    uiComponent.setStyle(style);
    uiComponent.setCellSize(cellWidth, cellHeight);
    rebuildStateDialogBoxes();
    const bool browserVisible = stateDialogMode == StateDialogMode::saveBrowser
        || stateDialogMode == StateDialogMode::loadBrowser;
    customMachineColumnWidthsActive = browserVisible;
    samplerColumnWidths.clear();
    if (browserVisible)
    {
        samplerColumnWidths.assign(stateDialogBoxes.size(), 1.0f);
        for (std::size_t col = 0; col < stateDialogBoxes.size(); ++col)
            for (const auto& cell : stateDialogBoxes[col])
                samplerColumnWidths[col] = std::max(samplerColumnWidths[col], cell.width);
    }
    for (std::size_t col = 0; col < stateDialogBoxes.size(); ++col)
        for (std::size_t row = 0; row < stateDialogBoxes[col].size(); ++row)
            stateDialogBoxes[col][row].isSelected = col == stateDialogColumn && row == stateDialogRow;
    updateCellStates(stateDialogBoxes, stateDialogBoxes.empty() ? 1 : stateDialogBoxes[0].size(), 2);
    overlayState.text = stateDialogMode == StateDialogMode::savePrompt ? "SAVE TRACKER STATE"
        : stateDialogMode == StateDialogMode::loadPrompt ? "LOAD TRACKER STATE"
        : stateDialogMode == StateDialogMode::overwriteConfirm ? "REPLACE EXISTING FILE?"
        : stateDialogMode == StateDialogMode::error ? stateDialogError.toStdString()
        : stateDialogMode == StateDialogMode::savingAudio ? "SAVING AUDIO"
        : stateDialogMode == StateDialogMode::saveBrowser ? "SAVE TRACKER STATE"
        : "LOAD TRACKER STATE";
    overlayState.color = stateDialogMode == StateDialogMode::error ? palette.textWarning : palette.textPrimary;
    overlayState.glowColor = palette.gridPlayhead;
    overlayState.glowStrength = 0.35f;
}

void TrackerMainUI::activateStateDialogCell()
{
    rebuildStateDialogBoxes();
    if (stateDialogColumn >= stateDialogBoxes.size() || stateDialogRow >= stateDialogBoxes[stateDialogColumn].size())
        return;
    auto& box = stateDialogBoxes[stateDialogColumn][stateDialogRow];
    if (!box.isDisabled && box.onActivate)
        box.onActivate();
}

void TrackerMainUI::moveStateDialogCursor(int rowDelta, int colDelta)
{
    rebuildStateDialogBoxes();
    if (stateDialogBoxes.empty() || stateDialogBoxes[0].empty())
        return;

    const bool browser = stateDialogMode == StateDialogMode::saveBrowser
        || stateDialogMode == StateDialogMode::loadBrowser;
    if (browser && rowDelta != 0)
    {
        const auto entries = stateFileBrowser.getEntries(isTrackerStateFile);
        if (rowDelta < 0 && stateDialogRow >= 3)
        {
            if (stateFileEntryCursor > 0)
            {
                --stateFileEntryCursor;
                if (stateFileEntryCursor < stateFileListScroll)
                    stateFileListScroll = stateFileEntryCursor;
                stateDialogRow = 3 + (stateFileEntryCursor - stateFileListScroll);
            }
            else
            {
                stateDialogRow = 2;
                stateDialogColumn = 0;
            }
            return;
        }
        if (rowDelta > 0 && stateDialogRow >= 2 && !entries.empty())
        {
            if (stateDialogRow == 2)
                stateFileEntryCursor = 0;
            else if (stateFileEntryCursor + 1 < entries.size())
                ++stateFileEntryCursor;
            else
                return;

            constexpr std::size_t maxVisibleFileEntries = 8;
            if (stateFileEntryCursor >= stateFileListScroll + maxVisibleFileEntries)
                stateFileListScroll = stateFileEntryCursor - maxVisibleFileEntries + 1;
            stateDialogRow = 3 + (stateFileEntryCursor - stateFileListScroll);
            stateDialogColumn = 0;
            return;
        }
    }

    const auto rows = stateDialogBoxes[0].size();
    if (rowDelta != 0)
        stateDialogRow = static_cast<std::size_t>(juce::jlimit(0, static_cast<int>(rows) - 1, static_cast<int>(stateDialogRow) + rowDelta));
    if (colDelta != 0)
    {
        const auto candidate = static_cast<std::size_t>(juce::jlimit(0, 1, static_cast<int>(stateDialogColumn) + colDelta));
        if (stateDialogRow < stateDialogBoxes[candidate].size() && !stateDialogBoxes[candidate][stateDialogRow].isDisabled)
            stateDialogColumn = candidate;
    }
}

void TrackerMainUI::moveStateBrowserCursorToTopEntry()
{
    // Rows 0-2 are the path, save name/search, and ../ controls. The first
    // actual directory/file entry is row 3; rebuild clamps this safely when a
    // folder has no visible entries.
    stateDialogColumn = 0;
    stateDialogRow = 3;
    stateFileEntryCursor = 0;
    stateFileListScroll = 0;
}

bool TrackerMainUI::handleStateDialogKey(const juce::KeyPress& key)
{
    const auto code = key.getKeyCode();
    if (code == juce::KeyPress::escapeKey) { closeStateDialog(); return true; }
    if (stateDialogMode == StateDialogMode::saveBrowser && stateFilenameEditing)
    {
        if (code == juce::KeyPress::returnKey) { stateFilenameEditing = false; return true; }
        if (code == juce::KeyPress::backspaceKey) { stateSaveName = stateSaveName.dropLastCharacters(1); return true; }
        const auto character = key.getTextCharacter();
        if (character >= 32 && character <= 126) { stateSaveName += juce::String::charToString(character); return true; }
    }
    if (stateDialogMode == StateDialogMode::loadBrowser)
    {
        if (code == juce::KeyPress::backspaceKey) { stateFileBrowser.backspaceSearch(); return true; }
        const auto character = key.getTextCharacter();
        if (character >= 32 && character <= 126) { stateFileBrowser.appendSearch(character); return true; }
    }
    if (code == juce::KeyPress::leftKey)
    {
        if (stateDialogMode == StateDialogMode::saveBrowser || stateDialogMode == StateDialogMode::loadBrowser)
        {
            stateFileBrowser.browseUp();
            if (stateDialogMode == StateDialogMode::saveBrowser)
                moveStateBrowserCursorToTopEntry();
        }
        else moveStateDialogCursor(0, -1);
        return true;
    }
    if (code == juce::KeyPress::rightKey)
    {
        if (stateDialogMode == StateDialogMode::saveBrowser)
        {
            // SAVE is intentionally fixed above the scrolling list, so it is
            // always reachable in one movement from any file entry.
            stateDialogColumn = 1;
            stateDialogRow = 1;
        }
        else
        {
            moveStateDialogCursor(0, 1);
        }
        return true;
    }
    if (code == juce::KeyPress::upKey) { moveStateDialogCursor(-1, 0); return true; }
    if (code == juce::KeyPress::downKey) { moveStateDialogCursor(1, 0); return true; }
    if (code == juce::KeyPress::returnKey) { activateStateDialogCell(); return true; }
    return true;
}
