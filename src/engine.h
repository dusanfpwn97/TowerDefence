#pragma once
#include "vk_renderer.h"
#include "camera.h"
#include "ui/ui.h"

struct EngineStats
{
	std::chrono::system_clock::time_point application_start_time;
	std::chrono::system_clock::time_point engine_start_time;
	std::chrono::system_clock::time_point renderer_start_time;

	std::chrono::system_clock::time_point frame_start_time;
	std::chrono::system_clock::time_point frame_end_time;
	RendererStats* renderer_stats;
};

class Engine
{
public:

	static Engine& Get();

	bool init();
	bool run();
	void update();
	void update_input();

	bool cleanup();

	Camera main_camera;
	VulkanRenderer renderer;
	UI ui;

	EngineStats engine_stats;

	bool should_quit = false;
	bool is_minimized = false;

};