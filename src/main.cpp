#include <atomic>
#include <chrono>
#include <thread>
#include <vector>
#include <ncurses.h>
#include "gui.h"
#include "Sequencer.h"

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
        gui.draw(grid, playbackPosition);
    }
}

int main() {
    Sequencer sequencer{10, 10};
    
    initGrid(grid, 10, 10);
    // std::thread playbackThread(playbackThreadFunction, grid.size());
    
    int ch;

    while ((ch = getch()) != 'q') {
        gui.keyPressed(ch);
        sequencer.prepareGridView(grid);
        gui.draw(grid, playbackPosition);
    }

    // playbackThread.join(); // Ensure the playback thread has fini
    return 0;
}