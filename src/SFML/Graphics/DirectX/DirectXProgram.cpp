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

#include <SFML/Graphics/DirectX/BuiltInShaders.hpp>
#include <SFML/Graphics/DirectX/DirectXProgram.hpp>
#include <SFML/Graphics/Texture.hpp>
#include <SFML/Graphics/Vertex.hpp>

#include <SFML/System/Err.hpp>

#include <algorithm>
#include <d3d11shader.h>
#include <d3dcompiler.h>
#include <mutex>
#include <ostream>
#include <utility>
#include <vector>

#include <cstring>

namespace sf::priv
{
namespace
{
namespace DirectXProgramImpl
{
template <typename T>
T loadFunction(HMODULE library, const char* name)
{
    return reinterpret_cast<T>(reinterpret_cast<void*>(GetProcAddress(library, name)));
}

struct Compiler
{
    Compiler()
    {
        library = LoadLibraryW(L"d3dcompiler_47.dll");
        if (library)
        {
            compile = loadFunction<pD3DCompile>(library, "D3DCompile");
            reflect = loadFunction<decltype(&D3DReflect)>(library, "D3DReflect");
        }
    }
    ~Compiler()
    {
        if (library)
            FreeLibrary(library);
    }
    Compiler(const Compiler&)            = delete;
    Compiler& operator=(const Compiler&) = delete;

    HMODULE               library{};
    pD3DCompile           compile{};
    decltype(&D3DReflect) reflect{};
};

Compiler& compiler()
{
    static Compiler value;
    return value;
}

struct Uniform
{
    std::string            name;
    UINT                   offset{};
    D3D11_SHADER_TYPE_DESC type{};
};
struct Constants
{
    UINT                   slot{};
    ComPtr<ID3D11Buffer>   buffer;
    std::vector<std::byte> data;
    std::vector<Uniform>   uniforms;
};
struct Sampler
{
    std::string    name;
    UINT           slot{};
    const Texture* texture{};
    bool           current{};
};
struct Stage
{
    struct Parameter
    {
        std::string                 name;
        UINT                        index{};
        UINT                        location{};
        BYTE                        mask{};
        D3D_REGISTER_COMPONENT_TYPE type{};
    };
    std::vector<Parameter> inputs;
    std::vector<Parameter> outputs;
    std::vector<Constants> constants;
    std::vector<Sampler>   samplers;
};

bool makeConstants(DirectXDevice& device, UINT size, ComPtr<ID3D11Buffer>& buffer)
{
    D3D11_BUFFER_DESC description{};
    description.ByteWidth      = (size + 15u) & ~15u;
    description.Usage          = D3D11_USAGE_DYNAMIC;
    description.BindFlags      = D3D11_BIND_CONSTANT_BUFFER;
    description.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    return device.check(device.device()->CreateBuffer(&description, nullptr, buffer.put()), "create shader constants");
}

bool reflect(DirectXDevice& device, ID3DBlob& bytecode, Stage& stage)
{
    ComPtr<ID3D11ShaderReflection> reflection;
    if (!device.check(compiler().reflect(bytecode.GetBufferPointer(),
                                         bytecode.GetBufferSize(),
                                         IID_ID3D11ShaderReflection,
                                         reinterpret_cast<void**>(reflection.put())),
                      "reflect HLSL shader"))
        return false;
    D3D11_SHADER_DESC description{};
    if (FAILED(reflection->GetDesc(&description)))
        return false;
    for (UINT index = 0; index < description.InputParameters; ++index)
    {
        D3D11_SIGNATURE_PARAMETER_DESC parameter{};
        if (FAILED(reflection->GetInputParameterDesc(index, &parameter)))
            return false;
        if (parameter.ReadWriteMask)
            stage.inputs.push_back({parameter.SemanticName,
                                    parameter.SemanticIndex,
                                    parameter.Register,
                                    parameter.ReadWriteMask,
                                    parameter.ComponentType});
    }
    for (UINT index = 0; index < description.OutputParameters; ++index)
    {
        D3D11_SIGNATURE_PARAMETER_DESC parameter{};
        if (FAILED(reflection->GetOutputParameterDesc(index, &parameter)))
            return false;
        stage.outputs.push_back(
            {parameter.SemanticName, parameter.SemanticIndex, parameter.Register, parameter.Mask, parameter.ComponentType});
    }
    for (UINT index = 0; index < description.BoundResources; ++index)
    {
        D3D11_SHADER_INPUT_BIND_DESC resource{};
        if (FAILED(reflection->GetResourceBindingDesc(index, &resource)))
            return false;
        if (resource.Type == D3D_SIT_CBUFFER)
        {
            if (resource.BindCount != 1 || resource.BindPoint >= D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT)
                return false;
            auto*                    reflected = reflection->GetConstantBufferByName(resource.Name);
            D3D11_SHADER_BUFFER_DESC buffer{};
            if (FAILED(reflected->GetDesc(&buffer)))
                return false;
            if (resource.BindPoint == 0)
            {
                if (std::strcmp(resource.Name, "SFMLDraw") != 0 || buffer.Size != sizeof(DirectXDrawParameters))
                {
                    err() << "HLSL register b0 is reserved for the SFMLDraw block; place user constants at b1 or higher"
                          << std::endl;
                    return false;
                }
                const std::array<const char*, 2> names{"sf_ModelViewProjectionMatrix", "sf_TextureTransform"};
                for (std::size_t member = 0; member < names.size(); ++member)
                {
                    auto*                      variable = reflected->GetVariableByName(names[member]);
                    D3D11_SHADER_VARIABLE_DESC info{};
                    D3D11_SHADER_TYPE_DESC     type{};
                    if (FAILED(variable->GetDesc(&info)) || FAILED(variable->GetType()->GetDesc(&type)) ||
                        info.StartOffset != (member == 0 ? 0u : 64u) || type.Type != D3D_SVT_FLOAT ||
                        type.Class != (member == 0 ? D3D_SVC_MATRIX_COLUMNS : D3D_SVC_VECTOR) ||
                        type.Rows != (member == 0 ? 4u : 1u) || type.Columns != 4 || type.Elements)
                    {
                        err() << "Invalid HLSL SFMLDraw layout" << std::endl;
                        return false;
                    }
                }
                continue;
            }
            Constants constants;
            constants.slot = resource.BindPoint;
            constants.data.resize(buffer.Size);
            if (!makeConstants(device, buffer.Size, constants.buffer))
                return false;
            for (UINT variable = 0; variable < buffer.Variables; ++variable)
            {
                auto*                      value = reflected->GetVariableByIndex(variable);
                D3D11_SHADER_VARIABLE_DESC info{};
                Uniform                    uniform;
                if (FAILED(value->GetDesc(&info)) || FAILED(value->GetType()->GetDesc(&uniform.type)))
                    return false;
                uniform.name   = info.Name;
                uniform.offset = info.StartOffset;
                if (info.DefaultValue)
                    std::memcpy(constants.data.data() + info.StartOffset, info.DefaultValue, info.Size);
                constants.uniforms.push_back(std::move(uniform));
            }
            stage.constants.push_back(std::move(constants));
        }
        else if (resource.Type == D3D_SIT_TEXTURE && resource.Dimension == D3D_SRV_DIMENSION_TEXTURE2D &&
                 resource.BindCount == 1)
        {
            if (resource.BindPoint >= D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT)
            {
                err() << "HLSL textures must use a matching tN/sN pair with N below 16" << std::endl;
                return false;
            }
            stage.samplers.push_back(
                {resource.Name, resource.BindPoint, nullptr, std::strcmp(resource.Name, "sf_Texture") == 0});
        }
        else if (resource.Type != D3D_SIT_SAMPLER)
        {
            err() << "Unsupported HLSL resource: " << resource.Name << std::endl;
            return false;
        }
    }
    return true;
}

bool compatible(const Stage& source, const Stage& destination)
{
    for (const auto& input : destination.inputs)
    {
        // These values are generated by the rasterizer rather than the previous shader.
        if (_stricmp(input.name.c_str(), "SV_IsFrontFace") == 0 ||
            _stricmp(input.name.c_str(), "SV_SampleIndex") == 0 || _stricmp(input.name.c_str(), "SV_PrimitiveID") == 0)
            continue;
        const auto found = std::find_if(source.outputs.begin(),
                                        source.outputs.end(),
                                        [&](const Stage::Parameter& output)
                                        {
                                            return _stricmp(input.name.c_str(), output.name.c_str()) == 0 &&
                                                   input.index == output.index && input.location == output.location &&
                                                   input.type == output.type && (input.mask & output.mask) == input.mask;
                                        });
        if (found == source.outputs.end())
        {
            err() << "Incompatible HLSL stage signature for " << input.name << input.index
                  << "; built-in stages use SV_POSITION, COLOR0, TEXCOORD0 in that order" << std::endl;
            return false;
        }
    }
    return true;
}

bool compile(DirectXDevice& device, std::string_view source, const char* prefix, ComPtr<ID3DBlob>& bytecode)
{
    if (!DirectXProgram::compilerAvailable())
    {
        err() << "HLSL loading requires d3dcompiler_47.dll" << std::endl;
        return false;
    }
    const auto       settings = device.settings();
    const auto       profile  = std::string(prefix) + (settings.majorVersion >= 11 ? "_5_0"
                                                       : settings.minorVersion     ? "_4_1"
                                                                                   : "_4_0");
    ComPtr<ID3DBlob> errors;
    const auto       result = compiler().compile(source.data(),
                                           source.size(),
                                           nullptr,
                                           nullptr,
                                           nullptr,
                                           "main",
                                           profile.c_str(),
                                           D3DCOMPILE_ENABLE_STRICTNESS,
                                           0,
                                           bytecode.put(),
                                           errors.put());
    if (FAILED(result) && errors)
        err() << "HLSL compilation failed: " << static_cast<const char*>(errors->GetBufferPointer()) << std::endl;
    return device.check(result, "compile HLSL shader");
}

struct UniformShape
{
    unsigned int columns;
    unsigned int rows;
    bool         integer;
};
UniformShape shape(ShaderUniformType type)
{
    switch (type)
    {
        case ShaderUniformType::Float:
            return {1, 1, false};
        case ShaderUniformType::Vec2:
            return {2, 1, false};
        case ShaderUniformType::Vec3:
            return {3, 1, false};
        case ShaderUniformType::Vec4:
            return {4, 1, false};
        case ShaderUniformType::Int:
            return {1, 1, true};
        case ShaderUniformType::IVec2:
            return {2, 1, true};
        case ShaderUniformType::IVec3:
            return {3, 1, true};
        case ShaderUniformType::IVec4:
            return {4, 1, true};
        case ShaderUniformType::Mat3:
            return {3, 3, false};
        case ShaderUniformType::Mat4:
            return {4, 4, false};
    }
    return {};
}

bool uploadConstants(DirectXDevice& device, ID3D11Buffer* buffer, const void* data, std::size_t size)
{
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (!device.check(device.context()->Map(buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped), "update shader constants"))
        return false;
    std::memcpy(mapped.pData, data, size);
    device.context()->Unmap(buffer, 0);
    return true;
}
} // namespace DirectXProgramImpl
} // namespace

struct DirectXProgram::Impl
{
    std::mutex                               mutex;
    ComPtr<ID3D11VertexShader>               vertex;
    ComPtr<ID3D11GeometryShader>             geometry;
    ComPtr<ID3D11PixelShader>                fragment;
    ComPtr<ID3D11InputLayout>                layout;
    ComPtr<ID3D11Buffer>                     draw;
    std::array<DirectXProgramImpl::Stage, 3> stages;
};

DirectXProgram::DirectXProgram() : m_impl(std::make_unique<Impl>())
{
}
DirectXProgram::~DirectXProgram() = default;
bool DirectXProgram::compilerAvailable()
{
    return DirectXProgramImpl::compiler().compile && DirectXProgramImpl::compiler().reflect;
}

std::shared_ptr<DirectXProgram> DirectXProgram::create(DirectXDevice&   device,
                                                       std::string_view vertex,
                                                       std::string_view geometry,
                                                       std::string_view fragment)
{
    using namespace DirectXProgramImpl;
    const auto guard = device.lock();
    if (device.isLost())
        return {};
    auto             program = std::shared_ptr<DirectXProgram>(new DirectXProgram);
    auto&            impl    = *program->m_impl;
    ComPtr<ID3DBlob> vertexCode;
    ComPtr<ID3DBlob> geometryCode;
    ComPtr<ID3DBlob> fragmentCode;
    const void*      vertexData = DirectXBuiltIn::vertex.data();
    std::size_t      vertexSize = DirectXBuiltIn::vertex.size();
    if (!vertex.empty())
    {
        if (!compile(device, vertex, "vs", vertexCode) || !reflect(device, *vertexCode.get(), impl.stages[0]))
            return {};
        vertexData = vertexCode->GetBufferPointer();
        vertexSize = vertexCode->GetBufferSize();
    }
    else
    {
        impl.stages[0].outputs = {{"SV_POSITION", 0, 0, 15, D3D_REGISTER_COMPONENT_FLOAT32},
                                  {"COLOR", 0, 1, 15, D3D_REGISTER_COMPONENT_FLOAT32},
                                  {"TEXCOORD", 0, 2, 3, D3D_REGISTER_COMPONENT_FLOAT32}};
    }
    if (!device.check(device.device()->CreateVertexShader(vertexData, vertexSize, nullptr, impl.vertex.put()),
                      "create vertex shader"))
        return {};
    const std::array<D3D11_INPUT_ELEMENT_DESC, 3> layout{
        {{"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, static_cast<UINT>(offsetof(Vertex, position)), D3D11_INPUT_PER_VERTEX_DATA, 0},
         {"COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, static_cast<UINT>(offsetof(Vertex, color)), D3D11_INPUT_PER_VERTEX_DATA, 0},
         {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, static_cast<UINT>(offsetof(Vertex, texCoords)), D3D11_INPUT_PER_VERTEX_DATA, 0}}};
    if (!device.check(device.device()->CreateInputLayout(layout.data(),
                                                         static_cast<UINT>(layout.size()),
                                                         vertexData,
                                                         vertexSize,
                                                         impl.layout.put()),
                      "create vertex layout"))
        return {};
    if (!geometry.empty())
    {
        if (!compile(device, geometry, "gs", geometryCode) || !reflect(device, *geometryCode.get(), impl.stages[1]) ||
            !device.check(device.device()->CreateGeometryShader(geometryCode->GetBufferPointer(),
                                                                geometryCode->GetBufferSize(),
                                                                nullptr,
                                                                impl.geometry.put()),
                          "create geometry shader"))
            return {};
    }
    const void* fragmentData = DirectXBuiltIn::fragment.data();
    std::size_t fragmentSize = DirectXBuiltIn::fragment.size();
    if (!fragment.empty())
    {
        if (!compile(device, fragment, "ps", fragmentCode) || !reflect(device, *fragmentCode.get(), impl.stages[2]))
            return {};
        fragmentData = fragmentCode->GetBufferPointer();
        fragmentSize = fragmentCode->GetBufferSize();
    }
    else
    {
        impl.stages[2].samplers.push_back({"sf_Texture", 0, nullptr, true});
        impl.stages[2].inputs = {{"COLOR", 0, 1, 15, D3D_REGISTER_COMPONENT_FLOAT32},
                                 {"TEXCOORD", 0, 2, 3, D3D_REGISTER_COMPONENT_FLOAT32}};
    }
    if ((!geometry.empty() && !compatible(impl.stages[0], impl.stages[1])) ||
        !compatible(impl.stages[geometry.empty() ? 0 : 1], impl.stages[2]))
        return {};
    if (!device.check(device.device()->CreatePixelShader(fragmentData, fragmentSize, nullptr, impl.fragment.put()),
                      "create pixel shader") ||
        !makeConstants(device, sizeof(DirectXDrawParameters), impl.draw))
        return {};
    return program;
}

void DirectXProgram::setUniform(const std::string& name, ShaderUniformType type, const void* values, std::size_t count)
{
    using namespace DirectXProgramImpl;
    const std::lock_guard guard(m_impl->mutex);
    const auto            input = shape(type);
    bool                  found = false;
    for (auto& stage : m_impl->stages)
        for (auto& buffer : stage.constants)
            for (const auto& uniform : buffer.uniforms)
            {
                if (uniform.name != name)
                    continue;
                found               = true;
                const auto& output  = uniform.type;
                const bool  integer = output.Type == D3D_SVT_INT || output.Type == D3D_SVT_BOOL;
                if (output.Rows != input.rows || output.Columns != input.columns || integer != input.integer ||
                    (!integer && output.Type != D3D_SVT_FLOAT) || count > std::max(output.Elements, 1u))
                {
                    err() << "Incompatible HLSL uniform or array length: " << name << std::endl;
                    continue;
                }
                const std::size_t packed = static_cast<std::size_t>(input.rows) * input.columns * 4;
                const std::size_t stride = input.rows > 1    ? static_cast<std::size_t>(input.rows) * 16
                                           : output.Elements ? 16
                                                             : packed;
                if (uniform.offset + (count - 1) * stride + (input.rows > 1 ? stride : packed) > buffer.data.size())
                    continue;
                for (std::size_t element = 0; element < count; ++element)
                {
                    auto*       destination = buffer.data.data() + uniform.offset + element * stride;
                    const auto* source      = static_cast<const std::byte*>(values) + element * packed;
                    if (input.rows == 1)
                        std::memcpy(destination, source, packed);
                    else
                        for (unsigned int column = 0; column < input.columns; ++column)
                            for (unsigned int row = 0; row < input.rows; ++row)
                            {
                                const auto offset = output.Class == D3D_SVC_MATRIX_ROWS ? row * 16 + column * 4
                                                                                        : column * 16 + row * 4;
                                std::memcpy(destination + offset, source + (column * input.rows + row) * 4, 4);
                            }
                }
            }
    if (!found)
        err() << "HLSL uniform not found: " << name << std::endl;
}

void DirectXProgram::setSampler(const std::string& name, const Texture* texture, bool current)
{
    const std::lock_guard guard(m_impl->mutex);
    bool                  found = false;
    for (auto& stage : m_impl->stages)
        for (auto& sampler : stage.samplers)
            if (sampler.name == name)
            {
                sampler.texture = texture;
                sampler.current = current;
                found           = true;
            }
    if (!found)
        err() << "HLSL texture not found: " << name << std::endl;
}

bool DirectXProgram::bind(DirectXDevice&               device,
                          const DirectXDrawParameters& parameters,
                          const Texture*               texture,
                          const DirectXImage&          attachment)
{
    using namespace DirectXProgramImpl;
    const std::lock_guard guard(m_impl->mutex);
    auto*                 context = device.context();
    if (!uploadConstants(device, m_impl->draw.get(), &parameters, sizeof(parameters)))
        return false;
    context->IASetInputLayout(m_impl->layout.get());
    context->VSSetShader(m_impl->vertex.get(), nullptr, 0);
    context->GSSetShader(m_impl->geometry.get(), nullptr, 0);
    context->PSSetShader(m_impl->fragment.get(), nullptr, 0);
    const auto& white = device.whiteTexture();
    if (!white)
        return false;
    for (std::size_t index = 0; index < m_impl->stages.size(); ++index)
    {
        const auto&                                                                  stage = m_impl->stages[index];
        std::array<ID3D11Buffer*, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT> buffers{};
        buffers[0] = m_impl->draw.get();
        for (const auto& buffer : stage.constants)
        {
            if (!uploadConstants(device, buffer.buffer.get(), buffer.data.data(), buffer.data.size()))
                return false;
            buffers[buffer.slot] = buffer.buffer.get();
        }
        std::array<ID3D11ShaderResourceView*, D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT> images{};
        std::array<ID3D11SamplerState*, D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT>       samplers{};
        for (const auto& sampler : stage.samplers)
        {
            const auto* selected = sampler.current ? texture : sampler.texture;
            const auto& image    = selected && selected->m_directXImage ? selected->m_directXImage : white;
            if (image->texture.get() == attachment.texture.get())
            {
                err() << "Cannot sample the active DirectX render target" << std::endl;
                return false;
            }
            images[sampler.slot]   = image->view.get();
            samplers[sampler.slot] = device.sampler(selected && selected->m_isSmooth,
                                                    selected && selected->m_isRepeated,
                                                    selected && selected->m_hasMipmap);
            if (!samplers[sampler.slot])
                return false;
        }
        const auto bufferCount = static_cast<UINT>(buffers.size());
        const auto sampleCount = static_cast<UINT>(images.size());
        if (index == 0)
        {
            context->VSSetConstantBuffers(0, bufferCount, buffers.data());
            context->VSSetShaderResources(0, sampleCount, images.data());
            context->VSSetSamplers(0, sampleCount, samplers.data());
        }
        else if (index == 1)
        {
            context->GSSetConstantBuffers(0, bufferCount, buffers.data());
            context->GSSetShaderResources(0, sampleCount, images.data());
            context->GSSetSamplers(0, sampleCount, samplers.data());
        }
        else
        {
            context->PSSetConstantBuffers(0, bufferCount, buffers.data());
            context->PSSetShaderResources(0, sampleCount, images.data());
            context->PSSetSamplers(0, sampleCount, samplers.data());
        }
    }
    return true;
}
} // namespace sf::priv
