#pragma once

#include <map>
#include <list>

// // Check for Windows
// #ifdef _WIN32
//     #include "path/to/windows/RtMidi.h"

// // Check for Mac OS
// #elif defined(__APPLE__)
//     #include "/opt/homebrew/include/rtmidi/RtMidi.h"

// // Assume Linux for any other case
// #else
//    #include "/usr/include/rtmidi/RtMidi.h"

// #endif

#include "../libs/rtmidi/RtMidi.h"

#include <thread> // std::this_thread::sleep_for
#include <chrono> // std::chrono::seconds
#include "MidiUtilsAbs.h"

typedef std::vector<unsigned char> MidiMessage;
typedef std::vector<MidiMessage> MidiMessageVector;

struct TimeStampedMessages
{
  long timestamp;
  MidiMessageVector messages;
};
/**
 * Maintains a linked list of midi messages tagged with timestamps
 */
class MidiQueue
{
public:
  MidiQueue();
  /** q a message at the specified time point*/
  void addMessage(const long timestamp, const MidiMessage &msg);
  /** get all q's messages for the specified time point and remove them from the linked likst */
  MidiMessageVector getAndClearMessages(long timestamp);
  /** removes all the messages form the q*/
  void clearAllMessages();

private:
  std::list<TimeStampedMessages> messageList;
};

/**
 *
 */
class MidiUtils : public MidiUtilsAbs
{
public:
  MidiUtils();
  ~MidiUtils();
  /** stores the midi out port */
  RtMidiOut *midiout;

  /** Presents command line promots to the user to allow them to initiate MIDI outpu */
  void interactiveInitMidi();

  /** returns a list of midi output devices */
  std::vector<std::string> getOutputDeviceList();

  /** opens the sent output device, ready for use
   * Should be in the range 0->(number of ports-1) inclusive
   */
  void selectOutputDevice(unsigned int deviceId);
  /** send note off messages on all channels to all notes */
  void allNotesOff() override;

  /** play a note */
  void playSingleNote(unsigned short channel, unsigned short note, unsigned short velocity, long offTick) override; 

  /** send any messages that are due to be sent at the sent tick
   * generally this means note offs.
   */
  void sendQueuedMessages(long tick) override;
  bool portIsReady() const;
private:
  bool portReady;
  MidiQueue midiQ;
  bool panicMode;
  void queueNoteOff(int channel, int note, long offTick);

public:
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

  static std::map<char, double> getKeyboardToMidiNotes(int transpose = 0)
  {
    std::map<char, double> key_to_note =
        {
            {'z', 48 + transpose},
            {'s', 49 + transpose},
            {'x', 50 + transpose},
            {'d', 51 + transpose},
            {'c', 52 + transpose},
            {'v', 53 + transpose},
            {'g', 54 + transpose},
            {'b', 55 + transpose},
            {'h', 56 + transpose},
            {'n', 57 + transpose},
            {'j', 58 + transpose},
            {'m', 59 + transpose}};
    return key_to_note;
  }
};
