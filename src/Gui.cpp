#include "Gui.h"


GridWidget::GridWidget() : lastStartCol{0}, lastStartRow{0}
{

}

GridWidget::~GridWidget()
{

}

void GridWidget::addGridListener(GridListener* listener)
{
    this->listener = listener; 
}

void GridWidget::draw(WINDOW* win, std::vector<std::vector<std::string>>& data, 
                      int rowsToDisplay, int colsToDisplay, 
                      int cursorCol, int cursorRow, 
                      // col, row/ x,y
                      std::vector<std::pair<int, int>> highlightCells)
{
    werase(win); // Clear the screen
    // assume we do not need to move the viewing window
    int startRow = lastStartRow;
    int startCol = lastStartCol;
    int endRow = startRow + rowsToDisplay;
    int endCol = startCol + colsToDisplay;
    // but if the cursor is out of bounds, need to redo-it 
    // if (cursorRow < startRow) startRow = cursorRow; // move view up
    // if (cursorRow >= endRow) startRow = cursorRow - rowsToDisplay + 1;
    if (cursorCol < startCol) startCol = cursorCol; // move view up
    if (cursorCol >= endCol) startCol = cursorCol - colsToDisplay + 1;
    if (cursorRow < startRow) startRow = cursorRow;
    if (cursorRow >= endRow) startRow = cursorRow - rowsToDisplay + 1;
    endRow = startRow + rowsToDisplay;
    endCol = startCol + colsToDisplay;
    
    // make sure we are not displaying beyond the size of the data
    if (endRow >= data[0].size()) endRow = data[0].size();
    if (endCol >= data.size()) endCol = data.size();
    
    // work out how big the cells can be based on the view 
    int winWidth, winHeight;
    getmaxyx(win, winHeight, winWidth); // query size of window
    
    if (endRow >= data[0].size()) endRow = data[0].size();
    if (endCol >= data.size()) endCol = data.size();
    

    int cellWidth = winWidth / colsToDisplay;
    int cellHeight = winHeight / rowsToDisplay;
    cellHeight = 3; 
    
    // cellHeight = 3;
    for (int row = startRow; row < endRow; ++row) {
        for (int col = startCol;col < endCol; ++col){
            // Calculate absolute x position in UI
            int x = (col - startCol) * cellWidth;
            int y = (row - startRow) * cellHeight;
            // Determine if the current cell is selected
            CellState state{CellState::NotSelected};
            
            // check against the highlight cells
            for (const std::pair<int, int>& p :  highlightCells){
                if (p.first == col && p.second == row){
                    state = CellState::Playing;
                    break;
                }
            }
            // editing supercedes playback highlighter 
            if (row == cursorRow && col == cursorCol){state = CellState::Editing;}
            
            // Draw the cell
            assert (data.size() >= col);
            // std::cout << "want row " << row << " got rows " << data[col].size() << std::endl;
            assert (data[col].size() >= row);
            
            drawCell(win, data[col][row], x, y, cellWidth-1, state);
        }
    }
    lastStartCol = startCol;
    lastStartRow = startRow;
    wrefresh(win);
}

void GridWidget::drawCell(WINDOW* win, std::string& value, int x, int y, int cellWidth, CellState state) {
    // Set color based on selection
    if (state == CellState::Playing) wattron(win, COLOR_PAIR(PLAY_COLOR_PAIR));
    else if (state == CellState::Editing) wattron(win, COLOR_PAIR(SEL_COLOR_PAIR));
    else if (state == CellState::NotSelected) wattron(win, COLOR_PAIR(NOSEL_COLOR_PAIR));

    // Draw box for the cell
    mvwaddch(win, y, x, ACS_ULCORNER);
    mvwhline(win, y, x + 1, ACS_HLINE, cellWidth);  
    mvwaddch(win, y, x + cellWidth, ACS_URCORNER);
    mvwvline(win, y + 1, x, ACS_VLINE, 1);
    mvwvline(win, y + 1, x + cellWidth, ACS_VLINE, 1);
    mvwaddch(win, y + 2, x, ACS_LLCORNER);
    mvwhline(win, y + 2, x + 1, ACS_HLINE, cellWidth);
    mvwaddch(win, y + 2, x + cellWidth, ACS_LRCORNER);
    
    // now set it back to the regular colour for the 
    // cell contents
    wattron(win, COLOR_PAIR(NOSEL_COLOR_PAIR));

    // Print value in the center of the box
    mvwprintw(win, y + 1, x + 2, "%s", value.c_str()); 
    // mvwprintw(win, y + 1, x + 2, "%s", "test"); 

    // Reset color
    attroff(COLOR_PAIR(SEL_COLOR_PAIR));
    attroff(COLOR_PAIR(NOSEL_COLOR_PAIR));
}

GUI::GUI(Sequencer* _sequencer, SequencerEditor* _seqEditor) : sequencer{_sequencer}, seqEditor{_seqEditor}, rw_mutex{std::make_unique<std::shared_mutex>()}
{
    seqFocus = true; 
    activeGrid = &seqGrid;
    initGUI();
}

GUI::~GUI()
{
    endwin(); // End curses mode

    free(seqPanel);// this will automatically free the wrapped win object 
    free(buttonPanel); // this tool
//    free(seqWin);
  //  free(buttonWin);
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

    init_color(COLOR_ORANGE, 1000, 500, 0);  // orange
    init_color(COLOR_YELLOWB, 1000, 1000, 0); // yellow bright
    init_color(COLOR_GREY, 500, 500, 500); // yellow bright

    // Initialize colors
    init_pair(SEL_COLOR_PAIR, COLOR_BLACK, COLOR_GREY); // Background color pair
    // init_pair(SEL_COLOR_PAIR, COLOR_GREY, COLOR_BLACK); // Background color pair
    
    init_pair(NOSEL_COLOR_PAIR, COLOR_WHITE, COLOR_BLACK); // Foreground color pair
    init_pair(PLAY_COLOR_PAIR, COLOR_ORANGE, COLOR_BLACK); // Background color pair

    
//    seqWin = newwin(DISPLAY_ROWS*CELL_HEIGHT, DISPLAY_COLS*CELL_WIDTH, 1, 1);
    seqWin = newwin(70, 100, 3, 0); // height, width, y offset, x offset 
    seqPanel = new_panel(seqWin);

    buttonWin = newwin(3, 100, 0, 0);
    buttonPanel = new_panel(buttonWin);

    update_panels();
    doupdate();
}

void GUI::drawControlPanel(WINDOW* win){
    // wmove(win, 1, 1);
    werase(win);
    std::string cursorStatus = 
        std::to_string(seqEditor->getCurrentSequence()) + ":" 
        + std::to_string(seqEditor->getCurrentStep()) + "["
        + std::to_string(sequencer->howManySteps(seqEditor->getCurrentSequence())) + "]";
    // add the info about the current if editing a step 
    if (seqEditor->getEditMode() == SequencerEditorMode::editingStep){
        int rowsInStep = sequencer->howManyStepDataRows(seqEditor->getCurrentSequence(), seqEditor->getCurrentStep());
        cursorStatus += ":" + std::to_string(seqEditor->getCurrentStepRow()) + "[" + std::to_string(rowsInStep) + "]";
    }

    std::string mode;
    switch(seqEditor->getEditMode()){
        case SequencerEditorMode::configuringSequence:
            mode = "Conf";
            break;
        case SequencerEditorMode::editingStep:
            mode = "Step";
            break;
        case SequencerEditorMode::selectingSeqAndStep:
            mode = "Seq";
            break;
             
    }

    std::vector<std::vector<std::string>> buttons = {{cursorStatus}, {"> play"},{"[] stop"}, {mode}};
    seqControlGrid.draw(win, buttons, 1, 6, 2, 0, std::vector<std::pair<int, int>>());

    // wprintw(win, "[Button 1]  [Button 2]  [Button 3]");
    wrefresh(win);   
}

int GUI::min(int a, int b) {
    return a < b ? a : b;
}

void GUI::draw()
{
    // uni lock as writing data
    std::unique_lock<std::shared_mutex> lock(*rw_mutex);

    // if(false){
    switch(seqEditor->getEditMode()){
        case SequencerEditorMode::selectingSeqAndStep:
        {
            drawSequenceView();
            break;
        }
        case SequencerEditorMode::editingStep:
        {
            drawStepView();
            break; 
        }
        case SequencerEditorMode::configuringSequence:
        {
            drawSeqConfigView();
            break;
        }
    }

    drawControlPanel(buttonWin);
    update_panels();
    doupdate();
}

void GUI::drawSequenceView()
{
    std::vector<std::pair<int, int>> playHeads;
    for (int col=0;col<sequencer->howManySequences(); ++col){  
        std::pair<int, int> colRow = {col, sequencer->getCurrentStep(col)};
        playHeads.push_back(std::move(colRow));
    }
    seqGrid.draw(seqWin, sequencer->getSequenceAsGridOfStrings(), 
                8, 6, 
                seqEditor->getCurrentSequence(), 
                seqEditor->getCurrentStep(), 
                playHeads);

}

void GUI::drawStepView()
{
    // Step* step = sequencer->getStep(seqEditor->getCurrentSequence(), seqEditor->getCurrentStep());
    // std::vector<std::vector<std::string>> grid = step->toStringGrid();
    std::vector<std::pair<int, int>> playHeads;
    if (sequencer->getCurrentStep(seqEditor->getCurrentSequence()) == seqEditor->getCurrentStep()){
        int cols = sequencer->howManyStepDataCols(seqEditor->getCurrentSequence(), seqEditor->getCurrentStep());
        for (int col=0;col<cols;++col){
            playHeads.push_back(std::pair(col, 0));
        }
    }
    std::vector<std::vector<std::string>> grid = sequencer->getStepAsGridOfStrings(seqEditor->getCurrentSequence(), seqEditor->getCurrentStep());
        stepGrid.draw(seqWin, 
        grid, 
        8, 6, 
        seqEditor->getCurrentStepCol(), 
        seqEditor->getCurrentStepRow(), 
        playHeads);
}

void GUI::drawSeqConfigView()
{
    std::vector<std::vector<std::string>> grid = sequencer->getSequenceConfigsAsGridOfStrings();
    stepGrid.draw(seqWin, 
                grid, 
        8, 6, 
        seqEditor->getCurrentSequence(), 
        seqEditor->getCurrentSeqParam(), 
        std::vector<std::pair<int, int>>());   
}

