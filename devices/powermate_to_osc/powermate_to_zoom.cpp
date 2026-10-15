// Griffin PowerMate -> MYK Tracker OSC zoom control.
//
// Positive dial rotation sends /zoom_in, negative rotation sends /zoom_out.
// Both messages carry the current X11 pointer position normalized to the
// desktop, which is the same coordinate space as a fullscreen tracker window.

#include <X11/Xlib.h>
#include <linux/input.h>

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <string>
#include <unistd.h>

#include "oscpkt/oscpkt.hh"
#include "oscpkt/udp.hh"

namespace
{
constexpr const char* kDefaultDevice = "/dev/input/by-id/usb-Griffin_Technology__Inc._Griffin_PowerMate-event-if00";
constexpr const char* kDefaultHost = "127.0.0.1";
constexpr int kDefaultPort = 9001;
constexpr const char* kZoomInAddress = "/zoom_in";
constexpr const char* kZoomOutAddress = "/zoom_out";

struct NormalizedPointer
{
    float x = 0.5f;
    float y = 0.5f;
};

class PointerPositionReader
{
public:
    PointerPositionReader()
        : display(XOpenDisplay(nullptr))
    {
        if (display == nullptr)
            std::cerr << "Warning: could not open X11 display; zoom will use the screen centre.\n";
    }

    ~PointerPositionReader()
    {
        if (display != nullptr)
            XCloseDisplay(display);
    }

    NormalizedPointer currentPosition() const
    {
        if (display == nullptr)
            return {};

        Window rootReturn = 0;
        Window childReturn = 0;
        int rootX = 0;
        int rootY = 0;
        int windowX = 0;
        int windowY = 0;
        unsigned int modifiers = 0;
        const int screen = DefaultScreen(display);
        const Window root = RootWindow(display, screen);
        if (XQueryPointer(display, root, &rootReturn, &childReturn,
                          &rootX, &rootY, &windowX, &windowY, &modifiers) == False)
            return {};

        const int width = DisplayWidth(display, screen);
        const int height = DisplayHeight(display, screen);
        if (width <= 0 || height <= 0)
            return {};

        return {
            std::clamp(static_cast<float>(rootX) / static_cast<float>(width), 0.0f, 1.0f),
            std::clamp(static_cast<float>(rootY) / static_cast<float>(height), 0.0f, 1.0f)
        };
    }

private:
    Display* display = nullptr;
};

void printUsage(const char* executable)
{
    std::cerr << "Usage: " << executable << " [event-device] [host] [port]\n"
              << "Defaults: " << kDefaultDevice << " " << kDefaultHost << " " << kDefaultPort << "\n";
}

bool sendZoom(oscpkt::UdpSocket& socket, bool zoomIn, const NormalizedPointer& pointer)
{
    oscpkt::Message message(zoomIn ? kZoomInAddress : kZoomOutAddress);
    message.pushFloat(pointer.x).pushFloat(pointer.y);

    oscpkt::PacketWriter packet;
    packet.startBundle().addMessage(message).endBundle();
    return socket.sendPacket(packet.packetData(), packet.packetSize());
}
} // namespace

int main(int argc, char* argv[])
{
    if (argc > 4)
    {
        printUsage(argv[0]);
        return EXIT_FAILURE;
    }

    const std::string device = argc > 1 ? argv[1] : kDefaultDevice;
    const std::string host = argc > 2 ? argv[2] : kDefaultHost;
    const int port = argc > 3 ? std::atoi(argv[3]) : kDefaultPort;
    if (port < 1 || port > 65535)
    {
        std::cerr << "Invalid UDP port: " << port << "\n";
        return EXIT_FAILURE;
    }

    const int input = open(device.c_str(), O_RDONLY | O_CLOEXEC);
    if (input < 0)
    {
        std::cerr << "Could not open " << device << ": " << std::strerror(errno) << "\n";
        return EXIT_FAILURE;
    }

    oscpkt::UdpSocket socket;
    socket.connectTo(host.c_str(), port);
    if (!socket.isOk())
    {
        std::cerr << "Could not connect OSC UDP socket to " << host << ':' << port
                  << ": " << socket.errorMessage() << "\n";
        close(input);
        return EXIT_FAILURE;
    }

    std::cout << "PowerMate zoom: " << device << " -> " << host << ':' << port << "\n";
    PointerPositionReader pointerReader;

    for (;;)
    {
        input_event event {};
        const ssize_t bytesRead = read(input, &event, sizeof(event));
        if (bytesRead == static_cast<ssize_t>(sizeof(event)))
        {
            if (event.type != EV_REL || event.code != REL_DIAL || event.value == 0)
                continue;

            const bool zoomIn = event.value > 0;
            // Most PowerMates report one unit per detent. Handle faster turns
            // too, but cap a malformed event so it cannot flood OSC.
            const int detents = std::min(std::abs(event.value), 32);
            const auto pointer = pointerReader.currentPosition();
            for (int i = 0; i < detents; ++i)
            {
                if (!sendZoom(socket, zoomIn, pointer))
                    std::cerr << "OSC send failed: " << socket.errorMessage() << "\n";
            }
            continue;
        }

        if (bytesRead < 0 && errno == EINTR)
            continue;

        std::cerr << "PowerMate input read failed: " << std::strerror(bytesRead < 0 ? errno : EIO) << "\n";
        break;
    }

    close(input);
    return EXIT_FAILURE;
}
