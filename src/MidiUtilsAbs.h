#pragma once 

class MidiUtilsAbs{
    public:
        MidiUtilsAbs() {}
        /** send the all notes off message */
        virtual void allNotesOff() = 0;
        /** play a note - would generally trigger a note on now
         * and schedule a note off for later
         */
        virtual void playSingleNote(unsigned short channel, unsigned short note, unsigned short velocity, long offTick) = 0;
        /**
         * send any queued notes, e.g. note offs 
         * q'd by playSingleNote
        */
        virtual void sendQueuedMessages(long tick) = 0;
};