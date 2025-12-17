#pragma once

#include <JuceHeader.h>
#include <memory>
#include <string>

#include "Utils.h"
#include "httplib.h"

class PluginProcessor;

/** Lightweight HTTP server that exposes a JSON API and serves the embedded web UI. */
class HttpServerThread : public juce::Thread
{
public:
    explicit HttpServerThread(PluginProcessor& processor);
    ~HttpServerThread() override = default;

    void run() override;
    void stopServer();

private:
    void initAPI();
    void registerStaticRoutes();
    bool tryServeFromDisk();
    void serveBinaryResource(const httplib::Request& req, httplib::Response& res);
    std::string guessMimeType(const std::string& path) const;

    PluginProcessor& pluginProc;
    httplib::Server svr;
    bool servingFromDisk{false};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HttpServerThread)
};
