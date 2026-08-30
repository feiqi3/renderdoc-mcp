#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace renderdoc::core {

// --- Common ---
using ResourceId = uint64_t;

enum class GraphicsApi { D3D11, D3D12, OpenGL, Vulkan, Unknown };
enum class ShaderStage { Vertex, Hull, Domain, Geometry, Pixel, Compute };

// ActionFlags: raw RenderDoc bitmask passthrough. The MCP serializer
// converts to pipe-separated strings ("Drawcall|Indexed|Instanced").
using ActionFlagBits = uint32_t;

// --- Session ---
struct CaptureInfo {
    std::string path;
    GraphicsApi api;
    bool degraded = false;
    uint32_t totalEvents = 0;
    uint32_t totalDraws = 0;
    std::string machineIdent;
    std::string driverName;
    bool hasCallstacks = false;
    uint64_t timestampBase = 0;
    struct GpuInfo {
        std::string name;
        std::string vendor;
        uint32_t deviceID = 0;
        std::string driver;
    };
    std::vector<GpuInfo> gpus;
};

struct SessionStatus {
    bool isOpen = false;
    std::string capturePath;
    GraphicsApi api = GraphicsApi::Unknown;
    uint32_t currentEventId = 0;
    uint32_t totalEvents = 0;
};

// --- Events ---
struct EventInfo {
    uint32_t eventId = 0;
    std::string name;
    ActionFlagBits flags = 0;
    uint32_t numIndices = 0;
    uint32_t numInstances = 0;
    uint32_t drawIndex = 0;
    std::vector<ResourceId> outputs;
};

// --- Pipeline ---
struct ShaderBindingDetail {
    std::string name;
    uint32_t bindPoint = 0;
    uint32_t byteSize = 0;
    uint32_t variableCount = 0;
    // "pushConstants" or "specializationConstants" for non-buffer-backed
    // constant blocks (values readable via read_cbuffer); empty for normal
    // buffer-backed bindings.
    std::string kind;
    // The actually bound resource at this slot at draw/dispatch time.
    // boundId == 0 means declared in the shader but nothing bound.
    // boundByteOffset includes dynamic offsets (Vulkan UBO dynamic,
    // D3D11 CBV offsets, GL glBindBufferRange offsets).
    ResourceId boundId = 0;
    uint64_t boundByteOffset = 0;
    uint64_t boundByteSize = 0;   // 0xFFFFFFFFFFFFFFFF = whole buffer
    bool boundHasRange = false;   // true only for buffer-backed bindings where
                                  // boundByteOffset/Size are meaningful
};

struct StageBindings {
    ResourceId shaderId = 0;
    std::vector<ShaderBindingDetail> constantBuffers;
    std::vector<ShaderBindingDetail> readOnlyResources;
    std::vector<ShaderBindingDetail> readWriteResources;
    std::vector<ShaderBindingDetail> samplers;
};

struct BoundResource {
    ResourceId id = 0;
    std::string name;
    std::string typeName;
    uint32_t bindPoint = 0;
};

struct RenderTargetInfo {
    ResourceId id = 0;
    std::string name;
    uint32_t width = 0;
    uint32_t height = 0;
    std::string format;
};

struct Viewport {
    float x = 0, y = 0, width = 0, height = 0, minDepth = 0, maxDepth = 0;
};

// A bound vertex/index buffer span.
struct BoundBufferSpan {
    uint32_t slot = 0;         // VB slot / binding index
    ResourceId id = 0;
    uint64_t byteOffset = 0;
    uint64_t byteSize = 0;     // 0 = unknown/whole buffer
    uint32_t byteStride = 0;   // for index buffers: index width (2/4)
};

struct VertexAttributeInfo {
    std::string name;          // shader input name (semantic / location var)
    int32_t vertexBuffer = -1; // vertex buffer slot/binding
    uint32_t byteOffset = 0;   // offset within each vertex
    bool tightlyPacked = false;// D3D APPEND_ALIGNED_ELEMENT semantics
    bool perInstance = false;
    int32_t instanceRate = 0;
    std::string format;
};

struct InputAssemblyInfo {
    std::string topology;
    bool primitiveRestart = false;
    std::optional<BoundBufferSpan> indexBuffer;
};

struct VertexInputInfo {
    std::vector<BoundBufferSpan> buffers;         // indexed by VB slot
    std::vector<VertexAttributeInfo> attributes;  // one per input element
};

struct DescriptorSetInfo {
    uint32_t setIndex = 0;
    ResourceId layoutId = 0;
    ResourceId setId = 0;
    bool pushDescriptor = false;
    struct DynamicOffset {
        uint64_t descriptorByteOffset = 0;       // identifies the descriptor in the set
        uint64_t dynamicBufferByteOffset = 0;    // the dynamic offset value applied
    };
    std::vector<DynamicOffset> dynamicOffsets;
};

// --- Pipeline: fixed-function state ---

struct ScissorRect {
    int32_t x = 0, y = 0, width = 0, height = 0;
    bool enabled = true;
};

struct StencilOpInfo {
    std::string func;        // CompareFunction name
    std::string failOp, depthFailOp, passOp;  // StencilOperation names
    uint32_t reference = 0;
    uint32_t compareMask = 0;
    uint32_t writeMask = 0;
};

struct DepthStencilInfo {
    bool depthTestEnable = false;
    bool depthWriteEnable = false;
    std::string depthFunc;   // CompareFunction name
    bool depthBoundsEnable = false;
    float minDepthBounds = 0.0f;
    float maxDepthBounds = 0.0f;
    bool stencilTestEnable = false;
    StencilOpInfo frontFace;
    StencilOpInfo backFace;
};

struct BlendEquationInfo {
    std::string source;        // BlendMultiplier name
    std::string destination;   // BlendMultiplier name
    std::string operation;     // BlendOperation name
};

struct BlendTargetInfo {
    bool blendEnable = false;
    BlendEquationInfo colorBlend;
    BlendEquationInfo alphaBlend;
    uint32_t writeMask = 0;
};

struct BlendStateInfo {
    bool independentBlend = false;
    std::vector<BlendTargetInfo> targets;
    float blendFactor[4] = {1.0f, 1.0f, 1.0f, 1.0f};
};

struct RasterizerInfo {
    std::string fillMode;    // Solid / Wireframe / Point
    std::string cullMode;    // NoCull / Front / Back / FrontAndBack
    bool frontCCW = false;
    bool rasterizerDiscard = false;
    bool depthClipEnable = true;
    bool scissorEnable = false;
    bool depthBiasEnable = false;
    float depthBias = 0.0f;
    float slopeScaledDepthBias = 0.0f;
    float depthBiasClamp = 0.0f;
};

struct MultisampleInfo {
    uint32_t rasterSamples = 0;
    uint32_t sampleMask = 0xFFFFFFFF;
    bool sampleShadingEnable = false;
    float minSampleShading = 0.0f;
    bool alphaToCoverage = false;
};

// Current layout/state of a live resource (Vulkan image layouts or
// D3D12 resource states). Opt-in via includeResourceStates.
struct ResourceLayoutInfo {
    ResourceId resourceId = 0;
    std::string name;
    struct LayoutRange {
        uint32_t baseMip = 0, baseLayer = 0, numMip = 0, numLayer = 0;
        std::string state;
    };
    std::vector<LayoutRange> layouts;
};

struct PipelineState {
    GraphicsApi api = GraphicsApi::Unknown;
    struct ShaderBinding {
        ShaderStage stage = ShaderStage::Vertex;
        ResourceId shaderId = 0;
        std::string entryPoint;
    };
    std::vector<ShaderBinding> shaders;
    std::vector<RenderTargetInfo> renderTargets;
    std::optional<RenderTargetInfo> depthTarget;
    std::vector<Viewport> viewports;
    std::optional<InputAssemblyInfo> inputAssembly;
    std::optional<VertexInputInfo> vertexInput;
    // Vulkan only: bound descriptor sets with raw dynamic offsets
    std::vector<DescriptorSetInfo> descriptorSets;
    std::vector<ScissorRect> scissors;
    std::optional<RasterizerInfo> rasterizer;
    std::optional<DepthStencilInfo> depthStencil;
    std::optional<BlendStateInfo> blend;
    std::optional<MultisampleInfo> multisample;
    // Vulkan: total pushed-constant bytes at this event
    uint32_t pushConstantByteSize = 0;
    // Vulkan/D3D12 only, opt-in: current layout/state of live resources
    std::vector<ResourceLayoutInfo> resourceStates;
};

// --- Raw buffer data ---

struct BufferDataResult {
    ResourceId resourceId = 0;
    uint64_t bufferLength = 0;   // total buffer size in bytes (0 = unknown)
    uint64_t byteOffset = 0;
    uint64_t byteSize = 0;       // bytes actually returned
    std::vector<uint8_t> bytes;
};

// --- Resources ---
struct ResourceInfo {
    ResourceId id = 0;
    std::string name;
    std::string type;
    uint64_t byteSize = 0;
    std::optional<uint32_t> width;
    std::optional<uint32_t> height;
    std::optional<uint32_t> depth;
    std::optional<uint32_t> mips;
    std::optional<uint32_t> arraySize;
    std::optional<std::string> format;
    std::optional<std::string> dimension;
    std::optional<bool> cubemap;
    std::optional<uint32_t> msSamp;
    struct FormatDetails {
        std::string name;
        uint32_t compCount = 0;
        uint32_t compByteWidth = 0;
        uint32_t compType = 0;
    };
    std::optional<FormatDetails> formatDetails;
    std::optional<uint64_t> gpuAddress;
};

// --- Passes ---
struct PassInfo {
    std::string name;
    uint32_t eventId = 0;
    uint32_t drawCount = 0;
    uint32_t dispatchCount = 0;
    std::vector<EventInfo> draws;
};

// --- Info/Stats ---
struct DebugMessage {
    uint32_t eventId = 0;
    std::string severity;
    std::string category;
    std::string message;
};

struct PerPassStats {
    std::string name;
    uint32_t drawCount = 0;
    uint32_t dispatchCount = 0;
    uint64_t totalTriangles = 0;
};

struct TopDraw {
    uint32_t eventId = 0;
    std::string name;
    uint32_t numIndices = 0;
};

struct LargestResource {
    std::string name;
    uint64_t byteSize = 0;
    std::string type;
    uint32_t width = 0;
    uint32_t height = 0;
};

struct CaptureStats {
    std::vector<PerPassStats> perPass;
    std::vector<TopDraw> topDraws;
    std::vector<LargestResource> largestResources;
};

// --- Shaders ---
struct SignatureElement {
    std::string varName;
    std::string semanticName;
    uint32_t semanticIndex = 0;
    uint32_t regIndex = 0;
};

struct ConstantBlock {
    std::string name;
    uint32_t bindPoint = 0;
    uint32_t byteSize = 0;
    uint32_t variableCount = 0;
};

struct ShaderReflection {
    ResourceId id = 0;
    ShaderStage stage = ShaderStage::Vertex;
    std::string entryPoint;
    std::vector<SignatureElement> inputSignature;
    std::vector<SignatureElement> outputSignature;
    std::vector<ConstantBlock> constantBlocks;
    std::vector<ShaderBindingDetail> readOnlyResources;
    std::vector<ShaderBindingDetail> readWriteResources;
};

struct ShaderDisassembly {
    ResourceId id = 0;
    ShaderStage stage = ShaderStage::Vertex;
    std::string disassembly;
    std::string target;
};

struct ShaderUsageInfo {
    ResourceId shaderId = 0;
    ShaderStage stage = ShaderStage::Vertex;
    std::string entryPoint;
    uint32_t usageCount = 0;
};

struct ShaderSearchMatch {
    ResourceId shaderId = 0;
    ShaderStage stage = ShaderStage::Vertex;
    std::string entryPoint;
    struct MatchLine {
        uint32_t line = 0;
        std::string text;
    };
    std::vector<MatchLine> matchingLines;
};

// --- Export ---
struct ExportResult {
    std::string outputPath;
    uint64_t byteSize = 0;
    uint32_t eventId = 0;
    int rtIndex = -1;
    uint32_t width = 0;
    uint32_t height = 0;
    ResourceId resourceId = 0;
    uint32_t mip = 0;
    uint32_t layer = 0;
    uint64_t offset = 0;
    uint64_t requestedSize = 0;
};

// --- Attach ---

struct AttachRequest {
    std::string remoteServer = "127.0.0.1";
    // Optional matchers. pid==0 means "no pid filter"; empty exeName means
    // "no name filter". If both are unset the first found target is used.
    std::string exeName;
    uint64_t pid = 0;
};

struct AttachTargetInfo {
    uint32_t ident = 0;
    uint32_t pid = 0;
    std::string targetName;   // usually the executable name on the target machine
    std::string api;          // empty until the target registered a graphics API
    bool isApiInited = false;
    std::string busyClient;   // non-empty if another client holds the target
};

struct AttachCandidates {
    std::vector<AttachTargetInfo> targets;
};

struct AttachResult {
    bool attachSuccess = false;
    bool isApiInited = false;
    uint32_t targetIdent = 0;
    uint32_t pid = 0;
    std::string targetName;
    std::string api;
};


// --- Capture ---
struct CaptureRequest {
    std::string exePath;
    std::string workingDir;
    std::string cmdLine;
    uint32_t delayFrames = 100;
    uint32_t cycleWindows = 0;
    std::string outputPath;
};

struct RemoteCaptureRequest {
    std::string remoteAddress = "127.0.0.1";
    uint32_t pid = 0;
    uint32_t ident = 0;
    uint32_t delayFrames = 100;
    uint32_t cycleWindows = 0;
    std::string outputPath;
};


struct CaptureResult {
    std::string capturePath;
    uint32_t pid = 0;
};

// --- Pixel Query ---
struct PixelValue {
    float floatValue[4] = {};
    uint32_t uintValue[4] = {};
    int32_t intValue[4] = {};
};

struct PixelModification {
    uint32_t eventId = 0;
    uint32_t fragmentIndex = 0;
    uint32_t primitiveId = 0;
    PixelValue shaderOut;
    PixelValue postMod;
    std::optional<float> depth;
    bool passed = false;
    std::vector<std::string> flags;
};

struct PixelHistoryResult {
    uint32_t x = 0, y = 0, eventId = 0;
    uint32_t targetIndex = 0;
    ResourceId targetId = 0;
    std::vector<PixelModification> modifications;
};

struct PickPixelResult {
    uint32_t x = 0, y = 0, eventId = 0;
    uint32_t targetIndex = 0;
    ResourceId targetId = 0;
    PixelValue color;
};

// --- Shader Debug ---
struct DebugVariable {
    std::string name;
    std::string type;       // VarType as string: "Float", "UInt", "SInt", "Bool", etc.
    uint32_t rows = 0;
    uint32_t cols = 0;
    uint32_t flags = 0;     // ShaderVariableFlags bitmask
    std::vector<float> floatValues;
    std::vector<uint32_t> uintValues;
    std::vector<int32_t> intValues;
    std::vector<DebugVariable> members;
};

struct DebugVariableChange {
    DebugVariable before;
    DebugVariable after;
};

struct DebugStep {
    uint32_t step = 0;
    uint32_t instruction = 0;
    std::string file;
    int32_t line = -1;
    std::vector<DebugVariableChange> changes;
};

struct ShaderDebugResult {
    uint32_t eventId = 0;
    std::string stage;
    uint32_t totalSteps = 0;
    std::vector<DebugVariable> inputs;
    std::vector<DebugVariable> outputs;
    std::vector<DebugStep> trace;
};

// --- Texture Stats ---
struct TextureStats {
    ResourceId id = 0;
    uint32_t eventId = 0;
    uint32_t mip = 0;
    uint32_t slice = 0;
    PixelValue minVal;
    PixelValue maxVal;
    struct HistogramBucket {
        uint32_t r = 0, g = 0, b = 0, a = 0;
    };
    std::vector<HistogramBucket> histogram;
};

// --- Shader Editing ---
enum class ShaderEncoding {
    Unknown = 0, DXBC = 1, GLSL = 2, SPIRV = 3,
    SPIRVAsm = 4, HLSL = 5, DXIL = 6,
    OpenGLSPIRV = 7, OpenGLSPIRVAsm = 8, Slang = 9
};

struct ShaderBuildResult {
    uint64_t shaderId = 0;   // 0 = failure
    std::string warnings;     // compiler warnings or error message
};

// --- Mesh Export ---
enum class MeshStage { VSOut = 1, GSOut = 2 };
enum class MeshTopology { TriangleList, TriangleStrip, TriangleFan, Other };

struct MeshVertex {
    float x = 0, y = 0, z = 0;
};

struct MeshData {
    uint32_t eventId = 0;
    MeshStage stage = MeshStage::VSOut;
    MeshTopology topology = MeshTopology::Other;
    std::vector<MeshVertex> vertices;
    std::vector<uint32_t> indices;
    std::vector<std::array<uint32_t, 3>> faces;
};

// --- Snapshot ---
struct SnapshotResult {
    std::string manifestPath;
    std::vector<std::string> files;
    std::vector<std::string> errors;
};

// --- Resource Usage ---
struct ResourceUsageEntry {
    uint32_t eventId = 0;
    std::string usage;
};

struct ResourceUsageResult {
    ResourceId resourceId = 0;
    std::vector<ResourceUsageEntry> entries;
};

// --- Assertions ---
// AssertResult uses std::map<string,string> for details to keep core layer
// free of nlohmann::json. The MCP serialization layer converts to JSON.
struct AssertResult {
    bool pass = false;
    std::string message;
    std::map<std::string, std::string> details;  // key-value pairs, all stringified
};

// Pixel assertion carries typed actual/expected values for precise serialization.
struct PixelAssertResult {
    bool pass = false;
    std::string message;
    float actual[4] = {};
    float expected[4] = {};
    float tolerance = 0.01f;
};

struct ImageCompareResult {
    bool pass = false;
    size_t diffPixels = 0;
    size_t totalPixels = 0;
    double diffRatio = 0.0;
    std::string diffOutputPath;
    std::string message;
};

// --- Phase 4: Pass Analysis ---

struct PassRange {
    std::string name;
    uint32_t beginEventId = 0;      // marker or first event in synthetic range
    uint32_t endEventId = 0;        // last event in range (inclusive)
    uint32_t firstDrawEventId = 0;  // first actual draw/dispatch inside the range
    bool synthetic = false;
};

struct AttachmentInfo {
    ResourceId resourceId = 0;
    std::string name;
    std::string format;
    uint32_t width = 0;
    uint32_t height = 0;
};

struct PassAttachments {
    std::string passName;
    uint32_t eventId = 0;
    std::vector<AttachmentInfo> colorTargets;
    AttachmentInfo depthTarget;
    bool hasDepth = false;
    bool synthetic = false;
};

struct PassStatistics {
    std::string name;
    uint32_t eventId = 0;
    uint32_t drawCount = 0;
    uint32_t dispatchCount = 0;
    uint64_t totalTriangles = 0;
    uint32_t rtWidth = 0;
    uint32_t rtHeight = 0;
    uint32_t attachmentCount = 0;
    bool synthetic = false;
};

struct PassEdge {
    std::string srcPass;
    std::string dstPass;
    std::vector<ResourceId> sharedResources;
};

struct PassDependencyGraph {
    std::vector<PassEdge> edges;
    uint32_t passCount = 0;
    uint32_t edgeCount = 0;
};

struct UnusedTarget {
    ResourceId resourceId = 0;
    std::string name;
    std::vector<std::string> writtenBy;
    uint32_t wave = 0;
};

struct UnusedTargetResult {
    std::vector<UnusedTarget> unused;
    uint32_t unusedCount = 0;
    uint32_t totalTargets = 0;
};

// --- GPU Performance Counters ---

struct CounterInfo {
    uint32_t id = 0;           // GPUCounter enum value
    std::string name;
    std::string category;
    std::string description;
    std::string resultType;    // "Float", "UInt", "Double", etc.
    uint32_t resultByteWidth = 0;
    std::string unit;          // "Seconds", "Bytes", "Percentage", etc.
};

struct CounterSample {
    uint32_t eventId = 0;
    uint32_t counterId = 0;
    std::string counterName;
    double value = 0.0;        // all types unified to double
    std::string unit;
};

struct CounterFetchResult {
    std::vector<CounterSample> rows;
    uint32_t totalCounters = 0;
    uint32_t totalEvents = 0;
};

// --- CBuffer Contents ---

struct ShaderVar {
    std::string name;
    std::string typeName;      // "float", "float4x4", "int", "struct", etc.
    uint8_t rows = 0;
    uint8_t columns = 0;
    std::vector<double> floatValues;
    std::vector<int64_t> intValues;
    std::vector<uint64_t> uintValues;
    std::vector<ShaderVar> members;
};

struct CBufferInfo {
    uint32_t index = 0;
    std::string name;
    uint32_t bindSet = 0;      // Vulkan set / DX space
    uint32_t bindSlot = 0;     // binding / register
    uint32_t byteSize = 0;
    bool bufferBacked = true;
    uint32_t variableCount = 0;
};

struct CBufferContents {
    uint32_t eventId = 0;
    ShaderStage stage = ShaderStage::Vertex;
    uint32_t bindSet = 0;
    uint32_t bindSlot = 0;
    std::string blockName;
    uint32_t byteSize = 0;
    std::vector<ShaderVar> variables;
};

} // namespace renderdoc::core
