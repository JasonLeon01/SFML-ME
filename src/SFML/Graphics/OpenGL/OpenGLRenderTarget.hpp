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

#include <SFML/Graphics/RenderTargetBackend.hpp>

#include <array>
#include <memory>

namespace sf::priv
{
class OpenGLRenderTarget final : public RenderTargetBackend
{
public:
    explicit OpenGLRenderTarget(bool programmable);
    ~OpenGLRenderTarget() override;

    void clear(RenderTarget& target, std::optional<Color> color, std::optional<StencilValue> stencil) override;
    void draw(RenderTarget& target, const Vertex* vertices, std::size_t count, PrimitiveType type, const RenderStates& states) override;
    void draw(RenderTarget&       target,
              const VertexBuffer& buffer,
              std::size_t         first,
              std::size_t         count,
              const RenderStates& states) override;
    bool setActive(bool active) override;
    void viewChanged() override;
    void pushGLStates(RenderTarget& target) override;
    void popGLStates(RenderTarget& target) override;
    void resetGLStates(RenderTarget& target) override;

private:
    void applyCurrentView(RenderTarget& target);
    void applyBlendMode(const BlendMode& mode);
    void applyStencilMode(const StencilMode& mode);
    void applyTransform(const Transform& transform);
    void applyTexture(const Texture* texture, CoordinateType coordinateType = CoordinateType::Pixels);
    void setupDraw(RenderTarget& target, bool useVertexCache, const RenderStates& states);
    void drawPrimitives(PrimitiveType type, std::size_t first, std::size_t count);
    void cleanupDraw(const RenderStates& states);
    void drawProgrammable(RenderTarget&       target,
                          const Vertex*       vertices,
                          unsigned int        buffer,
                          std::size_t         first,
                          std::size_t         count,
                          PrimitiveType       type,
                          const RenderStates& states);

    struct StatesCache
    {
        bool                  enable{};                //!< Is the cache enabled?
        bool                  glStatesSet{};           //!< Are our internal GL states set yet?
        bool                  viewChanged{};           //!< Has the current view changed since last draw?
        bool                  scissorEnabled{};        //!< Is scissor testing enabled?
        bool                  stencilEnabled{};        //!< Is stencil testing enabled?
        BlendMode             lastBlendMode;           //!< Cached blending mode
        StencilMode           lastStencilMode;         //!< Cached stencil
        std::uint64_t         lastTextureId{};         //!< Cached texture
        CoordinateType        lastCoordinateType{};    //!< Texture coordinate type
        bool                  texCoordsArrayEnabled{}; //!< Is `GL_TEXTURE_COORD_ARRAY` client state enabled?
        bool                  useVertexCache{};        //!< Did we previously use the vertex cache?
        std::array<Vertex, 4> vertexCache{};           //!< Pre-transformed vertices cache
    };


    struct ProgrammableState;
    std::unique_ptr<ProgrammableState> m_modern;
    StatesCache                        m_cache;
    std::uint64_t                      m_id;
};
} // namespace sf::priv
