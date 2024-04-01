#include <atomic>
#include <chrono>
#include <thread>
#include <vector>
#include <ncurses.h>
#include <mutex>


#define DISPLAY_ROWS 5
#define DISPLAY_COLS 10
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
int gridRows = 5; // Total rows in grid
int gridCols = 10; // Total cols in grid
std::vector<std::vector<int>> grid(gridRows, std::vector<int>(gridCols));
int startRow = 0, startCol = 0; // Top-left corner of the grid section being displayed
int cursorRow = 0, cursorCol = 0; // Cursor position within the displayed section


// Function prototypes
void initGrid(std::vector<std::vector<int>>& grid, int rows, int cols);
void drawTable(const std::vector<std::vector<int>>& grid, 
                int startRow, int startCol, 
                int cursorRow, int cursorCol,
                int displayRows, int displayCols,  
                int totalRows, int totalCols);
void drawCell(int value, int x, int y, CellState state);
int min(int a, int b);


void playbackThreadFunction(int maxPosition) {
    while (true) { // Add a condition for a graceful shutdown if needed
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
        int newPosition = (playbackPosition + 1) % maxPosition;
        playbackPosition.store(newPosition);
        // Assuming drawTable() can be safely called from this thread;
        // if not, you'll need to implement a thread-safe way to trigger drawing from the main thread.
        // drawTable(); 
        // printf("tick %i", playbackPosition.load());

        drawTable(grid, startRow, startCol, 
              cursorRow, cursorCol, 
              DISPLAY_ROWS, DISPLAY_COLS, 
              gridRows, gridCols);
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

void drawTable(const std::vector<std::vector<int>>& grid, 
                int startRow, int startCol, 
                int cursorRow, int cursorCol,
                int displayRows, int displayCols,  
                int totalRows, int totalCols){
    std::lock_guard<std::mutex> lock(drawTableMutex);
    erase(); // Clear the screen
    int cellHeight = 3, cellWidth = 4; // Dimens`ions of each cell including borders
    for (int i = 0; i < min(displayRows, totalRows - startRow); i++) {
        for (int j = 0; j < min(displayCols, totalCols - startCol); j++) {
            // Calculate position
            int x = j * cellWidth, y = i * cellHeight;
            // Determine if the current cell is selected
            CellState state =  i == cursorRow && j == cursorCol ? CellState::Editing : CellState::NotSelected;
            if (i == playbackPosition){state = CellState::Playing;}
            // Draw the cell
            drawCell(grid[startRow + i][startCol + j], x, y, state);
        }
    }
    refresh();
}

void drawCell(int value, int x, int y, CellState state) {
    // Set color based on selection
    if (state == CellState::Editing) attron(COLOR_PAIR(NOSEL_COLOR_PAIR));
    else if (state == CellState::Playing) attron(COLOR_PAIR(PLAY_COLOR_PAIR));
    else if (state == CellState::NotSelected) attron(COLOR_PAIR(SEL_COLOR_PAIR));

    // Draw box for the cell
    mvaddch(y, x, ACS_ULCORNER);
    mvhline(y, x + 1, ACS_HLINE, 2);
    mvaddch(y, x + 3, ACS_URCORNER);
    mvvline(y + 1, x, ACS_VLINE, 1);
    mvvline(y + 1, x + 3, ACS_VLINE, 1);
    mvaddch(y + 2, x, ACS_LLCORNER);
    mvhline(y + 2, x + 1, ACS_HLINE, 2);
    mvaddch(y + 2, x + 3, ACS_LRCORNER);

    // Print value in the center of the box
    mvprintw(y + 1, x + 1, "%d", value);

    // Reset color
    attroff(COLOR_PAIR(SEL_COLOR_PAIR));
    attroff(COLOR_PAIR(NOSEL_COLOR_PAIR));
}

int min(int a, int b) {
    return a < b ? a : b;
}




int main() {

    std::thread playbackThread(playbackThreadFunction, gridRows);

    // Initialize ncurses
    initscr();
    start_color(); // Initialize color functionality
    cbreak();
    noecho();
    keypad(stdscr, TRUE); // Enable keyboard mapping

    // Initialize colors
    init_pair(SEL_COLOR_PAIR, COLOR_WHITE, COLOR_BLACK); // Foreground color pair
    init_pair(NOSEL_COLOR_PAIR, COLOR_BLACK, COLOR_WHITE); // Background color pair
    init_pair(PLAY_COLOR_PAIR, COLOR_BLUE, COLOR_RED); // Background color pair

    // Initialize the grid
    initGrid(grid, gridRows, gridCols);

    // Initial draw
    drawTable(grid, startRow, startCol, 
              cursorRow, cursorCol, 
              DISPLAY_ROWS, DISPLAY_COLS, 
              gridRows, gridCols);


    // Main loop
    int ch;
    while ((ch = getch()) != 'q') {
        switch (ch) {
            case KEY_UP:
                if (cursorRow > 0) cursorRow--;
                else if (startRow > 0) startRow--;
                break;
            case KEY_DOWN:
                if (cursorRow < min(DISPLAY_ROWS, gridRows - startRow) - 1) cursorRow++;
                else if (startRow < gridRows - DISPLAY_ROWS) startRow++;
                break;
            case KEY_LEFT:
                if (cursorCol > 0) cursorCol--;
                else if (startCol > 0) startCol--;
                break;
            case KEY_RIGHT:
                if (cursorCol < min(DISPLAY_COLS, gridCols - startCol) - 1) cursorCol++;
                else if (startCol < gridCols - DISPLAY_COLS) startCol++;
                break;
            default:
                if (ch >= '0' && ch <= '9') { // Edit the current cell
                    grid[startRow + cursorRow][startCol + cursorCol] = ch - '0';
                }
                break;
        }
        
        drawTable(grid, startRow, startCol, 
              cursorRow, cursorCol, 
              DISPLAY_ROWS, DISPLAY_COLS, 
              gridRows, gridCols);
    }

    playbackThread.join(); // Ensure the playback thread has fini
    endwin(); // End curses mode

    return 0;
}