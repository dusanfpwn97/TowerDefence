#include "engine.h"
#include <vk_renderer.h>

bool Engine::init()
{
	renderer.init();

	return false;
}

bool Engine::run()
{
	renderer.run();

	return false;
}

bool Engine::cleanup()
{
	renderer.cleanup();

	return false;
}
