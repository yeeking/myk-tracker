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
#define DISPLAY_ROWS 4
#define DISPLAY_COLS 2
#define CELL_WIDTH 7
#define CELL_HEIGHT 3

#define SEL_COLOR_PAIR 1
#define NOSEL_COLOR_PAIR 2
#define PLAY_COLOR_PAIR 3

enum class GUIState{
    SeqView, 
    StepView,
    TrackConfig
};

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
    /** create a grid which displays the sent amount of columns and rows. It can draw a bigger data structure than this but this limits what it actually displays*/
        GridWidget(int displayRows, int displayCols);
        ~GridWidget();
        /** draw the sent data as a grid. Does not show the whole grid, only from startCol/row to endCol/row
         * also highlight all cells in the highlight vector
         * note that the first dimension of data is the 'column'
         * the second dimension is the row 
        */
        void draw(WINDOW* win, std::vector<std::vector<std::string>>& data, int rowsToDisplay, int colsToDisplay, int cursorX, int cursorY, std::vector<std::pair<int, int>> highlightCells);
        /** register for grid moving events*/
        void addGridListener(GridListener* listener);

    private:
        void drawCell(WINDOW* win, std::string& value, int x, int y, int cellWidth, CellState state);
        GridListener* listener; 
        
        int cursorRow;
        int cursorCol;

};

class GUI {
    public:
        GUI();
        ~GUI();
        void draw(std::vector<std::vector<std::string>>& data, int cursorX, int cursorY, std::vector<std::pair<int, int>> highlightCells);
        
    private:
        void initGUI();
        void drawControlPanel(WINDOW* win);

        int min(int a, int b);

        // ncurses bits 
        WINDOW* seqWin;
        PANEL* seqPanel;
        WINDOW* buttonWin;
        PANEL* buttonPanel; 
        
        /** pointer to currently active grid*/
        GridWidget* activeGrid;

        /** grid widget for the sequencer */
        GridWidget seqGrid;
        /** grid widget for sequencer track controls*/
        GridWidget trackControlGrid;
        
        /** grid widget for editing steps */
        GridWidget stepGrid; 


        std::mutex drawTableMutex;
        bool seqFocus; 
};