#pragma once 

#include <map>

class MidiUtilsAbs{
    public:
        MidiUtilsAbs() {}
        /** send the all notes off message */
        virtual void allNotesOff() = 0;
        /** play a note - would generally trigger a note on now
         * and schedule a note off for later
         */
        virtual void playSingleNote(unsigned short channel, unsigned short note, unsigned short velocity, unsigned short durInTicks) = 0;
        /**
         * send any queued notes, e.g. note offs 
         * q'd by playSingleNote
        */
        virtual void sendQueuedMessages(long tick) = 0;

    static std::map<char, double> getKeyboardToMidiNotes(int transpose = 0)
    {
        std::map<char, double> key_to_note =
        {
        { 'z', 0+transpose},
        { 's', 1+transpose},
        { 'x', 2+transpose},
        { 'd', 3+transpose},
        { 'c', 4+transpose},
        { 'v', 5+transpose},
        { 'g', 6+transpose},
        { 'b', 7+transpose},
        { 'h', 8+transpose},
        { 'n', 9+transpose},
        { 'j', 10+transpose},
        { 'm', 11+transpose}
        };
        return key_to_note;
    }
};