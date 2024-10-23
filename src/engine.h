#pragma once
#include "vk_renderer.h"


class Engine
{
public:

	bool init();
	bool run();
	bool cleanup();

	VulkanRenderer renderer;

};