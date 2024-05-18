#pragma once

#include <JuceHeader.h>

class RaggedTableComponent : public juce::Component
{
public:
    enum class CellState {
        NotSelected,
        Cursor,
        Highlight
    };

    RaggedTableComponent();
    void paint(juce::Graphics& g) override;

    void draw(std::vector<std::vector<std::string>>& data, int rowsToDisplay, int colsToDisplay, int cursorX, int cursorY, std::vector<std::pair<int, int>> highlightCells);
    void resized() override;

private:
    std::vector<std::vector<std::string>> tableData;
    int rowsVisible;
    int colsVisible;
    std::pair<int, int> cursorPosition;
    std::vector<std::pair<int, int>> highlightedCells;

    void drawCell(juce::Graphics& g, int x, int y, const std::string& value, CellState state);
    CellState getCellState(int x, int y);
};

