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


std::map<char, double> getKeyboardToMidiNotes(int transpose = 0)
{
    std::map<char, double> key_to_note =
    {
    { 'z', 48+transpose},
    { 's', 49+transpose},
    { 'x', 50+transpose},
    { 'd', 51+transpose},
    { 'c', 52+transpose},
    { 'v', 53+transpose},
    { 'g', 54+transpose},
    { 'b', 55+transpose},
    { 'h', 56+transpose},
    { 'n', 57+transpose},
    { 'j', 58+transpose},
    { 'm', 59+transpose}
    };
    return key_to_note;
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

     std::map<char, double> key_to_note = getKeyboardToMidiNotes();
    // maintains the data and sate of the sequencer
    Sequencer sequencer{5, 100};
    // maintains a stateful editor - knows the edit mode, etc. 
    SequencerEditor editor{&sequencer};   
    GUI gui{&sequencer, &editor};
    gui.draw();
    
    int ch;
    while ((ch = getch()) != 'q') {
        // handle keyboard note input 
        // bool key_note_match{false};
        for (const std::pair<char, double>& key_note : key_to_note)
        {
            if (ch == key_note.first) 
            { 
                // key_note_match = true;
                editor.enterNoteData(key_note.second); 
                break;// break the for loop
            }
        }
        switch (ch) {
            case '\t':
                break;
            case KEY_DC:
                editor.resetAtCursor();
                break; 
            case '\n':
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