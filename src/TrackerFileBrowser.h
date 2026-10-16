#pragma once

#include <JuceHeader.h>

#include <functional>
#include <vector>

/** Shared, message-thread-only directory browser state for tracker grids. */
class TrackerFileBrowser
{
public:
    struct Entry
    {
        juce::File file;
        bool isDirectory = false;
    };

    void open(const juce::File& directory);
    void close();
    bool isOpen() const { return currentDirectory.isDirectory(); }
    const juce::File& getDirectory() const { return currentDirectory; }
    const juce::String& getSearchQuery() const { return searchQuery; }
    void appendSearch(juce::juce_wchar character);
    void backspaceSearch();
    void clearSearch();
    void browseUp();
    bool browseInto(const juce::File& directory);
    std::vector<Entry> getEntries(const std::function<bool(const juce::File&)>& includeFile) const;
    juce::String consumePreferredFocusName();

private:
    juce::File currentDirectory;
    juce::String searchQuery;
    juce::String pendingFocusName;
};
