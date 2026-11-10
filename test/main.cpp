#include <catch2/catch_session.hpp>

#include <SFML/Main.hpp>

#ifdef SFML_GRAPHICS_TEST_MAIN
#include <SFML/Graphics/GraphicsBackend.hpp>
#include <SFML/Graphics/RenderTexture.hpp>

#include <SFML/Window/Vulkan.hpp>

#include <string_view>
#endif

////////////////////////////////////////////////////////////
int main(int argc, char** argv)
{
#ifdef SFML_GRAPHICS_TEST_MAIN
    if (argc > 1)
    {
        const std::string_view mode(argv[1]);
        if (mode == "--sfml-vulkan" || mode == "--sfml-gl-core" || mode == "--sfml-vulkan-high" ||
            mode == "--sfml-gles-high" || mode == "--sfml-gl-high")
        {
            bool selected = false;
            if (mode == "--sfml-vulkan" || mode == "--sfml-vulkan-high")
            {
                if (!sf::Vulkan::isAvailable())
                    return 77;
                selected = sf::setGraphicsBackend(sf::GraphicsBackend::Vulkan,
                                                  mode == "--sfml-vulkan" ? sf::VulkanSettings{} : sf::VulkanSettings{1, 3});
            }
            else if (mode == "--sfml-gles-high")
            {
                selected = sf::setGraphicsBackend(sf::GraphicsBackend::OpenGLES, sf::OpenGLESSettings{3, 1});
            }
            else if (mode == "--sfml-gl-high")
            {
                selected = sf::setGraphicsBackend(sf::GraphicsBackend::OpenGL,
                                                  sf::OpenGLSettings{4, 5, sf::OpenGLProfile::Compatibility});
            }
            else
            {
                selected = sf::setGraphicsBackend(sf::GraphicsBackend::OpenGL,
                                                  sf::OpenGLSettings{3, 2, sf::OpenGLProfile::Core});
            }
            if (!selected)
                return 1;
            sf::RenderTexture probe;
            if (!probe.resize({1, 1}))
                return mode == "--sfml-vulkan" ? 77 : 1;
            argv[1] = argv[0];
            ++argv;
            --argc;
        }
    }
#endif
    return Catch::Session().run(argc, argv);
}
