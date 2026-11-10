////////////////////////////////////////////////////////////
//
// SFML - Simple and Fast Multimedia Library
// Copyright (C) 2007-2026 Laurent Gomila (laurent@sfml-dev.org)
//
// This software is provided 'as-is', without any express or implied warranty.
// In no event will the authors be held liable for any damages arising from the use of this software.
//
// Permission is granted to anyone to use this software for any purpose,
// including commercial applications, and to alter it and redistribute it freely,
// subject to the following restrictions:
//
// 1. The origin of this software must not be misrepresented;
//    you must not claim that you wrote the original software.
//    If you use this software in a product, an acknowledgment
//    in the product documentation would be appreciated but is not required.
//
// 2. Altered source versions must be plainly marked as such,
//    and must not be misrepresented as being the original software.
//
// 3. This notice may not be removed or altered from any source distribution.
//
////////////////////////////////////////////////////////////

#include <SFML/Graphics/Texture.hpp>
#include <SFML/Graphics/Vertex.hpp>
#include <SFML/Graphics/Vulkan/BuiltInShaders.hpp>
#include <SFML/Graphics/Vulkan/VulkanProgram.hpp>

#include <SFML/System/Err.hpp>

#include <algorithm>
#include <ostream>
#include <spirv_reflect.h>
#include <tuple>
#include <utility>

#include <cstring>

namespace sf::priv
{
namespace
{
namespace VulkanProgramImpl
{
struct Member
{
    std::string   name;
    std::uint32_t offset;
    std::uint32_t components;
    std::uint32_t columns;
    std::uint32_t matrixStride;
    std::uint32_t arrayLength;
    std::uint32_t arrayStride;
    bool          floating;
    bool          rowMajor;
};

struct Binding
{
    VkDescriptorSetLayoutBinding layout{};
    std::string                  name;
    std::vector<Member>          members;
    std::vector<std::byte>       data;
    const Texture*               texture{};
    bool                         currentTexture{};
};

bool stencilFormat(VkFormat format)
{
    return format == VK_FORMAT_S8_UINT || format == VK_FORMAT_D16_UNORM_S8_UINT ||
           format == VK_FORMAT_D24_UNORM_S8_UINT || format == VK_FORMAT_D32_SFLOAT_S8_UINT;
}

std::uint32_t maxSpirvVersion(std::uint32_t apiVersion)
{
    if (apiVersion >= VK_MAKE_VERSION(1, 3, 0))
        return 0x00010600;
    if (apiVersion >= VK_MAKE_VERSION(1, 2, 0))
        return 0x00010500;
    if (apiVersion >= VK_MAKE_VERSION(1, 1, 0))
        return 0x00010300;
    return 0x00010000;
}

template <std::size_t N>
std::string_view bytes(const std::array<std::uint32_t, N>& words)
{
    return {reinterpret_cast<const char*>(words.data()), sizeof(words)};
}

void reflectMembers(std::vector<Member>& members, const SpvReflectBlockVariable& block, const std::string& prefix)
{
    for (std::uint32_t i = 0; i < block.member_count; ++i)
    {
        const auto& value = block.members[i];
        if (!value.name)
            continue;
        const auto name = prefix + value.name;
        if (value.member_count && !value.array.dims_count)
        {
            reflectMembers(members, value, name + ".");
            continue;
        }

        const auto  flags   = value.type_description ? value.type_description->type_flags : 0;
        const auto& numeric = value.numeric;
        if (!(flags & (SPV_REFLECT_TYPE_FLAG_FLOAT | SPV_REFLECT_TYPE_FLAG_INT | SPV_REFLECT_TYPE_FLAG_BOOL)) ||
            (numeric.scalar.width != 32 && !(flags & SPV_REFLECT_TYPE_FLAG_BOOL)) || value.array.dims_count > 1)
            continue;

        Member member{name,
                      value.absolute_offset,
                      std::max({1u, numeric.vector.component_count, numeric.matrix.row_count}),
                      std::max(1u, numeric.matrix.column_count),
                      numeric.matrix.stride,
                      value.array.dims_count ? value.array.dims[0] : 1u,
                      value.array.stride,
                      (flags & SPV_REFLECT_TYPE_FLAG_FLOAT) != 0,
                      (value.decoration_flags & SPV_REFLECT_DECORATION_ROW_MAJOR) != 0};
        if (member.arrayLength)
            members.push_back(std::move(member));
    }
}

bool reflect(const VulkanContext&          context,
             const SpvReflectShaderModule& module,
             VkShaderStageFlagBits         stage,
             std::vector<Binding>&         bindings)
{
    const auto* entry = spvReflectGetEntryPoint(&module, "main");
    if (!entry || static_cast<VkShaderStageFlagBits>(entry->shader_stage) != stage)
        return false;

    std::uint32_t pushConstants{};
    if (spvReflectEnumerateEntryPointPushConstantBlocks(&module, "main", &pushConstants, nullptr) !=
            SPV_REFLECT_RESULT_SUCCESS ||
        pushConstants)
        return false;

    std::uint32_t count{};
    if (spvReflectEnumerateEntryPointDescriptorBindings(&module, "main", &count, nullptr) != SPV_REFLECT_RESULT_SUCCESS)
        return false;
    std::vector<SpvReflectDescriptorBinding*> descriptors(count);
    if (count && spvReflectEnumerateEntryPointDescriptorBindings(&module, "main", &count, descriptors.data()) !=
                     SPV_REFLECT_RESULT_SUCCESS)
        return false;

    for (const auto* descriptor : descriptors)
    {
        const bool uniform = descriptor->descriptor_type == SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        const bool sampler = descriptor->descriptor_type == SPV_REFLECT_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER &&
                             descriptor->image.dim == SpvDim2D && !descriptor->image.arrayed && !descriptor->image.ms;
        if (descriptor->count != 1 || (!uniform && !sampler) || descriptor->set > 1)
            return false;
        if (uniform && (!descriptor->block.padded_size ||
                        descriptor->block.padded_size > context.properties.limits.maxUniformBufferRange))
            return false;

        if (!descriptor->set)
        {
            if ((descriptor->binding == 0 && uniform && descriptor->block.padded_size <= sizeof(VulkanDrawParameters)) ||
                (descriptor->binding == 1 && sampler))
                continue;
            return false;
        }

        const auto type = uniform ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER : VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        auto       it   = std::find_if(bindings.begin(),
                               bindings.end(),
                               [&](const auto& binding) { return binding.layout.binding == descriptor->binding; });
        if (it == bindings.end())
        {
            Binding binding;
            binding.layout = {descriptor->binding, type, 1, static_cast<VkShaderStageFlags>(stage), nullptr};
            binding.name   = descriptor->name ? descriptor->name : "";
            bindings.push_back(std::move(binding));
            it = std::prev(bindings.end());
        }
        else if (it->layout.descriptorType != type)
        {
            return false;
        }

        it->layout.stageFlags |= static_cast<VkShaderStageFlags>(stage);
        if (uniform)
        {
            it->data.resize(std::max(it->data.size(), static_cast<std::size_t>(descriptor->block.padded_size)));
            reflectMembers(it->members, descriptor->block, {});
        }
    }

    return true;
}

VkShaderModule makeModule(const VulkanContext&  context,
                          std::string_view      source,
                          VkShaderStageFlagBits stage,
                          std::vector<Binding>& bindings)
{
    if (source.size() < 5 * sizeof(std::uint32_t) || source.size() % sizeof(std::uint32_t))
        return VK_NULL_HANDLE;

    std::vector<std::uint32_t> words(source.size() / sizeof(std::uint32_t));
    std::memcpy(words.data(), source.data(), source.size());
    if (words[0] != SpvMagicNumber || words[1] < 0x00010000 || words[1] > maxSpirvVersion(context.apiVersion))
        return VK_NULL_HANDLE;

    struct Reflection
    {
        SpvReflectShaderModule module{};
        ~Reflection()
        {
            spvReflectDestroyShaderModule(&module);
        }
    } reflection;
    if (spvReflectCreateShaderModule(source.size(), words.data(), &reflection.module) != SPV_REFLECT_RESULT_SUCCESS ||
        !reflect(context, reflection.module, stage, bindings))
        return VK_NULL_HANDLE;

    VkShaderModuleCreateInfo info{};
    info.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    info.codeSize = source.size();
    info.pCode    = words.data();
    VkShaderModule shader{};
    if (context.vk.CreateShaderModule(context.device, &info, nullptr, &shader) != VK_SUCCESS)
        return VK_NULL_HANDLE;

    return shader;
}

bool operator==(const VulkanPipelineState& lhs, const VulkanPipelineState& rhs)
{
    return std::tie(lhs.colorFormat,
                    lhs.depthFormat,
                    lhs.samples,
                    lhs.primitive,
                    lhs.blend,
                    lhs.comparison,
                    lhs.stencilOp,
                    lhs.stencilOnly,
                    lhs.present) ==
           std::tie(rhs.colorFormat,
                    rhs.depthFormat,
                    rhs.samples,
                    rhs.primitive,
                    rhs.blend,
                    rhs.comparison,
                    rhs.stencilOp,
                    rhs.stencilOnly,
                    rhs.present);
}

VkDescriptorSetLayout createLayout(const VulkanContext& context, const VkDescriptorSetLayoutBinding* bindings, std::uint32_t count)
{
    VkDescriptorSetLayoutCreateInfo info{};
    info.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    info.bindingCount = count;
    info.pBindings    = bindings;
    VkDescriptorSetLayout layout{};
    if (context.vk.CreateDescriptorSetLayout(context.device, &info, nullptr, &layout) != VK_SUCCESS)
        return VK_NULL_HANDLE;
    return layout;
}
} // namespace VulkanProgramImpl
} // namespace

struct VulkanProgram::Impl
{
    explicit Impl(std::shared_ptr<VulkanContext> value) : context(std::move(value))
    {
    }

    std::shared_ptr<VulkanContext>                          context;
    std::vector<VkPipelineShaderStageCreateInfo>            stages;
    std::vector<VulkanProgramImpl::Binding>                 bindings;
    std::array<VkDescriptorSetLayout, 2>                    sets{};
    VkPipelineLayout                                        layout{};
    std::vector<std::pair<VulkanPipelineState, VkPipeline>> pipelines;
    std::mutex                                              uniformsMutex;
};

VulkanProgram::VulkanProgram(std::shared_ptr<VulkanContext> context) :
    m_impl(std::make_unique<Impl>(std::move(context)))
{
}

VulkanProgram::~VulkanProgram()
{
    const auto& ctx = *m_impl->context;
    for (const auto& pipeline : m_impl->pipelines)
        ctx.vk.DestroyPipeline(ctx.device, pipeline.second, nullptr);
    if (m_impl->layout)
        ctx.vk.DestroyPipelineLayout(ctx.device, m_impl->layout, nullptr);
    for (const VkDescriptorSetLayout layout : m_impl->sets)
        if (layout)
            ctx.vk.DestroyDescriptorSetLayout(ctx.device, layout, nullptr);
    for (const auto& stage : m_impl->stages)
        ctx.vk.DestroyShaderModule(ctx.device, stage.module, nullptr);
}

std::shared_ptr<VulkanProgram> VulkanProgram::create(VulkanDevice&    device,
                                                     std::string_view vertex,
                                                     std::string_view geometry,
                                                     std::string_view fragment)
{
    using namespace VulkanProgramImpl;
    const auto  lock = device.lock();
    const auto& ctx  = *device.context();
    if (!geometry.empty() && !ctx.features.geometryShader)
        return {};
    if (vertex.empty())
        vertex = bytes(VulkanShaders::vertex);
    if (fragment.empty())
        fragment = bytes(VulkanShaders::fragment);

    auto  result = std::shared_ptr<VulkanProgram>(new VulkanProgram(device.context()));
    auto& impl   = *result->m_impl;
    for (const auto& [stage, source] : {std::pair{VK_SHADER_STAGE_VERTEX_BIT, vertex},
                                        std::pair{VK_SHADER_STAGE_GEOMETRY_BIT, geometry},
                                        std::pair{VK_SHADER_STAGE_FRAGMENT_BIT, fragment}})
    {
        if (source.empty())
            continue;
        const VkShaderModule module = makeModule(ctx, source, stage, impl.bindings);
        if (!module)
        {
            err() << "Could not load Vulkan SPIR-V for the requested API version, stage, and SFML shader layout"
                  << std::endl;
            return {};
        }
        VkPipelineShaderStageCreateInfo stageInfo{};
        stageInfo.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stageInfo.stage  = stage;
        stageInfo.module = module;
        stageInfo.pName  = "main";
        impl.stages.push_back(stageInfo);
    }

    const VkShaderStageFlags flags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT |
                                     (geometry.empty() ? 0u : static_cast<VkShaderStageFlags>(VK_SHADER_STAGE_GEOMETRY_BIT));
    const std::array common{VkDescriptorSetLayoutBinding{0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, flags, nullptr},
                            VkDescriptorSetLayoutBinding{1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, flags, nullptr}};
    impl.sets[0] = createLayout(ctx, common.data(), static_cast<std::uint32_t>(common.size()));
    if (!impl.sets[0])
        return {};

    if (!impl.bindings.empty())
    {
        std::vector<VkDescriptorSetLayoutBinding> layout;
        layout.reserve(impl.bindings.size());
        for (const auto& binding : impl.bindings)
            layout.push_back(binding.layout);
        impl.sets[1] = createLayout(ctx, layout.data(), static_cast<std::uint32_t>(layout.size()));
        if (!impl.sets[1])
            return {};
    }

    VkPipelineLayoutCreateInfo pipeline{};
    pipeline.sType          = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipeline.setLayoutCount = impl.sets[1] ? 2u : 1u;
    pipeline.pSetLayouts    = impl.sets.data();
    if (ctx.vk.CreatePipelineLayout(ctx.device, &pipeline, nullptr, &impl.layout) != VK_SUCCESS)
        return {};
    return result;
}

void VulkanProgram::setUniform(const std::string& name, ShaderUniformType type, const void* values, std::size_t count)
{
    const auto  index      = static_cast<unsigned int>(type);
    const bool  matrix     = type == ShaderUniformType::Mat3 || type == ShaderUniformType::Mat4;
    const auto  columns    = matrix ? (type == ShaderUniformType::Mat3 ? 3u : 4u) : 1u;
    const auto  components = matrix ? columns : (index % 4) + 1;
    const bool  floating   = index < 4 || matrix;
    const auto* data       = static_cast<const std::byte*>(values);

    const std::lock_guard lock(m_impl->uniformsMutex);
    bool                  found = false;
    for (auto& binding : m_impl->bindings)
    {
        for (const auto& member : binding.members)
        {
            if (member.name != name && (binding.name.empty() || binding.name + "." + member.name != name))
                continue;
            if (member.components != components || member.columns != columns || member.floating != floating ||
                count > member.arrayLength || (count > 1 && !member.arrayStride) || (matrix && !member.matrixStride))
                continue;

            const std::size_t end = member.offset + (count - 1) * member.arrayStride +
                                    (columns - 1) * member.matrixStride + components * sizeof(std::uint32_t);
            if (end > binding.data.size())
                continue;

            for (std::size_t element = 0; element < count; ++element)
            {
                auto* destination = binding.data.data() + member.offset + element * member.arrayStride;
                for (std::size_t column = 0; column < columns; ++column)
                {
                    for (std::size_t row = 0; row < components; ++row)
                    {
                        const auto sourceIndex = element * columns * components + column * components + row;
                        const auto offset = member.rowMajor ? row * member.matrixStride + column * sizeof(std::uint32_t)
                                                            : column * member.matrixStride + row * sizeof(std::uint32_t);
                        std::memcpy(destination + offset, data + sourceIndex * sizeof(std::uint32_t), sizeof(std::uint32_t));
                    }
                }
            }
            found = true;
        }
    }

    if (!found)
        err() << "Vulkan uniform not found or has incompatible type: " << name << std::endl;
}

void VulkanProgram::setSampler(const std::string& name, const Texture* texture, bool current)
{
    const std::lock_guard lock(m_impl->uniformsMutex);
    bool                  found = false;
    for (auto& binding : m_impl->bindings)
    {
        if (binding.layout.descriptorType == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER && binding.name == name)
        {
            binding.texture        = texture;
            binding.currentTexture = current;
            found                  = true;
        }
    }

    if (!found && !(current && name == "sf_Texture"))
        err() << "Vulkan sampler not found: " << name << std::endl;
}

VkPipeline VulkanProgram::getPipeline(const VulkanPipelineState& state, VkRenderPass pass)
{
    using namespace VulkanProgramImpl;
    for (const auto& pipeline : m_impl->pipelines)
        if (pipeline.first == state)
            return pipeline.second;

    constexpr std::array topologies{VK_PRIMITIVE_TOPOLOGY_POINT_LIST,
                                    VK_PRIMITIVE_TOPOLOGY_LINE_LIST,
                                    VK_PRIMITIVE_TOPOLOGY_LINE_STRIP,
                                    VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
                                    VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP,
                                    VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN};
    constexpr std::array factors{VK_BLEND_FACTOR_ZERO,
                                 VK_BLEND_FACTOR_ONE,
                                 VK_BLEND_FACTOR_SRC_COLOR,
                                 VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR,
                                 VK_BLEND_FACTOR_DST_COLOR,
                                 VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR,
                                 VK_BLEND_FACTOR_SRC_ALPHA,
                                 VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
                                 VK_BLEND_FACTOR_DST_ALPHA,
                                 VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA};
    constexpr std::array
        equations{VK_BLEND_OP_ADD, VK_BLEND_OP_SUBTRACT, VK_BLEND_OP_REVERSE_SUBTRACT, VK_BLEND_OP_MIN, VK_BLEND_OP_MAX};
    constexpr std::array comparisons{VK_COMPARE_OP_NEVER,
                                     VK_COMPARE_OP_LESS,
                                     VK_COMPARE_OP_LESS_OR_EQUAL,
                                     VK_COMPARE_OP_GREATER,
                                     VK_COMPARE_OP_GREATER_OR_EQUAL,
                                     VK_COMPARE_OP_EQUAL,
                                     VK_COMPARE_OP_NOT_EQUAL,
                                     VK_COMPARE_OP_ALWAYS};
    constexpr std::array operations{VK_STENCIL_OP_KEEP,
                                    VK_STENCIL_OP_ZERO,
                                    VK_STENCIL_OP_REPLACE,
                                    VK_STENCIL_OP_INCREMENT_AND_CLAMP,
                                    VK_STENCIL_OP_DECREMENT_AND_CLAMP,
                                    VK_STENCIL_OP_INVERT};

    constexpr VkVertexInputBindingDescription binding{0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX};
    constexpr std::array
        attributes{VkVertexInputAttributeDescription{0, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, position)},
                   VkVertexInputAttributeDescription{1, 0, VK_FORMAT_R8G8B8A8_UNORM, offsetof(Vertex, color)},
                   VkVertexInputAttributeDescription{2, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, texCoords)}};
    VkPipelineVertexInputStateCreateInfo vertex{};
    vertex.sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertex.vertexBindingDescriptionCount   = 1;
    vertex.pVertexBindingDescriptions      = &binding;
    vertex.vertexAttributeDescriptionCount = static_cast<std::uint32_t>(attributes.size());
    vertex.pVertexAttributeDescriptions    = attributes.data();

    VkPipelineInputAssemblyStateCreateInfo assembly{};
    assembly.sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    assembly.topology = topologies[static_cast<std::size_t>(state.primitive)];

    VkPipelineViewportStateCreateInfo viewport{};
    viewport.sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport.viewportCount = 1;
    viewport.scissorCount  = 1;

    VkPipelineRasterizationStateCreateInfo raster{};
    raster.sType       = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode    = VK_CULL_MODE_NONE;
    raster.frontFace   = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    raster.lineWidth   = 1.f;

    VkPipelineMultisampleStateCreateInfo multisample{};
    multisample.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.rasterizationSamples = state.samples;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType             = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.stencilTestEnable = stencilFormat(state.depthFormat) &&
                                     (state.comparison != StencilMode{}.stencilComparison ||
                                      state.stencilOp != StencilUpdateOperation::Keep);
    depthStencil.front.failOp      = VK_STENCIL_OP_KEEP;
    depthStencil.front.passOp      = operations[static_cast<std::size_t>(state.stencilOp)];
    depthStencil.front.depthFailOp = depthStencil.front.passOp;
    depthStencil.front.compareOp   = comparisons[static_cast<std::size_t>(state.comparison)];
    depthStencil.back              = depthStencil.front;

    VkPipelineColorBlendAttachmentState blendAttachment{};
    blendAttachment.blendEnable         = state.blend != BlendNone;
    blendAttachment.srcColorBlendFactor = factors[static_cast<std::size_t>(state.blend.colorSrcFactor)];
    blendAttachment.dstColorBlendFactor = factors[static_cast<std::size_t>(state.blend.colorDstFactor)];
    blendAttachment.colorBlendOp        = equations[static_cast<std::size_t>(state.blend.colorEquation)];
    blendAttachment.srcAlphaBlendFactor = factors[static_cast<std::size_t>(state.blend.alphaSrcFactor)];
    blendAttachment.dstAlphaBlendFactor = factors[static_cast<std::size_t>(state.blend.alphaDstFactor)];
    blendAttachment.alphaBlendOp        = equations[static_cast<std::size_t>(state.blend.alphaEquation)];
    blendAttachment.colorWriteMask      = state.stencilOnly ? 0
                                                            : VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                                             VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo blend{};
    blend.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    blend.attachmentCount = 1;
    blend.pAttachments    = &blendAttachment;

    constexpr std::array             dynamicStates{VK_DYNAMIC_STATE_VIEWPORT,
                                       VK_DYNAMIC_STATE_SCISSOR,
                                       VK_DYNAMIC_STATE_STENCIL_COMPARE_MASK,
                                       VK_DYNAMIC_STATE_STENCIL_WRITE_MASK,
                                       VK_DYNAMIC_STATE_STENCIL_REFERENCE};
    VkPipelineDynamicStateCreateInfo dynamic{};
    dynamic.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic.dynamicStateCount = static_cast<std::uint32_t>(dynamicStates.size());
    dynamic.pDynamicStates    = dynamicStates.data();

    VkGraphicsPipelineCreateInfo pipeline{};
    pipeline.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipeline.stageCount          = static_cast<std::uint32_t>(m_impl->stages.size());
    pipeline.pStages             = m_impl->stages.data();
    pipeline.pVertexInputState   = &vertex;
    pipeline.pInputAssemblyState = &assembly;
    pipeline.pViewportState      = &viewport;
    pipeline.pRasterizationState = &raster;
    pipeline.pMultisampleState   = &multisample;
    pipeline.pDepthStencilState  = &depthStencil;
    pipeline.pColorBlendState    = &blend;
    pipeline.pDynamicState       = &dynamic;
    pipeline.layout              = m_impl->layout;
    pipeline.renderPass          = pass;

    const auto& ctx = *m_impl->context;
    VkPipeline  result{};
    if (ctx.vk.CreateGraphicsPipelines(ctx.device, VK_NULL_HANDLE, 1, &pipeline, nullptr, &result) != VK_SUCCESS)
    {
        err() << "Failed to create Vulkan graphics pipeline" << std::endl;
        return VK_NULL_HANDLE;
    }

    m_impl->pipelines.emplace_back(state, result);
    return result;
}

VulkanSample VulkanProgram::sample(const Texture* texture)
{
    return texture ? VulkanSample{texture->m_vulkanImage, texture->m_isSmooth, texture->m_isRepeated, texture->m_hasMipmap}
                   : VulkanSample{};
}

VulkanProgram::Descriptors VulkanProgram::prepareDescriptors(
    VulkanDevice&               device,
    const VulkanDrawParameters& parameters,
    const VulkanSample&         texture,
    const VulkanImage*          attachment)
{
    const std::lock_guard lock(m_impl->uniformsMutex);
    Descriptors           result;
    result.count   = m_impl->sets[1] ? 2u : 1u;
    result.sets[0] = device.allocateDescriptorSet(m_impl->sets[0], 1, 1);
    if (!result.sets[0])
        return {};
    if (result.count == 2)
    {
        const auto buffers = static_cast<std::uint32_t>(
            std::count_if(m_impl->bindings.begin(),
                          m_impl->bindings.end(),
                          [](const auto& binding)
                          { return binding.layout.descriptorType == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER; }));
        const auto images = static_cast<std::uint32_t>(m_impl->bindings.size()) - buffers;
        result.sets[1]    = device.allocateDescriptorSet(m_impl->sets[1], buffers, images);
        if (!result.sets[1])
            return {};
    }

    std::vector<VkDescriptorBufferInfo> buffers;
    std::vector<VkDescriptorImageInfo>  images;
    std::vector<VkWriteDescriptorSet>   writes;
    const auto                          maximumWrites = m_impl->bindings.size() + 2;
    buffers.reserve(maximumWrites);
    images.reserve(maximumWrites);
    writes.reserve(maximumWrites);

    const auto writeBuffer = [&](VkDescriptorSet set, std::uint32_t binding, const void* data, std::size_t size)
    {
        const auto upload = device.stage(data, size);
        if (!upload.buffer)
            return false;

        buffers.push_back({upload.buffer, upload.offset, size});
        VkWriteDescriptorSet write{};
        write.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet          = set;
        write.dstBinding      = binding;
        write.descriptorCount = 1;
        write.descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        write.pBufferInfo     = &buffers.back();
        writes.push_back(write);
        return true;
    };

    const auto writeImage = [&](VkDescriptorSet set, std::uint32_t binding, VulkanSample sample)
    {
        if (!sample.image)
            sample.image = device.whiteTexture();
        if (!sample.image || sample.image.get() == attachment)
            return false;

        const VkSampler sampler = device.sampler(sample.smooth, sample.repeated, sample.mipmapped);
        if (!sampler)
            return false;
        device.transition(*sample.image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        device.retain(sample.image);
        images.push_back({sampler, sample.image->view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL});
        VkWriteDescriptorSet write{};
        write.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet          = set;
        write.dstBinding      = binding;
        write.descriptorCount = 1;
        write.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.pImageInfo      = &images.back();
        writes.push_back(write);
        return true;
    };

    if (!writeBuffer(result.sets[0], 0, &parameters, sizeof(parameters)) || !writeImage(result.sets[0], 1, texture))
        return {};

    for (const auto& binding : m_impl->bindings)
    {
        if (binding.layout.descriptorType == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
        {
            if (!writeBuffer(result.sets[1], binding.layout.binding, binding.data.data(), binding.data.size()))
                return {};
        }
        else if (!writeImage(result.sets[1], binding.layout.binding, binding.currentTexture ? texture : sample(binding.texture)))
        {
            return {};
        }
    }

    const auto& ctx = *m_impl->context;
    ctx.vk.UpdateDescriptorSets(ctx.device, static_cast<std::uint32_t>(writes.size()), writes.data(), 0, nullptr);
    return result;
}

void VulkanProgram::bind(VkCommandBuffer commands, VkPipeline pipeline, const Descriptors& descriptors) const
{
    const auto& vk = m_impl->context->vk;
    vk.CmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    vk.CmdBindDescriptorSets(commands,
                             VK_PIPELINE_BIND_POINT_GRAPHICS,
                             m_impl->layout,
                             0,
                             descriptors.count,
                             descriptors.sets.data(),
                             0,
                             nullptr);
}
} // namespace sf::priv
