#include "core/attach.h"
#include "core/session.h"

#include <renderdoc_replay.h>

#include <string>
#include <vector>

namespace renderdoc::core {

namespace {

// RAII guard for ctrl->Shutdown() — single cleanup path, no manual Shutdown()
struct CtrlGuard {
    ITargetControl* c;
    ~CtrlGuard() { if (c) c->Shutdown(); }
};

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
    info.api = ctrl->GetAPI().c_str();
    info.busyClient = ctrl->GetBusyClient().c_str();
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
        result.isApiInited = !info.api.empty();
        return result;
    }

    return result;
}

} // namespace renderdoc::core
