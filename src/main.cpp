#include <atomic>
#include <chrono>
#include <thread>
#include <vector>
#include <ncurses.h>
#include "gui.h"
#include "Sequencer.h"
#include "SequencerEditor.h"
#include "SequencerCommands.h"
#include "SimpleClock.h"
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


    std::map<int,char> getIntToNoteMap()
    {
    std::map<int, char> intToNote = 
      {
        {0, 'c'}, 
        {1, 'C'},
        {2, 'd'},
        {3, 'D'},
        {4, 'e'},
        {5, 'f'},
        {6, 'F'},
        {7, 'g'},
        {8, 'G'},
        {9, 'a'},
        {10, 'A'},
        {11, 'b'}    
      };
      return intToNote;
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
    CommandProcessor::initialiseMIDI();
    SimpleClock clock;
    
    std::map<char, double> key_to_note = getKeyboardToMidiNotes();
    // maintains the data and sate of the sequencer
    Sequencer sequencer{4, 16};
    // maintains a stateful editor - knows the edit mode, etc. 
    SequencerEditor editor{&sequencer};   
    GUI gui{&sequencer, &editor};
    
    
    clock.setCallback([&sequencer, &gui](){
        sequencer.tick();
        gui.draw();

    });

    clock.start(50);

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
                editor.enterDataAtCursor(key_note.second); 
                break;// break the for loop
            }
        }
        switch (ch) {
            case '\t':
                break;
            case '-':
                editor.removeRow();
                break;
            case '=':
                editor.addRow();
                break;
            case '[':
                editor.decrementAtCursor();
                break;
            case ']':
                editor.incrementAtCursor();
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
            case 'p':
                if (editor.getEditMode() == SequencerEditorMode::editingStep){
                    sequencer.triggerStep(editor.getCurrentSequence(), editor.getCurrentStep(), editor.getCurrentStepRow());
                }
                // sequencer
                break;
            default:
                break;
        }
        
        gui.draw();
    }
    clock.stop();
    // playbackThread.join(); // Ensure the playback thread has fini
    return 0;
}