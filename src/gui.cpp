#include "gui.h"
#include <cassert> 
#include <iostream>


GridWidget::GridWidget(int displayHeightInRows, int displayWidthInCols) : displayHeightInRows{displayHeightInRows}, displayWidthInCols{displayWidthInCols}, cursorRow{0}, cursorCol{0}
{

}

GridWidget::~GridWidget()
{

}
void GridWidget::addGridListener(GridListener* listener)
{
    this->listener = listener; 
}

void GridWidget::draw(WINDOW* win, std::vector<std::vector<int>>& data, std::vector<std::pair<int, int>> highlightCells, int cellWidth, int cellHeight)
{
    // std::lock_guard<std::mutex> lock(drawMutex);
    werase(win); // Clear the screen
    // wmove(win, 1, 1);
    // box(win, 0, 0); // Draw a box around the edges of the window
    // if (cursorRow >= data.size()) cursorRow = data.size()-1;
    int maxRow = displayStartRow + displayHeightInRows;
    int maxCol = displayStartCol + displayWidthInCols;
    if (maxRow >= data.size()) maxRow = data.size(); 
    if (maxCol >= data[0].size()) maxCol = data[0].size();
    if (cursorRow >= maxRow) cursorRow = maxRow -1;
    if (cursorCol >= maxCol) cursorCol = maxCol -1;
    
    assert(maxRow <= data.size());
    assert(maxCol <= data[0].size());
    
    for (int row = displayStartRow; row < maxRow; ++row) {

        // if (cursorCol >= data[row].size()) cursorRow = data[row].size()-1;
        for (int col = displayStartCol;col < maxCol; ++col){
            // Calculate position
            int x = (col - displayStartCol) * cellWidth;
            int y = (row - displayStartRow) * cellHeight;
            // Determine if the current cell is selected
            CellState state{CellState::NotSelected};
            //  =  i == cursorRow && j == cursorCol ? CellState::Editing : CellState::NotSelected;
            // if (i == playbackPosition){state = CellState::Playing;}
            
            if (row == cursorRow && col == cursorCol){state = CellState::Editing;}
            
            // Draw the cell
            std::string v = std::to_string(data[row][col]);
            drawCell(win, v , x, y, cellWidth-1, state);
        }
    }
    wrefresh(win);
}

void GridWidget::drawCell(WINDOW* win, std::string value, int x, int y, int cellWidth, CellState state) {
    // Set color based on selection
    if (state == CellState::Editing) wattron(win, COLOR_PAIR(NOSEL_COLOR_PAIR));
    else if (state == CellState::Playing) wattron(win, COLOR_PAIR(PLAY_COLOR_PAIR));
    else if (state == CellState::NotSelected) wattron(win, COLOR_PAIR(SEL_COLOR_PAIR));

    // Draw box for the cell
    mvwaddch(win, y, x, ACS_ULCORNER);
    mvwhline(win, y, x + 1, ACS_HLINE, cellWidth);
    mvwaddch(win, y, x + cellWidth, ACS_URCORNER);
    mvwvline(win, y + 1, x, ACS_VLINE, 1);
    mvwvline(win, y + 1, x + cellWidth, ACS_VLINE, 1);
    mvwaddch(win, y + 2, x, ACS_LLCORNER);
    mvwhline(win, y + 2, x + 1, ACS_HLINE, cellWidth);
    mvwaddch(win, y + 2, x + cellWidth, ACS_LRCORNER);

    // Print value in the center of the box
    mvwprintw(win, y + 1, x + 2, "%s", value.c_str()); 

    // Reset color
    attroff(COLOR_PAIR(SEL_COLOR_PAIR));
    attroff(COLOR_PAIR(NOSEL_COLOR_PAIR));
}

void GridWidget::cursorLeft()
{   
    cursorCol --; 
    if (cursorCol< 0) cursorCol = 0;
    // work out if startCol changes
    if (cursorCol < displayStartCol) displayStartCol = cursorCol;
    //std::cout << "left " << cursorCol << ":" << displayStartCol<< std::endl;
}
void GridWidget::cursorRight()
{
    cursorCol++;// note I will limit it to the data width when draw receives its data
    // work out if startCol changes 
    if (cursorCol >= displayStartCol + displayWidthInCols){
        displayStartCol ++;
    } 
    //std::cout << "right " << cursorCol << ":" << displayStartCol<< std::endl;    
}
void GridWidget::cursorUp()
{
    cursorRow --; 
    if (cursorRow < 0) cursorRow = 0;
    if (cursorRow < displayStartRow) displayStartRow = cursorRow;
    //std::cout << "up " << cursorRow << ":" << displayStartRow << std::endl;

}
void GridWidget::cursorDown()
{
    cursorRow++; // will limit to data height when we get data in draw
    if (cursorRow >= displayStartRow + displayHeightInRows) {
        // cursor out of bounds. Move bounds along one
        displayStartRow ++; 
    }
    //std::cout << "down " << cursorRow << ":" << displayStartRow << std::endl;
}



GUI::GUI(int heightInRows, int widthInCols)  : seqGrid{heightInRows, widthInCols}
{

    seqFocus = true; 
    activeGrid = &seqGrid;
    initGUI();
}

GUI::~GUI()
{
    endwin(); // End curses mode

    delete seqPanel;
    delete buttonPanel;
    delete seqWin;
    delete buttonWin;
}

void GUI::initGUI()
{
   // Initialize ncurses
    initscr();
    start_color(); // Initialize color functionality
    cbreak();
    noecho();
    keypad(stdscr, TRUE); // Enable keyboard mapping
    curs_set(0); // Hide the cursor

    // Initialize colors
    init_pair(SEL_COLOR_PAIR, COLOR_WHITE, COLOR_BLACK); // Foreground color pair
    init_pair(NOSEL_COLOR_PAIR, COLOR_BLACK, COLOR_WHITE); // Background color pair
    init_pair(PLAY_COLOR_PAIR, COLOR_BLUE, COLOR_RED); // Background color pair


    seqWin = newwin(DISPLAY_ROWS*CELL_HEIGHT, DISPLAY_COLS*CELL_WIDTH, 1, 1);
    seqPanel = new_panel(seqWin);
    buttonWin = newwin(DISPLAY_ROWS*CELL_HEIGHT, CELL_WIDTH * 4, 1, DISPLAY_COLS*CELL_WIDTH + 1 );
    buttonPanel = new_panel(buttonWin);

    update_panels();
    doupdate();
}

void GUI::drawControlPanel(WINDOW* win){
    // wmove(win, 1, 1);
    werase(win);
    wprintw(win, "[Button 1]  [Button 2]  [Button 3]");
    wrefresh(win);
    
}


int GUI::min(int a, int b) {
    return a < b ? a : b;
}


void GUI::keyPressed(int ch, std::vector<std::vector<int>>& grid)
{
    switch (ch) {
        case '\t':
            seqFocus = !seqFocus;
            if (seqFocus) {
                //top_panel(seqPanel);
            } else {
                //top_panel(buttonPanel);
            }
            break;
        case KEY_UP:
            if (!seqFocus) break;
            activeGrid->cursorUp();


            // if (cursorRow > 0) cursorRow--;
            // else if (startRow > 0) startRow--;
            break;
        case KEY_DOWN:
            if (!seqFocus) break;
            activeGrid->cursorDown();

            // if (cursorRow < GUIUtils::min(DISPLAY_ROWS, totalGridRows - startRow) - 1) cursorRow++;
            // else if (startRow < totalGridRows - DISPLAY_ROWS) startRow++;
            break;
        case KEY_LEFT:
            if (!seqFocus) break;
            activeGrid->cursorLeft();

            // if (cursorCol > 0) cursorCol--;
            // else if (startCol > 0) startCol--;
            break;
        case KEY_RIGHT:
            if (!seqFocus) break;
            activeGrid->cursorRight();

            // if (cursorCol < GUIUtils::min(DISPLAY_COLS, totalGridCols - startCol) - 1) cursorCol++;
            // else if (startCol < totalGridCols - DISPLAY_COLS) startCol++;
            break;
        default:
            // if (ch >= '0' && ch <= '9') { // Edit the current cell
            //     grid[startRow + cursorRow][startCol + cursorCol] = ch - '0';
            // }
            break;
    }
}

void GUI::draw(std::vector<std::vector<int>>& grid, int playbackPos)
{
    seqGrid.draw(seqWin, grid, std::vector<std::pair<int, int>>(), CELL_WIDTH, CELL_HEIGHT);   
    update_panels();
    doupdate();
}