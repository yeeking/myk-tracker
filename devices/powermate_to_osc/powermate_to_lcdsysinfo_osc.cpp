#define OSCPKT_OSTREAM_OUTPUT

#include <linux/input.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <iostream>
#include <string>

#include "oscpkt/oscpkt.hh"
#include "oscpkt/udp.hh"

struct input_event ev[64];
int bus_val = 0;
oscpkt::UdpSocket sock;

void runClient(int port_num) {
    
  sock.connectTo("localhost", port_num);
  if (!sock.isOk()) {
    std::cerr << "Error connection to port " << port_num << ": " << sock.errorMessage() << "\n";
  } else {
    std::cout << "Client started, will send packets to port " << port_num << std::endl;
    
    // std::cout << "sock error: " << sock.errorMessage() << " -- is the server running?\n";
  }
}

void sendMessage(std::string path = "/ping", int val = 1)
{
    if (sock.isOk()) {
        oscpkt::Message msg(path); 
        msg.pushInt32(val);
        oscpkt::PacketWriter pw;
        pw.startBundle().startBundle().addMessage(msg).endBundle().endBundle();
        bool ok = sock.sendPacket(pw.packetData(), pw.packetSize());
        std::cout << "Client: sent " << path << ":" << val << ", ok=" << ok << "\n";
    }
}

void setBus(int bus_id, int val)
{
    std::string path = "/c_set";
    if (sock.isOk()) {
        oscpkt::Message msg(path); 
        msg.pushInt32(bus_id);
        msg.pushInt32(val);
        oscpkt::PacketWriter pw;
        pw.startBundle().startBundle().addMessage(msg).endBundle().endBundle();
        bool ok = sock.sendPacket(pw.packetData(), pw.packetSize());
        std::cout << "Client: sent " << path << ":" << val << ", ok=" << ok << "\n";
    }
}

void sendVectorToOSC(int vector)
{
    std::string path = "/";
    if (sock.isOk()) {
        oscpkt::Message msg(path); 
        // msg.pushInt32(bus_id);
        std::string value = std::to_string(vector);
        msg.pushStr(value);
        // msg.pushInt32(vector);
        oscpkt::PacketWriter pw;
        pw.startBundle().startBundle().addMessage(msg).endBundle().endBundle();
        bool ok = sock.sendPacket(pw.packetData(), pw.packetSize());
        std::cout << "Client: sent " << path << ":" << vector << ", ok=" << ok << "\n";
    }
}

/**
 * @brief respond to the vector from the powermate and use it to 
 * update a locally stored value which can later be sent over OSC
 * to a bus
 * 
 * @param bus_id 
 * @param vector 
 */
int updateLocalBusVal(int bus_id, int vector)
{
    bus_val += vector;
    return bus_val;
}

char getChar(int file_ref)
    {
        
        int rd, code_id, ev_count, key_code;
        ev_count = 0;
        key_code = 0;
        // keep reading until 
        // we get a key down or key held event
        while(1)
        {
            rd = read(file_ref, ev, sizeof(struct input_event) * 64);
            printf("%i %i %i \n", ev->code, ev->type, ev->value);
            //sendMessage("/n_set", ev->value);
            if (ev->code == 7)
            {// a scroll
                // sendVectorToOSC(ev->value);
                int val = updateLocalBusVal(1, ev->value);
                printf("Update bus 1 to %i\n", val);                
                sendVectorToOSC(val);
            }
            if (ev->code == 256)
            {// a click

            }
        }
        return key_code;  
    }


int main(int argc, char *argv[])
{
    int port = 57110; // supercollider port 
    if (argc == 2){
        port = std::stoi(std::string(argv[1]));
    }

    int file_ref = open("/dev/input/by-id/usb-Griffin_Technology__Inc._Griffin_PowerMate-event-if00", O_RDONLY);
    runClient(port);

    while(1)
        printf("%i", getChar(file_ref));
    return 0;
}