#ifndef CORE_LIBS_PRX_LIBSCEAGCDRIVER_GRAPHICS_INCLUDE_PIPELINEDESCRIPTION_HPP
#define CORE_LIBS_PRX_LIBSCEAGCDRIVER_GRAPHICS_INCLUDE_PIPELINEDESCRIPTION_HPP

#include "prx/libSceAgcDriver/Graphics/include/Pipeline.hpp"
#include "prx/libSceAgcDriver/Graphics/include/State.hpp"
#include "prx/libSceAgcDriver/Graphics/include/VertexInput.hpp"
#include <cstddef>
#include <optional>
#include <span>
#include <vector>

namespace AgcDriver::Graphics {

struct PipelineDescription {
    State state{};
    VertexInputLayout vertexInput;
    VkImageLayout attachmentLayout = VK_IMAGE_LAYOUT_UNDEFINED;
};

std::vector<std::byte> EncodePipelineDescription(const State& state, const VertexInputLayout& vertexInput, VkImageLayout attachmentLayout);
std::optional<PipelineDescription> DecodePipelineDescription(std::span<const std::byte> bytes);
bool SamePipelineKey(const Context& context, const PipelineSetLayouts& resources, std::span<const CompiledShader> shaders, const PipelineDescription& left, const PipelineDescription& right);

class IGraphicsPipelineRecorder {
public:
    virtual ~IGraphicsPipelineRecorder() = default;
    virtual void Created(const Context& context, const State& state, const VertexInputLayout& vertexInput, const PipelineSetLayouts& resources, std::span<const CompiledShader> shaders, VkImageLayout attachmentLayout) = 0;
};

}

#endif
