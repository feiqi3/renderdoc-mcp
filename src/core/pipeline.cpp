#include "core/pipeline.h"
#include "core/errors.h"
#include "core/resource_id.h"
#include "core/session.h"

#include <renderdoc_replay.h>

#include <tuple>

namespace renderdoc::core {

namespace {

const char* fillModeToString(::FillMode mode) {
    switch (mode) {
        case ::FillMode::Solid:     return "Solid";
        case ::FillMode::Wireframe: return "Wireframe";
        case ::FillMode::Point:     return "Point";
    }
    return "Solid";
}

const char* cullModeToString(::CullMode mode) {
    switch (mode) {
        case ::CullMode::NoCull:       return "NoCull";
        case ::CullMode::Front:        return "Front";
        case ::CullMode::Back:         return "Back";
        case ::CullMode::FrontAndBack: return "FrontAndBack";
    }
    return "NoCull";
}

const char* compareFuncToString(::CompareFunction func) {
    switch (func) {
        case ::CompareFunction::Never:        return "Never";
        case ::CompareFunction::AlwaysTrue:   return "Always";
        case ::CompareFunction::Less:         return "Less";
        case ::CompareFunction::LessEqual:    return "LessEqual";
        case ::CompareFunction::Greater:      return "Greater";
        case ::CompareFunction::GreaterEqual: return "GreaterEqual";
        case ::CompareFunction::Equal:        return "Equal";
        case ::CompareFunction::NotEqual:     return "NotEqual";
    }
    return "Always";
}

const char* blendMultiplierToString(::BlendMultiplier mult) {
    switch (mult) {
        case ::BlendMultiplier::Zero:           return "Zero";
        case ::BlendMultiplier::One:            return "One";
        case ::BlendMultiplier::SrcCol:         return "SrcCol";
        case ::BlendMultiplier::InvSrcCol:      return "InvSrcCol";
        case ::BlendMultiplier::DstCol:         return "DstCol";
        case ::BlendMultiplier::InvDstCol:      return "InvDstCol";
        case ::BlendMultiplier::SrcAlpha:       return "SrcAlpha";
        case ::BlendMultiplier::InvSrcAlpha:    return "InvSrcAlpha";
        case ::BlendMultiplier::DstAlpha:       return "DstAlpha";
        case ::BlendMultiplier::InvDstAlpha:    return "InvDstAlpha";
        case ::BlendMultiplier::SrcAlphaSat:    return "SrcAlphaSat";
        case ::BlendMultiplier::FactorRGB:      return "Factor";
        case ::BlendMultiplier::InvFactorRGB:   return "InvFactor";
        case ::BlendMultiplier::FactorAlpha:    return "FactorAlpha";
        case ::BlendMultiplier::InvFactorAlpha: return "InvFactorAlpha";
        case ::BlendMultiplier::Src1Col:        return "Src1Col";
        case ::BlendMultiplier::InvSrc1Col:     return "InvSrc1Col";
        case ::BlendMultiplier::Src1Alpha:      return "Src1Alpha";
        case ::BlendMultiplier::InvSrc1Alpha:   return "InvSrc1Alpha";
        default:                                return "One";
    }
}

const char* blendOpToString(::BlendOperation op) {
    switch (op) {
        case ::BlendOperation::Add:              return "Add";
        case ::BlendOperation::Subtract:         return "Subtract";
        case ::BlendOperation::ReversedSubtract: return "ReverseSubtract";
        case ::BlendOperation::Minimum:          return "Min";
        case ::BlendOperation::Maximum:          return "Max";
        default:                                 return "Add";
    }
}

const char* stencilOpToString(::StencilOperation op) {
    switch (op) {
        case ::StencilOperation::Keep:    return "Keep";
        case ::StencilOperation::Zero:    return "Zero";
        case ::StencilOperation::Replace: return "Replace";
        case ::StencilOperation::IncSat:  return "IncSat";
        case ::StencilOperation::DecSat:  return "DecSat";
        case ::StencilOperation::IncWrap: return "IncWrap";
        case ::StencilOperation::DecWrap: return "DecWrap";
        case ::StencilOperation::Invert:  return "Invert";
    }
    return "Keep";
}

void fillStencilOpInfo(const ::StencilFace& face, StencilOpInfo& out) {
    out.func        = compareFuncToString(face.function);
    out.failOp      = stencilOpToString(face.failOperation);
    out.depthFailOp = stencilOpToString(face.depthFailOperation);
    out.passOp      = stencilOpToString(face.passOperation);
    out.reference   = face.reference;
    out.compareMask = face.compareMask;
    out.writeMask   = face.writeMask;
}

void fillBlendEquation(const ::BlendEquation& eq, BlendEquationInfo& out) {
    out.source     = blendMultiplierToString(eq.source);
    out.destination= blendMultiplierToString(eq.destination);
    out.operation  = blendOpToString(eq.operation);
}

// Fill the common blend section from RenderDoc's ColorBlend list.
void fillBlendState(bool independentBlend, const rdcarray<::ColorBlend>& blends,
                    const rdcfixedarray<float, 4>& blendFactor, BlendStateInfo& out) {
    out.independentBlend = independentBlend;
    for (int i = 0; i < 4; i++) out.blendFactor[i] = blendFactor[i];
    for (size_t i = 0; i < blends.size(); i++) {
        BlendTargetInfo t;
        t.blendEnable = blends[i].enabled;
        fillBlendEquation(blends[i].colorBlend, t.colorBlend);
        fillBlendEquation(blends[i].alphaBlend, t.alphaBlend);
        t.writeMask = blends[i].writeMask;
        out.targets.push_back(std::move(t));
    }
}

// Map a RenderDoc ShaderStage to our ShaderStage enum. Returns false for
// stages we do not model (task/mesh etc.).
bool stageFromRdc(::ShaderStage in, ShaderStage& out) {
    switch (in) {
        case ::ShaderStage::Vertex:   out = ShaderStage::Vertex;   return true;
        case ::ShaderStage::Hull:     out = ShaderStage::Hull;     return true;
        case ::ShaderStage::Domain:   out = ShaderStage::Domain;   return true;
        case ::ShaderStage::Geometry: out = ShaderStage::Geometry; return true;
        case ::ShaderStage::Pixel:    out = ShaderStage::Pixel;    return true;
        case ::ShaderStage::Compute:  out = ShaderStage::Compute;  return true;
        default: return false;
    }
}

uint32_t rdcStageFor(ShaderStage stage) {
    switch (stage) {
        case ShaderStage::Vertex:   return (uint32_t)::ShaderStage::Vertex;
        case ShaderStage::Hull:     return (uint32_t)::ShaderStage::Hull;
        case ShaderStage::Domain:   return (uint32_t)::ShaderStage::Domain;
        case ShaderStage::Geometry: return (uint32_t)::ShaderStage::Geometry;
        case ShaderStage::Pixel:    return (uint32_t)::ShaderStage::Pixel;
        case ShaderStage::Compute:  return (uint32_t)::ShaderStage::Compute;
    }
    return 0;
}

std::string topologyToString(::Topology topo) {
    switch (topo) {
        case ::Topology::PointList:       return "PointList";
        case ::Topology::LineList:        return "LineList";
        case ::Topology::LineStrip:       return "LineStrip";
        case ::Topology::TriangleList:    return "TriangleList";
        case ::Topology::TriangleStrip:   return "TriangleStrip";
        case ::Topology::TriangleFan:     return "TriangleFan";
        case ::Topology::LineList_Adj:    return "LineList_Adj";
        case ::Topology::LineStrip_Adj:   return "LineStrip_Adj";
        case ::Topology::TriangleList_Adj:  return "TriangleList_Adj";
        case ::Topology::TriangleStrip_Adj: return "TriangleStrip_Adj";
        default:
            if (topo >= ::Topology::PatchList_1CPs && topo <= ::Topology::PatchList_32CPs)
                return "PatchList_" + std::to_string((int)topo - (int)::Topology::PatchList_1CPs + 1);
            return "Unknown";
    }
}

std::string nameFromInputSignature(const ::ShaderReflection* refl, uint32_t location) {
    if (!refl) return {};
    for (int i = 0; i < refl->inputSignature.count(); i++) {
        if (refl->inputSignature[i].regIndex == location)
            return refl->inputSignature[i].varName.c_str();
    }
    return {};
}

// A descriptor actually bound at draw/dispatch time, resolved through the
// descriptor store system.
struct ResolvedDescriptor {
    ResourceId resource = 0;
    ResourceId samplerObj = 0;
    uint64_t byteOffset = 0;
    uint64_t byteSize = 0;      // 0xFFFFFFFFFFFFFFFF = whole buffer
    bool bufferBacked = false;  // byteOffset/byteSize are meaningful
    uint32_t accessByteOffset = 0;  // to match Vulkan dynamic offsets
    uint64_t storeId = 0;
};

// Key: (rdc stage, descriptor category, reflection index, array element)
using ResolvedKey = std::tuple<uint32_t, DescriptorCategory, uint32_t, uint32_t>;

// Resolve every descriptor access at the current event into a map keyed by
// shader binding. When vkState is provided (Vulkan captures), dynamic offsets
// are folded into the resolved byte offsets, mirroring what the GPU used.
std::map<ResolvedKey, ResolvedDescriptor> resolveBoundDescriptors(
    IReplayController* ctrl, const VKPipe::State* vkState) {
    std::map<ResolvedKey, ResolvedDescriptor> out;
    const auto& accesses = ctrl->GetDescriptorAccess();
    if (accesses.empty()) return out;

    // One DescriptorRange per access (count=1) so returned descriptors align
    // 1:1 with the accesses, grouped per descriptor store.
    std::map<uint64_t, rdcarray<DescriptorRange>> rangesByStore;
    for (int i = 0; i < accesses.count(); i++) {
        const auto& acc = accesses[i];
        if (acc.index == DescriptorAccess::NoShaderBinding) continue;
        DescriptorRange range;
        range.offset = acc.byteOffset;
        range.descriptorSize = acc.byteSize;
        range.count = 1;
        range.type = acc.type;
        rangesByStore[toResourceId(acc.descriptorStore)].push_back(range);
    }

    std::map<uint64_t, rdcarray<Descriptor>> descByStore;
    std::map<uint64_t, rdcarray<SamplerDescriptor>> sampByStore;
    for (const auto& [store, ranges] : rangesByStore) {
        if (store == 0) continue;
        descByStore[store] = ctrl->GetDescriptors(fromResourceId(store), ranges);
        sampByStore[store] = ctrl->GetSamplerDescriptors(fromResourceId(store), ranges);
    }

    std::map<uint64_t, size_t> cursor;
    for (int i = 0; i < accesses.count(); i++) {
        const auto& acc = accesses[i];
        if (acc.index == DescriptorAccess::NoShaderBinding) continue;

        ShaderStage stage;
        if (!stageFromRdc(acc.stage, stage)) continue;

        uint64_t store = toResourceId(acc.descriptorStore);
        size_t idx = cursor[store]++;

        ResolvedDescriptor rd;
        rd.accessByteOffset = acc.byteOffset;
        rd.storeId = store;

        DescriptorCategory cat = CategoryForDescriptorType(acc.type);
        if (cat == DescriptorCategory::Sampler) {
            const auto& samps = sampByStore[store];
            if (idx < samps.size()) {
                rd.samplerObj = toResourceId(samps[idx].object);
                rd.byteOffset = 0;
                rd.byteSize = 0;
            }
        } else {
            const auto& descs = descByStore[store];
            if (idx < descs.size()) {
                rd.resource = toResourceId(descs[idx].resource);
                rd.byteOffset = descs[idx].byteOffset;
                rd.byteSize = descs[idx].byteSize;
                rd.bufferBacked =
                    (acc.type == DescriptorType::ConstantBuffer ||
                     acc.type == DescriptorType::Buffer || acc.type == DescriptorType::TypedBuffer ||
                     acc.type == DescriptorType::ReadWriteBuffer ||
                     acc.type == DescriptorType::ReadWriteTypedBuffer);
            }
        }

        // Fold Vulkan dynamic offsets into the byte offset (mirrors
        // PipeState::ApplyVulkanDynamicOffsets).
        if (vkState && cat == DescriptorCategory::ConstantBlock) {
            const auto& sets = acc.stage == ::ShaderStage::Compute
                                   ? vkState->compute.descriptorSets
                                   : vkState->graphics.descriptorSets;
            for (const auto& set : sets) {
                for (const auto& off : set.dynamicOffsets) {
                    if (toResourceId(set.descriptorSetResourceId) == store &&
                        off.descriptorByteOffset == acc.byteOffset) {
                        rd.byteOffset += off.dynamicBufferByteOffset;
                    }
                }
            }
        }

        out[{(uint32_t)acc.stage, cat, acc.index, acc.arrayElement}] = rd;
    }

    return out;
}

// Attach resolved bound resources onto the reflection-based binding lists.
void applyBoundResources(std::map<ShaderStage, StageBindings>& result,
                         const std::map<ResolvedKey, ResolvedDescriptor>& resolved) {
    if (resolved.empty()) return;

    auto patchVec = [&](uint32_t rdcStage, DescriptorCategory cat,
                        std::vector<ShaderBindingDetail>& vec) {
        for (uint32_t idx = 0; idx < vec.size(); idx++) {
            const ResolvedDescriptor* best = nullptr;
            for (const auto& [key, rd] : resolved) {
                if (std::get<0>(key) != rdcStage || std::get<1>(key) != cat ||
                    std::get<2>(key) != idx)
                    continue;
                if (!best || std::get<3>(key) == 0)
                    best = &rd;
                if (std::get<3>(key) == 0)
                    break;
            }
            if (!best) continue;
            ResourceId id = (cat == DescriptorCategory::Sampler) ? best->samplerObj : best->resource;
            if (id == 0) continue;
            vec[idx].boundId = id;
            vec[idx].boundHasRange = best->bufferBacked;
            vec[idx].boundByteOffset = best->byteOffset;
            vec[idx].boundByteSize = best->byteSize;
        }
    };

    for (auto& [stage, bindings] : result) {
        uint32_t st = rdcStageFor(stage);
        patchVec(st, DescriptorCategory::ConstantBlock, bindings.constantBuffers);
        patchVec(st, DescriptorCategory::ReadOnlyResource, bindings.readOnlyResources);
        patchVec(st, DescriptorCategory::ReadWriteResource, bindings.readWriteResources);
        patchVec(st, DescriptorCategory::Sampler, bindings.samplers);
    }
}

// Extract StageBindings from a RenderDoc ShaderReflection pointer and resource ID.
StageBindings extractStageBindings(const ::ShaderReflection* refl, ::ResourceId resourceId) {
    StageBindings bindings;
    bindings.shaderId = toResourceId(resourceId);
    if (!refl)
        return bindings;

    for (int i = 0; i < refl->constantBlocks.count(); i++) {
        const auto& cb = refl->constantBlocks[i];
        ShaderBindingDetail detail;
        detail.name = cb.name.c_str();
        detail.bindPoint = cb.fixedBindNumber;
        detail.byteSize = cb.byteSize;
        detail.variableCount = static_cast<uint32_t>(cb.variables.count());
        if (!cb.bufferBacked)
            detail.kind = cb.compileConstants ? "specializationConstants" : "pushConstants";
        bindings.constantBuffers.push_back(std::move(detail));
    }

    for (int i = 0; i < refl->readOnlyResources.count(); i++) {
        const auto& srv = refl->readOnlyResources[i];
        ShaderBindingDetail detail;
        detail.name = srv.name.c_str();
        detail.bindPoint = srv.fixedBindNumber;
        bindings.readOnlyResources.push_back(std::move(detail));
    }

    for (int i = 0; i < refl->readWriteResources.count(); i++) {
        const auto& uav = refl->readWriteResources[i];
        ShaderBindingDetail detail;
        detail.name = uav.name.c_str();
        detail.bindPoint = uav.fixedBindNumber;
        bindings.readWriteResources.push_back(std::move(detail));
    }

    for (int i = 0; i < refl->samplers.count(); i++) {
        const auto& samp = refl->samplers[i];
        ShaderBindingDetail detail;
        detail.name = samp.name.c_str();
        detail.bindPoint = samp.fixedBindNumber;
        bindings.samplers.push_back(std::move(detail));
    }

    return bindings;
}

// On OpenGL, fixedBindNumber in shader reflection is always 0 (bindings are
// dynamic).  Resolve actual bind points via the descriptor system.
void patchGLBindPoints(IReplayController* ctrl, const GLPipe::State* glState,
                       std::map<ShaderStage, StageBindings>& result) {
    if (!glState) return;

    const auto& accesses = ctrl->GetDescriptorAccess();
    if (accesses.empty()) return;

    // Build DescriptorRanges from accesses and query logical locations in one batch.
    rdcarray<DescriptorRange> ranges;
    ranges.reserve(accesses.size());
    for (int i = 0; i < accesses.count(); i++)
        ranges.push_back(DescriptorRange(accesses[i]));

    auto locations = ctrl->GetDescriptorLocations(glState->descriptorStore, ranges);

    // Map: (stage, descriptorType category, reflection index) -> fixedBindNumber
    for (int i = 0; i < accesses.count() && i < locations.count(); i++) {
        const auto& access = accesses[i];
        const auto& loc    = locations[i];

        // Map RenderDoc ShaderStage to our ShaderStage enum
        ShaderStage stage;
        switch (access.stage) {
            case ::ShaderStage::Vertex:   stage = ShaderStage::Vertex;   break;
            case ::ShaderStage::Hull:     stage = ShaderStage::Hull;     break;
            case ::ShaderStage::Domain:   stage = ShaderStage::Domain;   break;
            case ::ShaderStage::Geometry: stage = ShaderStage::Geometry;  break;
            case ::ShaderStage::Pixel:    stage = ShaderStage::Pixel;    break;
            case ::ShaderStage::Compute:  stage = ShaderStage::Compute;  break;
            default: continue;
        }

        auto it = result.find(stage);
        if (it == result.end()) continue;

        auto& bindings = it->second;
        uint16_t idx = access.index;
        uint32_t bindNum = loc.fixedBindNumber;

        auto cat = CategoryForDescriptorType(access.type);
        if (cat == DescriptorCategory::ConstantBlock) {
            if (idx < bindings.constantBuffers.size())
                bindings.constantBuffers[idx].bindPoint = bindNum;
        } else if (cat == DescriptorCategory::ReadOnlyResource) {
            if (idx < bindings.readOnlyResources.size())
                bindings.readOnlyResources[idx].bindPoint = bindNum;
        } else if (cat == DescriptorCategory::ReadWriteResource) {
            if (idx < bindings.readWriteResources.size())
                bindings.readWriteResources[idx].bindPoint = bindNum;
        } else if (cat == DescriptorCategory::Sampler) {
            if (idx < bindings.samplers.size())
                bindings.samplers[idx].bindPoint = bindNum;
        }
    }
}

} // anonymous namespace

PipelineState getPipelineState(const Session& session,
                                std::optional<uint32_t> eventId,
                                bool includeResourceStates) {
    auto* ctrl = session.controller(); // throws NoCaptureOpen if not open

    if (eventId.has_value())
        ctrl->SetFrameEvent(*eventId, true);

    // Cache texture & resource lists for RT info lookups.
    const auto& textures  = ctrl->GetTextures();
    const auto& resources = ctrl->GetResources();

    auto fillRTInfo = [&](RenderTargetInfo& rti) {
        uint64_t rawId = rti.id;
        for (int i = 0; i < textures.count(); i++) {
            uint64_t tid = toResourceId(textures[i].resourceId);
            if (tid == rawId) {
                rti.width  = textures[i].width;
                rti.height = textures[i].height;
                break;
            }
        }
        for (int i = 0; i < resources.count(); i++) {
            uint64_t rid = toResourceId(resources[i].resourceId);
            if (rid == rawId) {
                rti.name = resources[i].name.c_str();
                break;
            }
        }
    };

    auto resourceName = [&](uint64_t rawId) -> std::string {
        for (int i = 0; i < resources.count(); i++) {
            if (toResourceId(resources[i].resourceId) == rawId)
                return resources[i].name.c_str();
        }
        return {};
    };

    APIProperties props = ctrl->GetAPIProperties();

    PipelineState state;

    auto fillScissors = [&](const rdcarray<::Scissor>& scs) {
        for (size_t i = 0; i < scs.size(); i++) {
            ScissorRect s;
            s.x = scs[i].x;
            s.y = scs[i].y;
            s.width = scs[i].width;
            s.height = scs[i].height;
            s.enabled = scs[i].enabled;
            state.scissors.push_back(s);
        }
    };

    auto fillStencilDepth = [&](bool depthTest, bool depthWrite, ::CompareFunction depthFunc,
                                bool depthBounds, float minBounds, float maxBounds,
                                bool stencilTest, const ::StencilFace& front,
                                const ::StencilFace& back) {
        DepthStencilInfo ds;
        ds.depthTestEnable = depthTest;
        ds.depthWriteEnable = depthWrite;
        ds.depthFunc = compareFuncToString(depthFunc);
        ds.depthBoundsEnable = depthBounds;
        ds.minDepthBounds = minBounds;
        ds.maxDepthBounds = maxBounds;
        ds.stencilTestEnable = stencilTest;
        fillStencilOpInfo(front, ds.frontFace);
        fillStencilOpInfo(back, ds.backFace);
        state.depthStencil = std::move(ds);
    };

    switch (props.pipelineType) {
        case GraphicsAPI::D3D11: {
            state.api = GraphicsApi::D3D11;
            const D3D11Pipe::State* ps = ctrl->GetD3D11PipelineState();
            if (!ps) break;

            // Vertex Shader
            {
                PipelineState::ShaderBinding sb;
                sb.stage = ShaderStage::Vertex;
                sb.shaderId = toResourceId(ps->vertexShader.resourceId);
                if (ps->vertexShader.reflection)
                    sb.entryPoint = ps->vertexShader.reflection->entryPoint.c_str();
                state.shaders.push_back(std::move(sb));
            }
            // Pixel Shader
            {
                PipelineState::ShaderBinding sb;
                sb.stage = ShaderStage::Pixel;
                sb.shaderId = toResourceId(ps->pixelShader.resourceId);
                if (ps->pixelShader.reflection)
                    sb.entryPoint = ps->pixelShader.reflection->entryPoint.c_str();
                state.shaders.push_back(std::move(sb));
            }
            // Hull Shader
            if (ps->hullShader.resourceId != ::ResourceId::Null()) {
                PipelineState::ShaderBinding sb;
                sb.stage = ShaderStage::Hull;
                sb.shaderId = toResourceId(ps->hullShader.resourceId);
                if (ps->hullShader.reflection)
                    sb.entryPoint = ps->hullShader.reflection->entryPoint.c_str();
                state.shaders.push_back(std::move(sb));
            }
            // Domain Shader
            if (ps->domainShader.resourceId != ::ResourceId::Null()) {
                PipelineState::ShaderBinding sb;
                sb.stage = ShaderStage::Domain;
                sb.shaderId = toResourceId(ps->domainShader.resourceId);
                if (ps->domainShader.reflection)
                    sb.entryPoint = ps->domainShader.reflection->entryPoint.c_str();
                state.shaders.push_back(std::move(sb));
            }
            // Geometry Shader
            if (ps->geometryShader.resourceId != ::ResourceId::Null()) {
                PipelineState::ShaderBinding sb;
                sb.stage = ShaderStage::Geometry;
                sb.shaderId = toResourceId(ps->geometryShader.resourceId);
                if (ps->geometryShader.reflection)
                    sb.entryPoint = ps->geometryShader.reflection->entryPoint.c_str();
                state.shaders.push_back(std::move(sb));
            }
            // Compute Shader
            if (ps->computeShader.resourceId != ::ResourceId::Null()) {
                PipelineState::ShaderBinding sb;
                sb.stage = ShaderStage::Compute;
                sb.shaderId = toResourceId(ps->computeShader.resourceId);
                if (ps->computeShader.reflection)
                    sb.entryPoint = ps->computeShader.reflection->entryPoint.c_str();
                state.shaders.push_back(std::move(sb));
            }

            // Render Targets
            for (size_t i = 0; i < ps->outputMerger.renderTargets.size(); i++) {
                const auto& rt = ps->outputMerger.renderTargets[i];
                if (rt.resource == ::ResourceId::Null()) continue;
                RenderTargetInfo rti;
                rti.id = toResourceId(rt.resource);
                rti.format = rt.format.Name().c_str();
                fillRTInfo(rti);
                state.renderTargets.push_back(std::move(rti));
            }

            // Depth Target
            if (ps->outputMerger.depthTarget.resource != ::ResourceId::Null()) {
                RenderTargetInfo dti;
                dti.id = toResourceId(ps->outputMerger.depthTarget.resource);
                dti.format = ps->outputMerger.depthTarget.format.Name().c_str();
                fillRTInfo(dti);
                state.depthTarget = std::move(dti);
            }

            // Viewports
            for (const auto& vp : ps->rasterizer.viewports) {
                if (!vp.enabled) continue;
                Viewport v;
                v.x = vp.x;
                v.y = vp.y;
                v.width = vp.width;
                v.height = vp.height;
                v.minDepth = vp.minDepth;
                v.maxDepth = vp.maxDepth;
                state.viewports.push_back(v);
            }

            // Input assembly: topology + index buffer
            {
                InputAssemblyInfo ia;
                ia.topology = topologyToString(ps->inputAssembly.topology);
                if (ps->inputAssembly.indexBuffer.resourceId != ::ResourceId::Null()) {
                    BoundBufferSpan ib;
                    ib.id = toResourceId(ps->inputAssembly.indexBuffer.resourceId);
                    ib.byteOffset = ps->inputAssembly.indexBuffer.byteOffset;
                    ib.byteStride = ps->inputAssembly.indexBuffer.byteStride;
                    ia.indexBuffer = std::move(ib);
                }
                state.inputAssembly = std::move(ia);
            }

            // Vertex input: buffers + input layout elements
            {
                VertexInputInfo vi;
                for (size_t i = 0; i < ps->inputAssembly.vertexBuffers.size(); i++) {
                    const auto& vb = ps->inputAssembly.vertexBuffers[i];
                    if (vb.resourceId == ::ResourceId::Null()) continue;
                    BoundBufferSpan b;
                    b.slot = static_cast<uint32_t>(i);
                    b.id = toResourceId(vb.resourceId);
                    b.byteOffset = vb.byteOffset;
                    b.byteStride = vb.byteStride;
                    vi.buffers.push_back(std::move(b));
                }
                for (const auto& lay : ps->inputAssembly.layouts) {
                    VertexAttributeInfo a;
                    a.name = lay.semanticName.c_str() + std::to_string(lay.semanticIndex);
                    a.vertexBuffer = static_cast<int32_t>(lay.inputSlot);
                    a.byteOffset = lay.byteOffset;
                    a.tightlyPacked = (lay.byteOffset == D3D11Pipe::Layout::TightlyPacked);
                    a.perInstance = lay.perInstance;
                    a.instanceRate = static_cast<int32_t>(lay.instanceDataStepRate);
                    a.format = lay.format.Name().c_str();
                    vi.attributes.push_back(std::move(a));
                }
                if (!vi.buffers.empty() || !vi.attributes.empty())
                    state.vertexInput = std::move(vi);
            }

            // Scissors + rasterizer + depth-stencil + blend + MSAA
            fillScissors(ps->rasterizer.scissors);
            {
                RasterizerInfo r;
                r.fillMode = fillModeToString(ps->rasterizer.state.fillMode);
                r.cullMode = cullModeToString(ps->rasterizer.state.cullMode);
                r.frontCCW = ps->rasterizer.state.frontCCW;
                r.depthClipEnable = ps->rasterizer.state.depthClip;
                r.scissorEnable = ps->rasterizer.state.scissorEnable;
                r.depthBiasEnable = (ps->rasterizer.state.depthBias != 0 ||
                                     ps->rasterizer.state.slopeScaledDepthBias != 0);
                r.depthBias = static_cast<float>(ps->rasterizer.state.depthBias);
                r.slopeScaledDepthBias = ps->rasterizer.state.slopeScaledDepthBias;
                r.depthBiasClamp = ps->rasterizer.state.depthBiasClamp;
                state.rasterizer = std::move(r);
            }
            fillStencilDepth(ps->outputMerger.depthStencilState.depthEnable,
                             ps->outputMerger.depthStencilState.depthWrites,
                             ps->outputMerger.depthStencilState.depthFunction,
                             false, 0.0f, 0.0f,
                             ps->outputMerger.depthStencilState.stencilEnable,
                             ps->outputMerger.depthStencilState.frontFace,
                             ps->outputMerger.depthStencilState.backFace);
            {
                BlendStateInfo b;
                fillBlendState(ps->outputMerger.blendState.independentBlend,
                               ps->outputMerger.blendState.blends,
                               ps->outputMerger.blendState.blendFactor, b);
                state.blend = std::move(b);
            }
            {
                MultisampleInfo m;
                m.rasterSamples = ps->rasterizer.state.forcedSampleCount;
                m.sampleMask = ps->outputMerger.blendState.sampleMask;
                m.alphaToCoverage = ps->outputMerger.blendState.alphaToCoverage;
                state.multisample = std::move(m);
            }
            break;
        }

        case GraphicsAPI::D3D12: {
            state.api = GraphicsApi::D3D12;
            const D3D12Pipe::State* ps = ctrl->GetD3D12PipelineState();
            if (!ps) break;

            // Vertex Shader
            {
                PipelineState::ShaderBinding sb;
                sb.stage = ShaderStage::Vertex;
                sb.shaderId = toResourceId(ps->vertexShader.resourceId);
                if (ps->vertexShader.reflection)
                    sb.entryPoint = ps->vertexShader.reflection->entryPoint.c_str();
                state.shaders.push_back(std::move(sb));
            }
            // Pixel Shader
            {
                PipelineState::ShaderBinding sb;
                sb.stage = ShaderStage::Pixel;
                sb.shaderId = toResourceId(ps->pixelShader.resourceId);
                if (ps->pixelShader.reflection)
                    sb.entryPoint = ps->pixelShader.reflection->entryPoint.c_str();
                state.shaders.push_back(std::move(sb));
            }
            // Hull Shader
            if (ps->hullShader.resourceId != ::ResourceId::Null()) {
                PipelineState::ShaderBinding sb;
                sb.stage = ShaderStage::Hull;
                sb.shaderId = toResourceId(ps->hullShader.resourceId);
                if (ps->hullShader.reflection)
                    sb.entryPoint = ps->hullShader.reflection->entryPoint.c_str();
                state.shaders.push_back(std::move(sb));
            }
            // Domain Shader
            if (ps->domainShader.resourceId != ::ResourceId::Null()) {
                PipelineState::ShaderBinding sb;
                sb.stage = ShaderStage::Domain;
                sb.shaderId = toResourceId(ps->domainShader.resourceId);
                if (ps->domainShader.reflection)
                    sb.entryPoint = ps->domainShader.reflection->entryPoint.c_str();
                state.shaders.push_back(std::move(sb));
            }
            // Geometry Shader
            if (ps->geometryShader.resourceId != ::ResourceId::Null()) {
                PipelineState::ShaderBinding sb;
                sb.stage = ShaderStage::Geometry;
                sb.shaderId = toResourceId(ps->geometryShader.resourceId);
                if (ps->geometryShader.reflection)
                    sb.entryPoint = ps->geometryShader.reflection->entryPoint.c_str();
                state.shaders.push_back(std::move(sb));
            }
            // Compute Shader
            if (ps->computeShader.resourceId != ::ResourceId::Null()) {
                PipelineState::ShaderBinding sb;
                sb.stage = ShaderStage::Compute;
                sb.shaderId = toResourceId(ps->computeShader.resourceId);
                if (ps->computeShader.reflection)
                    sb.entryPoint = ps->computeShader.reflection->entryPoint.c_str();
                state.shaders.push_back(std::move(sb));
            }

            // Render Targets
            for (size_t i = 0; i < ps->outputMerger.renderTargets.size(); i++) {
                const auto& rt = ps->outputMerger.renderTargets[i];
                if (rt.resource == ::ResourceId::Null()) continue;
                RenderTargetInfo rti;
                rti.id = toResourceId(rt.resource);
                rti.format = rt.format.Name().c_str();
                fillRTInfo(rti);
                state.renderTargets.push_back(std::move(rti));
            }

            // Depth Target
            if (ps->outputMerger.depthTarget.resource != ::ResourceId::Null()) {
                RenderTargetInfo dti;
                dti.id = toResourceId(ps->outputMerger.depthTarget.resource);
                dti.format = ps->outputMerger.depthTarget.format.Name().c_str();
                fillRTInfo(dti);
                state.depthTarget = std::move(dti);
            }

            // Viewports
            for (const auto& vp : ps->rasterizer.viewports) {
                if (!vp.enabled) continue;
                Viewport v;
                v.x = vp.x;
                v.y = vp.y;
                v.width = vp.width;
                v.height = vp.height;
                v.minDepth = vp.minDepth;
                v.maxDepth = vp.maxDepth;
                state.viewports.push_back(v);
            }

            // Input assembly: topology + index buffer
            {
                InputAssemblyInfo ia;
                ia.topology = topologyToString(ps->inputAssembly.topology);
                if (ps->inputAssembly.indexBuffer.resourceId != ::ResourceId::Null()) {
                    BoundBufferSpan ib;
                    ib.id = toResourceId(ps->inputAssembly.indexBuffer.resourceId);
                    ib.byteOffset = ps->inputAssembly.indexBuffer.byteOffset;
                    ib.byteSize = ps->inputAssembly.indexBuffer.byteSize;
                    ib.byteStride = ps->inputAssembly.indexBuffer.byteStride;
                    ia.indexBuffer = std::move(ib);
                }
                state.inputAssembly = std::move(ia);
            }

            // Vertex input: buffers + input layout elements
            {
                VertexInputInfo vi;
                for (size_t i = 0; i < ps->inputAssembly.vertexBuffers.size(); i++) {
                    const auto& vb = ps->inputAssembly.vertexBuffers[i];
                    if (vb.resourceId == ::ResourceId::Null()) continue;
                    BoundBufferSpan b;
                    b.slot = static_cast<uint32_t>(i);
                    b.id = toResourceId(vb.resourceId);
                    b.byteOffset = vb.byteOffset;
                    b.byteSize = vb.byteSize;
                    b.byteStride = vb.byteStride;
                    vi.buffers.push_back(std::move(b));
                }
                for (const auto& lay : ps->inputAssembly.layouts) {
                    VertexAttributeInfo a;
                    a.name = lay.semanticName.c_str() + std::to_string(lay.semanticIndex);
                    a.vertexBuffer = static_cast<int32_t>(lay.inputSlot);
                    a.byteOffset = lay.byteOffset;
                    a.tightlyPacked = (lay.byteOffset == D3D12Pipe::Layout::TightlyPacked);
                    a.perInstance = lay.perInstance;
                    a.instanceRate = static_cast<int32_t>(lay.instanceDataStepRate);
                    a.format = lay.format.Name().c_str();
                    vi.attributes.push_back(std::move(a));
                }
                if (!vi.buffers.empty() || !vi.attributes.empty())
                    state.vertexInput = std::move(vi);
            }

            // Scissors + rasterizer + depth-stencil + blend
            fillScissors(ps->rasterizer.scissors);
            {
                RasterizerInfo r;
                r.fillMode = fillModeToString(ps->rasterizer.state.fillMode);
                r.cullMode = cullModeToString(ps->rasterizer.state.cullMode);
                r.frontCCW = ps->rasterizer.state.frontCCW;
                r.depthClipEnable = ps->rasterizer.state.depthClip;
                r.scissorEnable = true;
                r.depthBiasEnable = (ps->rasterizer.state.depthBias != 0.0f ||
                                     ps->rasterizer.state.slopeScaledDepthBias != 0.0f);
                r.depthBias = ps->rasterizer.state.depthBias;
                r.slopeScaledDepthBias = ps->rasterizer.state.slopeScaledDepthBias;
                r.depthBiasClamp = ps->rasterizer.state.depthBiasClamp;
                state.rasterizer = std::move(r);
            }
            fillStencilDepth(ps->outputMerger.depthStencilState.depthEnable,
                             ps->outputMerger.depthStencilState.depthWrites,
                             ps->outputMerger.depthStencilState.depthFunction,
                             false, 0.0f, 0.0f,
                             ps->outputMerger.depthStencilState.stencilEnable,
                             ps->outputMerger.depthStencilState.frontFace,
                             ps->outputMerger.depthStencilState.backFace);
            {
                BlendStateInfo b;
                fillBlendState(ps->outputMerger.blendState.independentBlend,
                               ps->outputMerger.blendState.blends,
                               ps->outputMerger.blendState.blendFactor, b);
                state.blend = std::move(b);
            }

            // Live resource states (D3D12 debug layer barrier tracking)
            if (includeResourceStates) {
                for (size_t i = 0; i < ps->resourceStates.size(); i++) {
                    ResourceLayoutInfo rl;
                    rl.resourceId = toResourceId(ps->resourceStates[i].resourceId);
                    rl.name = resourceName(rl.resourceId);
                    for (size_t j = 0; j < ps->resourceStates[i].states.size(); j++) {
                        ResourceLayoutInfo::LayoutRange lr;
                        lr.state = ps->resourceStates[i].states[j].name.c_str();
                        rl.layouts.push_back(std::move(lr));
                    }
                    state.resourceStates.push_back(std::move(rl));
                }
            }
            break;
        }

        case GraphicsAPI::OpenGL: {
            state.api = GraphicsApi::OpenGL;
            const GLPipe::State* ps = ctrl->GetGLPipelineState();
            if (!ps) break;

            // GL uses shaderResourceId instead of resourceId
            // Vertex Shader
            {
                PipelineState::ShaderBinding sb;
                sb.stage = ShaderStage::Vertex;
                sb.shaderId = toResourceId(ps->vertexShader.shaderResourceId);
                if (ps->vertexShader.reflection)
                    sb.entryPoint = ps->vertexShader.reflection->entryPoint.c_str();
                state.shaders.push_back(std::move(sb));
            }
            // Fragment Shader (maps to Pixel)
            {
                PipelineState::ShaderBinding sb;
                sb.stage = ShaderStage::Pixel;
                sb.shaderId = toResourceId(ps->fragmentShader.shaderResourceId);
                if (ps->fragmentShader.reflection)
                    sb.entryPoint = ps->fragmentShader.reflection->entryPoint.c_str();
                state.shaders.push_back(std::move(sb));
            }
            // Tess Control Shader (maps to Hull)
            if (ps->tessControlShader.shaderResourceId != ::ResourceId::Null()) {
                PipelineState::ShaderBinding sb;
                sb.stage = ShaderStage::Hull;
                sb.shaderId = toResourceId(ps->tessControlShader.shaderResourceId);
                if (ps->tessControlShader.reflection)
                    sb.entryPoint = ps->tessControlShader.reflection->entryPoint.c_str();
                state.shaders.push_back(std::move(sb));
            }
            // Tess Eval Shader (maps to Domain)
            if (ps->tessEvalShader.shaderResourceId != ::ResourceId::Null()) {
                PipelineState::ShaderBinding sb;
                sb.stage = ShaderStage::Domain;
                sb.shaderId = toResourceId(ps->tessEvalShader.shaderResourceId);
                if (ps->tessEvalShader.reflection)
                    sb.entryPoint = ps->tessEvalShader.reflection->entryPoint.c_str();
                state.shaders.push_back(std::move(sb));
            }
            // Geometry Shader
            if (ps->geometryShader.shaderResourceId != ::ResourceId::Null()) {
                PipelineState::ShaderBinding sb;
                sb.stage = ShaderStage::Geometry;
                sb.shaderId = toResourceId(ps->geometryShader.shaderResourceId);
                if (ps->geometryShader.reflection)
                    sb.entryPoint = ps->geometryShader.reflection->entryPoint.c_str();
                state.shaders.push_back(std::move(sb));
            }
            // Compute Shader
            if (ps->computeShader.shaderResourceId != ::ResourceId::Null()) {
                PipelineState::ShaderBinding sb;
                sb.stage = ShaderStage::Compute;
                sb.shaderId = toResourceId(ps->computeShader.shaderResourceId);
                if (ps->computeShader.reflection)
                    sb.entryPoint = ps->computeShader.reflection->entryPoint.c_str();
                state.shaders.push_back(std::move(sb));
            }

            // Render Targets (GL: color attachments on draw FBO, no format)
            for (size_t i = 0; i < ps->framebuffer.drawFBO.colorAttachments.size(); i++) {
                const auto& att = ps->framebuffer.drawFBO.colorAttachments[i];
                if (att.resource == ::ResourceId::Null()) continue;
                RenderTargetInfo rti;
                rti.id = toResourceId(att.resource);
                // GL does not expose a format name on color attachment at this level
                fillRTInfo(rti);
                state.renderTargets.push_back(std::move(rti));
            }

            // Depth Target (GL: depth attachment on draw FBO)
            if (ps->framebuffer.drawFBO.depthAttachment.resource != ::ResourceId::Null()) {
                RenderTargetInfo dti;
                dti.id = toResourceId(ps->framebuffer.drawFBO.depthAttachment.resource);
                fillRTInfo(dti);
                state.depthTarget = std::move(dti);
            }

            // Viewports
            for (const auto& vp : ps->rasterizer.viewports) {
                if (!vp.enabled) continue;
                Viewport v;
                v.x = vp.x;
                v.y = vp.y;
                v.width = vp.width;
                v.height = vp.height;
                v.minDepth = vp.minDepth;
                v.maxDepth = vp.maxDepth;
                state.viewports.push_back(v);
            }

            // Vertex input: VBOs + attributes (GL keeps topology/index buffer
            // here as implicit state derived from the last action)
            {
                VertexInputInfo vi;
                for (size_t i = 0; i < ps->vertexInput.vertexBuffers.size(); i++) {
                    const auto& vb = ps->vertexInput.vertexBuffers[i];
                    if (vb.resourceId == ::ResourceId::Null()) continue;
                    BoundBufferSpan b;
                    b.slot = static_cast<uint32_t>(i);
                    b.id = toResourceId(vb.resourceId);
                    b.byteOffset = vb.byteOffset;
                    b.byteStride = vb.byteStride;
                    vi.buffers.push_back(std::move(b));
                }
                const ::ShaderReflection* vsRefl = ps->vertexShader.reflection;
                for (const auto& attr : ps->vertexInput.attributes) {
                    if (!attr.enabled) continue;
                    VertexAttributeInfo a;
                    if (vsRefl && attr.boundShaderInput >= 0 &&
                        attr.boundShaderInput < vsRefl->inputSignature.count())
                        a.name = vsRefl->inputSignature[attr.boundShaderInput].varName.c_str();
                    a.vertexBuffer = static_cast<int32_t>(attr.vertexBufferSlot);
                    a.byteOffset = attr.byteOffset;
                    a.format = attr.format.Name().c_str();
                    if (attr.vertexBufferSlot < ps->vertexInput.vertexBuffers.size()) {
                        uint32_t div =
                            ps->vertexInput.vertexBuffers[attr.vertexBufferSlot].instanceDivisor;
                        a.perInstance = (div > 0);
                        a.instanceRate = static_cast<int32_t>(div);
                    }
                    vi.attributes.push_back(std::move(a));
                }
                if (!vi.buffers.empty() || !vi.attributes.empty())
                    state.vertexInput = std::move(vi);
            }

            // Input assembly (implicit state on GL)
            {
                InputAssemblyInfo ia;
                ia.topology = topologyToString(ps->vertexInput.topology);
                ia.primitiveRestart = ps->vertexInput.primitiveRestart;
                if (ps->vertexInput.indexBuffer != ::ResourceId::Null()) {
                    BoundBufferSpan ib;
                    ib.id = toResourceId(ps->vertexInput.indexBuffer);
                    ib.byteStride = ps->vertexInput.indexByteStride;
                    ia.indexBuffer = std::move(ib);
                }
                state.inputAssembly = std::move(ia);
            }

            // Scissors + rasterizer + depth-stencil + blend
            fillScissors(ps->rasterizer.scissors);
            {
                RasterizerInfo r;
                r.fillMode = fillModeToString(ps->rasterizer.state.fillMode);
                r.cullMode = cullModeToString(ps->rasterizer.state.cullMode);
                r.frontCCW = ps->rasterizer.state.frontCCW;
                r.depthClipEnable = true;
                r.scissorEnable = true;
                r.depthBiasEnable = (ps->rasterizer.state.depthBias != 0.0f ||
                                     ps->rasterizer.state.slopeScaledDepthBias != 0.0f);
                r.depthBias = ps->rasterizer.state.depthBias;
                r.slopeScaledDepthBias = ps->rasterizer.state.slopeScaledDepthBias;
                r.depthBiasClamp = 0.0f;
                state.rasterizer = std::move(r);
            }
            fillStencilDepth(ps->depthState.depthEnable,
                             ps->depthState.depthWrites,
                             ps->depthState.depthFunction,
                             ps->depthState.depthBounds,
                             ps->depthState.nearBound,
                             ps->depthState.farBound,
                             ps->stencilState.stencilEnable,
                             ps->stencilState.frontFace,
                             ps->stencilState.backFace);
            {
                BlendStateInfo b;
                fillBlendState(false, ps->framebuffer.blendState.blends,
                               ps->framebuffer.blendState.blendFactor, b);
                state.blend = std::move(b);
            }
            break;
        }

        case GraphicsAPI::Vulkan: {
            state.api = GraphicsApi::Vulkan;
            const VKPipe::State* ps = ctrl->GetVulkanPipelineState();
            if (!ps) break;

            // Vertex Shader
            {
                PipelineState::ShaderBinding sb;
                sb.stage = ShaderStage::Vertex;
                sb.shaderId = toResourceId(ps->vertexShader.resourceId);
                if (ps->vertexShader.reflection)
                    sb.entryPoint = ps->vertexShader.reflection->entryPoint.c_str();
                state.shaders.push_back(std::move(sb));
            }
            // Fragment Shader (maps to Pixel)
            {
                PipelineState::ShaderBinding sb;
                sb.stage = ShaderStage::Pixel;
                sb.shaderId = toResourceId(ps->fragmentShader.resourceId);
                if (ps->fragmentShader.reflection)
                    sb.entryPoint = ps->fragmentShader.reflection->entryPoint.c_str();
                state.shaders.push_back(std::move(sb));
            }
            // Tess Control Shader (maps to Hull)
            if (ps->tessControlShader.resourceId != ::ResourceId::Null()) {
                PipelineState::ShaderBinding sb;
                sb.stage = ShaderStage::Hull;
                sb.shaderId = toResourceId(ps->tessControlShader.resourceId);
                if (ps->tessControlShader.reflection)
                    sb.entryPoint = ps->tessControlShader.reflection->entryPoint.c_str();
                state.shaders.push_back(std::move(sb));
            }
            // Tess Eval Shader (maps to Domain)
            if (ps->tessEvalShader.resourceId != ::ResourceId::Null()) {
                PipelineState::ShaderBinding sb;
                sb.stage = ShaderStage::Domain;
                sb.shaderId = toResourceId(ps->tessEvalShader.resourceId);
                if (ps->tessEvalShader.reflection)
                    sb.entryPoint = ps->tessEvalShader.reflection->entryPoint.c_str();
                state.shaders.push_back(std::move(sb));
            }
            // Geometry Shader
            if (ps->geometryShader.resourceId != ::ResourceId::Null()) {
                PipelineState::ShaderBinding sb;
                sb.stage = ShaderStage::Geometry;
                sb.shaderId = toResourceId(ps->geometryShader.resourceId);
                if (ps->geometryShader.reflection)
                    sb.entryPoint = ps->geometryShader.reflection->entryPoint.c_str();
                state.shaders.push_back(std::move(sb));
            }
            // Compute Shader
            if (ps->computeShader.resourceId != ::ResourceId::Null()) {
                PipelineState::ShaderBinding sb;
                sb.stage = ShaderStage::Compute;
                sb.shaderId = toResourceId(ps->computeShader.resourceId);
                if (ps->computeShader.reflection)
                    sb.entryPoint = ps->computeShader.reflection->entryPoint.c_str();
                state.shaders.push_back(std::move(sb));
            }

            // Render Targets (Vulkan: color attachments via renderpass indices)
            {
                const auto& fb = ps->currentPass.framebuffer;
                for (size_t i = 0; i < ps->currentPass.renderpass.colorAttachments.size(); i++) {
                    uint32_t attIdx = ps->currentPass.renderpass.colorAttachments[i];
                    if (attIdx < fb.attachments.size()) {
                        const auto& att = fb.attachments[attIdx];
                        RenderTargetInfo rti;
                        rti.id = toResourceId(att.resource);
                        rti.format = att.format.Name().c_str();
                        fillRTInfo(rti);
                        state.renderTargets.push_back(std::move(rti));
                    }
                }

                // Depth Target
                uint32_t depthIdx = ps->currentPass.renderpass.depthstencilAttachment;
                if (depthIdx < fb.attachments.size()) {
                    const auto& att = fb.attachments[depthIdx];
                    if (att.resource != ::ResourceId::Null()) {
                        RenderTargetInfo dti;
                        dti.id = toResourceId(att.resource);
                        dti.format = att.format.Name().c_str();
                        fillRTInfo(dti);
                        state.depthTarget = std::move(dti);
                    }
                }
            }

            // Viewports
            for (const auto& vps : ps->viewportScissor.viewportScissors) {
                Viewport v;
                v.x = vps.vp.x;
                v.y = vps.vp.y;
                v.width = vps.vp.width;
                v.height = vps.vp.height;
                v.minDepth = vps.vp.minDepth;
                v.maxDepth = vps.vp.maxDepth;
                state.viewports.push_back(v);
            }

            // Descriptor sets with raw dynamic offsets (Vulkan-specific)
            {
                const VKPipe::Pipeline* pipe = nullptr;
                if (ps->graphics.pipelineResourceId != ::ResourceId::Null())
                    pipe = &ps->graphics;
                else if (ps->compute.pipelineResourceId != ::ResourceId::Null())
                    pipe = &ps->compute;
                if (pipe) {
                    for (size_t i = 0; i < pipe->descriptorSets.size(); i++) {
                        const auto& ds = pipe->descriptorSets[i];
                        DescriptorSetInfo dsi;
                        dsi.setIndex = static_cast<uint32_t>(i);
                        dsi.layoutId = toResourceId(ds.layoutResourceId);
                        dsi.setId = toResourceId(ds.descriptorSetResourceId);
                        dsi.pushDescriptor = ds.pushDescriptor;
                        for (const auto& off : ds.dynamicOffsets)
                            dsi.dynamicOffsets.push_back(
                                {off.descriptorByteOffset, off.dynamicBufferByteOffset});
                        state.descriptorSets.push_back(std::move(dsi));
                    }
                }
            }

            // Input assembly
            {
                InputAssemblyInfo ia;
                ia.topology = topologyToString(ps->inputAssembly.topology);
                ia.primitiveRestart = ps->inputAssembly.primitiveRestartEnable;
                if (ps->inputAssembly.indexBuffer.resourceId != ::ResourceId::Null()) {
                    BoundBufferSpan ib;
                    ib.slot = 0;
                    ib.id = toResourceId(ps->inputAssembly.indexBuffer.resourceId);
                    ib.byteOffset = ps->inputAssembly.indexBuffer.byteOffset;
                    ib.byteSize = ps->inputAssembly.indexBuffer.byteSize;
                    ib.byteStride = ps->inputAssembly.indexBuffer.byteStride;
                    ia.indexBuffer = std::move(ib);
                }
                state.inputAssembly = std::move(ia);
            }

            // Vertex input: buffers, bindings (rate) and attributes
            {
                VertexInputInfo vi;
                for (size_t i = 0; i < ps->vertexInput.vertexBuffers.size(); i++) {
                    const auto& vb = ps->vertexInput.vertexBuffers[i];
                    if (vb.resourceId == ::ResourceId::Null()) continue;
                    BoundBufferSpan b;
                    b.slot = static_cast<uint32_t>(i);
                    b.id = toResourceId(vb.resourceId);
                    b.byteOffset = vb.byteOffset;
                    b.byteSize = vb.byteSize;
                    b.byteStride = vb.byteStride;
                    vi.buffers.push_back(std::move(b));
                }
                for (const auto& attr : ps->vertexInput.attributes) {
                    VertexAttributeInfo a;
                    a.name = nameFromInputSignature(ps->vertexShader.reflection, attr.location);
                    a.vertexBuffer = static_cast<int32_t>(attr.binding);
                    a.byteOffset = attr.byteOffset;
                    a.format = attr.format.Name().c_str();
                    for (const auto& b : ps->vertexInput.bindings) {
                        if (b.vertexBufferBinding == attr.binding) {
                            a.perInstance = b.perInstance;
                            a.instanceRate = static_cast<int32_t>(b.instanceDivisor);
                            break;
                        }
                    }
                    vi.attributes.push_back(std::move(a));
                }
                state.vertexInput = std::move(vi);
            }

            // Scissors + rasterizer + depth-stencil + blend + MSAA + push constants
            for (size_t i = 0; i < ps->viewportScissor.viewportScissors.size(); i++) {
                ScissorRect s;
                s.x = ps->viewportScissor.viewportScissors[i].scissor.x;
                s.y = ps->viewportScissor.viewportScissors[i].scissor.y;
                s.width = ps->viewportScissor.viewportScissors[i].scissor.width;
                s.height = ps->viewportScissor.viewportScissors[i].scissor.height;
                s.enabled = ps->viewportScissor.viewportScissors[i].scissor.enabled;
                state.scissors.push_back(s);
            }
            {
                RasterizerInfo r;
                r.fillMode = fillModeToString(ps->rasterizer.fillMode);
                r.cullMode = cullModeToString(ps->rasterizer.cullMode);
                r.frontCCW = ps->rasterizer.frontCCW;
                r.rasterizerDiscard = ps->rasterizer.rasterizerDiscardEnable;
                r.depthClipEnable = ps->rasterizer.depthClipEnable;
                r.scissorEnable = true;
                r.depthBiasEnable = ps->rasterizer.depthBiasEnable;
                r.depthBias = ps->rasterizer.depthBias;
                r.slopeScaledDepthBias = ps->rasterizer.slopeScaledDepthBias;
                r.depthBiasClamp = ps->rasterizer.depthBiasClamp;
                state.rasterizer = std::move(r);
            }
            fillStencilDepth(ps->depthStencil.depthTestEnable,
                             ps->depthStencil.depthWriteEnable,
                             ps->depthStencil.depthFunction,
                             ps->depthStencil.depthBoundsEnable,
                             ps->depthStencil.minDepthBounds,
                             ps->depthStencil.maxDepthBounds,
                             ps->depthStencil.stencilTestEnable,
                             ps->depthStencil.frontFace,
                             ps->depthStencil.backFace);
            {
                BlendStateInfo b;
                fillBlendState(false, ps->colorBlend.blends,
                               ps->colorBlend.blendFactor, b);
                state.blend = std::move(b);
            }
            {
                MultisampleInfo m;
                m.rasterSamples = ps->multisample.rasterSamples;
                m.sampleMask = ps->multisample.sampleMask;
                m.sampleShadingEnable = ps->multisample.sampleShadingEnable;
                m.minSampleShading = ps->multisample.minSampleShading;
                m.alphaToCoverage = ps->colorBlend.alphaToCoverageEnable;
                state.multisample = std::move(m);
            }
            state.pushConstantByteSize = static_cast<uint32_t>(ps->pushconsts.size());

            // Current image layouts for live resources (opt-in)
            if (includeResourceStates) {
                for (size_t i = 0; i < ps->images.size(); i++) {
                    ResourceLayoutInfo rl;
                    rl.resourceId = toResourceId(ps->images[i].resourceId);
                    rl.name = resourceName(rl.resourceId);
                    for (size_t j = 0; j < ps->images[i].layouts.size(); j++) {
                        const auto& lay = ps->images[i].layouts[j];
                        ResourceLayoutInfo::LayoutRange lr;
                        lr.baseMip = lay.baseMip;
                        lr.baseLayer = lay.baseLayer;
                        lr.numMip = lay.numMip;
                        lr.numLayer = lay.numLayer;
                        lr.state = lay.name.c_str();
                        rl.layouts.push_back(std::move(lr));
                    }
                    state.resourceStates.push_back(std::move(rl));
                }
            }
            break;
        }

        default:
            break;
    }

    return state;
}

std::map<ShaderStage, StageBindings> getBindings(const Session& session,
                                                   std::optional<uint32_t> eventId) {
    auto* ctrl = session.controller(); // throws NoCaptureOpen if not open

    if (eventId.has_value())
        ctrl->SetFrameEvent(*eventId, true);

    APIProperties props = ctrl->GetAPIProperties();
    std::map<ShaderStage, StageBindings> result;

    switch (props.pipelineType) {
        case GraphicsAPI::D3D11: {
            const auto* state = ctrl->GetD3D11PipelineState();
            if (!state) break;
            if (state->vertexShader.reflection)
                result[ShaderStage::Vertex] = extractStageBindings(state->vertexShader.reflection, state->vertexShader.resourceId);
            if (state->pixelShader.reflection)
                result[ShaderStage::Pixel] = extractStageBindings(state->pixelShader.reflection, state->pixelShader.resourceId);
            if (state->hullShader.reflection)
                result[ShaderStage::Hull] = extractStageBindings(state->hullShader.reflection, state->hullShader.resourceId);
            if (state->domainShader.reflection)
                result[ShaderStage::Domain] = extractStageBindings(state->domainShader.reflection, state->domainShader.resourceId);
            if (state->geometryShader.reflection)
                result[ShaderStage::Geometry] = extractStageBindings(state->geometryShader.reflection, state->geometryShader.resourceId);
            if (state->computeShader.reflection)
                result[ShaderStage::Compute] = extractStageBindings(state->computeShader.reflection, state->computeShader.resourceId);
            break;
        }
        case GraphicsAPI::D3D12: {
            const auto* state = ctrl->GetD3D12PipelineState();
            if (!state) break;
            if (state->vertexShader.reflection)
                result[ShaderStage::Vertex] = extractStageBindings(state->vertexShader.reflection, state->vertexShader.resourceId);
            if (state->pixelShader.reflection)
                result[ShaderStage::Pixel] = extractStageBindings(state->pixelShader.reflection, state->pixelShader.resourceId);
            if (state->hullShader.reflection)
                result[ShaderStage::Hull] = extractStageBindings(state->hullShader.reflection, state->hullShader.resourceId);
            if (state->domainShader.reflection)
                result[ShaderStage::Domain] = extractStageBindings(state->domainShader.reflection, state->domainShader.resourceId);
            if (state->geometryShader.reflection)
                result[ShaderStage::Geometry] = extractStageBindings(state->geometryShader.reflection, state->geometryShader.resourceId);
            if (state->computeShader.reflection)
                result[ShaderStage::Compute] = extractStageBindings(state->computeShader.reflection, state->computeShader.resourceId);
            break;
        }
        case GraphicsAPI::OpenGL: {
            const auto* state = ctrl->GetGLPipelineState();
            if (!state) break;
            // GL uses shaderResourceId
            if (state->vertexShader.reflection)
                result[ShaderStage::Vertex] = extractStageBindings(state->vertexShader.reflection, state->vertexShader.shaderResourceId);
            if (state->fragmentShader.reflection)
                result[ShaderStage::Pixel] = extractStageBindings(state->fragmentShader.reflection, state->fragmentShader.shaderResourceId);
            if (state->tessControlShader.reflection)
                result[ShaderStage::Hull] = extractStageBindings(state->tessControlShader.reflection, state->tessControlShader.shaderResourceId);
            if (state->tessEvalShader.reflection)
                result[ShaderStage::Domain] = extractStageBindings(state->tessEvalShader.reflection, state->tessEvalShader.shaderResourceId);
            if (state->geometryShader.reflection)
                result[ShaderStage::Geometry] = extractStageBindings(state->geometryShader.reflection, state->geometryShader.shaderResourceId);
            if (state->computeShader.reflection)
                result[ShaderStage::Compute] = extractStageBindings(state->computeShader.reflection, state->computeShader.shaderResourceId);
            // Patch bind points with actual GL descriptor locations
            patchGLBindPoints(ctrl, state, result);
            break;
        }
        case GraphicsAPI::Vulkan: {
            const auto* state = ctrl->GetVulkanPipelineState();
            if (!state) break;
            if (state->vertexShader.reflection)
                result[ShaderStage::Vertex] = extractStageBindings(state->vertexShader.reflection, state->vertexShader.resourceId);
            if (state->fragmentShader.reflection)
                result[ShaderStage::Pixel] = extractStageBindings(state->fragmentShader.reflection, state->fragmentShader.resourceId);
            if (state->tessControlShader.reflection)
                result[ShaderStage::Hull] = extractStageBindings(state->tessControlShader.reflection, state->tessControlShader.resourceId);
            if (state->tessEvalShader.reflection)
                result[ShaderStage::Domain] = extractStageBindings(state->tessEvalShader.reflection, state->tessEvalShader.resourceId);
            if (state->geometryShader.reflection)
                result[ShaderStage::Geometry] = extractStageBindings(state->geometryShader.reflection, state->geometryShader.resourceId);
            if (state->computeShader.reflection)
                result[ShaderStage::Compute] = extractStageBindings(state->computeShader.reflection, state->computeShader.resourceId);
            break;
        }
        default:
            break;
    }

    // Resolve actually-bound resources (buffer/image ids + byte offsets,
    // including dynamic offsets) through the descriptor store and attach
    // them to the reflection entries.
    const VKPipe::State* vkState = (props.pipelineType == GraphicsAPI::Vulkan)
                                       ? ctrl->GetVulkanPipelineState()
                                       : nullptr;
    auto resolved = resolveBoundDescriptors(ctrl, vkState);
    applyBoundResources(result, resolved);

    return result;
}

} // namespace renderdoc::core
