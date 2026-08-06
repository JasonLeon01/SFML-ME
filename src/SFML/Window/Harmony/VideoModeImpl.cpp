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

#include <SFML/Window/Harmony/NativeAppImpl.hpp>
#include <SFML/Window/VideoModeImpl.hpp>


namespace sf::priv
{
std::vector<VideoMode> VideoModeImpl::getFullscreenModes()
{
    // Report only the display mode that is current at this query. Synthesizing
    // a rotated mode would advertise a mode that the XComponent does not own.
    return {getDesktopMode()};
}


VideoMode VideoModeImpl::getDesktopMode()
{
    const auto size = Harmony::getSurfaceSnapshot().size;
    return VideoMode(size.x && size.y ? size : Vector2u(1, 1), 32);
}

} // namespace sf::priv
