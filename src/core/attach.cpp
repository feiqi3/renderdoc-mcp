#include "core/attach.h"
#include "core/session.h"

#include <renderdoc_replay.h>

#include <chrono>
#include <string>
#include <vector>

namespace renderdoc::core {

namespace {

// RAII guard for ctrl->Shutdown() — single cleanup path, no manual Shutdown()
struct CtrlGuard {
    ITargetControl* c;
    ~CtrlGuard() { if (c) c->Shutdown(); }
};

// How long to pump messages while waiting for the initial APIUse packet.
// The target's control thread pushes it on its first tick (~10ms) after we
// connect, so this only needs to cover connection jitter.
constexpr auto kApiProbeTimeout = std::chrono::milliseconds(2000);

// Connect to one target ident and gather its info. ident/pid stay valid even
// when the probe fails to connect (empty targetName signals failure).
AttachTargetInfo probeTarget(const std::string& host, uint32_t ident) {
    AttachTargetInfo info;
    info.ident = ident;

    ITargetControl* ctrl = RENDERDOC_CreateTargetControl(
        rdcstr(host.c_str()), ident, rdcstr("renderdoc-mcp"), true);
    if (!ctrl)
        return info;

    CtrlGuard guard{ctrl};

    if (!ctrl->Connected())
        return info;

    info.pid = ctrl->GetPID();
    info.targetName = ctrl->GetTarget().c_str();
    info.busyClient = ctrl->GetBusyClient().c_str();

    // IMPORTANT: the API name is only surfaced to this client while pumping
    // ReceiveMessage(). The server pushes an ePacket_APIUse for each active
    // driver right after we connect; without pumping, GetAPI() stays empty
    // forever even when the target is actively rendering.
    // We wait for a PRESENTING API (i.e. capture-ready); a registered but not
    // yet presenting API is still recorded, just not marked as inited.
    auto deadline = std::chrono::steady_clock::now() + kApiProbeTimeout;
    while (std::chrono::steady_clock::now() < deadline) {
        TargetControlMessage msg = ctrl->ReceiveMessage(nullptr);

        if (msg.type == TargetControlMessageType::Disconnected)
            break;

        if (msg.type == TargetControlMessageType::RegisterAPI) {
            if (!msg.apiUse.name.isEmpty())
                info.api = msg.apiUse.name.c_str();
            if (msg.apiUse.presenting) {
                info.isApiInited = true;
                break; // capture-ready
            }
        }
    }
    return info;
}

bool matchesTarget(const AttachTargetInfo& info, const AttachRequest& req) {
    if (req.pid != 0 && info.pid != req.pid)
        return false;
    if (!req.exeName.empty() &&
        info.targetName.find(req.exeName) == std::string::npos)
        return false;
    return true;
}

std::vector<AttachTargetInfo> enumerateTargets(const std::string& host) {
    std::vector<AttachTargetInfo> targets;
    uint32_t ident = 0;
    while ((ident = RENDERDOC_EnumerateRemoteTargets(
                rdcstr(host.c_str()), ident)) != 0) {
        targets.push_back(probeTarget(host, ident));
    }
    return targets;
}

} // namespace

AttachCandidates enumerateAttachTargets(Session& session, const AttachRequest& req) {
    session.ensureReplayInitialized();
    AttachCandidates result;
    result.targets = enumerateTargets(req.remoteServer);
    return result;
}

AttachResult AttachProcess(Session& session, const AttachRequest& req) {
    session.ensureReplayInitialized();

    AttachResult result{};

    for (const auto& info : enumerateTargets(req.remoteServer)) {
        if (info.targetName.empty() && info.pid == 0)
            continue; // probe failed to connect to this ident
        if (!matchesTarget(info, req))
            continue;

        result.attachSuccess = true;
        result.targetIdent = info.ident;
        result.pid = info.pid;
        result.targetName = info.targetName;
        result.api = info.api;
        result.isApiInited = info.isApiInited;
        return result;
    }

    return result;
}

} // namespace renderdoc::core
