#include "mcp/tools/tools.h"
#include "mcp/tool_registry.h"
#include "mcp/serialization.h"
#include "core/session.h"
#include "core/pass_analysis.h"
#include "core/resources.h"

#include <algorithm>
#include <cstring>

namespace renderdoc::mcp::tools {

void registerResourceTools(ToolRegistry& registry) {
    // list_resources
    registry.registerTool({
        "list_resources",
        "List all GPU resources (textures, buffers, shaders, etc.) in the capture with type and size info",
        {{"type", "object"},
         {"properties", {
             {"type", {{"type", "string"}, {"description", "Filter by resource type keyword (e.g. Texture, Buffer, Shader)"}}},
             {"name", {{"type", "string"}, {"description", "Filter by name keyword (case-insensitive)"}}}
         }}},
        [](mcp::ToolContext& ctx, const nlohmann::json& args) -> nlohmann::json {
            auto& session = ctx.session;
            auto typeFilter = args.value("type", std::string());
            auto nameFilter = args.value("name", std::string());
            auto resources  = core::listResources(session, typeFilter, nameFilter);
            nlohmann::json result;
            result["resources"] = to_json_array(resources);
            result["count"]     = resources.size();
            return result;
        }
    });

    // get_buffer_data
    registry.registerTool({
        "get_buffer_data",
        "Read raw bytes from a buffer resource (vertex buffer, index buffer, SSBO, generic buffer) "
        "at the current event. Returns the bytes as hex plus decoded float/int/uint views. "
        "byteSize 0 defaults to 4096 bytes. Keep requests small (a few KB) to avoid flooding context.",
        {{"type", "object"},
         {"properties", {
             {"resourceId", {{"type", "string"}, {"description", "Buffer resource ID (e.g. ResourceId::123)"}}},
             {"byteOffset", {{"type", "integer"}, {"description", "Byte offset into the buffer (default 0)"}}},
             {"byteSize", {{"type", "integer"}, {"description", "Number of bytes to read (0 = default 4096, clamped to buffer end)"}}},
             {"floatCount", {{"type", "integer"}, {"description", "How many floats to decode (default 64, max 4096)"}}}
          }},
          {"required", {"resourceId"}}},
        [](mcp::ToolContext& ctx, const nlohmann::json& args) -> nlohmann::json {
            auto& session = ctx.session;
            auto id   = parseResourceId(args["resourceId"].get<std::string>());
            uint64_t byteOffset = args.value("byteOffset", 0);
            uint64_t byteSize   = args.value("byteSize", 0);
            uint32_t floatCount = args.value("floatCount", 64);
            if (floatCount > 4096) floatCount = 4096;

            auto data = core::getBufferData(session, id, byteOffset, byteSize);

            nlohmann::json result;
            result["resourceId"]  = resourceIdToString(data.resourceId);
            if (data.bufferLength > 0) result["bufferLength"] = data.bufferLength;
            result["byteOffset"]  = data.byteOffset;
            result["byteSize"]    = data.byteSize;

            static const char hexDigits[] = "0123456789abcdef";
            std::string hex;
            hex.reserve(data.bytes.size() * 2);
            for (uint8_t b : data.bytes) {
                hex.push_back(hexDigits[b >> 4]);
                hex.push_back(hexDigits[b & 0xF]);
            }
            result["hex"] = hex;

            size_t maxFloats = std::min<size_t>(floatCount, data.bytes.size() / 4);
            auto floats = nlohmann::json::array();
            for (size_t i = 0; i < maxFloats; i++) {
                float f;
                std::memcpy(&f, data.bytes.data() + i * 4, sizeof(f));
                floats.push_back(f);
            }
            if (!floats.empty()) result["floats"] = floats;

            auto uints = nlohmann::json::array();
            size_t maxUints = std::min<size_t>(64, data.bytes.size() / 4);
            for (size_t i = 0; i < maxUints; i++) {
                uint32_t u;
                std::memcpy(&u, data.bytes.data() + i * 4, sizeof(u));
                uints.push_back(u);
            }
            if (!uints.empty()) result["uints"] = uints;
            return result;
        }
    });

    // get_resource_info
    registry.registerTool({
        "get_resource_info",
        "Get detailed information about a specific GPU resource by its ID",
        {{"type", "object"},
         {"properties", {
             {"resourceId", {{"type", "string"}, {"description", "Resource ID string (e.g. ResourceId::123)"}}}
         }},
         {"required", {"resourceId"}}},
        [](mcp::ToolContext& ctx, const nlohmann::json& args) -> nlohmann::json {
            auto& session = ctx.session;
            auto idStr = args["resourceId"].get<std::string>();
            auto id    = parseResourceId(idStr);
            auto info  = core::getResourceDetails(session, id);
            return to_json(info);
        }
    });

    // list_passes
    registry.registerTool({
        "list_passes",
        "List all render passes in the capture. Returns marker-based passes when available, otherwise synthetic passes grouped by render target changes.",
        {{"type", "object"},
         {"properties", nlohmann::json::object()}},
        [](mcp::ToolContext& ctx, const nlohmann::json& /*args*/) -> nlohmann::json {
            auto& session = ctx.session;
            auto passes = core::enumeratePassRanges(session);
            nlohmann::json result;
            result["passes"] = to_json_array(passes);
            result["count"]  = passes.size();
            return result;
        }
    });

    // get_pass_info
    registry.registerTool({
        "get_pass_info",
        "Get details about a specific render pass including its draw calls",
        {{"type", "object"},
         {"properties", {
             {"eventId", {{"type", "integer"}, {"description", "Event ID of the pass marker"}}}
         }},
         {"required", {"eventId"}}},
        [](mcp::ToolContext& ctx, const nlohmann::json& args) -> nlohmann::json {
            auto& session = ctx.session;
            auto eventId = args["eventId"].get<uint32_t>();
            auto pass    = core::getPassInfo(session, eventId);
            return to_json(pass);
        }
    });
}

} // namespace renderdoc::mcp::tools
