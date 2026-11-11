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

#include <SFML/Graphics/Metal/MetalDevice.hpp>
#include <SFML/Graphics/RenderTargetBackend.hpp>

#include <SFML/Window/ContextSettings.hpp>

namespace sf::priv
{
class MetalRenderTarget final : public RenderTargetBackend
{
public:
    struct Impl;

    [[nodiscard]] static std::shared_ptr<MetalRenderTarget> create(std::shared_ptr<MetalDevice> device,
                                                                   std::shared_ptr<MetalImage>  image,
                                                                   const ContextSettings&       settings);
    ~MetalRenderTarget() override;

    void clear(RenderTarget& target, std::optional<Color> color, std::optional<StencilValue> stencil) override;
    void draw(RenderTarget& target, const Vertex* vertices, std::size_t count, PrimitiveType type, const RenderStates& states) override;
    void                                             draw(RenderTarget&       target,
                                                          const VertexBuffer& buffer,
                                                          std::size_t         first,
                                                          std::size_t         count,
                                                          const RenderStates& states) override;
    bool                                             setActive(bool active) override;
    void                                             viewChanged() override;
    [[nodiscard]] bool                               resize(Vector2u size);
    void                                             finish();
    [[nodiscard]] const ContextSettings&             settings() const;
    [[nodiscard]] const std::shared_ptr<MetalImage>& image() const;
    [[nodiscard]] static unsigned int                getMaximumAntiAliasingLevel(const MetalDevice& device);

private:
    MetalRenderTarget(std::shared_ptr<MetalDevice> device, const ContextSettings& settings);
    [[nodiscard]] bool    attach(std::shared_ptr<MetalImage> image);
    void                  drawVertices(RenderTarget&       target,
                                       const Vertex*       vertices,
                                       const VertexBuffer* buffer,
                                       std::size_t         first,
                                       std::size_t         count,
                                       PrimitiveType       type,
                                       const RenderStates& states,
                                       bool                clearing);
    std::unique_ptr<Impl> m_impl;
};
} // namespace sf::priv
