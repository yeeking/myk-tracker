#pragma once

#include "../libs/httplib.h"
#include "McpRouter.h"

#include <JuceHeader.h>

#include <condition_variable>
#include <deque>
#include <map>
#include <mutex>

/** Local-only stateless Streamable HTTP MCP server for the standalone host. */
class TrackerMcpServer final : public juce::Thread
{
public:
    explicit TrackerMcpServer(TrackerControlService& control, int port = 8080);
    ~TrackerMcpServer() override;

    void run() override;
    void stopServer();
    int getPort() const noexcept { return port; }
    bool isReady() const noexcept { return ready.load(); }

private:
    struct LegacySseSession
    {
        juce::String id;
        std::mutex mutex;
        std::condition_variable changed;
        std::deque<std::string> messages;
        bool closed = false;
    };

    void initialiseRoutes();
    void handleMcp(const httplib::Request&, httplib::Response&);
    void handleLegacySse(const httplib::Request&, httplib::Response&);
    std::shared_ptr<LegacySseSession> findLegacySseSession(const httplib::Request&) const;
    void closeLegacySseSessions();

    bool isAllowedLoopbackRequest(const httplib::Request&) const;
    void writeRouterResponse(httplib::Response&, const McpRouter::Response&);

    McpRouter router;
    httplib::Server server;
    int port;
    std::atomic<bool> ready { false };
    std::atomic<uint64_t> nextLegacySseId { 1 };
    mutable std::mutex legacySseMutex;
    std::map<juce::String, std::shared_ptr<LegacySseSession>> legacySseSessions;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TrackerMcpServer)
};
