#include "McpRouter.h"

#include "TrackerControlService.h"

namespace
{
constexpr auto kProtocolVersion = "2026-07-28";
constexpr auto kLegacyDefaultProtocolVersion = "2025-11-25";

juce::var object() { return juce::var(new juce::DynamicObject()); }

juce::var cacheHints(juce::var result)
{
    result.getDynamicObject()->setProperty("ttlMs", 0);
    result.getDynamicObject()->setProperty("cacheScope", "private");
    return result;
}

juce::var resultMeta()
{
    auto meta = object();
    auto serverInfo = object();
    serverInfo.getDynamicObject()->setProperty("name", "myk-tracker");
    serverInfo.getDynamicObject()->setProperty("version", "0.0.4");
    meta.getDynamicObject()->setProperty("io.modelcontextprotocol/serverInfo", serverInfo);
    return meta;
}

juce::var responseFor(const juce::var& id, juce::var result)
{
    result.getDynamicObject()->setProperty("resultType", "complete");
    result.getDynamicObject()->setProperty("_meta", resultMeta());
    auto out = object();
    out.getDynamicObject()->setProperty("jsonrpc", "2.0");
    out.getDynamicObject()->setProperty("id", id);
    out.getDynamicObject()->setProperty("result", result);
    return out;
}

juce::var errorFor(const juce::var& id, int code, const juce::String& message)
{
    auto out = object();
    auto error = object();
    out.getDynamicObject()->setProperty("jsonrpc", "2.0");
    out.getDynamicObject()->setProperty("id", id);
    error.getDynamicObject()->setProperty("code", code);
    error.getDynamicObject()->setProperty("message", message);
    out.getDynamicObject()->setProperty("error", error);
    return out;
}

McpRouter::Response errorResponse(const juce::var& id, int code, const juce::String& message, int status = 400)
{
    McpRouter::Response response;
    response.status = status;
    response.json = errorFor(id, code, message);
    return response;
}

bool hasProperty(const juce::var& value, const char* name)
{
    return value.isObject() && value.getDynamicObject()->hasProperty(name);
}

juce::var schema(const juce::StringArray& operations = {}, bool revisions = true)
{
    auto value = object();
    value.getDynamicObject()->setProperty("$schema", "https://json-schema.org/draft/2020-12/schema");
    value.getDynamicObject()->setProperty("type", "object");
    value.getDynamicObject()->setProperty("additionalProperties", false);
    auto properties = object();
    if (!operations.isEmpty())
    {
        auto operation = object();
        operation.getDynamicObject()->setProperty("type", "string");
        juce::Array<juce::var> values;
        for (const auto& item : operations) values.add(item);
        operation.getDynamicObject()->setProperty("enum", values);
        properties.getDynamicObject()->setProperty("action", operation);
    }
    if (revisions)
    {
        for (const auto* name : { "expectedContentRevision", "expectedViewRevision" })
        {
            auto revision = object();
            revision.getDynamicObject()->setProperty("type", "integer");
            revision.getDynamicObject()->setProperty("minimum", 0);
            properties.getDynamicObject()->setProperty(name, revision);
        }
    }
    auto addTyped = [&properties](const char* name, const char* type)
    {
        auto property = object();
        property.getDynamicObject()->setProperty("type", type);
        properties.getDynamicObject()->setProperty(name, property);
    };
    for (const auto* name : { "scope", "command", "playMode", "mode", "rhythm", "slotId", "controlId", "text", "filename", "path", "loadId" })
        addTyped(name, "string");
    for (const auto* name : { "setId", "sequenceId", "stepId", "row", "column", "length", "headCount", "headIndex", "ticksPerStep", "polyphony", "machineStackId", "stackId", "direction", "playerId", "startNote", "endNote" })
        addTyped(name, "integer");
    for (const auto* name : { "note", "velocity", "durationTicks", "bpm", "headProbability", "gainDb" })
        addTyped(name, "number");
    for (const auto* name : { "internalClock", "muted", "solo", "armed", "enabled", "confirm" })
        addTyped(name, "boolean");
    for (const auto* name : { "notes", "lengths" }) addTyped(name, "array");
    properties.getDynamicObject()->setProperty("value", object());
    value.getDynamicObject()->setProperty("properties", properties);
    return value;
}

juce::var resultSchema()
{
    auto value = object();
    value.getDynamicObject()->setProperty("type", "object");
    return value;
}

struct ToolDefinition
{
    const char* name;
    const char* description;
    TrackerControlService::CommandKind kind;
    juce::StringArray operations;
    bool readOnly = false;
    bool destructive = false;
};

juce::var toolJson(const ToolDefinition& definition);

const std::vector<ToolDefinition>& tools()
{
    static const std::vector<ToolDefinition> registry {
        { "tracker_get_state", "Read the tracker state or a named state scope.", TrackerControlService::CommandKind::getState, {}, true },
        { "tracker_notes", "Read non-empty track notes as compact n:[[stepId,note,...],...].", TrackerControlService::CommandKind::getTrackNotes, {}, true },
        { "tracker_step", "Read one step as compact v:[[command,note,velocity,duration],...].", TrackerControlService::CommandKind::getStepValues, {}, true },
        { "tracker_notes_set", "Replace a track's notes from compact per-step notes/chords; grows a track up to 128 steps and uses one velocity and durationTicks for all notes.", TrackerControlService::CommandKind::setTrackNotes, {}, false },
        { "tracker_lengths_set", "Set each step's note duration from a compact lengths array.", TrackerControlService::CommandKind::setTrackLengths, {}, false },
        { "tracker_transport", "Control transport, tempo, clock mode, and song mode.", TrackerControlService::CommandKind::transport, { "play", "stop", "toggle", "rewind", "set" } },
        { "tracker_set_step", "Create, patch, clear, or activate a tracker step.", TrackerControlService::CommandKind::setStep, { "set", "patch", "clear", "toggle_active" } },
        { "tracker_edit_sequence", "Edit routing and sequence-owned read heads (count, selected head, TPS, mode, polyphony, rhythm, probability) and focus the affected sequence.", TrackerControlService::CommandKind::editSequence, { "set" } },
        { "tracker_edit_song", "Edit song rows or song playback mode.", TrackerControlService::CommandKind::editSong, { "add", "remove", "set_mode", "select" } },
        { "tracker_edit_machine_stack", "Edit stack slots, routing, gain, mute, solo, order, and idempotent enablement.", TrackerControlService::CommandKind::editMachineStack, { "add", "remove", "move", "cycle_type", "toggle", "set_enabled", "set_gain", "set_send", "set_muted", "set_solo" } },
        { "tracker_machine_control", "Invoke a machine control directly by stable stack/slot/control address; legacy grid addressing remains available for compatibility.", TrackerControlService::CommandKind::machineControl, { "activate", "adjust", "insert", "set", "preview", "reset", "text", "backspace" } },
        { "tracker_load_sample", "Load a local sample into a stack sampler and map it to an inclusive MIDI note range; poll completion with loadId.", TrackerControlService::CommandKind::loadSample, { "load", "status" } },
        { "tracker_ui_action", "Perform a GUI-equivalent navigation or edit action.", TrackerControlService::CommandKind::uiAction, { "up", "down", "left", "right", "activate", "increment", "decrement", "add_row", "remove_row", "reset", "next_step", "mute", "arm", "note", "page" } },
        { "tracker_application", "Reset, quit, or open standalone audio settings.", TrackerControlService::CommandKind::application, { "reset", "quit", "audio_settings" }, false, true }
    };
    return registry;
}

const ToolDefinition* findTool(const juce::String& name)
{
    for (const auto& item : tools())
        if (name == item.name)
            return &item;
    return nullptr;
}

bool isSupportedLegacyProtocol(const juce::String& version)
{
    return version == "2024-11-05" || version == "2025-03-26"
        || version == "2025-06-18" || version == "2025-11-25";
}

juce::var legacyServerInfo()
{
    auto value = object();
    value.getDynamicObject()->setProperty("name", "myk-tracker");
    value.getDynamicObject()->setProperty("version", "0.0.4");
    return value;
}

juce::var legacyToolsListResult()
{
    auto result = object();
    juce::Array<juce::var> items;
    for (const auto& definition : tools()) items.add(toolJson(definition));
    result.getDynamicObject()->setProperty("tools", items);
    return result;
}

juce::var controlToolResult(const TrackerControlService::Result& controlResult)
{
    auto structured = controlResult.data.getDynamicObject() != nullptr ? controlResult.data : object();
    if (!controlResult.ok)
    {
        structured = object();
        structured.getDynamicObject()->setProperty("code", controlResult.code);
        structured.getDynamicObject()->setProperty("message", controlResult.message);
    }
    structured.getDynamicObject()->setProperty("contentRevision", static_cast<juce::int64>(controlResult.contentRevision));
    structured.getDynamicObject()->setProperty("viewRevision", static_cast<juce::int64>(controlResult.viewRevision));
    auto text = object();
    text.getDynamicObject()->setProperty("type", "text");
    text.getDynamicObject()->setProperty("text", juce::JSON::toString(structured, false));
    juce::Array<juce::var> content; content.add(text);
    auto result = object();
    result.getDynamicObject()->setProperty("content", content);
    // Older clients may ignore this field; the text block is always complete.
    result.getDynamicObject()->setProperty("structuredContent", structured);
    result.getDynamicObject()->setProperty("isError", !controlResult.ok);
    return result;
}

juce::var toolJson(const ToolDefinition& definition)
{
    auto value = object();
    auto annotations = object();
    annotations.getDynamicObject()->setProperty("readOnlyHint", definition.readOnly);
    annotations.getDynamicObject()->setProperty("destructiveHint", definition.destructive);
    annotations.getDynamicObject()->setProperty("openWorldHint", false);
    value.getDynamicObject()->setProperty("name", definition.name);
    value.getDynamicObject()->setProperty("description", definition.description);
    value.getDynamicObject()->setProperty("inputSchema", schema(definition.operations));
    value.getDynamicObject()->setProperty("outputSchema", resultSchema());
    value.getDynamicObject()->setProperty("annotations", annotations);
    return value;
}

juce::String expectedName(const juce::String& method, const juce::var& params)
{
    if (method == "tools/call") return params.getProperty("name", {}).toString();
    if (method == "resources/read") return params.getProperty("uri", {}).toString();
    return method;
}
} // namespace

McpRouter::McpRouter(TrackerControlService& controlIn) : control(controlIn) {}

McpRouter::Response McpRouter::routeLegacy(const juce::var&, const juce::var& id,
                                            const juce::String& method, const juce::var& params)
{
    if (method == "initialize")
    {
        const auto requestedVersion = params.getProperty("protocolVersion", {}).toString();
        if (!isSupportedLegacyProtocol(requestedVersion))
            return errorResponse(id, -32602, "Unsupported legacy MCP protocol version");
        const auto clientInfo = params.getProperty("clientInfo", juce::var());
        DBG("MYK Tracker MCP legacy client: " << clientInfo.getProperty("name", "unknown").toString()
            << " " << clientInfo.getProperty("version", "unknown").toString()
            << ", requested " << requestedVersion << ", negotiated legacy-tools");
        auto result = object();
        auto capabilities = object();
        auto toolCapabilities = object();
        toolCapabilities.getDynamicObject()->setProperty("listChanged", false);
        capabilities.getDynamicObject()->setProperty("tools", toolCapabilities);
        result.getDynamicObject()->setProperty("protocolVersion", requestedVersion.isNotEmpty() ? requestedVersion : kLegacyDefaultProtocolVersion);
        result.getDynamicObject()->setProperty("capabilities", capabilities);
        result.getDynamicObject()->setProperty("serverInfo", legacyServerInfo());
        Response response;
        response.json = responseFor(id, result);
        response.sessionId = "myk-legacy-" + juce::String::toHexString(juce::Random::getSystemRandom().nextInt64());
        return response;
    }

    if (method == "notifications/initialized")
    {
        Response response;
        response.status = 202;
        response.hasBody = false;
        return response;
    }

    if (method == "tools/list")
    {
        Response response;
        response.json = responseFor(id, legacyToolsListResult());
        return response;
    }

    if (method == "tools/call")
    {
        const auto toolName = params.getProperty("name", {}).toString();
        const auto* definition = findTool(toolName);
        if (definition == nullptr) return errorResponse(id, -32602, "Unknown tracker tool");
        const auto arguments = params.getProperty("arguments", juce::var());
        if (!arguments.isObject()) return errorResponse(id, -32602, "Tool arguments must be an object");
        TrackerControlService::Result controlResult;
        if (definition->kind == TrackerControlService::CommandKind::getState)
            controlResult = control.getState(arguments.getProperty("scope", "state").toString());
        else
        {
            TrackerControlService::Command command;
            command.kind = definition->kind;
            command.arguments = arguments;
            if (hasProperty(arguments, "expectedContentRevision")) command.expectedContentRevision = static_cast<std::uint64_t>((juce::int64) arguments.getProperty("expectedContentRevision", 0));
            if (hasProperty(arguments, "expectedViewRevision")) command.expectedViewRevision = static_cast<std::uint64_t>((juce::int64) arguments.getProperty("expectedViewRevision", 0));
            controlResult = control.execute(command);
        }
        Response response;
        response.json = responseFor(id, controlToolResult(controlResult));
        return response;
    }
    return errorResponse(id, -32601, "Method not found in legacy tools compatibility mode");
}

McpRouter::Response McpRouter::route(const Request& request)
{
    const auto message = juce::JSON::fromString(request.body);
    if (message.isVoid()) return errorResponse({}, -32700, "Parse error");
    if (!message.isObject()) return errorResponse({}, -32600, "JSON-RPC batches and non-object requests are unsupported");

    const auto id = message.getProperty("id", juce::var());
    const bool notification = !hasProperty(message, "id");
    if (message.getProperty("jsonrpc", {}).toString() != "2.0")
        return errorResponse(id, -32600, "Invalid JSON-RPC request");

    const auto method = message.getProperty("method", {}).toString();
    const auto params = message.getProperty("params", juce::var());
    if (method.isEmpty() || !params.isObject()) return errorResponse(id, -32600, "method and object params are required");

    // Legacy MCP clients announce their protocol in initialize rather than in
    // a per-request metadata envelope.  Keep this adapter intentionally tool
    // focused; resources and server-to-client features remain modern-only.
    if (method == "initialize" || (!hasProperty(params, "_meta")
        && (method == "notifications/initialized" || method == "tools/list" || method == "tools/call")))
        return routeLegacy(message, id, method, params);

    const auto meta = params.getProperty("_meta", juce::var());
    if (!meta.isObject() || meta.getProperty("io.modelcontextprotocol/protocolVersion", {}).toString() != kProtocolVersion)
        return errorResponse(id, -32022, "Request metadata does not declare MCP 2026-07-28");

    if (notification)
    {
        Response response;
        response.status = 202;
        response.hasBody = false;
        return response;
    }

    if (request.protocolVersion != kProtocolVersion)
        return errorResponse(id, -32022, "Unsupported MCP protocol version");
    if (request.methodHeader != method)
        return errorResponse(id, -32020, "Mcp-Method does not match request");
    if (request.nameHeader != expectedName(method, params))
        return errorResponse(id, -32020, "Mcp-Name does not match request");

    juce::var result;
    if (method == "server/discover")
    {
        result = object();
        auto capabilities = object();
        capabilities.getDynamicObject()->setProperty("tools", object());
        capabilities.getDynamicObject()->setProperty("resources", object());
        result.getDynamicObject()->setProperty("capabilities", capabilities);
    }
    else if (method == "tools/list")
    {
        result = object();
        juce::Array<juce::var> items;
        for (const auto& definition : tools()) items.add(toolJson(definition));
        result.getDynamicObject()->setProperty("tools", items);
        result = cacheHints(result);
    }
    else if (method == "tools/call")
    {
        const auto toolName = params.getProperty("name", {}).toString();
        const auto* definition = findTool(toolName);
        if (definition == nullptr) return errorResponse(id, -32602, "Unknown tracker tool");
        const auto arguments = params.getProperty("arguments", juce::var());
        if (!arguments.isObject()) return errorResponse(id, -32602, "Tool arguments must be an object");

        TrackerControlService::Result controlResult;
        if (definition->kind == TrackerControlService::CommandKind::getState)
            controlResult = control.getState(arguments.getProperty("scope", "state").toString());
        else
        {
            TrackerControlService::Command command;
            command.kind = definition->kind;
            command.arguments = arguments;
            if (hasProperty(arguments, "expectedContentRevision")) command.expectedContentRevision = static_cast<std::uint64_t>((juce::int64) arguments.getProperty("expectedContentRevision", 0));
            if (hasProperty(arguments, "expectedViewRevision")) command.expectedViewRevision = static_cast<std::uint64_t>((juce::int64) arguments.getProperty("expectedViewRevision", 0));
            controlResult = control.execute(command);
        }
        auto structured = controlResult.data.getDynamicObject() != nullptr ? controlResult.data : object();
        if (!controlResult.ok)
        {
            structured = object();
            structured.getDynamicObject()->setProperty("code", controlResult.code);
            structured.getDynamicObject()->setProperty("message", controlResult.message);
        }
        structured.getDynamicObject()->setProperty("contentRevision", static_cast<juce::int64>(controlResult.contentRevision));
        structured.getDynamicObject()->setProperty("viewRevision", static_cast<juce::int64>(controlResult.viewRevision));
        auto text = object();
        text.getDynamicObject()->setProperty("type", "text");
        text.getDynamicObject()->setProperty("text", juce::JSON::toString(structured, false));
        juce::Array<juce::var> content; content.add(text);
        result = object();
        result.getDynamicObject()->setProperty("content", content);
        result.getDynamicObject()->setProperty("structuredContent", structured);
        result.getDynamicObject()->setProperty("isError", !controlResult.ok);
    }
    else if (method == "resources/list")
    {
        result = object();
        juce::Array<juce::var> resources;
        for (const auto& uri : { "myktracker://state", "myktracker://view", "myktracker://song", "myktracker://capabilities" })
        {
            auto item = object();
            item.getDynamicObject()->setProperty("uri", uri);
            item.getDynamicObject()->setProperty("name", uri);
            item.getDynamicObject()->setProperty("mimeType", "application/json");
            resources.add(item);
        }
        result.getDynamicObject()->setProperty("resources", resources);
        result = cacheHints(result);
    }
    else if (method == "resources/templates/list")
    {
        result = object();
        juce::Array<juce::var> templates;
        for (const auto& uri : { "myktracker://sequence-set/{setId}", "myktracker://step/{setId}/{sequenceId}/{stepId}", "myktracker://machine-stack/{stackId}" })
        {
            auto item = object();
            item.getDynamicObject()->setProperty("uriTemplate", uri);
            item.getDynamicObject()->setProperty("name", uri);
            item.getDynamicObject()->setProperty("mimeType", "application/json");
            templates.add(item);
        }
        result.getDynamicObject()->setProperty("resourceTemplates", templates);
        result = cacheHints(result);
    }
    else if (method == "resources/read")
    {
        const auto uri = params.getProperty("uri", {}).toString();
        const auto resource = control.readResource(uri);
        if (!resource.ok) return errorResponse(id, -32602, resource.message);
        result = object();
        auto item = object();
        item.getDynamicObject()->setProperty("uri", uri);
        item.getDynamicObject()->setProperty("mimeType", "application/json");
        item.getDynamicObject()->setProperty("text", juce::JSON::toString(resource.data, false));
        juce::Array<juce::var> contents; contents.add(item);
        result.getDynamicObject()->setProperty("contents", contents);
        result = cacheHints(result);
    }
    else return errorResponse(id, -32601, "Method not found");

    Response response;
    response.json = responseFor(id, result);
    return response;
}
