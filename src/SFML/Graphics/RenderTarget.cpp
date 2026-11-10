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

#include <SFML/Graphics/Drawable.hpp>
#include <SFML/Graphics/GraphicsDevice.hpp>
#include <SFML/Graphics/RenderTargetBackend.hpp>
#include <SFML/Graphics/VertexBuffer.hpp>

#ifdef SFML_RENDER_GL
#include <SFML/Graphics/OpenGL/OpenGLRenderTarget.hpp>
#endif

#include <SFML/System/Err.hpp>

#include <algorithm>
#include <ostream>

#include <cmath>

namespace sf
{
void RenderTarget::clear(Color color)
{
    if (m_backend)
        m_backend->clear(*this, color, {});
}

void RenderTarget::clearStencil(StencilValue stencilValue)
{
    if (m_backend)
        m_backend->clear(*this, {}, stencilValue);
}

void RenderTarget::clear(Color color, StencilValue stencilValue)
{
    if (m_backend)
        m_backend->clear(*this, color, stencilValue);
}

void RenderTarget::setView(const View& view)
{
    m_view = view;
    if (m_backend)
        m_backend->viewChanged();
}


////////////////////////////////////////////////////////////
const View& RenderTarget::getView() const
{
    return m_view;
}


////////////////////////////////////////////////////////////
const View& RenderTarget::getDefaultView() const
{
    return m_defaultView;
}


////////////////////////////////////////////////////////////
IntRect RenderTarget::getViewport(const View& view) const
{
    const auto [width, height] = Vector2f(getSize());
    const FloatRect& viewport  = view.getViewport();

    return IntRect(Rect<long>({std::lround(width * viewport.position.x), std::lround(height * viewport.position.y)},
                              {std::lround(width * viewport.size.x), std::lround(height * viewport.size.y)}));
}


////////////////////////////////////////////////////////////
IntRect RenderTarget::getScissor(const View& view) const
{
    const auto [width, height] = Vector2f(getSize());
    const FloatRect& scissor   = view.getScissor();

    return IntRect(Rect<long>({std::lround(width * scissor.position.x), std::lround(height * scissor.position.y)},
                              {std::lround(width * scissor.size.x), std::lround(height * scissor.size.y)}));
}


////////////////////////////////////////////////////////////
Vector2f RenderTarget::mapPixelToCoords(Vector2i point) const
{
    return mapPixelToCoords(point, getView());
}


////////////////////////////////////////////////////////////
Vector2f RenderTarget::mapPixelToCoords(Vector2i point, const View& view) const
{
    // First, convert from viewport coordinates to homogeneous coordinates
    const FloatRect viewport = FloatRect(getViewport(view));
    const Vector2f
        normalized = Vector2f(-1, 1) +
                     Vector2f(2, -2).componentWiseMul(Vector2f(point) - viewport.position).componentWiseDiv(viewport.size);

    // Then transform by the inverse of the view matrix
    return view.getInverseTransform().transformPoint(normalized);
}


////////////////////////////////////////////////////////////
Vector2i RenderTarget::mapCoordsToPixel(Vector2f point) const
{
    return mapCoordsToPixel(point, getView());
}


////////////////////////////////////////////////////////////
Vector2i RenderTarget::mapCoordsToPixel(Vector2f point, const View& view) const
{
    // First, transform the point by the view matrix
    const Vector2f normalized = view.getTransform().transformPoint(point);

    // Then convert to viewport coordinates
    const FloatRect viewport = FloatRect(getViewport(view));
    return Vector2i(
        (normalized.componentWiseMul({1, -1}) + sf::Vector2f(1, 1)).componentWiseDiv({2, 2}).componentWiseMul(viewport.size) +
        viewport.position);
}


void RenderTarget::draw(const Drawable& drawable, const RenderStates& states)
{
    drawable.draw(*this, states);
}

void RenderTarget::draw(const Vertex* vertices, std::size_t vertexCount, PrimitiveType type, const RenderStates& states)
{
    if (m_backend && vertices && vertexCount)
        m_backend->draw(*this, vertices, vertexCount, type, states);
}

void RenderTarget::draw(const VertexBuffer& vertexBuffer, const RenderStates& states)
{
    draw(vertexBuffer, 0, vertexBuffer.getVertexCount(), states);
}

void RenderTarget::draw(const VertexBuffer& vertexBuffer, std::size_t firstVertex, std::size_t vertexCount, const RenderStates& states)
{
    const auto available = vertexBuffer.getVertexCount();
    if (m_backend && firstVertex < available && vertexCount)
        m_backend->draw(*this, vertexBuffer, firstVertex, std::min(vertexCount, available - firstVertex), states);
}

bool RenderTarget::isSrgb() const
{
    return false;
}

bool RenderTarget::setActive(bool active)
{
    return !m_backend || m_backend->setActive(active);
}

void RenderTarget::pushGLStates()
{
    if (m_backend)
        m_backend->pushGLStates(*this);
}

void RenderTarget::popGLStates()
{
    if (m_backend)
        m_backend->popGLStates(*this);
}

void RenderTarget::resetGLStates()
{
    if (m_backend)
        m_backend->resetGLStates(*this);
}

void RenderTarget::initialize()
{
    m_defaultView = View(FloatRect({0, 0}, Vector2f(getSize())));
    m_view        = m_defaultView;

#ifdef SFML_RENDER_GL
    if (!m_backend)
    {
        if (const auto* device = priv::getGraphicsDevice(); device && device->glContext)
        {
            const auto* gl = std::get_if<OpenGLSettings>(&device->settings);
            m_backend      = std::make_shared<priv::OpenGLRenderTarget>(!gl || gl->profile == OpenGLProfile::Core);
        }
    }
#endif
}

namespace priv
{
void RenderTargetBackend::pushGLStates(RenderTarget&)
{
    err() << "OpenGL state operations require an OpenGL render target" << std::endl;
}

void RenderTargetBackend::popGLStates(RenderTarget& target)
{
    pushGLStates(target);
}

void RenderTargetBackend::resetGLStates(RenderTarget& target)
{
    pushGLStates(target);
}
} // namespace priv
} // namespace sf
