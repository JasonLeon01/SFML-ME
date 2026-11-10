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
#include <SFML/Graphics/RenderTextureImplFBO.hpp>
#endif
#ifdef SFML_RENDER_VULKAN
#include <SFML/Graphics/Vulkan/VulkanRenderTarget.hpp>
#include <SFML/Graphics/Vulkan/VulkanWindowSurface.hpp>
#endif
#include <SFML/Graphics/Image.hpp>
#include <SFML/Graphics/RenderWindow.hpp>

#include <SFML/Window/VideoMode.hpp>
#include <SFML/Window/WindowImpl.hpp>
#include <SFML/Window/WindowSurface.hpp>


namespace sf
{
////////////////////////////////////////////////////////////
RenderWindow::RenderWindow(VideoMode mode, const String& title, std::uint32_t style, State state, const ContextSettings& settings)
{
    // Don't call the base class constructor because it contains virtual function calls
    create(mode, title, style, state, settings);
}


////////////////////////////////////////////////////////////
RenderWindow::RenderWindow(VideoMode mode, const String& title, State state, const ContextSettings& settings)
{
    // Don't call the base class constructor because it contains virtual function calls
    create(mode, title, sf::Style::Default, state, settings);
}


////////////////////////////////////////////////////////////
RenderWindow::RenderWindow(WindowHandle handle, const ContextSettings& settings)
{
    // Don't call the base class constructor because it contains virtual function calls
    create(handle, settings);
}


////////////////////////////////////////////////////////////
void RenderWindow::create(VideoMode mode, const String& title, std::uint32_t style, State state, const ContextSettings& settings)
{
    (void)priv::withGraphicsDevice(settings,
                                   [&](const priv::GraphicsDevice&, const ContextSettings& effective)
                                   {
                                       Window::create(mode, title, style, state, effective);
                                       return isOpen();
                                   });
}

void RenderWindow::create(WindowHandle handle, const ContextSettings& settings)
{
    (void)priv::withGraphicsDevice(settings,
                                   [&](const priv::GraphicsDevice&, const ContextSettings& effective)
                                   {
                                       Window::create(handle, effective);
                                       return isOpen();
                                   });
}

void RenderWindow::close()
{
    m_backend.reset();
    Window::close();
}

std::unique_ptr<priv::WindowSurface> RenderWindow::createSurface(const ContextSettings&  settings,
                                                                 const priv::WindowImpl& owner,
                                                                 unsigned int            bitsPerPixel)
{
#ifdef SFML_RENDER_DIRECTX
    if (std::holds_alternative<DirectXSettings>(settings.backend))
    {
        const auto* device = priv::getGraphicsDevice();
        return priv::DirectXWindowSurface::create(device->directX, owner.getNativeHandle(), owner.getSize(), settings);
    }
#endif
#ifdef SFML_RENDER_VULKAN
    if (std::holds_alternative<VulkanSettings>(settings.backend))
    {
        const auto* device = priv::getGraphicsDevice();
        return priv::VulkanWindowSurface::create(device->vulkan, owner.getNativeHandle(), owner.getSize(), settings);
    }
#endif
    return Window::createSurface(settings, owner, bitsPerPixel);
}


////////////////////////////////////////////////////////////
Vector2u RenderWindow::getSize() const
{
    return Window::getSize();
}


////////////////////////////////////////////////////////////
void RenderWindow::setIcon(const Image& icon)
{
    setIcon(icon.getSize(), icon.getPixelsPtr());
}


////////////////////////////////////////////////////////////
bool RenderWindow::isSrgb() const
{
    return getSettings().sRgbCapable;
}


////////////////////////////////////////////////////////////
bool RenderWindow::setActive(bool active)
{
    bool result = Window::setActive(active);

    // Update RenderTarget tracking
    if (result)
        result = RenderTarget::setActive(active);

#ifdef SFML_RENDER_GL
    if (!std::holds_alternative<OpenGLSettings>(getSettings().backend) &&
        !std::holds_alternative<OpenGLESSettings>(getSettings().backend))
        return result;

    // If FBOs are available, make sure none are bound when we
    // try to draw to the default framebuffer of the RenderWindow
    if (active && result && priv::RenderTextureImplFBO::isAvailable())
    {
        glCheck(GLEXT_glBindFramebuffer(GLEXT_GL_FRAMEBUFFER, m_defaultFrameBuffer));

        return true;
    }
#endif

    return result;
}


////////////////////////////////////////////////////////////
void RenderWindow::onCreate()
{
#ifdef SFML_RENDER_DIRECTX
    if (auto* surface = dynamic_cast<priv::DirectXWindowSurface*>(getSurface()))
        m_backend = surface->renderTarget();
#endif
#ifdef SFML_RENDER_VULKAN
    if (auto* surface = dynamic_cast<priv::VulkanWindowSurface*>(getSurface()))
        m_backend = surface->renderTarget();
#endif
#ifdef SFML_RENDER_GL
    if (!m_backend && priv::RenderTextureImplFBO::isAvailable())
    {
        // Retrieve the framebuffer ID we have to bind when targeting the window for rendering
        // We assume that this window's context is still active at this point
        glCheck(glGetIntegerv(GLEXT_GL_FRAMEBUFFER_BINDING, reinterpret_cast<GLint*>(&m_defaultFrameBuffer)));
    }

#endif

    // Just initialize the render target part
    RenderTarget::initialize();
}


////////////////////////////////////////////////////////////
void RenderWindow::onResize()
{
#ifdef SFML_RENDER_DIRECTX
    if (auto* surface = dynamic_cast<priv::DirectXWindowSurface*>(getSurface()))
        surface->resize(getSize());
#endif
#ifdef SFML_RENDER_VULKAN
    if (auto* surface = dynamic_cast<priv::VulkanWindowSurface*>(getSurface()))
        surface->resize(getSize());
#endif
    // Update the current view (recompute the viewport, which is stored in relative coordinates)
    setView(getView());
}

} // namespace sf
