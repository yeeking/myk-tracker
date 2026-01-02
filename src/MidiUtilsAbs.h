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
     /** maps from integer values, i.e. midi notes modded on 12
   * to note names, getIntToDrumMap()[0] == 'c' as c is note 0/12/24 etc.
   */
  static std::map<int, char> getIntToNoteMap()
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
            {11, 'b'}};
    return intToNote;
  }

  /** maps from integer values, i.e. midi notes modded on 12 or 24 to drum
   * names, e.g. 0->B for bassdrum. Can be used to display one character drum
   * names
   */
  static std::map<int, char> getIntToDrumMap()
  {
    std::map<int, char> intToDrum =
        {
            {0, 'B'},
            {1, 's'},
            {2, 'S'},
            {3, 'r'},
            {4, 'H'},
            {5, 'h'},
            {6, 't'},
            {7, 'T'},
            {8, 'c'},
            {9, 'R'},
            {10, 'C'},
            {11, 'p'}};
    return intToDrum;
  }
  /**
   * returns a mapping from a scale starting at note 48 ... 59
   * mapped to midi notes for drums remapped to get the most useful drums
   */
  static std::map<int, int> getScaleMidiToDrumMidi()
  {
    std::map<int, int> scaleToDrum =
        {
            {48, 36},
            {49, 38},
            {50, 40},
            {51, 37},
            {52, 42},
            {53, 46},
            {54, 50},
            {55, 45},
            {56, 39},
            {57, 51},
            {58, 57},
            {59, 75}};
    return scaleToDrum;
  }

  /** maps from drum names (e.g. B, s) to general midi notes
   * e.g. B(bass drum) -> 36.
   */
  static std::map<char, int> getDrumToMidiNoteMap()
  {
    std::map<char, int> drumToInt =
        {
            {'B', 36},
            {'s', 38},
            {'S', 40},
            {'r', 37},
            {'H', 42},
            {'h', 46},
            {'t', 50},
            {'T', 45},
            {'c', 39},
            {'R', 51},
            {'C', 57},
            {'p', 75}};
    return drumToInt;
  }

//   static std::map<char, double> getKeyboardToMidiNotes(int transpose = 0)
//   {
//     std::map<char, double> key_to_note =
//         {
//             {'z', 48 + transpose},
//             {'s', 49 + transpose},
//             {'x', 50 + transpose},
//             {'d', 51 + transpose},
//             {'c', 52 + transpose},
//             {'v', 53 + transpose},
//             {'g', 54 + transpose},
//             {'b', 55 + transpose},
//             {'h', 56 + transpose},
//             {'n', 57 + transpose},
//             {'j', 58 + transpose},
//             {'m', 59 + transpose}};
//     return key_to_note;
//   }
};