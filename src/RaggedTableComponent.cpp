
#include "RaggedTableComponent.h"

RaggedTableComponent::RaggedTableComponent()
: tableData{std::vector<std::vector<std::string>>()}, rowsVisible(0), colsVisible(0), cursorPosition(0, 0)
{
}

void RaggedTableComponent::draw(std::vector<std::vector<std::string>>& data, int rowsToDisplay, int colsToDisplay, int cursorX, int cursorY, std::vector<std::pair<int, int>> highlightCells)
{
    tableData = data;
    rowsVisible = rowsToDisplay;
    colsVisible = colsToDisplay;
    cursorPosition = { cursorX, cursorY };
    highlightedCells = highlightCells;
    repaint();
}

void RaggedTableComponent::resized()
{
    // Adjust component sizes if necessary
}

void RaggedTableComponent::paint(juce::Graphics& g)
{
    if (colsVisible == 0){return;}
    
    int cellWidth = getWidth() / colsVisible;
    int cellHeight = getHeight() / rowsVisible;
    g.fillAll(juce::Colours::black);

    // std::cout << "ragged paint cell width and height" << cellWidth << "," << cellHeight << std::endl;
    for (int col = 0; col < std::min(colsVisible, (int)tableData.size()); ++col)
    {
        for (int row = 0; row < std::min(rowsVisible, (int)tableData[col].size()); ++row)
        {
            drawCell(g, col * cellWidth, row * cellHeight, tableData[col][row], getCellState(col, row));
        }
    }
}

void RaggedTableComponent::drawCell(juce::Graphics& g, int x, int y, const std::string& value, CellState state)
{
    juce::Rectangle<int> cellBounds(x, y, getWidth() / colsVisible, getHeight() / rowsVisible);
    juce::Colour colour = juce::Colours::white;
    switch (state)
    {
        case CellState::Cursor:
            colour = juce::Colours::blue;
            break;
        case CellState::Highlight:
            colour = juce::Colours::orange;
            break;
        case CellState::NotSelected:
            colour = juce::Colours::grey;
            break;     
        default:
            colour = juce::Colours::grey;
            break;
    }

    Graphics::ScopedSaveState stateGuard(g);
    g.setColour(juce::Colours::black);
    g.fillRect(cellBounds);
    g.setColour(colour);
    g.drawRect(cellBounds, 4);
    
    g.setColour(juce::Colours::orange);
    // g.drawRect(cellBounds, 1); // Draw cell border
    g.setFont(cellBounds.getHeight() * 0.25);
    g.drawText(value, cellBounds, juce::Justification::centred, true);
}

RaggedTableComponent::CellState RaggedTableComponent::getCellState(int x, int y)
{
    if (x == cursorPosition.first && y == cursorPosition.second)
        return CellState::Cursor;

    for (const auto& cell : highlightedCells)
    {
        if (cell.first == x && cell.second == y)
            return CellState::Highlight;
    }

    return CellState::NotSelected;
}
