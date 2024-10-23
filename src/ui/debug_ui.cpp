#include "debug_ui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_vulkan.h"
#include "../engine.h"
#include "../vk_renderer.h"

void DebugUI::init()
{
}

void DebugUI::update()
{
    // imgui new frame
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplSDL2_NewFrame();

    ImGui::NewFrame();

    ImGui::Begin("Stats");

    ImGui::Text("frametime %f ms", Engine::Get().engine_stats.renderer_stats->frametime);
    ImGui::Text("drawtime %f ms", Engine::Get().engine_stats.renderer_stats->mesh_draw_time);
    ImGui::Text("triangles %i", Engine::Get().engine_stats.renderer_stats->triangle_count);
    ImGui::Text("draws %i", Engine::Get().engine_stats.renderer_stats->drawcall_count);
    ImGui::End();

    if (ImGui::Begin("background"))
    {

        ComputeEffect& selected = VulkanRenderer::Get().background_effects[VulkanRenderer::Get().currentBackgroundEffect];

        ImGui::Text("Selected effect: ", selected.name);

        ImGui::SliderInt("Effect Index", &VulkanRenderer::Get().currentBackgroundEffect, 0, (int)VulkanRenderer::Get().background_effects.size() - 1);

        ImGui::InputFloat4("data1", (float*)&selected.push_constants_data.data1);
        ImGui::InputFloat4("data2", (float*)&selected.push_constants_data.data2);
        ImGui::InputFloat4("data3", (float*)&selected.push_constants_data.data3);
        ImGui::InputFloat4("data4", (float*)&selected.push_constants_data.data4);

        ImGui::End();
    }

    ImGui::Render();
}
