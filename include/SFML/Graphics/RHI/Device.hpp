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

#pragma once

#include <SFML/Graphics/Export.hpp>

#include <SFML/Graphics/BlendMode.hpp>
#include <SFML/Graphics/CoordinateType.hpp>
#include <SFML/Graphics/GraphicsBackend.hpp>
#include <SFML/Graphics/Image.hpp>
#include <SFML/Graphics/PrimitiveType.hpp>
#include <SFML/Graphics/StencilMode.hpp>
#include <SFML/Graphics/Transform.hpp>
#include <SFML/Graphics/Vertex.hpp>

#include <array>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <cstddef>
#include <cstdint>

namespace sf
{
class WindowBase;
}

namespace sf::rhi
{
enum class BufferUsage
{
    Stream,
    Dynamic,
    Static
};
enum class UniformType : unsigned char
{
    Float,
    Vec2,
    Vec3,
    Vec4,
    Int,
    IVec2,
    IVec3,
    IVec4,
    Mat3,
    Mat4
};

/// RGBA8 2D texture. Uploads and readbacks use top-left image coordinates.
class SFML_GRAPHICS_API Texture
{
public:
    virtual ~Texture()                                                                                     = default;
    [[nodiscard]] virtual Vector2u getSize() const                                                         = 0;
    [[nodiscard]] virtual bool     isSrgb() const                                                          = 0;
    [[nodiscard]] virtual bool     isSmooth() const                                                        = 0;
    [[nodiscard]] virtual bool     isRepeated() const                                                      = 0;
    [[nodiscard]] virtual bool     hasMipmap() const                                                       = 0;
    virtual void                   setSmooth(bool smooth)                                                  = 0;
    virtual void                   setRepeated(bool repeated)                                              = 0;
    [[nodiscard]] virtual bool     upload(const std::uint8_t* pixels, Vector2u size, Vector2u offset = {}) = 0;
    [[nodiscard]] virtual bool     copy(const Texture& source, Vector2u offset = {})                       = 0;
    [[nodiscard]] virtual Image    read() const                                                            = 0;
    [[nodiscard]] virtual bool     generateMipmap()                                                        = 0;
    virtual void                   invalidateMipmap()                                                      = 0;
    [[nodiscard]] virtual std::array<float, 16> coordinateMatrix(CoordinateType type) const                = 0;
};

/// Byte-addressed vertex storage. Updates never resize the allocation.
class SFML_GRAPHICS_API Buffer
{
public:
    virtual ~Buffer()                                                                                     = default;
    [[nodiscard]] virtual std::size_t getSize() const                                                     = 0;
    [[nodiscard]] virtual bool        upload(const void* data, std::size_t bytes, std::size_t offset = 0) = 0;
    [[nodiscard]] virtual bool        copy(const Buffer& source)                                          = 0;
    [[nodiscard]] virtual bool        read(void* data, std::size_t bytes, std::size_t offset = 0) const   = 0;
};

/// Sources retain each backend's existing format (GLSL, SPIR-V, HLSL or MSL/metallib).
class SFML_GRAPHICS_API ShaderProgram
{
public:
    virtual ~ShaderProgram()                                                                                  = default;
    virtual void setUniform(const std::string& name, UniformType type, const void* values, std::size_t count) = 0;
    virtual void setSampler(const std::string& name, std::shared_ptr<Texture> texture, bool current = false)  = 0;
};

/// Pixel viewport/scissor use a top-left origin; projection uses SFML clip coordinates.
struct TargetState
{
    Vector2u  size;
    IntRect   viewport;
    IntRect   scissor;
    Transform projection;
    bool      sRgb{};
};

struct DrawSettings
{
    BlendMode                      blendMode{BlendAlpha};
    StencilMode                    stencilMode;
    Transform                      transform;
    CoordinateType                 coordinateType{CoordinateType::Pixels};
    std::shared_ptr<Texture>       texture;
    std::shared_ptr<ShaderProgram> shader;
};

/// Commands execute in order. Implementations retain submitted GPU resources as needed.
/// A context must not be recorded concurrently from different threads.
class SFML_GRAPHICS_API CommandContext
{
public:
    virtual ~CommandContext() = default;
    virtual void clear(const TargetState& state, std::optional<Color> color, std::optional<StencilValue> stencil = {}) = 0;
    virtual void draw(const TargetState&  target,
                      const Vertex*       vertices,
                      std::size_t         count,
                      PrimitiveType       type,
                      const DrawSettings& state = {}) = 0;
    virtual void draw(const TargetState&  target,
                      const Buffer&       buffer,
                      std::size_t         first,
                      std::size_t         count,
                      PrimitiveType       type,
                      const DrawSettings& state = {}) = 0;
};

class SFML_GRAPHICS_API RenderTarget
{
public:
    virtual ~RenderTarget()                                             = default;
    [[nodiscard]] virtual Vector2u               getSize() const        = 0;
    [[nodiscard]] virtual const ContextSettings& settings() const       = 0;
    [[nodiscard]] virtual CommandContext&        commands()             = 0;
    [[nodiscard]] virtual bool                   setActive(bool active) = 0;
    /// Resolve color attachments and make subsequent sampling/readback see completed draws.
    virtual void finish() = 0;
};

/// Release this surface and its render-target references before closing,
/// recreating or destroying the associated WindowBase.
/// One surface per native window. Call resize after processing a resize event.
class SFML_GRAPHICS_API WindowSurface
{
public:
    virtual ~WindowSurface()                                                                 = default;
    [[nodiscard]] virtual std::shared_ptr<RenderTarget> getRenderTarget() const              = 0;
    [[nodiscard]] virtual bool                          setActive(bool active)               = 0;
    virtual void                                        resize(Vector2u size)                = 0;
    virtual void                                        display()                            = 0;
    virtual void                                        setVerticalSyncEnabled(bool enabled) = 0;
    [[nodiscard]] virtual bool                          copyTo(Texture& destination, Vector2u offset = {}) const = 0;
};

struct Capabilities
{
    unsigned int maximumTextureSize{};
    unsigned int maximumAntiAliasingLevel{};
    bool         shaders{};
    bool         geometryShaders{};
};

class SFML_GRAPHICS_API Device
{
public:
    virtual ~Device()                                                                                      = default;
    [[nodiscard]] virtual GraphicsBackend                  backend() const                                 = 0;
    [[nodiscard]] virtual ContextSettings::BackendSettings settings() const                                = 0;
    [[nodiscard]] virtual Capabilities                     capabilities() const                            = 0;
    [[nodiscard]] virtual std::shared_ptr<Texture>         createTexture(Vector2u size, bool sRgb = false) = 0;
    [[nodiscard]] virtual std::shared_ptr<Buffer> createBuffer(std::size_t bytes, BufferUsage usage = BufferUsage::Stream) = 0;
    [[nodiscard]] virtual std::shared_ptr<ShaderProgram> createShader(std::string_view vertex,
                                                                      std::string_view geometry,
                                                                      std::string_view fragment)                  = 0;
    [[nodiscard]] virtual std::shared_ptr<RenderTarget>  createRenderTarget(std::shared_ptr<Texture> color,
                                                                            const ContextSettings&   settings = {}) = 0;
    [[nodiscard]] std::unique_ptr<WindowSurface> createWindowSurface(WindowBase& window, const ContextSettings& settings = {});
    [[nodiscard]] virtual bool submit(bool wait = false)                                  = 0;
    [[nodiscard]] virtual bool loadPipelineCache(const void* data, std::size_t size)      = 0;
    [[nodiscard]] virtual std::optional<std::vector<std::uint8_t>> getPipelineCacheData() = 0;
};

/// Acquire SFML's process-wide device, selected with setGraphicsBackend before first use.
/// A successful acquisition commits the backend, as does creating an SFML GPU resource.
[[nodiscard]] SFML_GRAPHICS_API std::shared_ptr<Device> getDevice();
} // namespace sf::rhi
