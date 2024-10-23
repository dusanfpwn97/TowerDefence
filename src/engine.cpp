#include "engine.h"
#include <vk_renderer.h>
#include "imgui_impl_sdl2.h"

Engine* engine = nullptr;

Engine& Engine::Get()
{
	return *engine;
}

bool Engine::init()
{
    // only one engine initialization is allowed with the application.
    assert(engine == nullptr);
    engine = this;
	main_camera.init();
	renderer.init(&main_camera);
    ui.init();
    engine_stats.renderer_stats = &renderer.renderer_stats;

    engine_stats.engine_start_time = std::chrono::system_clock::now();

	return true;
}

bool Engine::run()
{
    // main loop
    while (!should_quit)
    {
        update();

    }

	return false;
}

void Engine::update()
{
    engine_stats.frame_start_time = std::chrono::system_clock::now();
    engine_stats.frame_end_time = std::chrono::system_clock::now(); // Reset end time

    update_input();



    if (renderer.resize_requested)
    {
        renderer.resize_swapchain();
    }

    main_camera.update();



    if (is_minimized) return; // don't draw if minimized
    ui.update();
    renderer.update();
    renderer.draw();

    engine_stats.frame_end_time = std::chrono::system_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(engine_stats.frame_end_time - engine_stats.frame_start_time);
    engine_stats.renderer_stats->frametime = elapsed.count() / 1000.f;
}

void Engine::update_input()
{
    SDL_Event e;

    // Handle events on queue
    while (SDL_PollEvent(&e) != 0)
    {
        // close the window when user alt-f4s or clicks the X button
        if (e.type == SDL_QUIT)
        {
            should_quit = true;
        }

        if (e.type == SDL_WINDOWEVENT)
        {
            if (e.window.event == SDL_WINDOWEVENT_RESIZED)
            {
                renderer.resize_requested = true;
            }
            if (e.window.event == SDL_WINDOWEVENT_MINIMIZED)
            {
                is_minimized = true;
            }
            if (e.window.event == SDL_WINDOWEVENT_RESTORED)
            {
                is_minimized = false;
            }
        }

        main_camera.processSDLEvent(e);

        ImGui_ImplSDL2_ProcessEvent(&e);
    }
}



bool Engine::cleanup()
{
	renderer.cleanup();

	return true;
}
