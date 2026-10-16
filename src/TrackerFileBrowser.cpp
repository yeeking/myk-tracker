#include "TrackerFileBrowser.h"

#include <algorithm>

void TrackerFileBrowser::open(const juce::File& directory)
{
    currentDirectory = directory.isDirectory()
        ? directory : juce::File::getSpecialLocation(juce::File::userHomeDirectory);
    searchQuery.clear();
    pendingFocusName.clear();
}

void TrackerFileBrowser::close()
{
    currentDirectory = juce::File();
    searchQuery.clear();
    pendingFocusName.clear();
}

void TrackerFileBrowser::appendSearch(juce::juce_wchar character)
{
    if (character >= 32 && character <= 126)
        searchQuery += juce::String::charToString(character).toLowerCase();
}

void TrackerFileBrowser::backspaceSearch()
{
    searchQuery = searchQuery.dropLastCharacters(1);
}

void TrackerFileBrowser::clearSearch()
{
    searchQuery.clear();
}

void TrackerFileBrowser::browseUp()
{
    if (!currentDirectory.isDirectory())
        return;
    const auto parent = currentDirectory.getParentDirectory();
    if (parent == currentDirectory)
        return;
    pendingFocusName = currentDirectory.getFileName();
    currentDirectory = parent;
    searchQuery.clear();
}

bool TrackerFileBrowser::browseInto(const juce::File& directory)
{
    if (!directory.isDirectory())
        return false;
    currentDirectory = directory;
    searchQuery.clear();
    pendingFocusName.clear();
    return true;
}

std::vector<TrackerFileBrowser::Entry> TrackerFileBrowser::getEntries(
    const std::function<bool(const juce::File&)>& includeFile) const
{
    std::vector<Entry> directories;
    std::vector<Entry> files;
    if (!currentDirectory.isDirectory())
        return {};

    for (juce::DirectoryEntry entry : juce::RangedDirectoryIterator(currentDirectory, false, "*", juce::File::findFilesAndDirectories))
    {
        const auto file = entry.getFile();
        if (file.getFileName().startsWithChar('.'))
            continue;
        if (!searchQuery.isEmpty() && !file.getFileName().containsIgnoreCase(searchQuery))
            continue;
        if (file.isDirectory())
            directories.push_back({ file, true });
        else if (includeFile && includeFile(file))
            files.push_back({ file, false });
    }

    const auto sortByName = [] (const Entry& a, const Entry& b)
    {
        return a.file.getFileName().compareIgnoreCase(b.file.getFileName()) < 0;
    };
    std::sort(directories.begin(), directories.end(), sortByName);
    std::sort(files.begin(), files.end(), sortByName);
    directories.insert(directories.end(), files.begin(), files.end());
    return directories;
}

juce::String TrackerFileBrowser::consumePreferredFocusName()
{
    return std::exchange(pendingFocusName, {});
}
