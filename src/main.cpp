#include <atomic>
#include <chrono>
#include <thread>
#include <vector>
#include <mutex>
#include <string> 
#include <ncurses.h>
#include <panel.h>

// how many there are
#define START_ROWS 20
#define START_COLS 10
// how many to display
#define DISPLAY_ROWS 10
#define DISPLAY_COLS 4
#define CELL_WIDTH 7
#define CELL_HEIGHT 3

#define SEL_COLOR_PAIR 1
#define NOSEL_COLOR_PAIR 2
#define PLAY_COLOR_PAIR 3

enum class CellState {
    NotSelected,
    Editing,
    Playing
};


std::atomic<int> playbackPosition(0);
std::mutex drawTableMutex;
int totalGridRows = START_ROWS; // Total rows in grid
int totalGridCols = START_COLS; // Total cols in grid
std::vector<std::vector<int>> grid(totalGridRows, std::vector<int>(totalGridCols));
int startRow = 0, startCol = 0; // Top-left corner of the grid section being displayed
int cursorRow = 0, cursorCol = 0; // Cursor position within the displayed section

// Function prototypes
void initGrid(std::vector<std::vector<int>>& grid, int rows, int cols);
void drawTable(WINDOW* win, 
                const std::vector<std::vector<int>>& grid, 
                int startRow, int startCol, 
                int cursorRow, int cursorCol,
                int displayRows, int displayCols,  
                int totalRows, int totalCols);
void drawCell(WINDOW* win, std::string value, int x, int y, int cellWidth, CellState state);
void drawControlPanel(WINDOW* win);

int min(int a, int b);


void playbackThreadFunction(WINDOW* win, int maxPosition) {
    while (true) { // Add a condition for a graceful shutdown if needed
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
        int newPosition = (playbackPosition + 1) % maxPosition;
        playbackPosition.store(newPosition);
        // Assuming drawTable() can be safely called from this thread;
        // if not, you'll need to implement a thread-safe way to trigger drawing from the main thread.
        // drawTable(); 
        // printf("tick %i", playbackPosition.load());

        drawTable(win, grid, startRow, startCol, 
              cursorRow, cursorCol, 
              DISPLAY_ROWS, DISPLAY_COLS, 
              totalGridRows, totalGridCols);
    }
}


void initGrid(std::vector<std::vector<int>>& grid, int rows, int cols) {
    for (int i = 0; i < rows; i++) {
        for (int j = 0; j < cols; j++) {
            grid[i][j] = (i + j) % 10; // Example initialization
            grid[i][j] += 100;
        }
    }
}

void drawControlPanel(WINDOW* win){
    // wmove(win, 1, 1);
    werase(win);
    wprintw(win, "[Button 1]  [Button 2]  [Button 3]");
    wrefresh(win);
    
}

void drawTable(WINDOW* win, const std::vector<std::vector<int>>& grid, 
                int startRow, int startCol, 
                int cursorRow, int cursorCol,
                int displayRows, int displayCols,  
                int totalRows, int totalCols){
    std::lock_guard<std::mutex> lock(drawTableMutex);
    werase(win); // Clear the screen
    // wmove(win, 1, 1);
    // box(win, 0, 0); // Draw a box around the edges of the window

    for (int i = 0; i < min(displayRows, totalRows - startRow); i++) {
        for (int j = 0; j < min(displayCols, totalCols - startCol); j++) {
            // Calculate position
            int x = j * CELL_WIDTH, y = i * CELL_HEIGHT;
            // Determine if the current cell is selected
            CellState state{CellState::NotSelected};
            //  =  i == cursorRow && j == cursorCol ? CellState::Editing : CellState::NotSelected;
            if (i == playbackPosition){state = CellState::Playing;}
            if (i == cursorRow && j == cursorCol){state = CellState::Editing;}
            
            // Draw the cell
            std::string v = std::to_string(grid[startRow + i][startCol + j]);
            drawCell(win, v , x, y, CELL_WIDTH-1, state);
        }
    }
    wrefresh(win);

}

void drawCell(WINDOW* win, std::string value, int x, int y, int cellWidth, CellState state) {
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

int min(int a, int b) {
    return a < b ? a : b;
}


int main() {

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

    // set up the panels
    WINDOW* seqWin = newwin(DISPLAY_ROWS*CELL_HEIGHT, DISPLAY_COLS*CELL_WIDTH, 1, 1);
    PANEL* seqPanel = new_panel(seqWin);
    WINDOW* buttonWin = newwin(DISPLAY_ROWS*CELL_HEIGHT, CELL_WIDTH * 4, 1, DISPLAY_COLS*CELL_WIDTH + 1 );
    PANEL* buttonPanel = new_panel(buttonWin);

 
    update_panels();
    doupdate();

    // wprintw(scrollWin, "Scrollable content here.\\ \\ \\ Use arrow keys to scroll.");
    // wmove(buttonWin, 1, 1);
    // wprintw(buttonWin, "[Button 1]  [Button 2]  [Button 3]");



    // Initialize the grid
    initGrid(grid, totalGridRows, totalGridCols);

    // Initial draw
    drawTable(seqWin, grid,  startRow, startCol, 
              cursorRow, cursorCol, 
              DISPLAY_ROWS, DISPLAY_COLS, 
              totalGridRows, totalGridCols);
    
    drawControlPanel(buttonWin);

    // 
    std::thread playbackThread(playbackThreadFunction, seqWin, totalGridRows);


    // Main loop
    int ch;
    bool seqFocus = true;

    while ((ch = getch()) != 'q') {
        switch (ch) {
            case '\t':
                seqFocus = !seqFocus;
                if (seqFocus) {
                    top_panel(seqPanel);
                } else {
                    top_panel(buttonPanel);
                }
                break;
            case KEY_UP:
                if (!seqFocus) break;

                if (cursorRow > 0) cursorRow--;
                else if (startRow > 0) startRow--;
                break;
            case KEY_DOWN:
                if (!seqFocus) break;
                
                if (cursorRow < min(DISPLAY_ROWS, totalGridRows - startRow) - 1) cursorRow++;
                else if (startRow < totalGridRows - DISPLAY_ROWS) startRow++;
                break;
            case KEY_LEFT:
                if (!seqFocus) break;
                
                if (cursorCol > 0) cursorCol--;
                else if (startCol > 0) startCol--;
                break;
            case KEY_RIGHT:
                if (!seqFocus) break;
                
                if (cursorCol < min(DISPLAY_COLS, totalGridCols - startCol) - 1) cursorCol++;
                else if (startCol < totalGridCols - DISPLAY_COLS) startCol++;
                break;
            default:
                if (ch >= '0' && ch <= '9') { // Edit the current cell
                    grid[startRow + cursorRow][startCol + cursorCol] = ch - '0';
                }
                break;
        }

        drawTable(seqWin, grid, startRow, startCol, 
              cursorRow, cursorCol, 
              DISPLAY_ROWS, DISPLAY_COLS, 
              totalGridRows, totalGridCols);
        update_panels();
        doupdate();
    

    }

    playbackThread.join(); // Ensure the playback thread has fini
    endwin(); // End curses mode
    delete seqWin;
    delete buttonWin;
    return 0;
}