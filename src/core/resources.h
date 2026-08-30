#pragma once

#include "core/types.h"
#include <optional>
#include <string>
#include <vector>

namespace renderdoc::core {

class Session;

std::vector<ResourceInfo> listResources(const Session& session,
                                         const std::string& typeFilter = "",
                                         const std::string& nameFilter = "");

ResourceInfo getResourceDetails(const Session& session, ResourceId id);

// Read raw bytes from a buffer resource at the current event.
// Throws if the resource is not a buffer or the read fails.
BufferDataResult getBufferData(const Session& session, ResourceId id,
                               uint64_t byteOffset = 0,
                               uint64_t byteSize = 0);

std::vector<PassInfo> listPasses(const Session& session);

PassInfo getPassInfo(const Session& session, uint32_t eventId);

} // namespace renderdoc::core
