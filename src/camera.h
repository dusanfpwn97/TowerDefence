#pragma once
#include <vk_types.h>
#include <SDL_events.h>

class Camera {
public:

    void init();

    glm::vec3 velocity;
    glm::vec3 position;
    // vertical rotation
    float pitch{ 0.f };
    // horizontal rotation
    float yaw{ 0.f };

    glm::mat4 getViewMatrix();
    glm::mat4 getRotationMatrix();

    bool is_right_mouse_button_down = false;

    void processSDLEvent(SDL_Event& e);

    void update();
};