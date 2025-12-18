#include "HTTPServer.h"

#include <filesystem>
#include <vector>

#include "PluginProcessor.h"

namespace
{
void logRequestTiming(const httplib::Request& req, const httplib::Response& res, double startMs)
{
    const auto elapsed = juce::Time::getMillisecondCounterHiRes() - startMs;
    DBG("HttpServer timing " << req.method << " " << req.path << " -> " << res.status << " in "
                             << juce::String(elapsed, 2) << "ms");
}
} // namespace

HttpServerThread::HttpServerThread(PluginProcessor& processor)
    : juce::Thread("HTTP Server Thread"), pluginProc(processor)
{
    initAPI();
}

void HttpServerThread::initAPI()
{
    svr.set_logger([](const httplib::Request& req, const httplib::Response& res) {
        DBG("HttpServer " << req.method << " " << req.path << " -> " << res.status);
    });

    registerStaticRoutes();

    svr.Get("/state", [this](const httplib::Request& req, httplib::Response& res) {
        const auto start = juce::Time::getMillisecondCounterHiRes();
        auto state = pluginProc.getUiState();
        res.set_content(juce::JSON::toString(state).toStdString(), "application/json");
        logRequestTiming(req, res, start);
    });

    svr.Post("/command", [this](const httplib::Request& req, httplib::Response& res) {
        const auto start = juce::Time::getMillisecondCounterHiRes();
        juce::DynamicObject::Ptr resp = new juce::DynamicObject();
        auto parsed = juce::JSON::parse(req.body);

        if (parsed.isVoid())
        {
            res.status = 400;
            resp->setProperty("ok", false);
            resp->setProperty("error", "Invalid JSON payload");
        }
        else
        {
            juce::String error;
            bool ok = pluginProc.handleCommand(parsed, error);
            resp->setProperty("ok", ok);
            if (!error.isEmpty())
                resp->setProperty("error", error);
        }

        resp->setProperty("state", pluginProc.getUiState());
        res.set_content(juce::JSON::toString(juce::var(resp)).toStdString(), "application/json");
        logRequestTiming(req, res, start);
    });
}

void HttpServerThread::registerStaticRoutes()
{
    servingFromDisk = tryServeFromDisk();

    auto serve = [this](const httplib::Request& req, httplib::Response& res) {
        if (servingFromDisk)
            return; // served by mounted file system handler
        serveBinaryResource(req, res);
    };

    if (!servingFromDisk)
    {
        // fallback to embedded assets
        svr.Get("/", serve);
        svr.Get(R"(/.*)", serve);
    }
}

bool HttpServerThread::tryServeFromDisk()
{
    std::vector<std::filesystem::path> candidates;
    try
    {
        auto workingDir = getBinary();
        candidates.push_back(workingDir / "ui");
        candidates.push_back(workingDir / "../ui");
        candidates.push_back(workingDir / "../../ui");
        candidates.push_back(workingDir / "../../../ui");
        candidates.push_back(workingDir / "../../../../ui");
    }
    catch (const std::exception& e)
    {
        DBG("HTTPServer unable to locate binary: " << e.what());
    }

    for (const auto& dir : candidates)
    {
        if (std::filesystem::exists(dir) && std::filesystem::is_directory(dir))
        {
            auto ret = svr.set_mount_point("/", dir.string());
            if (ret)
            {
                DBG("HTTPServer serving UI from disk: " << dir.string());
                return true;
            }
        }
    }
    DBG("HTTPServer falling back to embedded UI");
    return false;
}

void HttpServerThread::serveBinaryResource(const httplib::Request& req, httplib::Response& res)
{
    juce::String path = req.path;
    if (path == "/" || path.isEmpty())
        path = "index.html";
    else
        path = path.substring(1); // remove leading slash

    int size = 0;
    const char* data = nullptr;

    for (int i = 0; i < BinaryData::namedResourceListSize; ++i)
    {
        juce::String original(BinaryData::originalFilenames[i]);
        if (original.endsWithIgnoreCase(path))
        {
            data = BinaryData::getNamedResource(BinaryData::namedResourceList[i], size);
            path = original;
            break;
        }
    }

    if (data == nullptr)
    {
        res.status = 404;
        res.set_content("404: File not found", "text/plain");
        return;
    }

    res.set_content(data, static_cast<size_t>(size), guessMimeType(path.toStdString()).c_str());
}

std::string HttpServerThread::guessMimeType(const std::string& path) const
{
    if (path.find(".html") != std::string::npos)
        return "text/html";
    if (path.find(".css") != std::string::npos)
        return "text/css";
    if (path.find(".js") != std::string::npos)
        return "application/javascript";
    if (path.find(".svg") != std::string::npos)
        return "image/svg+xml";
    if (path.find(".png") != std::string::npos)
        return "image/png";
    return "text/plain";
}

void HttpServerThread::run()
{
    DBG("HTTP server starting on 127.0.0.1:8080");
    svr.listen("127.0.0.1", 8080);
}

void HttpServerThread::stopServer()
{
    DBG("HTTP server stopping");
    svr.stop();
    stopThread(1000);
}
