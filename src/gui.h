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
#define DISPLAY_ROWS 5
#define DISPLAY_COLS 3
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

class GridListener{
    public:
        GridListener();
        /** override this to receive events when a grid cell is entered*/
        virtual void cellEntered(int row, int col) = 0;

};

struct GUIUtils{
    static int min(int a, int b) {return a < b ? a : b;}
};

/**
 * represents a grid that you can move the cursor around. Does not store 
 * data
*/
class GridWidget{
    public:
        GridWidget();
        ~GridWidget();
        /** draw the sent data as a grid. Does not show the whole grid, only from startCol/row to endCol/row
         * also highlight all cells in the highlight vector
        */
        void draw(WINDOW* win, std::vector<std::vector<int>>& data, std::vector<std::pair<int, int>> highlightCells, int startRow, int startCol, int endRow, int endCol);
        void cursorLeft();
        void cursorRight();
        void cursorUp();
        void cursorDown();
        /** register for grid moving events*/
        void addGridListener(GridListener* listener);

    private:
        void drawCell(WINDOW* win, std::string value, int x, int y, int cellWidth, CellState state);
        GridListener* listener; 
        int cursorRow;
        int cursorCol;

};

class GUI {
    public:
        GUI();
        ~GUI();
        void keyPressed(int ch, std::vector<std::vector<int>>& grid);
        void draw(std::vector<std::vector<int>>& grid, int playbackPos);
        
    private:
        void initGUI();
        void drawTable(WINDOW* win, 
                        const std::vector<std::vector<int>>& grid, 
                        int startRow, int startCol, 
                        int cursorRow, int cursorCol, int playbackPosition, 
                        int displayRows, int displayCols,  
                        int totalRows, int totalCols);
        void drawCell(WINDOW* win, std::string value, int x, int y, int cellWidth, CellState state);
        void drawControlPanel(WINDOW* win);

        int min(int a, int b);

        WINDOW* seqWin;// 
        PANEL* seqPanel;// 
        WINDOW* buttonWin;//
        PANEL* buttonPanel;//  


        GridWidget seqGrid;

        std::mutex drawTableMutex;

        int totalGridRows;// = START_ROWS; // Total rows in grid
        int totalGridCols;// = START_COLS; // Total cols in grid
        // std::vector<std::vector<int>> grid;//(totalGridRows, std::vector<int>(totalGridCols));
        int startRow = 0;// 
        int startCol = 0; // Top-left corner of the grid section being displayed
        int cursorRow = 0;
        int cursorCol = 0; // Cursor position within the displayed section
        bool seqFocus; 
};