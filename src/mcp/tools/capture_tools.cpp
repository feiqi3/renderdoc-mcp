#include "mcp/tools/tools.h"
#include "mcp/tool_registry.h"
#include "mcp/serialization.h"
#include "core/attach.h"
#include "core/capture.h"
#include "core/info.h"
#include "core/session.h"

namespace renderdoc::mcp::tools {

void registerCaptureTools(ToolRegistry& registry) {
    registry.registerTool({
        "capture_frame",
        "Launch an application with RenderDoc injected, capture a frame after a delay, "
        "and automatically open it for analysis. Returns capture info (API, total "
        "event/draw counts) and the .rdc file path.",
        {{"type", "object"},
         {"properties",
          {{"exePath", {{"type", "string"},
                        {"description", "Absolute path to the target executable"}}},
           {"workingDir", {{"type", "string"},
                           {"description", "Working directory for the target. "
                                           "Defaults to exePath's parent directory"}}},
           {"cmdLine", {{"type", "string"},
                        {"description", "Command line arguments for the target"}}},
           {"delayFrames", {{"type", "integer"},
                            {"description", "Number of frames to wait before capturing. "
                                            "Default: 100"}}},
           {"cycleWindows", {{"type", "integer"},
                             {"description", "Number of times to cycle the active window "
                                             "before capturing (multi-swapchain apps). "
                                             "Default: 0"}}},
           {"outputPath", {{"type", "string"},
                           {"description", "Path for the .rdc file. Default: auto-generated "
                                           "in temp directory"}}}}},
         {"required", {"exePath"}}},
        [](mcp::ToolContext& ctx, const nlohmann::json& args) -> nlohmann::json {
            auto& session = ctx.session;
            core::CaptureRequest req;
            req.exePath = args.at("exePath").get<std::string>();
            req.workingDir = args.value("workingDir", "");
            req.cmdLine = args.value("cmdLine", "");
            req.delayFrames = args.value("delayFrames", 100);
            req.cycleWindows = args.value("cycleWindows", 0);
            req.outputPath = args.value("outputPath", "");

            auto result = core::captureFrame(session, req);

            // Session is now open — return same format as open_capture + path
            auto info = core::getCaptureInfo(session);
            auto j = to_json(info);
            j["path"] = result.capturePath;
            j["pid"] = result.pid;
            return j;
        }
    });

    registry.registerTool({
        "list_attach_targets",
        "List all RenderDoc-injected targets on a machine (defaults to the local "
        "machine) that can be attached to for live capture. Use this to discover "
        "the ident/pid to pass to attach_process / capture_frame_remote.",
        {{"type", "object"},
         {"properties",
          {{"remoteServer", {{"type", "string"},
                             {"description", "Host running the injected targets. "
                                             "Default: 127.0.0.1"}}}}},
         {"required", nlohmann::json::array()}},
        [](mcp::ToolContext& ctx, const nlohmann::json& args) -> nlohmann::json {
            core::AttachRequest req;
            req.remoteServer = args.value("remoteServer", "127.0.0.1");

            auto candidates = core::enumerateAttachTargets(ctx.session, req);

            nlohmann::json targets = nlohmann::json::array();
            for (const auto& t : candidates.targets) {
                targets.push_back({
                    {"ident", t.ident},
                    {"pid", t.pid},
                    {"targetName", t.targetName},
                    {"api", t.api},
                    {"busyClient", t.busyClient},
                });
            }
            return {{"targets", std::move(targets)},
                    {"count", candidates.targets.size()}};
        }
    });

    registry.registerTool({
        "attach_process",
        "Attach to a RenderDoc-injected running process, matched by pid and/or "
        "executable name. Returns the targetIdent needed by capture_frame_remote, "
        "plus whether the graphics API has initialised. Note: the target must have "
        "been launched with RenderDoc injection (e.g. via capture_frame, "
        "renderdoccmd, or the RenderDoc UI) beforehand.",
        {{"type", "object"},
         {"properties",
          {{"remoteServer", {{"type", "string"},
                             {"description", "Host running the target. Default: 127.0.0.1"}}},
           {"pid", {{"type", "integer"},
                    {"description", "Process ID to match. 0 = no pid filter"}}},
           {"exeName", {{"type", "string"},
                        {"description", "Executable name substring to match. "
                                        "Empty = no name filter"}}}}},
         {"required", nlohmann::json::array()}},
        [](mcp::ToolContext& ctx, const nlohmann::json& args) -> nlohmann::json {
            core::AttachRequest req;
            req.remoteServer = args.value("remoteServer", "127.0.0.1");
            req.pid = args.value("pid", 0);
            req.exeName = args.value("exeName", "");

            auto result = core::AttachProcess(ctx.session, req);

            if (!result.attachSuccess)
                throw std::runtime_error(
                    "No matching RenderDoc-injected target found on " + req.remoteServer +
                    ". Ensure the process was launched with RenderDoc injection.");

            return {{"attached", true},
                    {"targetIdent", result.targetIdent},
                    {"pid", result.pid},
                    {"targetName", result.targetName},
                    {"api", result.api},
                    {"isApiInited", result.isApiInited}};
        }
    });

    registry.registerTool({
        "capture_frame_remote",
        "Connect to an already-injected running process (local or remote machine), "
        "trigger a frame capture, copy the resulting .rdc over the network, and open "
        "it for analysis. Identify the target either by ident (from attach_process / "
        "list_attach_targets) or by pid. The process must already have RenderDoc "
        "injection active.",
        {{"type", "object"},
         {"properties",
          {{"remoteAddress", {{"type", "string"},
                              {"description", "Host running the target. Default: 127.0.0.1"}}},
           {"ident", {{"type", "integer"},
                      {"description", "Target ident from attach_process / "
                                      "list_attach_targets. 0 = locate by pid"}}},
           {"pid", {{"type", "integer"},
                    {"description", "Process ID to match when ident is 0 or to "
                                    "verify the ident. 0 = no pid filter"}}},
           {"delayFrames", {{"type", "integer"},
                            {"description", "Number of frames to wait before capturing. "
                                            "Default: 100"}}},
           {"cycleWindows", {{"type", "integer"},
                             {"description", "Number of times to cycle the active window "
                                             "before capturing (multi-swapchain apps). "
                                             "Default: 0"}}},
           {"outputPath", {{"type", "string"},
                           {"description", "Local path for the .rdc file. Default: "
                                           "auto-generated in temp directory"}}}}},
         {"required", nlohmann::json::array()}},
        [](mcp::ToolContext& ctx, const nlohmann::json& args) -> nlohmann::json {
            auto& session = ctx.session;
            core::RemoteCaptureRequest req;
            req.remoteAddress = args.value("remoteAddress", "127.0.0.1");
            req.ident = args.value("ident", 0);
            req.pid = args.value("pid", 0);
            req.delayFrames = args.value("delayFrames", 100);
            req.cycleWindows = args.value("cycleWindows", 0);
            req.outputPath = args.value("outputPath", "");

            auto result = core::captureFrameRemote(session, req);

            auto info = core::getCaptureInfo(session);
            auto j = to_json(info);
            j["path"] = result.capturePath;
            j["pid"] = result.pid;
            return j;
        }
    });
}

} // namespace renderdoc::mcp::tools
