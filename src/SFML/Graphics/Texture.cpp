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
#include <SFML/Graphics/DirectX/DirectXWindowSurface.hpp>
#endif

#ifdef SFML_RENDER_GL
#include <SFML/Graphics/GLCheck.hpp>
#include <SFML/Graphics/GLExtensions.hpp>
#include <SFML/Graphics/OpenGL/OpenGLResource.hpp>
#include <SFML/Graphics/TextureSaver.hpp>
#endif
#ifdef SFML_RENDER_VULKAN
#include <SFML/Graphics/Vulkan/VulkanWindowSurface.hpp>
#endif

#include <SFML/Graphics/Image.hpp>
#include <SFML/Graphics/Texture.hpp>

#include <SFML/Window/Context.hpp>
#include <SFML/Window/Window.hpp>

#include <SFML/System/Err.hpp>
#include <SFML/System/Exception.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <ostream>
#include <utility>

#include <cassert>
#include <cstring>


namespace
{
// A nested named namespace is used here to allow unity builds of SFML.
namespace TextureImpl
{
// Thread-safe unique identifier generator,
// is used for states cache (see RenderTarget)
std::uint64_t getUniqueId() noexcept
{
    static std::atomic<std::uint64_t> id(1); // start at 1, zero is "no texture"

    return id.fetch_add(1);
}
} // namespace TextureImpl
} // namespace


namespace sf
{
#ifdef SFML_RENDER_GL
using TextureContextLock = priv::OpenGLResource::TransientContextLock;
#endif
////////////////////////////////////////////////////////////
Texture::Texture() : m_cacheId(TextureImpl::getUniqueId())
{
}


////////////////////////////////////////////////////////////
Texture::Texture(const std::filesystem::path& filename, bool sRgb) : Texture()
{
    if (!loadFromFile(filename, sRgb))
        throw Exception("Failed to load texture from file");
}


////////////////////////////////////////////////////////////
Texture::Texture(const std::filesystem::path& filename, bool sRgb, const IntRect& area) : Texture()
{
    if (!loadFromFile(filename, sRgb, area))
        throw Exception("Failed to load texture from file");
}


////////////////////////////////////////////////////////////
Texture::Texture(const void* data, std::size_t size, bool sRgb) : Texture()
{
    if (!loadFromMemory(data, size, sRgb))
        throw Exception("Failed to load texture from memory");
}


////////////////////////////////////////////////////////////
Texture::Texture(const void* data, std::size_t size, bool sRgb, const IntRect& area) : Texture()
{
    if (!loadFromMemory(data, size, sRgb, area))
        throw Exception("Failed to load texture from memory");
}


////////////////////////////////////////////////////////////
Texture::Texture(InputStream& stream, bool sRgb) : Texture()
{
    if (!loadFromStream(stream, sRgb))
        throw Exception("Failed to load texture from stream");
}


////////////////////////////////////////////////////////////
Texture::Texture(InputStream& stream, bool sRgb, const IntRect& area) : Texture()
{
    if (!loadFromStream(stream, sRgb, area))
        throw Exception("Failed to load texture from stream");
}


////////////////////////////////////////////////////////////
Texture::Texture(const Image& image, bool sRgb) : Texture()
{
    if (!loadFromImage(image, sRgb))
        throw Exception("Failed to load texture from image");
}


////////////////////////////////////////////////////////////
Texture::Texture(const Image& image, bool sRgb, const IntRect& area) : Texture()
{
    if (!loadFromImage(image, sRgb, area))
        throw Exception("Failed to load texture from image");
}


////////////////////////////////////////////////////////////
Texture::Texture(Vector2u size, bool sRgb) : Texture()
{
    if (!resize(size, sRgb))
        throw Exception("Failed to create texture");
}


////////////////////////////////////////////////////////////
Texture::Texture(const Texture& copy) :
    m_isSmooth(copy.m_isSmooth),
    m_sRgb(copy.m_sRgb),
    m_isRepeated(copy.m_isRepeated),
    m_cacheId(TextureImpl::getUniqueId())
{
    if (!copy.m_texture && !copy.m_vulkanImage && !copy.m_directXImage)
    {
        return;
    }

    if (resize(copy.getSize(), copy.isSrgb()))
    {
        update(copy);
    }
    else
    {
        err() << "Failed to copy texture, failed to resize texture" << std::endl;
    }
}


////////////////////////////////////////////////////////////
Texture::~Texture()
{
#ifdef SFML_RENDER_GL
    if (m_texture)
    {
        const TextureContextLock lock;
        const GLuint             texture = m_texture;
        glCheck(glDeleteTextures(1, &texture));
    }
#endif

#ifndef NDEBUG
    // Set m_texture and m_cacheId to an invalid value to help the assert and glIsTexture in bind detect trying
    // to bind this texture in cases where it has already been destroyed but its memory not yet deallocated
    m_texture = 0xFFFFFFFFu;
    m_cacheId = 0xFFFFFFFFFFFFFFFFull;
#endif
}

////////////////////////////////////////////////////////////
Texture::Texture(Texture&& right) noexcept :
    m_glContext(std::move(right.m_glContext)),
    m_vulkanImage(std::move(right.m_vulkanImage)),
    m_directXImage(std::move(right.m_directXImage)),
    m_size(std::exchange(right.m_size, {})),
    m_actualSize(std::exchange(right.m_actualSize, {})),
    m_texture(std::exchange(right.m_texture, 0)),
    m_isSmooth(std::exchange(right.m_isSmooth, false)),
    m_sRgb(std::exchange(right.m_sRgb, false)),
    m_isRepeated(std::exchange(right.m_isRepeated, false)),
    m_pixelsFlipped(std::exchange(right.m_pixelsFlipped, false)),
    m_fboAttachment(std::exchange(right.m_fboAttachment, false)),
    m_hasMipmap(std::exchange(right.m_hasMipmap, false)),
    m_cacheId(std::exchange(right.m_cacheId, 0))
{
}

////////////////////////////////////////////////////////////
Texture& Texture::operator=(Texture&& right) noexcept
{
    // Catch self-moving.
    if (&right == this)
    {
        return *this;
    }

#ifdef SFML_RENDER_GL
    if (m_texture)
    {
        const TextureContextLock lock;
        const GLuint             texture = m_texture;
        glCheck(glDeleteTextures(1, &texture));
    }
#endif

    m_glContext    = std::move(right.m_glContext);
    m_vulkanImage  = std::move(right.m_vulkanImage);
    m_directXImage = std::move(right.m_directXImage);

    // Move old to new.
    m_size          = std::exchange(right.m_size, {});
    m_actualSize    = std::exchange(right.m_actualSize, {});
    m_texture       = std::exchange(right.m_texture, 0);
    m_isSmooth      = std::exchange(right.m_isSmooth, false);
    m_sRgb          = std::exchange(right.m_sRgb, false);
    m_isRepeated    = std::exchange(right.m_isRepeated, false);
    m_pixelsFlipped = std::exchange(right.m_pixelsFlipped, false);
    m_fboAttachment = std::exchange(right.m_fboAttachment, false);
    m_hasMipmap     = std::exchange(right.m_hasMipmap, false);
    m_cacheId       = std::exchange(right.m_cacheId, 0);
    return *this;
}


////////////////////////////////////////////////////////////
bool Texture::resize(Vector2u size, bool sRgb)
{
    // Check if texture parameters are valid before creating it
    if ((size.x == 0) || (size.y == 0))
    {
        err() << "Failed to resize texture, invalid size (" << size.x << "x" << size.y << ")" << std::endl;
        return false;
    }

    const auto* device = priv::getGraphicsDevice();
    if (!device)
        return false;

#ifdef SFML_RENDER_DIRECTX
    if (device->directX)
    {
        auto image = device->directX->makeTexture(size, sRgb);
        if (!image)
            return false;
        m_directXImage = std::move(image);
        m_size = m_actualSize = size;
        m_sRgb                = sRgb;
        m_pixelsFlipped = m_fboAttachment = m_hasMipmap = false;
        m_cacheId                                       = TextureImpl::getUniqueId();
        return true;
    }
#endif
#ifdef SFML_RENDER_VULKAN
    if (device->vulkan)
    {
        const auto lock  = device->vulkan->lock();
        const auto limit = device->vulkan->context()->properties.limits.maxImageDimension2D;
        if (size.x > limit || size.y > limit)
            return false;

        auto image = device->vulkan->makeTexture(size, sRgb);
        if (!image)
            return false;

        m_vulkanImage = std::move(image);
        m_size = m_actualSize = size;
        m_sRgb                = sRgb;
        m_pixelsFlipped       = false;
        m_fboAttachment       = false;
        m_hasMipmap           = false;
        m_cacheId             = TextureImpl::getUniqueId();
        return true;
    }
#endif
#ifdef SFML_RENDER_GL
    const TextureContextLock lock;

    // Make sure that extensions are initialized
    priv::ensureExtensionsInit();

    // Compute the internal texture dimensions depending on NPOT textures support
    const Vector2u actualSize(getValidSize(size.x), getValidSize(size.y));

    // Check the maximum texture size
    const unsigned int maxSize = getMaximumSize();
    if ((actualSize.x > maxSize) || (actualSize.y > maxSize))
    {
        err() << "Failed to create texture, its internal size is too high "
              << "(" << actualSize.x << "x" << actualSize.y << ", "
              << "maximum is " << maxSize << "x" << maxSize << ")" << std::endl;
        return false;
    }

    // All the validity checks passed, we can store the new texture settings
    m_size          = size;
    m_actualSize    = actualSize;
    m_pixelsFlipped = false;
    m_fboAttachment = false;

    m_glContext = device->glContext;

    // Create the OpenGL texture if it doesn't exist yet
    if (!m_texture)
    {
        GLuint texture = 0;
        glCheck(glGenTextures(1, &texture));
        m_texture = texture;
    }

    // Make sure that the current texture binding will be preserved
    const priv::TextureSaver save;

    static const bool textureEdgeClamp = GLEXT_texture_edge_clamp || GLEXT_GL_VERSION_1_2 ||
                                         Context::isExtensionAvailable("GL_EXT_texture_edge_clamp");

    if (!textureEdgeClamp)
    {
        static bool warned = false;

        if (!warned)
        {
            err() << "OpenGL extension SGIS_texture_edge_clamp unavailable" << '\n'
                  << "Artifacts may occur along texture edges" << '\n'
                  << "Ensure that hardware acceleration is enabled if available" << std::endl;

            warned = true;
        }
    }

    static const bool textureSrgb = GLEXT_texture_sRGB;

    m_sRgb = sRgb;

    if (m_sRgb && !textureSrgb)
    {
        static bool warned = false;

        if (!warned)
        {
#ifndef SFML_OPENGL_ES
            err() << "OpenGL extension EXT_texture_sRGB unavailable" << '\n';
#else
            err() << "OpenGL ES extension EXT_sRGB unavailable" << '\n';
#endif
            err() << "Automatic sRGB to linear conversion disabled" << std::endl;

            warned = true;
        }

        m_sRgb = false;
    }

#ifndef SFML_OPENGL_ES
    const GLint textureWrapParam = m_isRepeated ? GL_REPEAT : (textureEdgeClamp ? GLEXT_GL_CLAMP_TO_EDGE : GLEXT_GL_CLAMP);
#else
    const GLint textureWrapParam = m_isRepeated ? GL_REPEAT : GLEXT_GL_CLAMP_TO_EDGE;
#endif

    // Initialize the texture
    glCheck(glBindTexture(GL_TEXTURE_2D, m_texture));
    glCheck(glTexImage2D(GL_TEXTURE_2D,
                         0,
                         (m_sRgb ? GLEXT_GL_SRGB8_ALPHA8 : GL_RGBA),
                         static_cast<GLsizei>(m_actualSize.x),
                         static_cast<GLsizei>(m_actualSize.y),
                         0,
                         GL_RGBA,
                         GL_UNSIGNED_BYTE,
                         nullptr));
    glCheck(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, textureWrapParam));
    glCheck(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, textureWrapParam));
    glCheck(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, m_isSmooth ? GL_LINEAR : GL_NEAREST));
    glCheck(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, m_isSmooth ? GL_LINEAR : GL_NEAREST));
    m_cacheId = TextureImpl::getUniqueId();

    m_hasMipmap = false;

    return true;
#else
    (void)sRgb;
    return false;
#endif
}


////////////////////////////////////////////////////////////
bool Texture::loadFromFile(const std::filesystem::path& filename, bool sRgb, const IntRect& area)
{
    Image image;
    return image.loadFromFile(filename) && loadFromImage(image, sRgb, area);
}


////////////////////////////////////////////////////////////
bool Texture::loadFromMemory(const void* data, std::size_t size, bool sRgb, const IntRect& area)
{
    Image image;
    return image.loadFromMemory(data, size) && loadFromImage(image, sRgb, area);
}


////////////////////////////////////////////////////////////
bool Texture::loadFromStream(InputStream& stream, bool sRgb, const IntRect& area)
{
    Image image;
    return image.loadFromStream(stream) && loadFromImage(image, sRgb, area);
}


////////////////////////////////////////////////////////////
bool Texture::loadFromImage(const Image& image, bool sRgb, const IntRect& area)
{
    // Retrieve the image size
    const auto size = Vector2i(image.getSize());

    // Load the entire image if the source area is either empty or contains the whole image
    if (area.size.x == 0 || (area.size.y == 0) ||
        ((area.position.x <= 0) && (area.position.y <= 0) && (area.size.x >= size.x) && (area.size.y >= size.y)))
    {
        // Load the entire image
        return resize(image.getSize(), sRgb) && upload(image.getPixelsPtr(), image.getSize(), {});
    }

    // Load a sub-area of the image
    assert(area.size.x > 0 && "Area size x cannot be negative");
    assert(area.size.y > 0 && "Area size y cannot be negative");
    assert(area.position.x < size.x && "Area position x is out of image bounds");
    assert(area.position.y < size.y && "Area position y is out of image bounds");

    // Adjust the rectangle to the size of the image
    IntRect rectangle    = area;
    rectangle.position.x = std::max(rectangle.position.x, 0);
    rectangle.position.y = std::max(rectangle.position.y, 0);
    rectangle.size.x     = std::min(rectangle.size.x, size.x - rectangle.position.x);
    rectangle.size.y     = std::min(rectangle.size.y, size.y - rectangle.position.y);

    const auto croppedSize = Vector2u(rectangle.size);
    if (!resize(croppedSize, sRgb))
        return false;

    const auto                stride = static_cast<std::size_t>(croppedSize.x) * 4;
    std::vector<std::uint8_t> cropped(stride * croppedSize.y);
    const auto*               src = image.getPixelsPtr() + 4 * (rectangle.position.x + size.x * rectangle.position.y);
    for (unsigned int y = 0; y < croppedSize.y; ++y)
    {
        std::memcpy(cropped.data() + y * stride, src, stride);
        src += 4 * size.x;
    }
    return upload(cropped.data(), croppedSize, {});
}


////////////////////////////////////////////////////////////
Vector2u Texture::getSize() const
{
    return m_size;
}


////////////////////////////////////////////////////////////
Image Texture::copyToImage() const
{
#ifdef SFML_RENDER_DIRECTX
    if (m_directXImage)
        return priv::getGraphicsDevice()->directX->readImage(*m_directXImage);
#endif
#ifdef SFML_RENDER_VULKAN
    if (m_vulkanImage)
    {
        const auto& device = priv::getGraphicsDevice()->vulkan;
        const auto  lock   = device->lock();
        return device->readImage(m_vulkanImage);
    }
#endif
#ifdef SFML_RENDER_GL
    // Easy case: empty texture
    if (!m_texture)
        return {};

    const TextureContextLock lock;

    // Make sure that the current texture binding will be preserved
    const priv::TextureSaver save;

    // Create an array of pixels
    std::vector<std::uint8_t> pixels(m_size.x * m_size.y * 4);

#ifdef SFML_OPENGL_ES

    // OpenGL ES doesn't have the glGetTexImage function, the only way to read
    // from a texture is to bind it to a FBO and use glReadPixels
    GLuint frameBuffer = 0;
    glCheck(GLEXT_glGenFramebuffers(1, &frameBuffer));
    if (frameBuffer)
    {
        GLint previousFrameBuffer = 0;
        glCheck(glGetIntegerv(GLEXT_GL_FRAMEBUFFER_BINDING, &previousFrameBuffer));

        glCheck(GLEXT_glBindFramebuffer(GLEXT_GL_FRAMEBUFFER, frameBuffer));
        glCheck(GLEXT_glFramebufferTexture2D(GLEXT_GL_FRAMEBUFFER, GLEXT_GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_texture, 0));
        glCheck(glReadPixels(0,
                             0,
                             static_cast<GLsizei>(m_size.x),
                             static_cast<GLsizei>(m_size.y),
                             GL_RGBA,
                             GL_UNSIGNED_BYTE,
                             pixels.data()));
        glCheck(GLEXT_glDeleteFramebuffers(1, &frameBuffer));

        glCheck(GLEXT_glBindFramebuffer(GLEXT_GL_FRAMEBUFFER, static_cast<GLuint>(previousFrameBuffer)));

        if (m_pixelsFlipped)
        {
            // Flip the texture vertically
            const auto stride             = static_cast<std::ptrdiff_t>(m_size.x * 4);
            auto       currentRowIterator = pixels.begin();
            auto       nextRowIterator    = pixels.begin() + stride;
            auto       reverseRowIterator = pixels.begin() + (stride * static_cast<std::ptrdiff_t>(m_size.y - 1));
            for (unsigned int i = 0; i < m_size.y / 2; ++i)
            {
                std::swap_ranges(currentRowIterator, nextRowIterator, reverseRowIterator);
                currentRowIterator = nextRowIterator;
                nextRowIterator += stride;
                reverseRowIterator -= stride;
            }
        }
    }

#else

    if ((m_size == m_actualSize) && !m_pixelsFlipped)
    {
        // Texture is not padded nor flipped, we can use a direct copy
        glCheck(glBindTexture(GL_TEXTURE_2D, m_texture));
        glCheck(glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data()));
    }
    else
    {
        // Texture is either padded or flipped, we have to use a slower algorithm

        // All the pixels will first be copied to a temporary array
        std::vector<std::uint8_t> allPixels(m_actualSize.x * m_actualSize.y * 4);
        glCheck(glBindTexture(GL_TEXTURE_2D, m_texture));
        glCheck(glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, allPixels.data()));

        // Then we copy the useful pixels from the temporary array to the final one
        const std::uint8_t* src      = allPixels.data();
        std::uint8_t*       dst      = pixels.data();
        int                 srcPitch = static_cast<int>(m_actualSize.x * 4);
        const unsigned int  dstPitch = m_size.x * 4;

        // Handle the case where source pixels are flipped vertically
        if (m_pixelsFlipped)
        {
            src += static_cast<unsigned int>(srcPitch * static_cast<int>(m_size.y - 1));
            srcPitch = -srcPitch;
        }

        for (unsigned int i = 0; i < m_size.y; ++i)
        {
            std::memcpy(dst, src, dstPitch);
            src += srcPitch;
            dst += dstPitch;
        }
    }

#endif // SFML_OPENGL_ES

    return {m_size, pixels.data()};
#else
    return {};
#endif
}


////////////////////////////////////////////////////////////
void Texture::update(const std::uint8_t* pixels)
{
    // Update the whole texture
    update(pixels, m_size, {0, 0});
}


////////////////////////////////////////////////////////////
void Texture::update(const std::uint8_t* pixels, Vector2u size, Vector2u dest)
{
    if (!pixels || (!m_texture && !m_vulkanImage && !m_directXImage))
        return;

    assert(dest.x + size.x <= m_size.x && "Destination x coordinate is outside of texture");
    assert(dest.y + size.y <= m_size.y && "Destination y coordinate is outside of texture");

    (void)upload(pixels, size, dest);
}

bool Texture::upload(const std::uint8_t* pixels, Vector2u size, Vector2u dest)
{
    if (!pixels || !size.x || !size.y)
        return false;

#ifdef SFML_RENDER_DIRECTX
    if (m_directXImage)
    {
        if (!priv::getGraphicsDevice()->directX->upload(*m_directXImage, pixels, size, dest))
            return false;
        m_hasMipmap = false;
        m_cacheId   = TextureImpl::getUniqueId();
        return true;
    }
#endif
#ifdef SFML_RENDER_VULKAN
    if (m_vulkanImage)
    {
        const auto& device = priv::getGraphicsDevice()->vulkan;
        const auto  lock   = device->lock();
        if (!device->upload(m_vulkanImage, pixels, size, dest))
            return false;
        m_hasMipmap = false;
        m_cacheId   = TextureImpl::getUniqueId();
        return true;
    }
#endif
#ifdef SFML_RENDER_GL
    if (!m_texture)
        return false;

    const TextureContextLock lock;

    // Make sure that the current texture binding will be preserved
    const priv::TextureSaver save;

    // Copy pixels from the given array to the texture
    glCheck(glBindTexture(GL_TEXTURE_2D, m_texture));
    glCheck(glTexSubImage2D(GL_TEXTURE_2D,
                            0,
                            static_cast<GLint>(dest.x),
                            static_cast<GLint>(dest.y),
                            static_cast<GLsizei>(size.x),
                            static_cast<GLsizei>(size.y),
                            GL_RGBA,
                            GL_UNSIGNED_BYTE,
                            pixels));
    glCheck(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, m_isSmooth ? GL_LINEAR : GL_NEAREST));
    m_hasMipmap     = false;
    m_pixelsFlipped = false;
    m_cacheId       = TextureImpl::getUniqueId();

    // Force an OpenGL flush, so that the texture data will appear updated
    // in all contexts immediately (solves problems in multi-threaded apps)
    glCheck(glFlush());
    return true;
#else
    (void)dest;
    return false;
#endif
}


////////////////////////////////////////////////////////////
void Texture::update(const Texture& texture)
{
    // Update the whole texture
    update(texture, {0, 0});
}


////////////////////////////////////////////////////////////
void Texture::update(const Texture& texture, Vector2u dest)
{
    if ((!m_texture && !m_vulkanImage && !m_directXImage) ||
        (!texture.m_texture && !texture.m_vulkanImage && !texture.m_directXImage))
        return;

    assert(dest.x + texture.m_size.x <= m_size.x && "Destination x coordinate is outside of texture");
    assert(dest.y + texture.m_size.y <= m_size.y && "Destination y coordinate is outside of texture");

#ifdef SFML_RENDER_DIRECTX
    if (m_directXImage && texture.m_directXImage)
    {
        if (priv::getGraphicsDevice()->directX->copy(*texture.m_directXImage, *m_directXImage, dest))
        {
            m_hasMipmap = false;
            m_cacheId   = TextureImpl::getUniqueId();
        }
        return;
    }
#endif
#ifdef SFML_RENDER_VULKAN
    if (m_vulkanImage && texture.m_vulkanImage)
    {
        const auto& device = priv::getGraphicsDevice()->vulkan;
        const auto  lock   = device->lock();
        if (device->copy(texture.m_vulkanImage, m_vulkanImage, dest))
        {
            m_hasMipmap = false;
            m_cacheId   = TextureImpl::getUniqueId();
        }
        return;
    }
#endif
#ifdef SFML_RENDER_GL
    if (!m_texture || !texture.m_texture)
        return;


    {
        const TextureContextLock lock;

        // Make sure that extensions are initialized
        priv::ensureExtensionsInit();
    }

    if (GLEXT_framebuffer_object && GLEXT_framebuffer_blit)
    {
        const TextureContextLock lock;

        // Save the current bindings so we can restore them after we are done
        GLint readFramebuffer = 0;
        GLint drawFramebuffer = 0;

        glCheck(glGetIntegerv(GLEXT_GL_READ_FRAMEBUFFER_BINDING, &readFramebuffer));
        glCheck(glGetIntegerv(GLEXT_GL_DRAW_FRAMEBUFFER_BINDING, &drawFramebuffer));

        // Create the framebuffers
        GLuint sourceFrameBuffer = 0;
        GLuint destFrameBuffer   = 0;
        glCheck(GLEXT_glGenFramebuffers(1, &sourceFrameBuffer));
        glCheck(GLEXT_glGenFramebuffers(1, &destFrameBuffer));

        if (!sourceFrameBuffer || !destFrameBuffer)
        {
            err() << "Cannot copy texture, failed to create a frame buffer object" << std::endl;
            return;
        }

        // Link the source texture to the source frame buffer
        glCheck(GLEXT_glBindFramebuffer(GLEXT_GL_READ_FRAMEBUFFER, sourceFrameBuffer));
        glCheck(GLEXT_glFramebufferTexture2D(GLEXT_GL_READ_FRAMEBUFFER,
                                             GLEXT_GL_COLOR_ATTACHMENT0,
                                             GL_TEXTURE_2D,
                                             texture.m_texture,
                                             0));

        // Link the destination texture to the destination frame buffer
        glCheck(GLEXT_glBindFramebuffer(GLEXT_GL_DRAW_FRAMEBUFFER, destFrameBuffer));
        glCheck(
            GLEXT_glFramebufferTexture2D(GLEXT_GL_DRAW_FRAMEBUFFER, GLEXT_GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_texture, 0));

        // A final check, just to be sure...
        const GLenum sourceStatus = glCheck(GLEXT_glCheckFramebufferStatus(GLEXT_GL_READ_FRAMEBUFFER));

        const GLenum destStatus = glCheck(GLEXT_glCheckFramebufferStatus(GLEXT_GL_DRAW_FRAMEBUFFER));

        if ((sourceStatus == GLEXT_GL_FRAMEBUFFER_COMPLETE) && (destStatus == GLEXT_GL_FRAMEBUFFER_COMPLETE))
        {
            // Scissor testing affects framebuffer blits as well
            // Since we don't want scissor testing to interfere with our copying, we temporarily disable it for the blit if it is enabled
            GLboolean scissorEnabled = GL_FALSE;
            glCheck(glGetBooleanv(GL_SCISSOR_TEST, &scissorEnabled));

            if (scissorEnabled == GL_TRUE)
                glCheck(glDisable(GL_SCISSOR_TEST));

            // Blit the texture contents from the source to the destination texture
            glCheck(GLEXT_glBlitFramebuffer(0,
                                            texture.m_pixelsFlipped ? static_cast<GLint>(texture.m_size.y) : 0,
                                            static_cast<GLint>(texture.m_size.x),
                                            texture.m_pixelsFlipped ? 0 : static_cast<GLint>(texture.m_size.y), // Source rectangle, flip y if source is flipped
                                            static_cast<GLint>(dest.x),
                                            static_cast<GLint>(dest.y),
                                            static_cast<GLint>(dest.x + texture.m_size.x),
                                            static_cast<GLint>(dest.y + texture.m_size.y), // Destination rectangle
                                            GL_COLOR_BUFFER_BIT,
                                            GL_NEAREST));

            // Re-enable scissor testing if it was previously enabled
            if (scissorEnabled == GL_TRUE)
                glCheck(glEnable(GL_SCISSOR_TEST));
        }
        else
        {
            err() << "Cannot copy texture, failed to link texture to frame buffer" << std::endl;
        }

        // Restore previously bound framebuffers
        glCheck(GLEXT_glBindFramebuffer(GLEXT_GL_READ_FRAMEBUFFER, static_cast<GLuint>(readFramebuffer)));
        glCheck(GLEXT_glBindFramebuffer(GLEXT_GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(drawFramebuffer)));

        // Delete the framebuffers
        glCheck(GLEXT_glDeleteFramebuffers(1, &sourceFrameBuffer));
        glCheck(GLEXT_glDeleteFramebuffers(1, &destFrameBuffer));

        // Make sure that the current texture binding will be preserved
        const priv::TextureSaver save;

        // Set the parameters of this texture
        glCheck(glBindTexture(GL_TEXTURE_2D, m_texture));
        glCheck(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, m_isSmooth ? GL_LINEAR : GL_NEAREST));
        m_hasMipmap     = false;
        m_pixelsFlipped = false;
        m_cacheId       = TextureImpl::getUniqueId();

        // Force an OpenGL flush, so that the texture data will appear updated
        // in all contexts immediately (solves problems in multi-threaded apps)
        glCheck(glFlush());

        return;
    }


    update(texture.copyToImage(), dest);
#endif
}


////////////////////////////////////////////////////////////
void Texture::update(const Image& image)
{
    // Update the whole texture
    update(image.getPixelsPtr(), image.getSize(), {0, 0});
}


////////////////////////////////////////////////////////////
void Texture::update(const Image& image, Vector2u dest)
{
    update(image.getPixelsPtr(), image.getSize(), dest);
}


////////////////////////////////////////////////////////////
void Texture::update(const Window& window)
{
    update(window, {0, 0});
}


////////////////////////////////////////////////////////////
void Texture::update(const Window& window, Vector2u dest)
{
    if (!window.isOpen() || (!m_texture && !m_vulkanImage && !m_directXImage))
        return;

    assert(dest.x + window.getSize().x <= m_size.x && "Destination x coordinate is outside of texture");
    assert(dest.y + window.getSize().y <= m_size.y && "Destination y coordinate is outside of texture");

#ifdef SFML_RENDER_DIRECTX
    if (m_directXImage)
    {
        const auto* surface = dynamic_cast<const priv::DirectXWindowSurface*>(window.getSurface());
        if (surface && surface->copyTo(m_directXImage, dest))
        {
            m_hasMipmap = false;
            m_cacheId   = TextureImpl::getUniqueId();
        }
        return;
    }
#endif
#ifdef SFML_RENDER_VULKAN
    if (m_vulkanImage)
    {
        const auto* surface = dynamic_cast<const priv::VulkanWindowSurface*>(window.getSurface());
        if (surface && surface->copyTo(m_vulkanImage, dest))
        {
            m_hasMipmap = false;
            m_cacheId   = TextureImpl::getUniqueId();
        }
        return;
    }
#endif
#ifdef SFML_RENDER_GL
    if (!m_texture || !window.setActive(true))
    {
        return;
    }

    const TextureContextLock lock;

    // Make sure that the current texture binding will be preserved
    const priv::TextureSaver save;

    // Copy pixels from the back-buffer to the texture
    glCheck(glBindTexture(GL_TEXTURE_2D, m_texture));
    glCheck(glCopyTexSubImage2D(GL_TEXTURE_2D,
                                0,
                                static_cast<GLint>(dest.x),
                                static_cast<GLint>(dest.y),
                                0,
                                0,
                                static_cast<GLsizei>(window.getSize().x),
                                static_cast<GLsizei>(window.getSize().y)));
    glCheck(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, m_isSmooth ? GL_LINEAR : GL_NEAREST));
    m_hasMipmap     = false;
    m_pixelsFlipped = true;
    m_cacheId       = TextureImpl::getUniqueId();

    // Force an OpenGL flush, so that the texture will appear updated
    // in all contexts immediately (solves problems in multi-threaded apps)
    glCheck(glFlush());
#endif
}


////////////////////////////////////////////////////////////
void Texture::setSmooth(bool smooth)
{
    if (smooth == m_isSmooth)
    {
        return;
    }

    m_isSmooth = smooth;

#ifdef SFML_RENDER_GL
    if (!m_texture)
    {
        return;
    }

    const TextureContextLock lock;

    // Make sure that the current texture binding will be preserved
    const priv::TextureSaver save;

    glCheck(glBindTexture(GL_TEXTURE_2D, m_texture));
    glCheck(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, m_isSmooth ? GL_LINEAR : GL_NEAREST));

    if (m_hasMipmap)
    {
        glCheck(glTexParameteri(GL_TEXTURE_2D,
                                GL_TEXTURE_MIN_FILTER,
                                m_isSmooth ? GL_LINEAR_MIPMAP_LINEAR : GL_NEAREST_MIPMAP_LINEAR));
    }
    else
    {
        glCheck(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, m_isSmooth ? GL_LINEAR : GL_NEAREST));
    }
#endif
}


////////////////////////////////////////////////////////////
bool Texture::isSmooth() const
{
    return m_isSmooth;
}


////////////////////////////////////////////////////////////
bool Texture::isSrgb() const
{
    return m_sRgb;
}


////////////////////////////////////////////////////////////
void Texture::setRepeated(bool repeated)
{
    if (repeated == m_isRepeated)
    {
        return;
    }

    m_isRepeated = repeated;

#ifdef SFML_RENDER_GL
    if (!m_texture)
    {
        return;
    }

    const TextureContextLock lock;

    // Make sure that the current texture binding will be preserved
    const priv::TextureSaver save;

    static const bool textureEdgeClamp = GLEXT_texture_edge_clamp;

    if (!m_isRepeated && !textureEdgeClamp)
    {
        static bool warned = false;

        if (!warned)
        {
            err() << "OpenGL extension SGIS_texture_edge_clamp unavailable" << '\n'
                  << "Artifacts may occur along texture edges" << '\n'
                  << "Ensure that hardware acceleration is enabled if available" << std::endl;

            warned = true;
        }
    }

#ifndef SFML_OPENGL_ES
    const GLint textureWrapParam = m_isRepeated ? GL_REPEAT : (textureEdgeClamp ? GLEXT_GL_CLAMP_TO_EDGE : GLEXT_GL_CLAMP);
#else
    const GLint textureWrapParam = m_isRepeated ? GL_REPEAT : GLEXT_GL_CLAMP_TO_EDGE;
#endif

    glCheck(glBindTexture(GL_TEXTURE_2D, m_texture));
    glCheck(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, textureWrapParam));
    glCheck(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, textureWrapParam));
#endif
}


////////////////////////////////////////////////////////////
bool Texture::isRepeated() const
{
    return m_isRepeated;
}


////////////////////////////////////////////////////////////
bool Texture::generateMipmap()
{
#ifdef SFML_RENDER_DIRECTX
    if (m_directXImage)
    {
        m_hasMipmap = priv::getGraphicsDevice()->directX->generateMipmap(*m_directXImage);
        return m_hasMipmap;
    }
#endif
#ifdef SFML_RENDER_VULKAN
    if (m_vulkanImage)
    {
        const auto& device = priv::getGraphicsDevice()->vulkan;
        const auto  lock   = device->lock();
        m_hasMipmap        = device->generateMipmap(m_vulkanImage);
        return m_hasMipmap;
    }
#endif
#ifdef SFML_RENDER_GL
    if (!m_texture)
        return false;

    const TextureContextLock lock;

    // Make sure that extensions are initialized
    priv::ensureExtensionsInit();

    if (!GLEXT_framebuffer_object)
        return false;

    // Make sure that the current texture binding will be preserved
    const priv::TextureSaver save;

    glCheck(glBindTexture(GL_TEXTURE_2D, m_texture));
    glCheck(GLEXT_glGenerateMipmap(GL_TEXTURE_2D));
    glCheck(glTexParameteri(GL_TEXTURE_2D,
                            GL_TEXTURE_MIN_FILTER,
                            m_isSmooth ? GL_LINEAR_MIPMAP_LINEAR : GL_NEAREST_MIPMAP_LINEAR));

    m_hasMipmap = true;

    return true;
#else
    return false;
#endif
}


////////////////////////////////////////////////////////////
void Texture::invalidateMipmap()
{
    if (!m_hasMipmap)
        return;

#ifdef SFML_RENDER_GL
    if (m_texture)
    {
        const TextureContextLock lock;

        // Make sure that the current texture binding will be preserved
        const priv::TextureSaver save;

        glCheck(glBindTexture(GL_TEXTURE_2D, m_texture));
        glCheck(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, m_isSmooth ? GL_LINEAR : GL_NEAREST));
    }
#endif

    m_hasMipmap = false;
}


////////////////////////////////////////////////////////////
std::array<float, 16> Texture::coordinateMatrix(const Texture* texture, CoordinateType coordinateType)
{
    std::array matrix{1.f, 0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 0.f, 1.f};
    if (!texture || !texture->m_actualSize.x || !texture->m_actualSize.y)
        return matrix;

    const auto width  = static_cast<float>(texture->m_actualSize.x);
    const auto height = static_cast<float>(texture->m_actualSize.y);
    if (coordinateType == CoordinateType::Pixels)
    {
        matrix[0] = 1.f / width;
        matrix[5] = 1.f / height;
    }
    else
    {
        matrix[0] = static_cast<float>(texture->m_size.x) / width;
        matrix[5] = static_cast<float>(texture->m_size.y) / height;
    }

    if (texture->m_pixelsFlipped)
    {
        matrix[5]  = -matrix[5];
        matrix[13] = static_cast<float>(texture->m_size.y) / height;
    }
    return matrix;
}

void Texture::bind(const Texture* texture, CoordinateType coordinateType)
{
#ifdef SFML_RENDER_GL
    if (const auto* device = priv::getGraphicsDevice(); device && device->glContext)
    {
        const TextureContextLock lock;
        priv::ensureExtensionsInit();
        glCheck(glBindTexture(GL_TEXTURE_2D, texture ? texture->m_texture : 0));
#ifndef SFML_OPENGL_ES
        if (std::get<OpenGLSettings>(device->settings).profile != OpenGLProfile::Core)
        {
            const auto matrix = coordinateMatrix(texture, coordinateType);
            glCheck(glMatrixMode(GL_TEXTURE));
            glCheck(glLoadMatrixf(matrix.data()));
            glCheck(glMatrixMode(GL_MODELVIEW));
        }
#else
        (void)coordinateType;
#endif
        return;
    }
#else
    (void)texture;
    (void)coordinateType;
#endif
    err() << "Texture::bind requires OpenGL" << std::endl;
}


////////////////////////////////////////////////////////////
unsigned int Texture::getMaximumSize()
{
    const auto* device = priv::getGraphicsDevice();
    if (!device)
        return 0;

#ifdef SFML_RENDER_DIRECTX
    if (device->directX)
        return device->directX->maximumTextureSize();
#endif
#ifdef SFML_RENDER_VULKAN
    if (device->vulkan)
        return device->vulkan->context()->properties.limits.maxImageDimension2D;
#endif
#ifdef SFML_RENDER_GL
    static const unsigned int size = []
    {
        const TextureContextLock transientLock;

        GLint value = 0;

        // Make sure that extensions are initialized
        priv::ensureExtensionsInit();

        glCheck(glGetIntegerv(GL_MAX_TEXTURE_SIZE, &value));

        return static_cast<unsigned int>(value);
    }();

    return size;
#else
    return 0;
#endif
}


////////////////////////////////////////////////////////////
Texture& Texture::operator=(const Texture& right)
{
    Texture temp(right);

    swap(temp);

    return *this;
}


////////////////////////////////////////////////////////////
void Texture::swap(Texture& right) noexcept
{
    std::swap(m_glContext, right.m_glContext);
    std::swap(m_vulkanImage, right.m_vulkanImage);
    std::swap(m_directXImage, right.m_directXImage);
    std::swap(m_size, right.m_size);
    std::swap(m_actualSize, right.m_actualSize);
    std::swap(m_texture, right.m_texture);
    std::swap(m_isSmooth, right.m_isSmooth);
    std::swap(m_sRgb, right.m_sRgb);
    std::swap(m_isRepeated, right.m_isRepeated);
    std::swap(m_pixelsFlipped, right.m_pixelsFlipped);
    std::swap(m_fboAttachment, right.m_fboAttachment);
    std::swap(m_hasMipmap, right.m_hasMipmap);
    std::swap(m_cacheId, right.m_cacheId);
}


////////////////////////////////////////////////////////////
unsigned int Texture::getNativeHandle() const
{
    return m_texture;
}


////////////////////////////////////////////////////////////
unsigned int Texture::getValidSize(unsigned int size)
{
#ifdef SFML_RENDER_GL
    if (GLEXT_texture_non_power_of_two)
    {
        // If hardware supports NPOT textures, then just return the unmodified size
        return size;
    }

    // If hardware doesn't support NPOT textures, we calculate the nearest power of two
    unsigned int powerOfTwo = 1;
    while (powerOfTwo < size)
        powerOfTwo *= 2;

    return powerOfTwo;
#else
    return size;
#endif
}


////////////////////////////////////////////////////////////
void swap(Texture& left, Texture& right) noexcept
{
    left.swap(right);
}

} // namespace sf
