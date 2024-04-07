#include <atomic>
#include <chrono>
#include <thread>
#include <vector>
#include <ncurses.h>
#include "gui.h"
#include "Sequencer.h"
#include "SequencerUtils.h"

std::atomic<int> playbackPosition(0);
// Main loop
GUI gui{};

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
    Sequencer sequencer{5, 100};
    // maintains a stateful editor - knows the edit mode, etc. 
    SequencerEditor editor{&sequencer};   
    // create a grid suitable for storing the data from the sequencer 
    // the sequencerr will write to this grid when we ask it to
    std::vector<std::vector<std::string>> grid(10, std::vector<std::string>(2));


    sequencer.prepareGridView(grid);
    gui.draw(grid, editor.getCurrentSequence(), editor.getCurrentStep(), std::vector<std::pair<int, int>>());

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
        }
        
        sequencer.prepareGridView(grid);
        gui.draw(grid, editor.getCurrentSequence(), editor.getCurrentStep(), std::vector<std::pair<int, int>>());                
    }

    // playbackThread.join(); // Ensure the playback thread has fini
    return 0;
}