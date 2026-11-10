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
#ifdef SFML_RENDER_GL
#include <SFML/Graphics/GLCheck.hpp>
#include <SFML/Graphics/GLExtensions.hpp>
#include <SFML/Graphics/OpenGL/OpenGLResource.hpp>
#endif
#ifdef SFML_RENDER_VULKAN
#include <SFML/Graphics/Vulkan/VulkanDevice.hpp>
#endif
#include <SFML/Graphics/RenderTarget.hpp>
#include <SFML/Graphics/Vertex.hpp>
#include <SFML/Graphics/VertexBuffer.hpp>

#include <SFML/System/Err.hpp>

#include <limits>
#include <ostream>
#include <utility>

#include <cstddef>
#include <cstring>


#ifdef SFML_RENDER_GL
namespace
{
// A nested named namespace is used here to allow unity builds of SFML.
namespace VertexBufferImpl
{
GLenum usageToGlEnum(sf::VertexBuffer::Usage usage)
{
    switch (usage)
    {
        case sf::VertexBuffer::Usage::Static:
            return GLEXT_GL_STATIC_DRAW;
        case sf::VertexBuffer::Usage::Dynamic:
            return GLEXT_GL_DYNAMIC_DRAW;
        default:
            return GLEXT_GL_STREAM_DRAW;
    }
}
} // namespace VertexBufferImpl
} // namespace
#endif


namespace sf
{
#ifdef SFML_RENDER_GL
using VertexContextLock = priv::OpenGLResource::TransientContextLock;
#endif
////////////////////////////////////////////////////////////
VertexBuffer::VertexBuffer(PrimitiveType type) : m_primitiveType(type)
{
}


////////////////////////////////////////////////////////////
VertexBuffer::VertexBuffer(Usage usage) : m_usage(usage)
{
}


////////////////////////////////////////////////////////////
VertexBuffer::VertexBuffer(PrimitiveType type, Usage usage) : m_primitiveType(type), m_usage(usage)
{
}


////////////////////////////////////////////////////////////
VertexBuffer::VertexBuffer(const VertexBuffer& copy) : m_primitiveType(copy.m_primitiveType), m_usage(copy.m_usage)
{
    if (copy.m_size)
    {
        if (!create(copy.m_size))
        {
            err() << "Could not create vertex buffer for copying" << std::endl;
            return;
        }

        if (!update(copy))
            err() << "Could not copy vertex buffer" << std::endl;
    }
}


////////////////////////////////////////////////////////////
VertexBuffer::~VertexBuffer()
{
#ifdef SFML_RENDER_GL
    if (m_buffer)
    {
        const VertexContextLock contextLock;

        glCheck(GLEXT_glDeleteBuffers(1, &m_buffer));
    }
#endif
}


////////////////////////////////////////////////////////////
bool VertexBuffer::create(std::size_t vertexCount)
{
    const auto* device = priv::getGraphicsDevice();
    if (!device || vertexCount > std::numeric_limits<std::ptrdiff_t>::max() / sizeof(Vertex))
        return false;

#ifdef SFML_RENDER_VULKAN
    if (device->vulkan)
    {
        const auto lock   = device->vulkan->lock();
        auto       buffer = device->vulkan->makeBuffer(vertexCount * sizeof(Vertex), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
        if (!buffer)
            return false;
        m_vulkanBuffer = std::move(buffer);
        m_size         = vertexCount;
        return true;
    }
#endif
#ifdef SFML_RENDER_GL
    const VertexContextLock contextLock;
    priv::ensureExtensionsInit();

    if (!m_buffer)
        glCheck(GLEXT_glGenBuffers(1, &m_buffer));

    if (!m_buffer)
    {
        err() << "Could not create vertex buffer, generation failed" << std::endl;
        return false;
    }

    glCheck(GLEXT_glBindBuffer(GLEXT_GL_ARRAY_BUFFER, m_buffer));
    glCheck(GLEXT_glBufferData(GLEXT_GL_ARRAY_BUFFER,
                               static_cast<GLsizeiptrARB>(sizeof(Vertex) * vertexCount),
                               nullptr,
                               VertexBufferImpl::usageToGlEnum(m_usage)));
    glCheck(GLEXT_glBindBuffer(GLEXT_GL_ARRAY_BUFFER, 0));

    m_glContext = device->glContext;
    m_size      = vertexCount;

    return true;
#else
    return false;
#endif
}


////////////////////////////////////////////////////////////
std::size_t VertexBuffer::getVertexCount() const
{
    return m_size;
}


////////////////////////////////////////////////////////////
bool VertexBuffer::update(const Vertex* vertices)
{
    return update(vertices, m_size, 0);
}


////////////////////////////////////////////////////////////
bool VertexBuffer::update(const Vertex* vertices, std::size_t vertexCount, unsigned int offset)
{
    if (!vertices || (!m_buffer && !m_vulkanBuffer) ||
        vertexCount > std::numeric_limits<std::ptrdiff_t>::max() / sizeof(Vertex) ||
        (offset && (offset > m_size || vertexCount > m_size - offset)))
        return false;

#ifdef SFML_RENDER_VULKAN
    if (m_vulkanBuffer)
    {
        const auto& device = priv::getGraphicsDevice()->vulkan;
        const auto  lock   = device->lock();
        if (!offset && vertexCount >= m_size)
        {
            auto replacement = device->makeBuffer(vertexCount * sizeof(Vertex), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
            if (!replacement)
                return false;
            m_vulkanBuffer = std::move(replacement);
            m_size         = vertexCount;
        }
        else if (m_vulkanBuffer.use_count() > 1 && !device->flush())
        {
            // Earlier draws refer to the old contents of this buffer.
            return false;
        }
        m_vulkanBuffer->write(static_cast<VkDeviceSize>(offset) * sizeof(Vertex), vertices, vertexCount * sizeof(Vertex));
        return true;
    }
#endif
#ifdef SFML_RENDER_GL
    const VertexContextLock contextLock;

    glCheck(GLEXT_glBindBuffer(GLEXT_GL_ARRAY_BUFFER, m_buffer));

    // Check if we need to resize or orphan the buffer
    if (vertexCount >= m_size)
    {
        glCheck(GLEXT_glBufferData(GLEXT_GL_ARRAY_BUFFER,
                                   static_cast<GLsizeiptrARB>(sizeof(Vertex) * vertexCount),
                                   nullptr,
                                   VertexBufferImpl::usageToGlEnum(m_usage)));

        m_size = vertexCount;
    }

    glCheck(GLEXT_glBufferSubData(GLEXT_GL_ARRAY_BUFFER,
                                  static_cast<GLintptrARB>(sizeof(Vertex) * offset),
                                  static_cast<GLsizeiptrARB>(sizeof(Vertex) * vertexCount),
                                  vertices));

    glCheck(GLEXT_glBindBuffer(GLEXT_GL_ARRAY_BUFFER, 0));

    return true;
#else
    return false;
#endif
}


////////////////////////////////////////////////////////////
bool VertexBuffer::update(const VertexBuffer& vertexBuffer)
{
    if (&vertexBuffer == this)
        return m_buffer || m_vulkanBuffer;

#ifdef SFML_RENDER_VULKAN
    if (m_vulkanBuffer && vertexBuffer.m_vulkanBuffer)
    {
        const auto& device = priv::getGraphicsDevice()->vulkan;
        const auto  lock   = device->lock();
        auto replacement = device->makeBuffer(vertexBuffer.m_size * sizeof(Vertex), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
        if (!replacement)
            return false;
        replacement->write(0, vertexBuffer.m_vulkanBuffer->mapped, vertexBuffer.m_size * sizeof(Vertex));
        m_vulkanBuffer = std::move(replacement);
        m_size         = vertexBuffer.m_size;
        return true;
    }
#endif
#ifdef SFML_RENDER_GL
    if (!m_buffer || !vertexBuffer.m_buffer)
        return false;

    const VertexContextLock contextLock;

    // Make sure that extensions are initialized
    priv::ensureExtensionsInit();

    if (GLEXT_copy_buffer)
    {
        if (m_size != vertexBuffer.m_size && !create(vertexBuffer.m_size))
            return false;
        glCheck(GLEXT_glBindBuffer(GLEXT_GL_COPY_READ_BUFFER, vertexBuffer.m_buffer));
        glCheck(GLEXT_glBindBuffer(GLEXT_GL_COPY_WRITE_BUFFER, m_buffer));

        glCheck(GLEXT_glCopyBufferSubData(GLEXT_GL_COPY_READ_BUFFER,
                                          GLEXT_GL_COPY_WRITE_BUFFER,
                                          0,
                                          0,
                                          static_cast<GLsizeiptr>(sizeof(Vertex) * vertexBuffer.m_size)));

        glCheck(GLEXT_glBindBuffer(GLEXT_GL_COPY_WRITE_BUFFER, 0));
        glCheck(GLEXT_glBindBuffer(GLEXT_GL_COPY_READ_BUFFER, 0));

        return true;
    }

#ifndef SFML_OPENGL_ES
    glCheck(GLEXT_glBindBuffer(GLEXT_GL_ARRAY_BUFFER, m_buffer));
    glCheck(GLEXT_glBufferData(GLEXT_GL_ARRAY_BUFFER,
                               static_cast<GLsizeiptrARB>(sizeof(Vertex) * vertexBuffer.m_size),
                               nullptr,
                               VertexBufferImpl::usageToGlEnum(m_usage)));

    void* const destination = glCheck(GLEXT_glMapBuffer(GLEXT_GL_ARRAY_BUFFER, GLEXT_GL_WRITE_ONLY));

    glCheck(GLEXT_glBindBuffer(GLEXT_GL_ARRAY_BUFFER, vertexBuffer.m_buffer));

    const void* const source = glCheck(GLEXT_glMapBuffer(GLEXT_GL_ARRAY_BUFFER, GLEXT_GL_READ_ONLY));

    if (destination && source)
        std::memcpy(destination, source, sizeof(Vertex) * vertexBuffer.m_size);

    const bool sourceResult = source && glCheck(GLEXT_glUnmapBuffer(GLEXT_GL_ARRAY_BUFFER)) == GL_TRUE;

    glCheck(GLEXT_glBindBuffer(GLEXT_GL_ARRAY_BUFFER, m_buffer));

    const bool destinationResult = destination && glCheck(GLEXT_glUnmapBuffer(GLEXT_GL_ARRAY_BUFFER)) == GL_TRUE;

    glCheck(GLEXT_glBindBuffer(GLEXT_GL_ARRAY_BUFFER, 0));

    m_size = vertexBuffer.m_size;
    return sourceResult && destinationResult;
#else
    return false;
#endif
#else
    return false;
#endif
}


////////////////////////////////////////////////////////////
VertexBuffer& VertexBuffer::operator=(const VertexBuffer& right)
{
    VertexBuffer temp(right);

    swap(temp);

    return *this;
}


////////////////////////////////////////////////////////////
void VertexBuffer::swap(VertexBuffer& right) noexcept
{
    std::swap(m_glContext, right.m_glContext);
    std::swap(m_vulkanBuffer, right.m_vulkanBuffer);
    std::swap(m_size, right.m_size);
    std::swap(m_buffer, right.m_buffer);
    std::swap(m_primitiveType, right.m_primitiveType);
    std::swap(m_usage, right.m_usage);
}


////////////////////////////////////////////////////////////
unsigned int VertexBuffer::getNativeHandle() const
{
    return m_buffer;
}


////////////////////////////////////////////////////////////
void VertexBuffer::bind(const VertexBuffer* vertexBuffer)
{
#ifdef SFML_RENDER_GL
    if (const auto* device = priv::getGraphicsDevice(); device && device->glContext)
    {
        const VertexContextLock lock;
        priv::ensureExtensionsInit();
        glCheck(GLEXT_glBindBuffer(GLEXT_GL_ARRAY_BUFFER, vertexBuffer ? vertexBuffer->m_buffer : 0));
        return;
    }
#else
    (void)vertexBuffer;
#endif
    err() << "VertexBuffer::bind requires OpenGL" << std::endl;
}


////////////////////////////////////////////////////////////
void VertexBuffer::setPrimitiveType(PrimitiveType type)
{
    m_primitiveType = type;
}


////////////////////////////////////////////////////////////
PrimitiveType VertexBuffer::getPrimitiveType() const
{
    return m_primitiveType;
}


////////////////////////////////////////////////////////////
void VertexBuffer::setUsage(Usage usage)
{
    m_usage = usage;
}


////////////////////////////////////////////////////////////
VertexBuffer::Usage VertexBuffer::getUsage() const
{
    return m_usage;
}


////////////////////////////////////////////////////////////
bool VertexBuffer::isAvailable()
{
    return priv::getGraphicsDevice() != nullptr;
}


////////////////////////////////////////////////////////////
void VertexBuffer::draw(RenderTarget& target, RenderStates states) const
{
    if (m_size)
        target.draw(*this, 0, m_size, states);
}


////////////////////////////////////////////////////////////
void swap(VertexBuffer& left, VertexBuffer& right) noexcept
{
    left.swap(right);
}

} // namespace sf
