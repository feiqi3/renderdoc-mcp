#pragma once

#include "core/types.h"

namespace renderdoc::core {

class Session;

// Enumerate all renderdoc-injected targets reachable on req.remoteServer
// ("127.0.0.1" lists targets injected on the local machine).
AttachCandidates enumerateAttachTargets(Session& session, const AttachRequest& req);

// Attach to the target matching req.pid / req.exeName. On success the returned
// targetIdent can be passed to captureFrameRemote.
AttachResult AttachProcess(Session& session, const AttachRequest& req);

} // namespace renderdoc::core
