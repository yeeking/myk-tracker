#pragma once

#include <JuceHeader.h>

class TrackerControlService;

/** Stateless JSON-RPC/MCP dispatcher.  It deliberately knows nothing about
    sockets so protocol behaviour can be tested without starting a listener. */
class McpRouter
{
public:
    struct Request
    {
        juce::String body;
        juce::String protocolVersion;
        juce::String methodHeader;
        juce::String nameHeader;
    };

    struct Response
    {
        int status = 200;
        bool hasBody = true;
        juce::var json;
        juce::String sessionId;
    };

    explicit McpRouter(TrackerControlService& control);

    Response route(const Request& request);

private:
    Response routeLegacy(const juce::var& message, const juce::var& id, const juce::String& method, const juce::var& params);
    TrackerControlService& control;
};
