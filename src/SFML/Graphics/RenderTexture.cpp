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
#include <SFML/Graphics/DirectX/DirectXRenderTarget.hpp>
#endif
#include <SFML/Graphics/RenderTexture.hpp>
#include <SFML/Graphics/RenderTextureImpl.hpp>

#ifdef SFML_RENDER_GL
#include <SFML/Graphics/RenderTextureImplDefault.hpp>
#include <SFML/Graphics/RenderTextureImplFBO.hpp>
#endif
#ifdef SFML_RENDER_VULKAN
#include <SFML/Graphics/Vulkan/VulkanRenderTarget.hpp>
#endif
#ifdef SFML_RENDER_METAL
#include <SFML/Graphics/Metal/MetalRenderTarget.hpp>
#endif

#include <SFML/System/Err.hpp>
#include <SFML/System/Exception.hpp>

#include <memory>
#include <ostream>
#include <utility>


namespace sf
{
////////////////////////////////////////////////////////////
RenderTexture::RenderTexture() = default;


////////////////////////////////////////////////////////////
RenderTexture::RenderTexture(Vector2u size, const ContextSettings& settings)
{
    if (!resize(size, settings))
        throw Exception("Failed to create render texture");
}


////////////////////////////////////////////////////////////
RenderTexture::~RenderTexture() = default;


////////////////////////////////////////////////////////////
RenderTexture::RenderTexture(RenderTexture&&) noexcept = default;


////////////////////////////////////////////////////////////
RenderTexture& RenderTexture::operator=(RenderTexture&&) noexcept = default;


////////////////////////////////////////////////////////////
bool RenderTexture::resize(Vector2u size, const ContextSettings& settings)
{
    return priv::withGraphicsDevice(settings,
                                    [&](const priv::GraphicsDevice& device, const ContextSettings& effective)
                                    {
                                        Texture texture;
                                        texture.setRepeated(m_texture.isRepeated());
                                        if (!texture.resize(size, settings.sRgbCapable))
                                        {
                                            err() << "Failed to create the render texture's color attachment" << std::endl;
                                            return false;
                                        }

#ifdef SFML_RENDER_DIRECTX
                                        if (device.directX)
                                        {
                                            auto backend = priv::DirectXRenderTarget::create(device.directX,
                                                                                             texture.m_directXImage,
                                                                                             effective);
                                            if (!backend)
                                                return false;
                                            m_backend = std::move(backend);
                                            m_texture = std::move(texture);
                                            RenderTarget::initialize();
                                            return true;
                                        }
#endif
#ifdef SFML_RENDER_METAL
                                        if (device.metal)
                                        {
                                            auto backend = priv::MetalRenderTarget::create(device.metal,
                                                                                           texture.m_metalImage,
                                                                                           effective);
                                            if (!backend)
                                                return false;
                                            m_backend = std::move(backend);
                                            m_texture = std::move(texture);
                                            RenderTarget::initialize();
                                            return true;
                                        }
#endif
#ifdef SFML_RENDER_VULKAN
                                        if (device.vulkan)
                                        {
                                            auto backend = priv::VulkanRenderTarget::create(device.vulkan,
                                                                                            texture.m_vulkanImage,
                                                                                            effective);
                                            if (!backend)
                                                return false;
                                            m_backend = std::move(backend);
                                            m_texture = std::move(texture);
                                            RenderTarget::initialize();
                                            return true;
                                        }
#else
        (void)device;
#endif
#ifdef SFML_RENDER_GL
                                        std::unique_ptr<priv::RenderTextureImpl> implementation;
                                        if (priv::RenderTextureImplFBO::isAvailable())
                                        {
                                            implementation          = std::make_unique<priv::RenderTextureImplFBO>();
                                            texture.m_fboAttachment = true;
                                        }
                                        else
                                        {
                                            implementation = std::make_unique<priv::RenderTextureImplDefault>();
                                        }

                                        if (!implementation->create(texture.m_actualSize, texture.m_texture, effective))
                                            return false;

                                        m_impl    = std::move(implementation);
                                        m_texture = std::move(texture);
                                        m_backend.reset();
                                        RenderTarget::initialize();
                                        return true;
#else
        return false;
#endif
                                    });
}


////////////////////////////////////////////////////////////
unsigned int RenderTexture::getMaximumAntiAliasingLevel()
{
    const auto* device = priv::getGraphicsDevice();
    if (!device)
        return 0;

#ifdef SFML_RENDER_DIRECTX
    if (device->directX)
        return priv::DirectXRenderTarget::getMaximumAntiAliasingLevel(*device->directX);
#endif
#ifdef SFML_RENDER_METAL
    if (device->metal)
        return priv::MetalRenderTarget::getMaximumAntiAliasingLevel(*device->metal);
#endif
#ifdef SFML_RENDER_VULKAN
    if (device->vulkan)
        return priv::VulkanRenderTarget::getMaximumAntiAliasingLevel(*device->vulkan);
#endif
#ifdef SFML_RENDER_GL
    if (priv::RenderTextureImplFBO::isAvailable())
        return priv::RenderTextureImplFBO::getMaximumAntiAliasingLevel();
#endif

    return 0;
}


////////////////////////////////////////////////////////////
void RenderTexture::setSmooth(bool smooth)
{
    m_texture.setSmooth(smooth);
}


////////////////////////////////////////////////////////////
bool RenderTexture::isSmooth() const
{
    return m_texture.isSmooth();
}


////////////////////////////////////////////////////////////
void RenderTexture::setRepeated(bool repeated)
{
    m_texture.setRepeated(repeated);
}


////////////////////////////////////////////////////////////
bool RenderTexture::isRepeated() const
{
    return m_texture.isRepeated();
}


////////////////////////////////////////////////////////////
bool RenderTexture::generateMipmap()
{
    return m_texture.generateMipmap();
}


////////////////////////////////////////////////////////////
bool RenderTexture::setActive(bool active)
{
#ifdef SFML_RENDER_DIRECTX
    if (m_texture.m_directXImage)
        return RenderTarget::setActive(active);
#endif
#ifdef SFML_RENDER_METAL
    if (m_texture.m_metalImage)
        return RenderTarget::setActive(active);
#endif
#ifdef SFML_RENDER_VULKAN
    if (m_texture.m_vulkanImage)
        return RenderTarget::setActive(active);
#endif
    if (m_impl && m_impl->activate(active))
        return RenderTarget::setActive(active);

    return false;
}


////////////////////////////////////////////////////////////
void RenderTexture::display()
{
#ifdef SFML_RENDER_DIRECTX
    if (m_texture.m_directXImage)
    {
        static_cast<priv::DirectXRenderTarget&>(*m_backend).finish();
        m_texture.invalidateMipmap();
        return;
    }
#endif
#ifdef SFML_RENDER_METAL
    if (m_texture.m_metalImage)
    {
        static_cast<priv::MetalRenderTarget&>(*m_backend).finish();
        (void)priv::getGraphicsDevice()->metal->submit(false);
        m_texture.invalidateMipmap();
        return;
    }
#endif
#ifdef SFML_RENDER_VULKAN
    if (m_texture.m_vulkanImage)
    {
        static_cast<priv::VulkanRenderTarget&>(*m_backend).finish();
        m_texture.invalidateMipmap();
        return;
    }
#endif
#ifdef SFML_RENDER_GL
    if (!m_impl)
        return;

    if (priv::RenderTextureImplFBO::isAvailable())
    {
        // Perform a RenderTarget-only activation if we are using FBOs
        if (!RenderTarget::setActive())
            return;
    }
    else
    {
        // Perform a full activation if we are not using FBOs
        if (!setActive())
            return;
    }

    // Update the target texture
    m_impl->updateTexture(m_texture.m_texture);
    m_texture.m_pixelsFlipped = true;
    m_texture.invalidateMipmap();
#endif
}


////////////////////////////////////////////////////////////
Vector2u RenderTexture::getSize() const
{
    return m_texture.getSize();
}


////////////////////////////////////////////////////////////
bool RenderTexture::isSrgb() const
{
    return m_impl ? m_impl->isSrgb() : m_texture.isSrgb();
}


////////////////////////////////////////////////////////////
const Texture& RenderTexture::getTexture() const
{
    return m_texture;
}

} // namespace sf
