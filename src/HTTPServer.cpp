#include "HTTPServer.h"

namespace
{
constexpr auto kProtocolVersion = "2026-07-28";

bool isExactLoopbackOrigin(const juce::String& raw)
{
    const auto value = raw.trim().toLowerCase();
    if (value.isEmpty()) return true;
    const bool http = value.startsWith("http://");
    const bool https = value.startsWith("https://");
    if (!http && !https) return false;
    const auto authority = value.fromFirstOccurrenceOf("://", false, false).upToFirstOccurrenceOf("/", false, false);
    const auto host = authority.upToFirstOccurrenceOf(":", false, false);
    if (host != "localhost" && host != "127.0.0.1") return false;
    if (authority == host) return true;
    const auto port = authority.substring(host.length() + 1);
    return port.isNotEmpty() && port.containsOnly("0123456789") && juce::isPositiveAndBelow(port.getIntValue(), 65536);
}

bool isExactLoopbackHost(const juce::String& raw)
{
    const auto value = raw.trim().toLowerCase();
    if (value.startsWith("[::1]"))
        return value == "[::1]" || value.substring(5).startsWith(":");
    const auto host = value.upToFirstOccurrenceOf(":", false, false);
    if (host != "localhost" && host != "127.0.0.1") return false;
    if (value == host) return true;
    const auto port = value.substring(host.length() + 1);
    return port.isNotEmpty() && port.containsOnly("0123456789") && juce::isPositiveAndBelow(port.getIntValue(), 65536);
}
} // namespace

TrackerMcpServer::TrackerMcpServer(TrackerControlService& control, int portIn)
    : juce::Thread("MYK Tracker MCP server"), router(control), port(portIn)
{
    initialiseRoutes();
}

TrackerMcpServer::~TrackerMcpServer() { stopServer(); }

void TrackerMcpServer::initialiseRoutes()
{
    server.set_payload_max_length(1024 * 1024);
    server.set_keep_alive_max_count(16);
    server.Get("/health", [this] (const httplib::Request&, httplib::Response& response)
    {
        auto body = juce::var(new juce::DynamicObject());
        body.getDynamicObject()->setProperty("ok", ready.load());
        body.getDynamicObject()->setProperty("protocolVersion", kProtocolVersion);
        body.getDynamicObject()->setProperty("port", port);
        body.getDynamicObject()->setProperty("server", "myk-tracker");
        response.set_content(juce::JSON::toString(body, false).toStdString(), "application/json");
    });
    // LM Studio's compatibility path still uses the pre-Streamable-HTTP SSE
    // transport: GET establishes an event stream, whose endpoint event is used
    // for subsequent POSTs.  Modern clients continue to use POST /mcp only.
    server.Get("/mcp", [this] (const httplib::Request& request, httplib::Response& response) { handleLegacySse(request, response); });
    server.Post("/mcp", [this] (const httplib::Request& request, httplib::Response& response) { handleMcp(request, response); });
}

void TrackerMcpServer::run()
{
    ready.store(true);
    if (!server.listen("127.0.0.1", port))
    {
        ready.store(false);
        DBG("MYK Tracker MCP server could not listen on 127.0.0.1:" << port);
    }
}

void TrackerMcpServer::stopServer()
{
    ready.store(false);
    closeLegacySseSessions();
    server.stop();
    if (isThreadRunning()) stopThread(2000);
}

std::shared_ptr<TrackerMcpServer::LegacySseSession> TrackerMcpServer::findLegacySseSession(const httplib::Request& request) const
{
    auto id = request.has_param("sessionId") ? request.get_param_value("sessionId") : request.get_header_value("Mcp-Session-Id");
    if (id.empty()) return {};
    const std::lock_guard<std::mutex> lock(legacySseMutex);
    const auto it = legacySseSessions.find(juce::String(id));
    return it != legacySseSessions.end() ? it->second : std::shared_ptr<LegacySseSession> {};
}

void TrackerMcpServer::closeLegacySseSessions()
{
    std::vector<std::shared_ptr<LegacySseSession>> sessions;
    {
        const std::lock_guard<std::mutex> lock(legacySseMutex);
        for (const auto& entry : legacySseSessions) sessions.push_back(entry.second);
        legacySseSessions.clear();
    }
    for (const auto& session : sessions)
    {
        const std::lock_guard<std::mutex> lock(session->mutex);
        session->closed = true;
        session->changed.notify_all();
    }
}

void TrackerMcpServer::handleLegacySse(const httplib::Request& request, httplib::Response& response)
{
    if (!isAllowedLoopbackRequest(request))
    {
        response.status = 403;
        response.set_content("{\"error\":\"loopback_origin_required\"}", "application/json");
        return;
    }

    auto session = std::make_shared<LegacySseSession>();
    session->id = "myk-sse-" + juce::String(nextLegacySseId.fetch_add(1));
    {
        const std::lock_guard<std::mutex> lock(legacySseMutex);
        // A small cap prevents abandoned compatibility clients from retaining
        // unbounded state.  Existing streams remain valid until disconnected.
        if (legacySseSessions.size() >= 16)
        {
            response.status = 503;
            response.set_content("{\"error\":\"too_many_legacy_sessions\"}", "application/json");
            return;
        }
        legacySseSessions.emplace(session->id, session);
    }

    response.status = 200;
    response.set_header("Cache-Control", "no-cache, no-store");
    response.set_header("Connection", "keep-alive");
    response.set_header("X-Accel-Buffering", "no");
    const auto endpoint = "event: endpoint\ndata: /mcp?sessionId=" + session->id.toStdString() + "\n\n";
    auto sentEndpoint = std::make_shared<std::atomic<bool>>(false);
    response.set_chunked_content_provider("text/event-stream",
        [session, endpoint, sentEndpoint] (size_t, httplib::DataSink& sink)
        {
            if (!sentEndpoint->exchange(true)) return sink.write(endpoint.data(), endpoint.size());

            std::unique_lock<std::mutex> lock(session->mutex);
            session->changed.wait_for(lock, std::chrono::seconds(10), [&] { return session->closed || !session->messages.empty(); });
            if (session->closed) return false;
            if (session->messages.empty())
            {
                static constexpr char heartbeat[] = ": keepalive\n\n";
                return sink.write(heartbeat, sizeof(heartbeat) - 1);
            }
            auto message = std::move(session->messages.front());
            session->messages.pop_front();
            lock.unlock();
            const auto event = "event: message\ndata: " + message + "\n\n";
            return sink.write(event.data(), event.size());
        },
        [this, session] (bool)
        {
            {
                const std::lock_guard<std::mutex> lock(session->mutex);
                session->closed = true;
                session->changed.notify_all();
            }
            const std::lock_guard<std::mutex> lock(legacySseMutex);
            const auto it = legacySseSessions.find(session->id);
            if (it != legacySseSessions.end() && it->second == session) legacySseSessions.erase(it);
        });
}

bool TrackerMcpServer::isAllowedLoopbackRequest(const httplib::Request& request) const
{
    if (request.has_header("Origin") && !isExactLoopbackOrigin(request.get_header_value("Origin"))) return false;
    return !request.has_header("Host") || isExactLoopbackHost(request.get_header_value("Host"));
}

void TrackerMcpServer::writeRouterResponse(httplib::Response& response, const McpRouter::Response& routed)
{
    response.status = routed.status;
    if (routed.sessionId.isNotEmpty())
        response.set_header("Mcp-Session-Id", routed.sessionId.toStdString());
    if (routed.hasBody)
        response.set_content(juce::JSON::toString(routed.json, false).toStdString(), "application/json");
}

void TrackerMcpServer::handleMcp(const httplib::Request& request, httplib::Response& response)
{
    response.set_header("Cache-Control", "no-store");
    response.set_header("MCP-Protocol-Version", kProtocolVersion);
    if (!isAllowedLoopbackRequest(request))
    {
        response.status = 403;
        response.set_content("{\"error\":\"loopback_origin_required\"}", "application/json");
        return;
    }
    if (!request.has_header("Content-Type") || !juce::String(request.get_header_value("Content-Type")).containsIgnoreCase("application/json"))
    {
        response.status = 415;
        response.set_content("{\"error\":\"application_json_required\"}", "application/json");
        return;
    }
    McpRouter::Request routedRequest;
    routedRequest.body = request.body;
    routedRequest.protocolVersion = request.get_header_value("Mcp-Protocol-Version");
    routedRequest.methodHeader = request.get_header_value("Mcp-Method");
    routedRequest.nameHeader = request.get_header_value("Mcp-Name");
    auto routed = router.route(routedRequest);

    // A POST directed at a legacy SSE endpoint publishes its JSON-RPC result
    // on that stream and is acknowledged with 202, per the old MCP transport.
    if (auto session = findLegacySseSession(request))
    {
        if (routed.hasBody)
        {
            // An SSE event payload must either be one line or prefix every
            // physical line with "data:".  Use compact JSON so legacy clients
            // receive one complete JSON-RPC object in their event.data.
            const auto json = juce::JSON::toString(routed.json, true).toStdString();
            {
                const std::lock_guard<std::mutex> lock(session->mutex);
                if (!session->closed) session->messages.push_back(json);
            }
            session->changed.notify_one();
        }
        response.status = 202;
        return;
    }
    writeRouterResponse(response, routed);
}
