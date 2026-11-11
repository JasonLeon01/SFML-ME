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


////////////////////////////////////////////////////////////
// Headers
////////////////////////////////////////////////////////////
#include <SFML/Graphics/GraphicsDevice.hpp>
#ifdef SFML_RENDER_DIRECTX
#include <SFML/Graphics/DirectX/DirectXProgram.hpp>
#endif
#ifdef SFML_RENDER_METAL
#include <SFML/Graphics/Metal/MetalProgram.hpp>
#endif
#include <SFML/Graphics/ShaderUniformType.hpp>

#ifdef SFML_RENDER_GL
#include <SFML/Graphics/GLCheck.hpp>
#include <SFML/Graphics/GLExtensions.hpp>
#include <SFML/Graphics/OpenGL/OpenGLResource.hpp>
#endif
#ifdef SFML_RENDER_VULKAN
#include <SFML/Graphics/Vulkan/VulkanProgram.hpp>
#endif
#include <SFML/Graphics/Shader.hpp>
#include <SFML/Graphics/Texture.hpp>

#include <SFML/System/Err.hpp>
#include <SFML/System/Exception.hpp>
#include <SFML/System/InputStream.hpp>
#include <SFML/System/Utils.hpp>
#include <SFML/System/Vector2.hpp>
#include <SFML/System/Vector3.hpp>

#include <algorithm>
#include <array>
#include <fstream>
#include <iomanip>
#include <limits>
#include <ostream>
#include <utility>
#include <vector>

#include <cstdint>

namespace
{
namespace ShaderImpl
{
#ifdef SFML_RENDER_GL
// Retrieve the maximum number of texture units available
std::size_t getMaxTextureUnits()
{
    static const GLint maxUnits = []
    {
        GLint value = 0;
        glCheck(glGetIntegerv(GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS, &value));

        return value;
    }();

    return static_cast<std::size_t>(maxUnits);
}

#endif

// Read the contents of a file into an array of char
bool getFileContents(const std::filesystem::path& filename, std::vector<char>& buffer)
{
    if (auto file = std::ifstream(filename, std::ios_base::binary))
    {
        file.seekg(0, std::ios_base::end);
        const std::ifstream::pos_type size = file.tellg();
        if (size > 0)
        {
            file.seekg(0, std::ios_base::beg);
            buffer.resize(static_cast<std::size_t>(size));
            return static_cast<bool>(file.read(buffer.data(), static_cast<std::streamsize>(size)));
        }
    }

    return false;
}

// Read the contents of a stream into an array of char
bool getStreamContents(sf::InputStream& stream, std::vector<char>& buffer)
{
    bool                success = false;
    const std::optional size    = stream.getSize();
    if (size > std::size_t{0})
    {
        buffer.resize(*size);

        if (!stream.seek(0).has_value())
        {
            sf::err() << "Failed to seek shader stream" << std::endl;
            return false;
        }

        const std::optional read = stream.read(buffer.data(), *size);
        success                  = (read == size);
    }
    return success;
}

// Transforms an array of 2D vectors into a contiguous array of scalars
std::vector<float> flatten(const sf::Vector2f* vectorArray, std::size_t length)
{
    const std::size_t vectorSize = 2;

    std::vector<float> contiguous(vectorSize * length);
    for (std::size_t i = 0; i < length; ++i)
    {
        contiguous[vectorSize * i]     = vectorArray[i].x;
        contiguous[vectorSize * i + 1] = vectorArray[i].y;
    }

    return contiguous;
}

// Transforms an array of 3D vectors into a contiguous array of scalars
std::vector<float> flatten(const sf::Vector3f* vectorArray, std::size_t length)
{
    const std::size_t vectorSize = 3;

    std::vector<float> contiguous(vectorSize * length);
    for (std::size_t i = 0; i < length; ++i)
    {
        contiguous[vectorSize * i]     = vectorArray[i].x;
        contiguous[vectorSize * i + 1] = vectorArray[i].y;
        contiguous[vectorSize * i + 2] = vectorArray[i].z;
    }

    return contiguous;
}

// Transforms an array of 4D vectors into a contiguous array of scalars
std::vector<float> flatten(const sf::Glsl::Vec4* vectorArray, std::size_t length)
{
    const std::size_t vectorSize = 4;

    std::vector<float> contiguous(vectorSize * length);
    for (std::size_t i = 0; i < length; ++i)
    {
        contiguous[vectorSize * i]     = vectorArray[i].x;
        contiguous[vectorSize * i + 1] = vectorArray[i].y;
        contiguous[vectorSize * i + 2] = vectorArray[i].z;
        contiguous[vectorSize * i + 3] = vectorArray[i].w;
    }

    return contiguous;
}
#ifdef SFML_RENDER_GL
bool isProgrammable(const sf::priv::GraphicsDevice& device)
{
    const auto* gl = std::get_if<sf::OpenGLSettings>(&device.settings);
    return !gl || gl->profile == sf::OpenGLProfile::Core;
}

constexpr std::string_view defaultVertex = R"(
in vec4 sf_Vertex;
in vec4 sf_Color;
in vec4 sf_MultiTexCoord0;
uniform mat4 sf_ModelViewProjectionMatrix;
uniform mat4 sf_TextureMatrix;
out vec4 sf_FrontColor;
out vec4 sf_TexCoord0;

void main()
{
    gl_Position = sf_ModelViewProjectionMatrix * sf_Vertex;
    gl_PointSize = 1.0;
    sf_FrontColor = sf_Color;
    sf_TexCoord0 = sf_TextureMatrix * sf_MultiTexCoord0;
}
)";

constexpr std::string_view defaultFragment = R"(
uniform sampler2D sf_Texture;
uniform bool sf_TextureEnabled;
in vec4 sf_FrontColor;
in vec4 sf_TexCoord0;
out vec4 sf_FragColor;

void main()
{
    sf_FragColor = sf_TextureEnabled ? texture(sf_Texture, sf_TexCoord0.xy) * sf_FrontColor : sf_FrontColor;
}
)";

std::string builtIn(std::string_view body)
{
#ifdef SFML_OPENGL_ES
    std::string source = "#version 300 es\nprecision highp float;\n";
#else
    std::string source = "#version 150\n";
#endif
    source += body;
    return source;
}
#endif
} // namespace ShaderImpl
} // namespace


namespace sf
{
////////////////////////////////////////////////////////////
using namespace ShaderImpl;

#ifdef SFML_RENDER_GL
using priv::OpenGLResource;
using TransientContextLock = OpenGLResource::TransientContextLock;

struct Shader::UniformBinder
{
    UniformBinder(Shader& shader, const std::string& name) : program(shader.m_shaderProgram)
    {
        if (program)
        {
            glCheck(glGetIntegerv(GL_CURRENT_PROGRAM, &savedProgram));
            if (program != static_cast<GLuint>(savedProgram))
                glCheck(glUseProgram(program));
            location = shader.getUniformLocation(name);
        }
    }

    ~UniformBinder()
    {
        if (program && program != static_cast<GLuint>(savedProgram))
            glCheck(glUseProgram(static_cast<GLuint>(savedProgram)));
    }

    UniformBinder(const UniformBinder&)            = delete;
    UniformBinder& operator=(const UniformBinder&) = delete;

    TransientContextLock lock;
    GLint                savedProgram{};
    GLuint               program;
    GLint                location{-1};
};
#endif


////////////////////////////////////////////////////////////
Shader::Shader(const std::filesystem::path& filename, Type type)
{
    if (!loadFromFile(filename, type))
        throw Exception("Failed to load shader from file");
}


////////////////////////////////////////////////////////////
Shader::Shader(const std::filesystem::path& vertexShaderFilename, const std::filesystem::path& fragmentShaderFilename)
{
    if (!loadFromFile(vertexShaderFilename, fragmentShaderFilename))
        throw Exception("Failed to load shader from files");
}


////////////////////////////////////////////////////////////
Shader::Shader(const std::filesystem::path& vertexShaderFilename,
               const std::filesystem::path& geometryShaderFilename,
               const std::filesystem::path& fragmentShaderFilename)
{
    if (!loadFromFile(vertexShaderFilename, geometryShaderFilename, fragmentShaderFilename))
        throw Exception("Failed to load shader from files");
}


////////////////////////////////////////////////////////////
Shader::Shader(std::string_view shader, Type type)
{
    if (!loadFromMemory(shader, type))
        throw Exception("Failed to load shader from memory");
}


////////////////////////////////////////////////////////////
Shader::Shader(std::string_view vertexShader, std::string_view fragmentShader)
{
    if (!loadFromMemory(vertexShader, fragmentShader))
        throw Exception("Failed to load shader from memory");
}


////////////////////////////////////////////////////////////
Shader::Shader(std::string_view vertexShader, std::string_view geometryShader, std::string_view fragmentShader)
{
    if (!loadFromMemory(vertexShader, geometryShader, fragmentShader))
        throw Exception("Failed to load shader from memory");
}


////////////////////////////////////////////////////////////
Shader::Shader(InputStream& stream, Type type)
{
    if (!loadFromStream(stream, type))
        throw Exception("Failed to load shader from stream");
}


////////////////////////////////////////////////////////////
Shader::Shader(InputStream& vertexShaderStream, InputStream& fragmentShaderStream)
{
    if (!loadFromStream(vertexShaderStream, fragmentShaderStream))
        throw Exception("Failed to load shader from streams");
}


////////////////////////////////////////////////////////////
Shader::Shader(InputStream& vertexShaderStream, InputStream& geometryShaderStream, InputStream& fragmentShaderStream)
{
    if (!loadFromStream(vertexShaderStream, geometryShaderStream, fragmentShaderStream))
        throw Exception("Failed to load shader from streams");
}


////////////////////////////////////////////////////////////
Shader::~Shader()
{
#ifdef SFML_RENDER_GL
    if (m_shaderProgram)
    {
        const TransientContextLock lock;
        glCheck(glDeleteProgram(m_shaderProgram));
    }
#endif
}

Shader::Shader(Shader&& source) noexcept :
    m_glContext(std::move(source.m_glContext)),
    m_vulkanProgram(std::move(source.m_vulkanProgram)),
    m_directXProgram(std::move(source.m_directXProgram)),
    m_metalProgram(std::move(source.m_metalProgram)),
    m_shaderProgram(std::exchange(source.m_shaderProgram, 0u)),
    m_currentTexture(std::exchange(source.m_currentTexture, -1)),
    m_textures(std::move(source.m_textures)),
    m_uniforms(std::move(source.m_uniforms))
{
}

Shader& Shader::operator=(Shader&& right) noexcept
{
    if (&right == this)
        return *this;

    Shader temporary(std::move(right));
    std::swap(m_glContext, temporary.m_glContext);
    std::swap(m_vulkanProgram, temporary.m_vulkanProgram);
    std::swap(m_directXProgram, temporary.m_directXProgram);
    std::swap(m_metalProgram, temporary.m_metalProgram);
    std::swap(m_shaderProgram, temporary.m_shaderProgram);
    std::swap(m_currentTexture, temporary.m_currentTexture);
    std::swap(m_textures, temporary.m_textures);
    std::swap(m_uniforms, temporary.m_uniforms);
    return *this;
}

////////////////////////////////////////////////////////////
bool Shader::loadFromFile(const std::filesystem::path& filename, Type type)
{
    // Read the file
    std::vector<char> shader;
    if (!getFileContents(filename, shader))
    {
        err() << "Failed to open shader file\n" << formatDebugPathInfo(filename) << std::endl;
        return false;
    }

    // Compile the shader program
    if (type == Type::Vertex)
        return compile({shader.data(), shader.size()}, {}, {});

    if (type == Type::Geometry)
        return compile({}, {shader.data(), shader.size()}, {});

    return compile({}, {}, {shader.data(), shader.size()});
}


////////////////////////////////////////////////////////////
bool Shader::loadFromFile(const std::filesystem::path& vertexShaderFilename,
                          const std::filesystem::path& fragmentShaderFilename)
{
    // Read the vertex shader file
    std::vector<char> vertexShader;
    if (!getFileContents(vertexShaderFilename, vertexShader))
    {
        err() << "Failed to open vertex shader file\n" << formatDebugPathInfo(vertexShaderFilename) << std::endl;
        return false;
    }

    // Read the fragment shader file
    std::vector<char> fragmentShader;
    if (!getFileContents(fragmentShaderFilename, fragmentShader))
    {
        err() << "Failed to open fragment shader file\n" << formatDebugPathInfo(fragmentShaderFilename) << std::endl;
        return false;
    }

    // Compile the shader program
    return compile({vertexShader.data(), vertexShader.size()}, {}, {fragmentShader.data(), fragmentShader.size()});
}


////////////////////////////////////////////////////////////
bool Shader::loadFromFile(const std::filesystem::path& vertexShaderFilename,
                          const std::filesystem::path& geometryShaderFilename,
                          const std::filesystem::path& fragmentShaderFilename)
{
    // Read the vertex shader file
    std::vector<char> vertexShader;
    if (!getFileContents(vertexShaderFilename, vertexShader))
    {
        err() << "Failed to open vertex shader file\n" << formatDebugPathInfo(vertexShaderFilename) << std::endl;
        return false;
    }

    // Read the geometry shader file
    std::vector<char> geometryShader;
    if (!getFileContents(geometryShaderFilename, geometryShader))
    {
        err() << "Failed to open geometry shader file\n" << formatDebugPathInfo(geometryShaderFilename) << std::endl;
        return false;
    }

    // Read the fragment shader file
    std::vector<char> fragmentShader;
    if (!getFileContents(fragmentShaderFilename, fragmentShader))
    {
        err() << "Failed to open fragment shader file\n" << formatDebugPathInfo(fragmentShaderFilename) << std::endl;
        return false;
    }

    // Compile the shader program
    return compile({vertexShader.data(), vertexShader.size()},
                   {geometryShader.data(), geometryShader.size()},
                   {fragmentShader.data(), fragmentShader.size()});
}


////////////////////////////////////////////////////////////
bool Shader::loadFromMemory(std::string_view shader, Type type)
{
    // Compile the shader program
    if (type == Type::Vertex)
        return compile(shader, {}, {});

    if (type == Type::Geometry)
        return compile({}, shader, {});

    return compile({}, {}, shader);
}


////////////////////////////////////////////////////////////
bool Shader::loadFromMemory(std::string_view vertexShader, std::string_view fragmentShader)
{
    // Compile the shader program
    return compile(vertexShader, {}, fragmentShader);
}


////////////////////////////////////////////////////////////
bool Shader::loadFromMemory(std::string_view vertexShader, std::string_view geometryShader, std::string_view fragmentShader)
{
    // Compile the shader program
    return compile(vertexShader, geometryShader, fragmentShader);
}


////////////////////////////////////////////////////////////
bool Shader::loadFromStream(InputStream& stream, Type type)
{
    // Read the shader code from the stream
    std::vector<char> shader;
    if (!getStreamContents(stream, shader))
    {
        err() << "Failed to read shader from stream" << std::endl;
        return false;
    }

    // Compile the shader program
    if (type == Type::Vertex)
        return compile({shader.data(), shader.size()}, {}, {});

    if (type == Type::Geometry)
        return compile({}, {shader.data(), shader.size()}, {});

    return compile({}, {}, {shader.data(), shader.size()});
}


////////////////////////////////////////////////////////////
bool Shader::loadFromStream(InputStream& vertexShaderStream, InputStream& fragmentShaderStream)
{
    // Read the vertex shader code from the stream
    std::vector<char> vertexShader;
    if (!getStreamContents(vertexShaderStream, vertexShader))
    {
        err() << "Failed to read vertex shader from stream" << std::endl;
        return false;
    }

    // Read the fragment shader code from the stream
    std::vector<char> fragmentShader;
    if (!getStreamContents(fragmentShaderStream, fragmentShader))
    {
        err() << "Failed to read fragment shader from stream" << std::endl;
        return false;
    }

    // Compile the shader program
    return compile({vertexShader.data(), vertexShader.size()}, {}, {fragmentShader.data(), fragmentShader.size()});
}


////////////////////////////////////////////////////////////
bool Shader::loadFromStream(InputStream& vertexShaderStream, InputStream& geometryShaderStream, InputStream& fragmentShaderStream)
{
    // Read the vertex shader code from the stream
    std::vector<char> vertexShader;
    if (!getStreamContents(vertexShaderStream, vertexShader))
    {
        err() << "Failed to read vertex shader from stream" << std::endl;
        return false;
    }

    // Read the geometry shader code from the stream
    std::vector<char> geometryShader;
    if (!getStreamContents(geometryShaderStream, geometryShader))
    {
        err() << "Failed to read geometry shader from stream" << std::endl;
        return false;
    }

    // Read the fragment shader code from the stream
    std::vector<char> fragmentShader;
    if (!getStreamContents(fragmentShaderStream, fragmentShader))
    {
        err() << "Failed to read fragment shader from stream" << std::endl;
        return false;
    }

    // Compile the shader program
    return compile({vertexShader.data(), vertexShader.size()},
                   {geometryShader.data(), geometryShader.size()},
                   {fragmentShader.data(), fragmentShader.size()});
}


////////////////////////////////////////////////////////////
using priv::ShaderUniformType;

void Shader::setUniform(const std::string& name, float value)
{
    setUniformData(name, ShaderUniformType::Float, &value, 1);
}

void Shader::setUniform(const std::string& name, Glsl::Vec2 value)
{
    const std::array values{value.x, value.y};
    setUniformData(name, ShaderUniformType::Vec2, values.data(), 1);
}

void Shader::setUniform(const std::string& name, const Glsl::Vec3& value)
{
    const std::array values{value.x, value.y, value.z};
    setUniformData(name, ShaderUniformType::Vec3, values.data(), 1);
}

void Shader::setUniform(const std::string& name, const Glsl::Vec4& value)
{
    const std::array values{value.x, value.y, value.z, value.w};
    setUniformData(name, ShaderUniformType::Vec4, values.data(), 1);
}

void Shader::setUniform(const std::string& name, int value)
{
    setUniformData(name, ShaderUniformType::Int, &value, 1);
}

void Shader::setUniform(const std::string& name, Glsl::Ivec2 value)
{
    const std::array values{value.x, value.y};
    setUniformData(name, ShaderUniformType::IVec2, values.data(), 1);
}

void Shader::setUniform(const std::string& name, const Glsl::Ivec3& value)
{
    const std::array values{value.x, value.y, value.z};
    setUniformData(name, ShaderUniformType::IVec3, values.data(), 1);
}

void Shader::setUniform(const std::string& name, const Glsl::Ivec4& value)
{
    const std::array values{value.x, value.y, value.z, value.w};
    setUniformData(name, ShaderUniformType::IVec4, values.data(), 1);
}

void Shader::setUniform(const std::string& name, bool value)
{
    setUniform(name, int(value));
}

void Shader::setUniform(const std::string& name, Glsl::Bvec2 value)
{
    setUniform(name, Glsl::Ivec2(value));
}

void Shader::setUniform(const std::string& name, const Glsl::Bvec3& value)
{
    setUniform(name, Glsl::Ivec3(value));
}

void Shader::setUniform(const std::string& name, const Glsl::Bvec4& value)
{
    setUniform(name, Glsl::Ivec4(value));
}

void Shader::setUniform(const std::string& name, const Glsl::Mat3& value)
{
    setUniformData(name, ShaderUniformType::Mat3, value.array.data(), 1);
}

void Shader::setUniform(const std::string& name, const Glsl::Mat4& value)
{
    setUniformData(name, ShaderUniformType::Mat4, value.array.data(), 1);
}

void Shader::setUniform(const std::string& name, const Texture& texture)
{
#ifdef SFML_RENDER_DIRECTX
    if (m_directXProgram)
    {
        m_directXProgram->setSampler(name, &texture, false);
        return;
    }
#endif
#ifdef SFML_RENDER_METAL
    if (m_metalProgram)
    {
        m_metalProgram->setSampler(name, &texture, false);
        return;
    }
#endif
#ifdef SFML_RENDER_VULKAN
    if (m_vulkanProgram)
    {
        m_vulkanProgram->setSampler(name, &texture, false);
        return;
    }
#endif
#ifdef SFML_RENDER_GL
    if (!m_shaderProgram)
        return;

    const TransientContextLock lock;

    // Find the location of the variable in the shader
    const int location = getUniformLocation(name);
    if (location != -1)
    {
        // Store the location -> texture mapping
        const auto it = m_textures.find(location);
        if (it == m_textures.end())
        {
            // New entry, make sure there are enough texture units
            if (m_textures.size() + 1 >= getMaxTextureUnits())
            {
                err() << "Impossible to use texture " << std::quoted(name)
                      << " for shader: all available texture units are used" << std::endl;
                return;
            }

            m_textures[location] = &texture;
        }
        else
        {
            // Location already used, just replace the texture
            it->second = &texture;
        }
    }
#else
    (void)name;
    (void)texture;
#endif
}


////////////////////////////////////////////////////////////
void Shader::setUniform(const std::string& name, CurrentTextureType)
{
#ifdef SFML_RENDER_DIRECTX
    if (m_directXProgram)
    {
        m_directXProgram->setSampler(name, nullptr, true);
        return;
    }
#endif
#ifdef SFML_RENDER_METAL
    if (m_metalProgram)
    {
        m_metalProgram->setSampler(name, nullptr, true);
        return;
    }
#endif
#ifdef SFML_RENDER_VULKAN
    if (m_vulkanProgram)
    {
        m_vulkanProgram->setSampler(name, nullptr, true);
        return;
    }
#endif
#ifdef SFML_RENDER_GL
    if (!m_shaderProgram)
        return;

    const TransientContextLock lock;

    // Find the location of the variable in the shader
    m_currentTexture = getUniformLocation(name);
#else
    (void)name;
#endif
}


////////////////////////////////////////////////////////////
void Shader::setUniformArray(const std::string& name, const float* values, std::size_t length)
{
    setUniformData(name, ShaderUniformType::Float, values, length);
}

void Shader::setUniformArray(const std::string& name, const Glsl::Vec2* values, std::size_t length)
{
    const auto packed = flatten(values, length);
    setUniformData(name, ShaderUniformType::Vec2, packed.data(), length);
}

void Shader::setUniformArray(const std::string& name, const Glsl::Vec3* values, std::size_t length)
{
    const auto packed = flatten(values, length);
    setUniformData(name, ShaderUniformType::Vec3, packed.data(), length);
}

void Shader::setUniformArray(const std::string& name, const Glsl::Vec4* values, std::size_t length)
{
    const auto packed = flatten(values, length);
    setUniformData(name, ShaderUniformType::Vec4, packed.data(), length);
}

void Shader::setUniformArray(const std::string& name, const Glsl::Mat3* values, std::size_t length)
{
    std::vector<float> packed;
    packed.reserve(9 * length);
    for (std::size_t index = 0; index < length; ++index)
        packed.insert(packed.end(), values[index].array.begin(), values[index].array.end());
    setUniformData(name, ShaderUniformType::Mat3, packed.data(), length);
}

void Shader::setUniformArray(const std::string& name, const Glsl::Mat4* values, std::size_t length)
{
    std::vector<float> packed;
    packed.reserve(16 * length);
    for (std::size_t index = 0; index < length; ++index)
        packed.insert(packed.end(), values[index].array.begin(), values[index].array.end());
    setUniformData(name, ShaderUniformType::Mat4, packed.data(), length);
}

void Shader::setUniformData(const std::string& name, ShaderUniformType type, const void* values, std::size_t count)
{
    if (!values || !count)
        return;

#ifdef SFML_RENDER_DIRECTX
    if (m_directXProgram)
    {
        m_directXProgram->setUniform(name, type, values, count);
        return;
    }
#endif
#ifdef SFML_RENDER_METAL
    if (m_metalProgram)
    {
        m_metalProgram->setUniform(name, type, values, count);
        return;
    }
#endif
#ifdef SFML_RENDER_VULKAN
    if (m_vulkanProgram)
    {
        m_vulkanProgram->setUniform(name, type, values, count);
        return;
    }
#endif
#ifdef SFML_RENDER_GL
    if (!m_shaderProgram || count > static_cast<std::size_t>(std::numeric_limits<GLsizei>::max()))
        return;

    const UniformBinder binder(*this, name);
    const auto          location = binder.location;
    if (location < 0)
        return;

    const auto  size = static_cast<GLsizei>(count);
    const auto* f    = static_cast<const GLfloat*>(values);
    const auto* i    = static_cast<const GLint*>(values);
    switch (type)
    {
        case ShaderUniformType::Float:
            glCheck(glUniform1fv(location, size, f));
            break;
        case ShaderUniformType::Vec2:
            glCheck(glUniform2fv(location, size, f));
            break;
        case ShaderUniformType::Vec3:
            glCheck(glUniform3fv(location, size, f));
            break;
        case ShaderUniformType::Vec4:
            glCheck(glUniform4fv(location, size, f));
            break;
        case ShaderUniformType::Int:
            glCheck(glUniform1iv(location, size, i));
            break;
        case ShaderUniformType::IVec2:
            glCheck(glUniform2iv(location, size, i));
            break;
        case ShaderUniformType::IVec3:
            glCheck(glUniform3iv(location, size, i));
            break;
        case ShaderUniformType::IVec4:
            glCheck(glUniform4iv(location, size, i));
            break;
        case ShaderUniformType::Mat3:
            glCheck(glUniformMatrix3fv(location, size, GL_FALSE, f));
            break;
        case ShaderUniformType::Mat4:
            glCheck(glUniformMatrix4fv(location, size, GL_FALSE, f));
            break;
    }
#endif
}

unsigned int Shader::getNativeHandle() const
{
    return m_shaderProgram;
}


////////////////////////////////////////////////////////////
void Shader::bind(const Shader* shader)
{
#ifdef SFML_RENDER_GL
    if (const auto* device = priv::getGraphicsDevice(); device && device->glContext)
    {
        const TransientContextLock lock;
        priv::ensureExtensionsInit();
        if (shader && shader->m_shaderProgram)
        {
            glCheck(glUseProgram(shader->m_shaderProgram));
            shader->bindTextures();
            if (shader->m_currentTexture != -1)
                glCheck(glUniform1i(shader->m_currentTexture, 0));
        }
        else
        {
            glCheck(glUseProgram(0));
        }
        return;
    }
#else
    (void)shader;
#endif
    err() << "Shader::bind requires OpenGL" << std::endl;
}

bool Shader::isAvailable()
{
    const auto* device = priv::getGraphicsDevice();
#ifdef SFML_RENDER_DIRECTX
    if (device && device->directX)
        return priv::DirectXProgram::compilerAvailable();
#endif
    return device != nullptr;
}

bool Shader::isGeometryAvailable()
{
    const auto* device = priv::getGraphicsDevice();
    if (!device)
        return false;

#ifdef SFML_RENDER_DIRECTX
    if (device->directX)
        return priv::DirectXProgram::compilerAvailable();
#endif
#ifdef SFML_RENDER_METAL
    if (device->metal)
        return false;
#endif
#ifdef SFML_RENDER_VULKAN
    if (device->vulkan)
        return device->vulkan->context()->features.geometryShader != VK_FALSE;
#endif
#ifdef SFML_RENDER_GL
    const TransientContextLock lock;
    priv::ensureExtensionsInit();
#ifdef SFML_OPENGL_ES
    return SF_GLAD_GL_ES_VERSION_3_2 || SF_GLAD_GL_EXT_geometry_shader;
#else
    return GLEXT_GL_VERSION_3_2 || GLEXT_geometry_shader4;
#endif
#else
    return false;
#endif
}

bool Shader::loadDefaultOpenGL()
{
#ifdef SFML_RENDER_GL
    return compile(builtIn(defaultVertex), {}, builtIn(defaultFragment));
#else
    return false;
#endif
}

bool Shader::compile(std::string_view vertex, std::string_view geometry, std::string_view fragment)
{
    const auto* device = priv::getGraphicsDevice();
    if (!device || (vertex.empty() && geometry.empty() && fragment.empty()))
        return false;

    if (!geometry.empty() && !isGeometryAvailable())
    {
        err() << "Geometry shaders are unavailable on the selected graphics device" << std::endl;
        return false;
    }

#ifdef SFML_RENDER_DIRECTX
    if (device->directX)
    {
        auto shader = priv::DirectXProgram::create(*device->directX, vertex, geometry, fragment);
        if (!shader)
            return false;
        m_directXProgram = std::move(shader);
        return true;
    }
#endif
#ifdef SFML_RENDER_METAL
    if (device->metal)
    {
        auto shader = priv::MetalProgram::create(*device->metal, vertex, fragment);
        if (!shader)
            return false;
        m_metalProgram = std::move(shader);
        return true;
    }
#endif
#ifdef SFML_RENDER_VULKAN
    if (device->vulkan)
    {
        auto shader = priv::VulkanProgram::create(*device->vulkan, vertex, geometry, fragment);
        if (!shader)
            return false;
        m_vulkanProgram = std::move(shader);
        return true;
    }
#endif
#ifdef SFML_RENDER_GL
    const TransientContextLock lock;
    priv::ensureExtensionsInit();

    const bool programmable     = isProgrammable(*device);
    const auto fallbackVertex   = programmable && vertex.empty() ? builtIn(defaultVertex) : std::string{};
    const auto fallbackFragment = programmable && fragment.empty() ? builtIn(defaultFragment) : std::string{};
    if (!fallbackVertex.empty())
        vertex = fallbackVertex;
    if (!fallbackFragment.empty())
        fragment = fallbackFragment;

    const auto program = glCheck(glCreateProgram());
    if (!program)
        return false;

    const auto attach = [program](GLenum stage, std::string_view source)
    {
        if (source.empty())
            return true;
        if (source.size() > static_cast<std::size_t>(std::numeric_limits<GLint>::max()))
            return false;

        const auto shader = glCheck(glCreateShader(stage));
        if (!shader)
            return false;
        const auto* data = source.data();
        const auto  size = static_cast<GLint>(source.size());
        glCheck(glShaderSource(shader, 1, &data, &size));
        glCheck(glCompileShader(shader));

        GLint compiled{};
        glCheck(glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled));
        if (compiled)
        {
            glCheck(glAttachShader(program, shader));
        }
        else
        {
            GLint length{};
            glCheck(glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &length));
            std::vector<char> log(static_cast<std::size_t>(std::max(length, 1)));
            glCheck(glGetShaderInfoLog(shader, length, nullptr, log.data()));
            err() << "Failed to compile OpenGL shader\n" << log.data() << std::endl;
        }
        glCheck(glDeleteShader(shader));
        return compiled != GL_FALSE;
    };

    if (!attach(GL_VERTEX_SHADER, vertex) || !attach(GL_GEOMETRY_SHADER, geometry) || !attach(GL_FRAGMENT_SHADER, fragment))
    {
        glCheck(glDeleteProgram(program));
        return false;
    }

    if (programmable)
    {
        glCheck(glBindAttribLocation(program, 0, "sf_Vertex"));
        glCheck(glBindAttribLocation(program, 1, "sf_Color"));
        glCheck(glBindAttribLocation(program, 2, "sf_MultiTexCoord0"));
    }
    glCheck(glLinkProgram(program));
    GLint linked{};
    glCheck(glGetProgramiv(program, GL_LINK_STATUS, &linked));
    if (!linked)
    {
        GLint length{};
        glCheck(glGetProgramiv(program, GL_INFO_LOG_LENGTH, &length));
        std::vector<char> log(static_cast<std::size_t>(std::max(length, 1)));
        glCheck(glGetProgramInfoLog(program, length, nullptr, log.data()));
        err() << "Failed to link OpenGL shader\n" << log.data() << std::endl;
        glCheck(glDeleteProgram(program));
        return false;
    }

    if (m_shaderProgram)
        glCheck(glDeleteProgram(m_shaderProgram));
    m_glContext      = device->glContext;
    m_shaderProgram  = program;
    m_currentTexture = -1;
    m_textures.clear();
    m_uniforms.clear();
    glCheck(glFlush());
    return true;
#else
    return false;
#endif
}

#ifdef SFML_RENDER_GL
void Shader::bindTextures() const
{
    auto it = m_textures.begin();
    for (std::size_t i = 0; i < m_textures.size(); ++i)
    {
        const auto index = static_cast<GLsizei>(i + 1);
        glCheck(glUniform1i(it->first, index));
        glCheck(GLEXT_glActiveTexture(GLEXT_GL_TEXTURE0 + static_cast<GLenum>(index)));
        Texture::bind(it->second);
        ++it;
    }

    // Make sure that the texture unit which is left active is the number 0
    glCheck(GLEXT_glActiveTexture(GLEXT_GL_TEXTURE0));
}


////////////////////////////////////////////////////////////
int Shader::getUniformLocation(const std::string& name)
{
    // Check the cache
    if (const auto it = m_uniforms.find(name); it != m_uniforms.end())
    {
        // Already in cache, return it
        return it->second;
    }

    // Not in cache, request the location from OpenGL
    const int location = glGetUniformLocation(m_shaderProgram, name.c_str());
    m_uniforms.try_emplace(name, location);

    if (location == -1)
        err() << "Uniform " << std::quoted(name) << " not found in shader" << std::endl;

    return location;
}

#endif
} // namespace sf
