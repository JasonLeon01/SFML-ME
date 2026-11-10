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

#include <SFML/Graphics/RenderTarget.hpp>

#include <optional>

namespace sf::priv
{
class RenderTargetBackend
{
public:
    virtual ~RenderTargetBackend() = default;

    virtual void clear(RenderTarget& target, std::optional<Color> color, std::optional<StencilValue> stencil) = 0;
    virtual void draw(RenderTarget&       target,
                      const Vertex*       vertices,
                      std::size_t         count,
                      PrimitiveType       type,
                      const RenderStates& states)                                                             = 0;
    virtual void draw(RenderTarget&       target,
                      const VertexBuffer& buffer,
                      std::size_t         first,
                      std::size_t         count,
                      const RenderStates& states)                                                             = 0;
    virtual bool setActive(bool active)                                                                       = 0;
    virtual void viewChanged()                                                                                = 0;

    virtual void pushGLStates(RenderTarget& target);
    virtual void popGLStates(RenderTarget& target);
    virtual void resetGLStates(RenderTarget& target);
};
} // namespace sf::priv
