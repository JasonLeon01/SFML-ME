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

#include <SFML/Graphics/Metal/BuiltInShaders.hpp>
#include <SFML/Graphics/Metal/MetalObjects.hpp>
#include <SFML/Graphics/Metal/MetalRHI.hpp>
#include <SFML/Graphics/Vertex.hpp>

#include <SFML/System/Err.hpp>

#include <algorithm>
#include <ostream>

#include <cstddef>
#include <cstring>

namespace sf::priv
{
namespace
{
namespace MetalProgramImpl
{
constexpr NSUInteger maximumTextures = 16;

id<MTLLibrary> loadLibrary(id<MTLDevice> device, std::string_view code)
{
    NSError*       error   = nil;
    id<MTLLibrary> library = nil;
    if (code.size() >= 4 && code.substr(0, 4) == "MTLB")
    {
        const dispatch_data_t data = dispatch_data_create(code.data(), code.size(), nullptr, DISPATCH_DATA_DESTRUCTOR_DEFAULT);
        library = [device newLibraryWithData:data error:&error];
        if (!library)
            err() << "Failed to load Metal library: " << describe(error) << std::endl;
        return library;
    }
    NSString* const source = [[NSString alloc]
        initWithBytes:code.data()
               length:code.size()
             encoding:NSUTF8StringEncoding];
    if (!source)
    {
        err() << "Metal shader source is not valid UTF-8" << std::endl;
        return nil;
    }
    library = [device newLibraryWithSource:source options:nil error:&error];
    if (!library)
        err() << "Failed to compile Metal shader:" << '\n' << describe(error) << std::endl;
    return library;
}

id<MTLFunction> selectFunction(id<MTLLibrary> library, MTLFunctionType type, const char* stage)
{
    id<MTLFunction> candidate;
    std::size_t     matches = 0;
    for (NSString* name in library.functionNames) // NOLINT(cppcoreguidelines-init-variables)
    {
        const id<MTLFunction> function = [library newFunctionWithName:name];
        if (function.functionType != type)
            continue;
        if ([name isEqualToString:@"main0"])
            return function;
        candidate = function;
        ++matches;
    }
    if (matches == 1)
        return candidate;
    if (matches)
        err() << "Metal library contains several " << stage << " functions, name the entry point main0" << std::endl;
    else
        err() << "Metal library contains no " << stage << " function" << std::endl;
    return nil;
}

MTLVertexDescriptor* vertexLayout()
{
    auto* const layout                                  = [MTLVertexDescriptor vertexDescriptor];
    layout.attributes[0].format                         = MTLVertexFormatFloat2;
    layout.attributes[0].offset                         = offsetof(Vertex, position);
    layout.attributes[0].bufferIndex                    = metalVertexBufferIndex;
    layout.attributes[1].format                         = MTLVertexFormatUChar4Normalized;
    layout.attributes[1].offset                         = offsetof(Vertex, color);
    layout.attributes[1].bufferIndex                    = metalVertexBufferIndex;
    layout.attributes[2].format                         = MTLVertexFormatFloat2;
    layout.attributes[2].offset                         = offsetof(Vertex, texCoords);
    layout.attributes[2].bufferIndex                    = metalVertexBufferIndex;
    layout.layouts[metalVertexBufferIndex].stride       = sizeof(Vertex);
    layout.layouts[metalVertexBufferIndex].stepFunction = MTLVertexStepFunctionPerVertex;
    return layout;
}

MTLRenderPipelineDescriptor* describePipeline(const MetalProgram::Impl& program, const MetalPipelineKey& key)
{
    auto* const descriptor                  = [MTLRenderPipelineDescriptor new];
    descriptor.vertexFunction               = program.vertex;
    descriptor.fragmentFunction             = program.fragment;
    descriptor.vertexDescriptor             = program.layout;
    descriptor.rasterSampleCount            = key[3];
    descriptor.depthAttachmentPixelFormat   = static_cast<MTLPixelFormat>(key[1]);
    descriptor.stencilAttachmentPixelFormat = static_cast<MTLPixelFormat>(key[2]);
    auto* const color                       = descriptor.colorAttachments[0];
    color.pixelFormat                       = static_cast<MTLPixelFormat>(key[0]);
    color.blendingEnabled                   = YES;
    color.sourceRGBBlendFactor              = static_cast<MTLBlendFactor>(key[4]);
    color.destinationRGBBlendFactor         = static_cast<MTLBlendFactor>(key[5]);
    color.rgbBlendOperation                 = static_cast<MTLBlendOperation>(key[6]);
    color.sourceAlphaBlendFactor            = static_cast<MTLBlendFactor>(key[7]);
    color.destinationAlphaBlendFactor       = static_cast<MTLBlendFactor>(key[8]);
    color.alphaBlendOperation               = static_cast<MTLBlendOperation>(key[9]);
    color.writeMask                         = static_cast<MTLColorWriteMask>(key[10]);
    return descriptor;
}

NSUInteger elementSize(MTLDataType type)
{
    switch (type)
    {
        case MTLDataTypeBool:
            return 1;
        case MTLDataTypeBool2:
            return 2;
        case MTLDataTypeBool3:
        case MTLDataTypeBool4:
            return 4;
        case MTLDataTypeFloat:
        case MTLDataTypeInt:
            return 4;
        case MTLDataTypeFloat2:
        case MTLDataTypeInt2:
            return 8;
        case MTLDataTypeFloat3:
        case MTLDataTypeInt3:
        case MTLDataTypeFloat4:
        case MTLDataTypeInt4:
            return 16;
        case MTLDataTypeFloat3x3:
            return 48;
        case MTLDataTypeFloat4x4:
            return 64;
        default:
            return 0;
    }
}

bool compatible(MTLDataType output, ShaderUniformType input)
{
    switch (input)
    {
        case ShaderUniformType::Float:
            return output == MTLDataTypeFloat;
        case ShaderUniformType::Vec2:
            return output == MTLDataTypeFloat2;
        case ShaderUniformType::Vec3:
            return output == MTLDataTypeFloat3;
        case ShaderUniformType::Vec4:
            return output == MTLDataTypeFloat4;
        case ShaderUniformType::Int:
            return output == MTLDataTypeInt || output == MTLDataTypeBool;
        case ShaderUniformType::IVec2:
            return output == MTLDataTypeInt2 || output == MTLDataTypeBool2;
        case ShaderUniformType::IVec3:
            return output == MTLDataTypeInt3 || output == MTLDataTypeBool3;
        case ShaderUniformType::IVec4:
            return output == MTLDataTypeInt4 || output == MTLDataTypeBool4;
        case ShaderUniformType::Mat3:
            return output == MTLDataTypeFloat3x3;
        case ShaderUniformType::Mat4:
            return output == MTLDataTypeFloat4x4;
    }
    return false;
}

// Columns and rows of the tightly packed values passed by sf::Shader
std::pair<std::size_t, std::size_t> shape(ShaderUniformType type)
{
    switch (type)
    {
        case ShaderUniformType::Float:
        case ShaderUniformType::Int:
            return {1, 1};
        case ShaderUniformType::Vec2:
        case ShaderUniformType::IVec2:
            return {2, 1};
        case ShaderUniformType::Vec3:
        case ShaderUniformType::IVec3:
            return {3, 1};
        case ShaderUniformType::Vec4:
        case ShaderUniformType::IVec4:
            return {4, 1};
        case ShaderUniformType::Mat3:
            return {3, 3};
        case ShaderUniformType::Mat4:
            return {4, 4};
    }
    return {};
}

void addUniform(MetalUniformBuffer& buffer, NSString* name, NSUInteger offset, MTLDataType type, MTLArrayType* array)
{
    MetalUniform uniform;
    uniform.name   = name.UTF8String;
    uniform.offset = offset;
    uniform.type   = type;
    if (array)
    {
        uniform.type     = array.elementType;
        uniform.elements = array.arrayLength;
        uniform.stride   = array.stride;
    }
    if (elementSize(uniform.type))
        buffer.uniforms.push_back(std::move(uniform));
}

bool reflect(NSArray<id<MTLBinding>>* bindings, MetalStage& stage, bool vertex)
{
    for (id<MTLBinding> binding in bindings) // NOLINT(cppcoreguidelines-init-variables)
    {
        if (!binding.used)
            continue;
        if (binding.type == MTLBindingTypeBuffer)
        {
            const auto buffer = static_cast<id<MTLBufferBinding>>(binding);
            if (vertex && binding.index == metalVertexBufferIndex)
                continue;
            if (binding.index == 0)
            {
                if (buffer.bufferDataSize != sizeof(MetalDrawParameters) && buffer.bufferDataSize != metalDrawPrefixSize)
                {
                    err() << "Metal buffer(0) is reserved for SFMLDraw (float4x4 sf_ModelViewProjectionMatrix, "
                             "float4 sf_TextureTransform, optionally followed by float4x4 sf_ModelViewMatrix, "
                             "float4x4 sf_ProjectionMatrix)"
                          << std::endl;
                    return false;
                }
                stage.draw = true;
                continue;
            }
            if (binding.index >= metalVertexBufferIndex)
            {
                err() << "Metal shader buffers must use indices 1 to " << metalVertexBufferIndex - 1 << std::endl;
                return false;
            }
            MetalUniformBuffer uniforms;
            uniforms.index = binding.index;
            uniforms.data.resize(buffer.bufferDataSize);
            if (MTLStructType* const layout = buffer.bufferStructType)
            {
                for (MTLStructMember* member in layout.members) // NOLINT(cppcoreguidelines-init-variables)
                    addUniform(uniforms,
                               member.name,
                               member.offset,
                               member.dataType,
                               member.dataType == MTLDataTypeArray ? member.arrayType : nil);
            }
            else
            {
                addUniform(uniforms, binding.name, 0, buffer.bufferDataType, nil);
            }
            stage.buffers.push_back(std::move(uniforms));
        }
        else if (binding.type == MTLBindingTypeTexture || binding.type == MTLBindingTypeSampler)
        {
            if (binding.index >= maximumTextures)
            {
                err() << "Metal shader textures and samplers must use indices below " << maximumTextures << std::endl;
                return false;
            }
            if (binding.type == MTLBindingTypeSampler)
                stage.samplers.push_back(binding.index);
            else
                // Like GLSL samplers left at unit 0, unassigned textures sample the current texture
                stage.textures.push_back({binding.name.UTF8String, binding.index, nullptr, true});
        }
    }
    return true;
}

void setBytes(id<MTLRenderCommandEncoder> encoder, bool vertex, const void* data, NSUInteger size, NSUInteger index)
{
    if (vertex)
        [encoder setVertexBytes:data length:size atIndex:index];
    else
        [encoder setFragmentBytes:data length:size atIndex:index];
}
} // namespace MetalProgramImpl
} // namespace


////////////////////////////////////////////////////////////
id<MTLRenderPipelineState> MetalProgram::Impl::pipeline(MetalDevice::Impl& device, const MetalPipelineKey& key)
{
    auto& state = pipelines[key];
    if (!state)
        state = device.makePipeline(MetalProgramImpl::describePipeline(*this, key));
    return state;
}

bool MetalProgram::Impl::bind(MetalDevice::Impl&          device,
                              id<MTLRenderCommandEncoder> encoder,
                              const MetalDrawParameters&  parameters,
                              const rhi::Texture*         texture,
                              id<MTLTexture>              attachment)
{
    using namespace MetalProgramImpl;
    const std::lock_guard guard(mutex);
    for (std::size_t index = 0; index < stages.size(); ++index)
    {
        const auto& stage       = stages[index];
        const bool  vertexStage = index == 0;
        if (stage.draw)
            setBytes(encoder, vertexStage, &parameters, sizeof(parameters), 0);
        for (const auto& buffer : stage.buffers)
        {
            if (buffer.data.empty())
                continue;
            if (buffer.data.size() <= metalInlineDataLimit)
            {
                setBytes(encoder, vertexStage, buffer.data.data(), buffer.data.size(), buffer.index);
                continue;
            }
            const auto staging = device.allocate(buffer.data.size(), 256);
            if (!staging.buffer)
                return false;
            std::memcpy(staging.contents, buffer.data.data(), buffer.data.size());
            if (vertexStage)
                [encoder setVertexBuffer:staging.buffer offset:staging.offset atIndex:buffer.index];
            else
                [encoder setFragmentBuffer:staging.buffer offset:staging.offset atIndex:buffer.index];
        }
        for (const NSUInteger slot : stage.samplers)
        {
            const auto                found    = std::find_if(stage.textures.begin(),
                                            stage.textures.end(),
                                            [slot](const MetalSampler& sampler) { return sampler.index == slot; });
            const auto*               selected = found == stage.textures.end() ? nullptr
                                                 : found->current              ? texture
                                                                               : found->texture.get();
            const id<MTLSamplerState> state    = device.sampler(selected && selected->isSmooth(),
                                                             selected && selected->isRepeated(),
                                                             selected && selected->hasMipmap());
            if (vertexStage)
                [encoder setVertexSamplerState:state atIndex:slot];
            else
                [encoder setFragmentSamplerState:state atIndex:slot];
        }
        for (const auto& sampler : stage.textures)
        {
            const auto*          selected = sampler.current ? texture : sampler.texture.get();
            const id<MTLTexture> image    = selected && nativeMetalImage(selected) ? nativeMetalImage(selected)->texture
                                                                                   : device.white->texture;
            if (image == attachment)
            {
                err() << "Cannot sample the active Metal render target" << std::endl;
                return false;
            }
            if (vertexStage)
                [encoder setVertexTexture:image atIndex:sampler.index];
            else
                [encoder setFragmentTexture:image atIndex:sampler.index];
        }
    }
    return true;
}


////////////////////////////////////////////////////////////
MetalProgram::MetalProgram() : m_impl(std::make_unique<Impl>())
{
}

MetalProgram::~MetalProgram() = default;

MetalProgram::Impl& MetalProgram::impl() const
{
    return *m_impl;
}

std::shared_ptr<MetalProgram> MetalProgram::create(MetalDevice& device, std::string_view vertex, std::string_view fragment)
{
    using namespace MetalProgramImpl;
    const auto guard = device.lock();
    @autoreleasepool
    {
        auto& context = device.impl();
        if (!device.whiteTexture())
            return nullptr;
        auto  program = std::shared_ptr<MetalProgram>(new MetalProgram);
        auto& impl    = *program->m_impl;
        if (vertex.empty())
        {
            impl.vertex = [context.library newFunctionWithName:@(MetalBuiltIn::vertexFunction.data())];
        }
        else if (const id<MTLLibrary> library = loadLibrary(context.device, vertex))
        {
            impl.vertex = selectFunction(library, MTLFunctionTypeVertex, "vertex");
        }
        if (fragment.empty())
        {
            impl.fragment = [context.library newFunctionWithName:@(MetalBuiltIn::fragmentFunction.data())];
        }
        else if (const id<MTLLibrary> library = loadLibrary(context.device, fragment))
        {
            impl.fragment = selectFunction(library, MTLFunctionTypeFragment, "fragment");
        }
        if (!impl.vertex || !impl.fragment)
            return nullptr;
        if (impl.vertex.vertexAttributes.count)
            impl.layout = vertexLayout();

        // Reflection requires a pipeline, so a representative one is built (and cached) right away
        const MetalPipelineKey           key{MTLPixelFormatRGBA8Unorm,
                                   MTLPixelFormatInvalid,
                                   MTLPixelFormatInvalid,
                                   1,
                                   MTLBlendFactorSourceAlpha,
                                   MTLBlendFactorOneMinusSourceAlpha,
                                   MTLBlendOperationAdd,
                                   MTLBlendFactorOne,
                                   MTLBlendFactorOneMinusSourceAlpha,
                                   MTLBlendOperationAdd,
                                   MTLColorWriteMaskAll};
        MTLRenderPipelineReflection*     reflection = nil;
        const id<MTLRenderPipelineState> state      = context.makePipeline(describePipeline(impl, key),
                                                                      MTLPipelineOptionBindingInfo | MTLPipelineOptionBufferTypeInfo,
                                                                      &reflection);
        if (!state || !reflect(reflection.vertexBindings, impl.stages[0], true) ||
            !reflect(reflection.fragmentBindings, impl.stages[1], false))
            return nullptr;
        impl.pipelines[key] = state;
        return program;
    }
}

void MetalProgram::setUniform(const std::string& name, ShaderUniformType type, const void* values, std::size_t count)
{
    if (!values || !count)
        return;
    using namespace MetalProgramImpl;
    const std::lock_guard guard(m_impl->mutex);
    const auto [columns, rows] = shape(type);
    const std::size_t packed   = columns * rows * 4;
    bool              found    = false;
    for (auto& stage : m_impl->stages)
        for (auto& buffer : stage.buffers)
            for (const auto& uniform : buffer.uniforms)
            {
                if (uniform.name != name)
                    continue;
                found = true;
                if (!compatible(uniform.type, type) || count > std::max<NSUInteger>(uniform.elements, 1))
                {
                    err() << "Incompatible Metal uniform or array length: " << name << std::endl;
                    continue;
                }
                const std::size_t size   = elementSize(uniform.type);
                const std::size_t stride = uniform.elements ? uniform.stride : size;
                if (uniform.offset + (count - 1) * stride + size > buffer.data.size())
                    continue;
                for (std::size_t element = 0; element < count; ++element)
                {
                    auto*       destination = buffer.data.data() + uniform.offset + element * stride;
                    const auto* source      = static_cast<const std::byte*>(values) + element * packed;
                    if (uniform.type == MTLDataTypeBool || uniform.type == MTLDataTypeBool2 ||
                        uniform.type == MTLDataTypeBool3 || uniform.type == MTLDataTypeBool4)
                    {
                        for (std::size_t component = 0; component < columns; ++component)
                        {
                            std::int32_t value{};
                            std::memcpy(&value, source + component * sizeof(value), sizeof(value));
                            destination[component] = std::byte{value != 0};
                        }
                    }
                    else if (rows == 1)
                    {
                        std::memcpy(destination, source, packed);
                    }
                    else
                    {
                        for (std::size_t column = 0; column < columns; ++column)
                            std::memcpy(destination + column * 16, source + column * rows * 4, rows * 4);
                    }
                }
            }
    if (!found)
        err() << "Metal uniform not found: " << name << std::endl;
}

void MetalProgram::setSampler(const std::string& name, std::shared_ptr<rhi::Texture> texture, bool current)
{
    const std::lock_guard guard(m_impl->mutex);
    bool                  found = false;
    for (auto& stage : m_impl->stages)
        for (auto& sampler : stage.textures)
            if (sampler.name == name)
            {
                sampler.texture = texture;
                sampler.current = current;
                found           = true;
            }
    if (!found)
        err() << "Metal texture not found: " << name << std::endl;
}
} // namespace sf::priv
