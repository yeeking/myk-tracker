#include "MidiUtils.h"

MidiQueue::MidiQueue()
{
}
void MidiQueue::addMessage(const long timestamp, const MidiMessage &msg)
{
    // iterate over the list and insert the event
    // at a point where the ts ==  or <
    bool done = false;
    for (TimeStampedMessages &item : messageList)
    {
        if (item.timestamp == timestamp)
        {
            item.messages.push_back(msg); // now we pass by value
            done = true;
            break;
        }
    }
    if (!done)
    {
        // std::cout << "EventQueue::addEvent no timestapm match for " << timestamp << std::endl;
        TimeStampedMessages item{timestamp, MidiMessageVector{msg}};
        messageList.push_back(item);
    }
}
MidiMessageVector MidiQueue::getAndClearMessages(long timestamp)
{
    MidiMessageVector retMessages{};
    std::list<TimeStampedMessages>::iterator it = messageList.begin();
    while (it != messageList.end())
    // for (it=messageList.begin(); it!=messageList.end(); ++it)
    {
        // TimestampedCallbacks item = *it;
        if (it->timestamp == timestamp)
        // if (it->timestamp == timestamp)
        {
            // trigger all the callbacks
            for (MidiMessage &msg : it->messages)
            {
                retMessages.push_back(msg);
            }
            // erase it
            it = messageList.erase(it);
            // break;
        }
        else
            ++it;
    }
    return retMessages;
}

void MidiQueue::clearAllMessages()
{
    messageList.clear();
}

//////////////////////
// end of MidiQueue
//////////////////////

//////////////////////
// start of MidiUtils
//////////////////////

MidiUtils::MidiUtils() : portReady{false}, panicMode{false}
{
    try{
        midiout = new RtMidiOut();
    }
    catch (RtMidiError &error){
        std::cout << "MidiStepDataReceiver::initMidi problem creating RTNidiOut. Error message: " << error.getMessage() << std::endl;
    }
}

MidiUtils::~MidiUtils()
{
    std::cout << "MidiUtils::all notes off" << std::endl;
    allNotesOff();
    std::cout << "MidiUtils::closing port " << std::endl;
    midiout->closePort();
    delete midiout;
}

bool MidiUtils::portIsReady() const 
{
    return portReady;
}

void MidiUtils::interactiveInitMidi()
{

    std::string portName;
    unsigned int i = 0, nPorts = midiout->getPortCount();
    if (nPorts == 0){
        std::cout << "No output ports available!" << std::endl;
    }

    if (nPorts == 1){
        std::cout << "\nOpening " << midiout->getPortName() << std::endl;
    }
    else{
        for (i = 0; i < nPorts; i++){
            portName = midiout->getPortName(i);
            std::cout << "  Output port #" << i << ": " << portName << '\n';
        }
        do{
            std::cout << "\nChoose a port number: ";
            std::cin >> i;
        } while (i >= nPorts);
    }

    std::cout << "\n";
    std::cout << "Preparing to open the port... " << std::endl;
    try{
        midiout->openPort(i);
    }catch (RtMidiError &error){
        std::cout << "MidiStepDataReceiver::initMidi problem creating RTNidiOut connection. Error message: " << error.getMessage() << std::endl;
    }
    std::cout << "Port opened... " << std::endl;
    portReady = true; 
}

std::vector<std::string> MidiUtils::getOutputDeviceList()
{
    std::vector<std::string> deviceList;

    std::string portName;
    unsigned int i = 0, nPorts = midiout->getPortCount();
    for (i = 0; i < nPorts; i++)
    {
        deviceList.push_back(midiout->getPortName(i));
    }
    return deviceList;
}

void MidiUtils::selectOutputDevice(int deviceId)
{
    midiout->openPort(deviceId);
}

void MidiUtils::allNotesOff()
{
    panicMode = true; // don't allow any new messages to go
    // clear the queue:
    midiQ.clearAllMessages();
    // std::cout << "MidiUtils:: All notes off " << std::endl;
    //  send 16 all notes off messages
    std::vector<unsigned char> message = {0, 0, 0}; // 0x7b = 123, 0
    for (int chan = 0; chan < 16; ++chan)
    {
        message[0] = 176 + chan;
        message[1] = 0x7b; // 123
        message[2] = 0;
        midiout->sendMessage(&message);
    }
    panicMode = false;
}

void MidiUtils::playSingleNote(int channel, int note, int velocity, long offTick)
{
    if (panicMode) return;
    if (!portReady) return;

    // std::cout << "MidiStepDataReceiver:: playSingleNote "<< std::endl;
    std::vector<unsigned char> message = {0, 0, 0};

    message[0] = 144 + channel; // 128 + channel
    message[1] = note;          // note value
    message[2] = velocity;      // velocity value
    // std::cout << "playSingleNote " << message[1] << "off "<< offTick << std::endl;

    midiout->sendMessage(&message);
    queueNoteOff(channel, note, offTick);
}

void MidiUtils::sendQueuedMessages(long tick)
{
    int msgind = 0;
    for (MidiMessage &msg : midiQ.getAndClearMessages(tick))
    {
        midiout->sendMessage(&msg);
        msgind++;
    }
}

void MidiUtils::queueNoteOff(int channel, int note, long offTick)
{
    std::vector<unsigned char> message = {0, 0, 0};
    message[0] = 128 + channel;
    message[1] = note;
    message[2] = 0;
    midiQ.addMessage(offTick, message);
}