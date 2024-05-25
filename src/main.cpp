#include <atomic>
#include <chrono>
#include <thread>
#include <vector>
#include <ncurses.h>
#include <signal.h>
#include "Gui.h"
#include "Sequencer.h"
#include "SequencerEditor.h"
#include "SequencerCommands.h"
#include "SimpleClock.h"
#include "MidiUtils.h"
#include "TrackerController.h"

std::atomic<int> playbackPosition(0);
// Main loop



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

// Signal handler for SIGINT
void handle_sigint(int sig) {
    // You can put your custom logic here
   // printw("Ctrl-C pressed, but we won't quit!\n");
  //  refresh();  // Refresh the screen to update the display
}

int main() {
    SimpleClock seqClock;
    SimpleClock guiClock;
    MidiUtils midiUtils;
    midiUtils.interactiveInitMidi();

    CommandProcessor::assignMasterClock(&seqClock);
    
    CommandProcessor::assignMidiUtils(&midiUtils);
    CommandProcessor::sendAllNotesOff();

    std::map<char, double> key_to_note = MidiUtilsAbs::getKeyboardToMidiNotes(0);
    // maintains the data and sate of the sequencer
    Sequencer sequencer{16, 8};
    // maintains a stateful editor - knows the edit mode, etc. 
    SequencerEditor editor{&sequencer};  

    TrackerController trackerController{&sequencer, &seqClock, &editor}; 

    GUI gui{&sequencer, &editor, &trackerController};

    // tell it to call a special function when the user uses ctrl-c to quit
    signal(SIGINT, handle_sigint);

    seqClock.setCallback([&sequencer, &seqClock, &editor](){
        CommandProcessor::sendQueuedMIDI(seqClock.getCurrentTick());
        sequencer.tick();
    });
    guiClock.setCallback([&gui](){
        gui.draw();
    });

    int intervalMs = 50;
    // seqClock.start(intervalMs);
    seqClock.setBPM(120);
    guiClock.start(1000 / 25);
    bool redraw = false; 
    
    int quitCount = 0;
    int ch;
    // keys to set length of a step 
    std::vector<char> lenKeys = {'q', 'w', 'e', 'r'};

    while(quitCount < 2){
        ch = getch();
        if (ch == 27) quitCount ++; // two hits on ESC to quit
        else quitCount = 0; 
        // while ((ch = getch()) != 'q') {
        // handle keyboard note input 
        // bool key_note_match{false};
        for (const std::pair<char, double>& key_note : key_to_note)
        {
            if (ch == key_note.first){ 
                // key_note_match = true;
                editor.enterStepData(key_note.second, Step::noteInd);
                break;// break the for loop
            }
        }
        // do the velocity controls
        for (int num=1;num<5;++num){
            if (ch == num + 48){
                editor.enterStepData(num * (128/4), Step::velInd);
                break; 
            }
        }
        for (int i=0;i<lenKeys.size();++i){
            if (lenKeys[i] == ch){
                editor.enterStepData(i+1, Step::lengthInd);
                break;
            }
        }
        switch (ch) {
            case 'R':
                CommandProcessor::sendAllNotesOff();
                sequencer.rewindAtNextZero();
                break;
            case ' ':
                CommandProcessor::sendAllNotesOff();
                if (sequencer.isPlaying()){
                    sequencer.stop();
                }
                else{
                    sequencer.rewindAtNextZero();
                    sequencer.play();
                }
                
                sequencer.rewindAtNextZero();
                break;
                  
            case '\t':
                editor.nextStep();
                break;
            case '-':
                editor.removeRow();
                break;
            case '=':
                editor.addRow();
                break;
            case '_':
                trackerController.decrementBPM();
                break;
            case '+':
                trackerController.incrementBPM();
                break;
                
            case '[':
                editor.decrementAtCursor();
                break;
            case ']':
                editor.incrementAtCursor();
                break;
            case ',':
                editor.decrementOctave();
                break;
            case '.':
                editor.incrementOctave();
                break;
            case 'M':
                sequencer.toggleSequenceMute(editor.getCurrentSequence());
                break;
            case KEY_DC:
                editor.resetAtCursor();
                CommandProcessor::sendAllNotesOff();
                break; 
            case '\n':
                editor.enterAtCursor();
                break;
            case 'S':
                editor.gotoSequenceConfigPage();
                break;
            case KEY_UP:
                editor.moveCursorUp();
                redraw = true; 
                break;
            case KEY_DOWN:
                editor.moveCursorDown();
                redraw = true; 
                break;
            case KEY_LEFT:
                editor.moveCursorLeft();
                redraw = true; 
                break;
            case KEY_RIGHT:
                editor.moveCursorRight();
                redraw = true; 
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
        sequencer.updateSeqStringGrid();
        // if (redraw) {gui.draw();redraw = false;}
    }
    CommandProcessor::sendAllNotesOff();
    seqClock.stop();
    guiClock.stop();
    return 0;
}