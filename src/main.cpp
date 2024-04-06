#include <atomic>
#include <chrono>
#include <thread>
#include <vector>
#include <ncurses.h>
#include "gui.h"
#include "Sequencer.h"
#include "SequencerUtils.h"

std::atomic<int> playbackPosition(0);
std::vector<std::vector<std::string>> grid;
// Main loop
GUI gui{3, 3};

void initGrid(std::vector<std::vector<std::string>>& grid, int rows, int cols);

void initGrid(std::vector<std::vector<std::string>>& grid, int rows, int cols) {
    grid.resize(rows);
    for (int row = 0; row < rows; row++) {
        if (grid[row].size() != cols) grid[row].resize(cols);
        for (int col = 0; col < cols; col++) {
            grid[row][col] = std::to_string(col); 
        }
    }
}

void playbackThreadFunction(int maxPosition) {
    while (true) { // Add a condition for a graceful shutdown if needed
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
        int newPosition = (playbackPosition + 1) % maxPosition;
        playbackPosition.store(newPosition);
        // gui.draw(grid, playbackPosition);
    }
}

int main() {
    // maintains the data and sate of the sequencer
    Sequencer sequencer{10, 10};
    // maintains a stateful editor - knows the edit mode, etc. 
    SequencerEditor editor{&sequencer};   
    // create a grid suitable for storing the data from the sequencer 
    // the sequencerr will write to this grid when we ask it to
    initGrid(grid, 10, 10);
    
    int ch;

    while ((ch = getch()) != 'q') {
        switch (ch) {
            case '\t':
                break;
            case KEY_UP:
                editor.moveCursorUp();
                break;
            case KEY_DOWN:
                editor.moveCursorDown();
                break;
            case KEY_LEFT:
                editor.moveCursorLeft();
                break;
            case KEY_RIGHT:
                editor.moveCursorRight();
                break;
            default:
                break;
        }// end first checks on the key pressed 
            
        gui.keyPressed(ch);
        
        sequencer.prepareGridView(grid);
        gui.draw(grid, editor.getCurrentSequence(), editor.getCurrentStep(), std::vector<std::pair<int, int>>());

                 
    }

    // playbackThread.join(); // Ensure the playback thread has fini
    return 0;
}