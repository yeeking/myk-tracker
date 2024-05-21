#pragma once

#include <JuceHeader.h>
#include <mutex> 
#include <shared_mutex>

class StringTable : public juce::Component
{
public:
    enum class CellState {
        NotSelected,
        Cursor,
        Highlight
    };

    StringTable();
    void paint(juce::Graphics& g) override;
    void resized() override;

    void updateData(std::vector<std::vector<std::string>>& data, size_t rowsToDisplay, size_t colsToDisplay, size_t cursorX, size_t cursorY, std::vector<std::pair<int, int>> highlightCells, bool showCursor=true);
    
private:
    std::unique_ptr<std::shared_mutex> rw_mutex;
    std::vector<std::vector<std::string>> tableData;
    size_t rowsVisible;
    size_t colsVisible;
    std::pair<int, int> cursorPosition;
    std::vector<std::pair<int, int>> highlightedCells;
    bool showCursor; 
    
    void drawCell(juce::Graphics& g, int x, int y, const std::string& value, CellState state);
    CellState getCellState(int x, int y);

    // local variables used to store drawing state 
    // since the app calling 'draw' and the internal ui update repaint
    // are de-coupled
    size_t startCol;
    size_t endCol;
    size_t startRow;
    size_t endRow; 

    size_t lastStartCol;
    size_t lastStartRow; 
};

