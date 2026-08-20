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
#include <SFML/Window/Harmony/WindowImplHarmony.hpp>

#include <SFML/System/Exception.hpp>

#include <utility>


namespace sf::priv
{
WindowImplHarmony::WindowImplHarmony(WindowHandle handle)
{
    initialize(handle);
}


WindowImplHarmony::WindowImplHarmony(VideoMode, const String&, std::uint32_t, State state, const ContextSettings&)
{
    initialize(nullptr);

    const bool                       fullscreen = state == State::Fullscreen;
    Harmony::HostState&              host       = Harmony::getHostState();
    sf::priv::Harmony::HostCallbacks callbacks;
    {
        const std::lock_guard lock(host.mutex);
        host.windowConfigurationSet = true;
        host.fullscreen             = fullscreen;
        host.keepScreenOn           = true;
        callbacks                   = host.callbacks;
    }

    // Android's native entry point keeps the display awake for the lifetime
    // of the mobile window and applies immersive mode for fullscreen windows.
    // Ask the Stage host to provide the same behavior without blocking this
    // application thread on ArkUI work.
    if (callbacks.configureWindow)
        callbacks.configureWindow(fullscreen, true, callbacks.userData);
}


WindowImplHarmony::~WindowImplHarmony()
{
    if (!m_claimed)
        return;

    sf::priv::Harmony::HostCallbacks callbacks;
    {
        auto&                 host = Harmony::getHostState();
        const std::lock_guard lock(host.mutex);
        if (!host.destroyed && host.windowConfigurationSet)
        {
            host.windowConfigurationSet = false;
            host.fullscreen             = false;
            host.keepScreenOn           = false;
            callbacks                   = host.callbacks;
        }
    }

    if (callbacks.configureWindow)
        callbacks.configureWindow(false, false, callbacks.userData);
    Harmony::releaseWindow();
}


void WindowImplHarmony::initialize(WindowHandle requestedHandle)
{
    if (!Harmony::claimWindow())
        throw Exception("Only one owning SFML window is supported by the Harmony mobile backend");

    m_claimed          = true;
    const auto surface = Harmony::getSurfaceSnapshot();
    if (!surface.window)
    {
        Harmony::releaseWindow();
        m_claimed = false;
        throw Exception("The Harmony XComponent surface is not ready");
    }
    if (requestedHandle && requestedHandle != surface.window)
    {
        Harmony::releaseWindow();
        m_claimed = false;
        throw Exception("A Harmony WindowHandle must refer to the registered XComponent surface");
    }

    m_size                     = surface.size;
    auto&                 host = Harmony::getHostState();
    const std::lock_guard lock(host.mutex);
    m_hasFocus = host.focused;
}


WindowHandle WindowImplHarmony::getNativeHandle() const
{
    return Harmony::getSurfaceSnapshot().window;
}


Vector2i WindowImplHarmony::getPosition() const
{
    return {};
}


void WindowImplHarmony::setPosition(Vector2i)
{
}


Vector2u WindowImplHarmony::getSize() const
{
    return Harmony::getSurfaceSnapshot().size;
}


void WindowImplHarmony::setSize(Vector2u)
{
}


void WindowImplHarmony::setMinimumSize(const std::optional<Vector2u>&)
{
}


void WindowImplHarmony::setMaximumSize(const std::optional<Vector2u>&)
{
}


void WindowImplHarmony::setTitle(const String&)
{
}


void WindowImplHarmony::setIcon(Vector2u, const std::uint8_t*)
{
}


void WindowImplHarmony::setVisible(bool)
{
}


void WindowImplHarmony::setMouseCursorVisible(bool visible)
{
    Harmony::HostState&              host = Harmony::getHostState();
    sf::priv::Harmony::HostCallbacks callbacks;
    {
        const std::lock_guard lock(host.mutex);
        callbacks = host.callbacks;
    }
    if (callbacks.setPointerVisible)
        callbacks.setPointerVisible(visible, callbacks.userData);
}


void WindowImplHarmony::setMouseCursorGrabbed(bool)
{
    // Pointer confinement is unavailable to ordinary mobile applications.
}


void WindowImplHarmony::setMouseCursor(const CursorImpl& cursor)
{
    Harmony::HostState&              host = Harmony::getHostState();
    sf::priv::Harmony::HostCallbacks callbacks;
    {
        const std::lock_guard lock(host.mutex);
        callbacks = host.callbacks;
    }

    if (const auto type = cursor.getSystemType())
    {
        if (callbacks.setSystemPointer)
            callbacks.setSystemPointer(static_cast<unsigned int>(*type), callbacks.userData);
    }
    else if (callbacks.setCustomPointer && !cursor.getPixels().empty())
    {
        callbacks.setCustomPointer(cursor.getPixels().data(), cursor.getSize(), cursor.getHotspot(), callbacks.userData);
    }
}


void WindowImplHarmony::setKeyRepeatEnabled(bool enabled)
{
    Harmony::HostState&   host = Harmony::getHostState();
    const std::lock_guard lock(host.mutex);
    host.keyRepeatEnabled = enabled;
}


void WindowImplHarmony::requestFocus()
{
    // ArkUI owns focus traversal. XComponent focus must be requested by the host.
}


bool WindowImplHarmony::hasFocus() const
{
    auto&                 host = Harmony::getHostState();
    const std::lock_guard lock(host.mutex);
    return host.focused;
}


void WindowImplHarmony::processEvents()
{
    for (auto& event : Harmony::drainEvents())
    {
        if (const auto* resized = event.getIf<Event::Resized>())
            m_size = resized->size;
        else if (event.is<Event::FocusGained>())
            m_hasFocus = true;
        else if (event.is<Event::FocusLost>())
            m_hasFocus = false;

        pushEvent(event);
    }
}

} // namespace sf::priv
