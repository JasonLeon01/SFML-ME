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

#include <SFML/Graphics/GraphicsDevice.hpp>
#include <SFML/Graphics/Vulkan/VulkanProgram.hpp>
#include <SFML/Graphics/Vulkan/VulkanRHI.hpp>
#include <SFML/Graphics/Vulkan/VulkanRenderTarget.hpp>
#include <SFML/Graphics/Vulkan/VulkanWindowSurface.hpp>

#include <SFML/Window/WindowImpl.hpp>

#include <cstring>

namespace sf::priv
{
template <>
bool VulkanBufferResource::upload(const void* data, std::size_t size, std::size_t offset)
{
    if (!data || !fits(size, offset))
        return false;
    const auto lock = device->lock();
    if (device->isLost() || (buffer.use_count() > 1 && !device->flush()))
        return false;
    if (size)
        buffer->write(offset, data, size);
    return true;
}
template <>
bool VulkanBufferResource::read(void* data, std::size_t size, std::size_t offset) const
{
    if (!data || !fits(size, offset))
        return false;
    const auto lock = device->lock();
    if (!device->flush())
        return false;
    buffer->invalidate();
    if (size)
        std::memcpy(data, static_cast<const std::byte*>(buffer->mapped) + offset, size);
    return true;
}
template <>
bool VulkanBufferResource::copy(const rhi::Buffer& source)
{
    const auto* other = dynamic_cast<const VulkanBufferResource*>(&source);
    if (!other || other->device != device || other->bytes != bytes)
        return false;
    const auto lock = device->lock();
    if (!device->flush())
        return false;
    if (other != this && bytes)
    {
        other->buffer->invalidate();
        buffer->write(0, other->buffer->mapped, bytes);
    }
    return true;
}
namespace
{
class VulkanRhi final :
    public NativeDeviceResource<VulkanDevice, VulkanImage, VulkanBuffer, VulkanProgram, VulkanRenderTarget>
{
public:
    VulkanRhi(std::shared_ptr<VulkanDevice> value, const VulkanSettings& settings) :
        NativeDeviceResource(std::move(value), GraphicsBackend::Vulkan, settings)
    {
    }
    [[nodiscard]] rhi::Capabilities capabilities() const override
    {
        return {m_native->context()->properties.limits.maxImageDimension2D,
                VulkanRenderTarget::getMaximumAntiAliasingLevel(*m_native),
                true,
                m_native->context()->features.geometryShader != VK_FALSE};
    }
    std::unique_ptr<rhi::WindowSurface> createSurface(const WindowImpl& owner, unsigned int, const ContextSettings& settings) override
    {
        auto surface = VulkanWindowSurface::create(m_native, owner.getNativeHandle(), owner.getSize(), settings);
        return surface ? std::make_unique<NativeSurface<VulkanTextureResource, VulkanWindowSurface>>(std::move(surface))
                       : nullptr;
    }
    bool submit(bool) override
    {
        const auto lock = m_native->lock();
        return m_native->flush();
    }
    bool loadPipelineCache(const void* data, std::size_t size) override
    {
        return m_native->loadPipelineCache(data, size);
    }
    std::optional<std::vector<std::uint8_t>> getPipelineCacheData() override
    {
        return m_native->getPipelineCacheData();
    }
};
} // namespace
std::shared_ptr<rhi::Device> createVulkanRhi(const VulkanSettings& settings)
{
    auto device = VulkanDevice::create(settings);
    return device ? std::make_shared<VulkanRhi>(std::move(device), settings) : nullptr;
}
} // namespace sf::priv
