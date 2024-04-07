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
    GUI gui{&sequencer, &editor};
    
    gui.draw();
    
    int ch;

    while ((ch = getch()) != 'q') {
        switch (ch) {
            case '\t':
                break;
            case KEY_ENTER:
                editor.enterAtCursor();
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
        
        gui.draw();
    }

    // playbackThread.join(); // Ensure the playback thread has fini
    return 0;
}