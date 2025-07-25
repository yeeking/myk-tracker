
#include "StringTable.h"

StringTable::StringTable()
: rw_mutex{std::make_unique<std::shared_mutex>()}, tableData{std::vector<std::vector<std::string>>()}, rowsVisible(0), colsVisible(0), cursorPosition(0, 0), startCol{0}, endCol{0}, startRow{0}, endRow{0}, lastStartCol{0}, lastStartRow{0}, showCursor{true}, armedSeq{4096}
{
}

void StringTable::updateData(std::vector<std::vector<std::string>>& data, size_t rowsToDisplay, size_t colsToDisplay, size_t cursorCol, size_t cursorRow, std::vector<std::pair<int, int>> highlightCells, bool _showCursor, size_t _armedSeq)
{
    // std::cout << "StringTable::updateData armed " << _armedY << std::endl;
    // unique lock is used when writing 
    // std::unique_lock<std::shared_mutex> lock(*rw_mutex);

    // check all the requested values
    // and set up the view on the grid 
    // according to its size and the previous view for nice cursor movement 
    showCursor = _showCursor;
    startRow = lastStartRow;
    startCol = lastStartCol;
    endRow = startRow + rowsToDisplay;
    endCol = startCol + colsToDisplay;

    if (cursorCol < startCol) startCol = cursorCol; // move view up
    if (cursorCol >= endCol) startCol = cursorCol - colsToDisplay + 1;
    if (cursorRow < startRow) startRow = cursorRow;
    if (cursorRow >= endRow) startRow = cursorRow - rowsToDisplay + 1;
    endRow = startRow + rowsToDisplay;
    endCol = startCol + colsToDisplay;
    // make sure we are not displaying beyond the size of the data
    if (endRow >= data[0].size()) endRow = data[0].size();
    if (endCol >= data.size()) endCol = data.size();
    
    lastStartCol = startCol;
    lastStartRow = startRow;

    // now copy all needed data to class member data so 
    // it can be accesses when paint is automatically called. 
    tableData = std::vector<std::vector<std::string>>{};
    // Copy the desired data
    for (size_t col = startCol; col < endCol; ++col) {
        std::vector<std::string> colData;
        for (size_t row = startRow; row < endRow; ++row) {
            colData.push_back(data[col][row]);
        }
        tableData.push_back(colData);
    }
    // tableData = data;
    rowsVisible = rowsToDisplay;
    colsVisible = colsToDisplay;
    cursorPosition = { cursorCol, cursorRow };
    highlightedCells = highlightCells;
    this->armedSeq = _armedSeq; 
    // repaint();
}

void StringTable::resized()
{
    // Adjust component sizes if necessary
}

void StringTable::paint(juce::Graphics& g)
{
    // shared lock is ok as this function only reads data 
    // std::shared_lock<std::shared_mutex> lock(*rw_mutex);

    if (colsVisible == 0){return;}
    

    int cellWidth = getWidth() / static_cast<int>(colsVisible);
    int cellHeight = getHeight() / static_cast<int>(rowsVisible);

    g.fillAll(juce::Colours::black);

    // std::cout << "ragged paint cell width and height" << cellWidth << "," << cellHeight << std::endl;
    for (size_t col = startCol; col < endCol; ++col)
    {
        for (size_t row = startRow; row < endRow; ++row)
        {
            // drawCell(g, (col - startCol) * cellWidth, (row-startRow) * cellHeight, tableData[col][row], getCellState(col, row));
            drawCell(g, 
            static_cast<int>(col - startCol) * cellWidth, // x
            static_cast<int>(row-startRow) * cellHeight, // y
            tableData[col-startCol][row-startRow], getCellState(col, row));
            
        }
    }
}

void StringTable::drawCell(juce::Graphics& g, int x, int y, const std::string& value, CellState state)
{
    juce::Colour colour = juce::Colours::white;
    if (state == CellState::Cursor && !showCursor){
        state = CellState::NotSelected;
    }   
 
    switch (state)
    {
        case CellState::Cursor:
            colour = juce::Colours::greenyellow;
            break;
        case CellState::Armed:
            colour = juce::Colours::red;
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

    // Graphics::ScopedSaveState stateGuard(g);
    juce::Rectangle<int> cellBounds(x, y, getWidth() / colsVisible, getHeight() / rowsVisible);

    g.setColour(juce::Colours::black);
    g.fillRect(cellBounds);
    g.setColour(colour);
    if (state == CellState::Cursor && showCursor){g.drawRect(cellBounds, 8);}
    {g.drawRect(cellBounds, 4);}
    
    g.setColour(juce::Colours::orange);
    // g.drawRect(cellBounds, 1); // Draw cell border
    g.setFont(cellBounds.getWidth() * 0.25);
    g.drawText(value, cellBounds, juce::Justification::centred, true);
}

StringTable::CellState StringTable::getCellState(int x, int y)
{
    // cursor has highest priority 
    if (x == cursorPosition.first && y == cursorPosition.second)
        return CellState::Cursor;
    // armed is next
    if (x == this->armedSeq) return CellState::Armed; 
    
    for (const auto& cell : highlightedCells)
    {
        if (cell.first == x && cell.second == y)
            return CellState::Highlight;
    }
    

    return CellState::NotSelected;
}
