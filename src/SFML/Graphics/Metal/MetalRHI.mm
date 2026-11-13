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
#include <SFML/Graphics/Metal/MetalRHI.hpp>
#include <SFML/Graphics/Metal/MetalRenderTarget.hpp>
#include <SFML/Graphics/Metal/MetalWindowSurface.hpp>

#include <SFML/Window/WindowImpl.hpp>

#include <cstring>

namespace sf::priv
{
template <>
bool MetalBufferResource::upload(const void* data, std::size_t size, std::size_t offset)
{
    if (!data || !fits(size, offset))
        return false;
    const auto lock = device->lock();
    return !size || device->write(*buffer, data, offset, size);
}
template <>
bool MetalBufferResource::copy(const rhi::Buffer& source)
{
    const auto* other = dynamic_cast<const MetalBufferResource*>(&source);
    if (!other || other->device != device || other->bytes != bytes)
        return false;
    const auto lock = device->lock();
    return other == this || !bytes || device->copy(*other->buffer, *buffer, bytes);
}
template <>
bool MetalBufferResource::read(void* data, std::size_t size, std::size_t offset) const
{
    if (!data || !fits(size, offset))
        return false;
    const auto lock = device->lock();
    if (!device->submit(true))
        return false;
    if (size)
        std::memcpy(data, static_cast<const std::byte*>(buffer->buffer.contents) + offset, size);
    return true;
}
namespace
{
class MetalRhi final : public NativeDeviceResource<MetalDevice, MetalImage, MetalBuffer, MetalProgram, MetalRenderTarget>
{
public:
    explicit MetalRhi(std::shared_ptr<MetalDevice> value) :
        NativeDeviceResource(std::move(value), GraphicsBackend::Metal, MetalSettings{})
    {
    }
    [[nodiscard]] rhi::Capabilities capabilities() const override
    {
        return {m_native->maximumTextureSize(), m_native->maximumSamples(), true, false};
    }
    std::unique_ptr<rhi::WindowSurface> createSurface(const WindowImpl& owner, unsigned int, const ContextSettings& settings) override
    {
        auto surface = MetalWindowSurface::create(m_native, owner.getMetalLayer(), owner.getSize(), settings);
        return surface ? std::make_unique<NativeSurface<MetalTextureResource, MetalWindowSurface>>(std::move(surface))
                       : nullptr;
    }
    bool submit(bool wait) override
    {
        return m_native->submit(wait);
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
std::shared_ptr<rhi::Device> createMetalRhi()
{
    auto device = MetalDevice::create();
    return device ? std::make_shared<MetalRhi>(std::move(device)) : nullptr;
}
} // namespace sf::priv
