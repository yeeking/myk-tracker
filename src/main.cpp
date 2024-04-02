#include <atomic>
#include <chrono>
#include <thread>
#include <vector>
#include <ncurses.h>
#include "gui.h"

std::atomic<int> playbackPosition(0);
std::vector<std::vector<int>> grid;
// Main loop
GUI gui;

void initGrid(std::vector<std::vector<int>>& grid, int rows, int cols);

void initGrid(std::vector<std::vector<int>>& grid, int rows, int cols) {
    grid.resize(rows);
    for (int row = 0; row < rows; row++) {
        if (grid[row].size() != cols) grid[row].resize(cols);
        for (int col = 0; col < cols; col++) {
            grid[row][col] = (row + col) % 10; // Example initialization
            // grid[i][j] += 100;
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

    initGrid(grid, 20, 10);
    // std::thread playbackThread(playbackThreadFunction, grid.size());
    
    int ch;

    while ((ch = getch()) != 'q') {
        gui.keyPressed(ch, grid);
        
        gui.draw(grid, playbackPosition);
    }

    // playbackThread.join(); // Ensure the playback thread has fini
    return 0;
}