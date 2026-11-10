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

#include <SFML/Graphics/Export.hpp>

#include <SFML/Window/ContextSettings.hpp>

namespace sf
{
enum class GraphicsBackend
{
    Automatic,
    OpenGL,
    OpenGLES,
    Vulkan,
    DirectX,
    Metal
};

/// Select a graphics backend and optional minimum version before creating GPU resources.
/// Returns false if the backend is unavailable in this build, the settings are invalid,
/// or an already initialized device cannot meet the request.
[[nodiscard]] SFML_GRAPHICS_API bool setGraphicsBackend(GraphicsBackend                         backend,
                                                        const ContextSettings::BackendSettings& settings = {});

/// Return the active backend, or Automatic before the first successful initialization.
[[nodiscard]] SFML_GRAPHICS_API GraphicsBackend getGraphicsBackend();
} // namespace sf
