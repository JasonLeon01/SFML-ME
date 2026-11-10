#include <SFML/Window/ContextSettings.hpp>

#include <catch2/catch_test_macros.hpp>

#include <type_traits>

TEST_CASE("[Window] sf::ContextSettings")
{
    SECTION("Type traits")
    {
        STATIC_CHECK(std::is_trivially_copy_constructible_v<sf::ContextSettings>);
        STATIC_CHECK(std::is_trivially_copy_assignable_v<sf::ContextSettings>);
        STATIC_CHECK(std::is_trivially_move_constructible_v<sf::ContextSettings>);
        STATIC_CHECK(std::is_trivially_move_assignable_v<sf::ContextSettings>);
    }

    SECTION("Construction")
    {
        SECTION("Aggregate initialization -- Nothing")
        {
            constexpr sf::ContextSettings contextSettings;
            STATIC_CHECK(contextSettings.depthBits == 0);
            STATIC_CHECK(contextSettings.stencilBits == 0);
            STATIC_CHECK(contextSettings.antiAliasingLevel == 0);
            STATIC_CHECK(std::holds_alternative<std::monostate>(contextSettings.backend));
            STATIC_CHECK(contextSettings.sRgbCapable == false);
        }

        SECTION("Aggregate initialization -- Everything")
        {
            constexpr sf::ContextSettings contextSettings{1, 1, 2, true, sf::OpenGLSettings{4, 5, sf::OpenGLProfile::Core}};
            STATIC_CHECK(contextSettings.depthBits == 1);
            STATIC_CHECK(contextSettings.stencilBits == 1);
            STATIC_CHECK(contextSettings.antiAliasingLevel == 2);
            constexpr auto gl = std::get<sf::OpenGLSettings>(contextSettings.backend);
            STATIC_CHECK(gl.majorVersion == 4);
            STATIC_CHECK(gl.minorVersion == 5);
            STATIC_CHECK(gl.profile == sf::OpenGLProfile::Core);
            STATIC_CHECK(contextSettings.sRgbCapable == true);
        }
    }
}
