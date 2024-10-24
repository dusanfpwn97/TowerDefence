﻿
#include "vk_renderer.h"

#include "vk_images.h"
#include "vk_loader.h"
#include "vk_descriptors.h"

#include <SDL.h>
#include <SDL_vulkan.h>

#include <vk_initializers.h>
#include <vk_types.h>

#include "VkBootstrap.h"

#include "imgui_impl_sdl2.h"
#include "imgui_impl_vulkan.h"

#include <glm/gtx/transform.hpp>
#include <camera.h>

#define VMA_IMPLEMENTATION
#include "vk_mem_alloc.h"

constexpr bool bUseValidationLayers = true;
VulkanRenderer* renderer = nullptr;

VulkanRenderer& VulkanRenderer::Get()
{
    return *renderer;
}

void VulkanRenderer::init(Camera *camera)
{
    // only one renderer initialization is allowed with the application.
    assert(renderer == nullptr);
    renderer = this;

    init_sdl();
    init_vulkan();
    init_swapchain();
    init_command_pools_and_buffers();
    init_sync_structures();
    init_descriptors();
    init_pipelines();
    init_default_data();
    init_renderables();
    init_imgui();

    // everything went fine
    is_initialized = true;
    main_camera = camera;

}

void VulkanRenderer::init_sdl()
{
    // We initialize SDL and create a window with it.
    SDL_Init(SDL_INIT_VIDEO);
    SDL_WindowFlags window_flags = (SDL_WindowFlags)(SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);

    _window = SDL_CreateWindow("Dusan Engine", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, _windowExtent.width, _windowExtent.height, window_flags);

}

void VulkanRenderer::init_vulkan()
{
    vkb::InstanceBuilder builder;

    // make the vulkan instance, with basic debug features
    auto inst_ret = builder.set_app_name("Example Vulkan Application")
        .request_validation_layers(bUseValidationLayers)
        .use_default_debug_messenger()
        .require_api_version(1, 3, 0)
        .build();
    system("cls"); // delete stupid galaxy message

    vkb::Instance vkb_inst = inst_ret.value();

    // grab the instance
    vk_instance = vkb_inst.instance;
    vk_debug_messenger = vkb_inst.debug_messenger;

    SDL_Vulkan_CreateSurface(_window, vk_instance, &vk_surface);

    //vulkan 1.3 features
    VkPhysicalDeviceVulkan13Features features{ .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES };
    features.dynamicRendering = true;
    features.synchronization2 = true;

    //vulkan 1.2 features
    VkPhysicalDeviceVulkan12Features features12{ .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES };
    features12.bufferDeviceAddress = true;
    features12.descriptorIndexing = true;


    //use vkbootstrap to select a gpu. 
    //We want a gpu that can write to the SDL surface and supports vulkan 1.3 with the correct features
    vkb::PhysicalDeviceSelector selector{ vkb_inst };
    vkb::PhysicalDevice physicalDevice = selector
        .set_minimum_version(1, 3)
        .set_required_features_13(features)
        .set_required_features_12(features12)
        .set_surface(vk_surface)
        .select()
        .value();
    // physicalDevice.features.
    // create the final vulkan device

    vkb::DeviceBuilder deviceBuilder{ physicalDevice };

    vkb::Device vkbDevice = deviceBuilder.build().value();

    // Get the VkDevice handle used in the rest of a vulkan application
    vk_device = vkbDevice.device;
    vk_gpu = physicalDevice.physical_device;

    // use vkbootstrap to get a Graphics queue
    vk_graphics_queue = vkbDevice.get_queue(vkb::QueueType::graphics).value();

    vk_graphics_queue_family = vkbDevice.get_queue_index(vkb::QueueType::graphics).value();

    // initialize the memory allocator
    VmaAllocatorCreateInfo allocatorInfo = {};
    allocatorInfo.physicalDevice = vk_gpu;
    allocatorInfo.device = vk_device;
    allocatorInfo.instance = vk_instance;
    allocatorInfo.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
    vmaCreateAllocator(&allocatorInfo, &vma_allocator);
}

void VulkanRenderer::init_swapchain()
{
    create_swapchain(_windowExtent.width, _windowExtent.height);

    //depth image size will match the window
    VkExtent3D drawImageExtent = { _windowExtent.width, _windowExtent.height, 1 };

    //hardcoding the draw format to 32 bit float
    draw_image.image_format = VK_FORMAT_R16G16B16A16_SFLOAT;
    draw_image.image_extent = drawImageExtent;

    VkImageUsageFlags drawImageUsages{};
    drawImageUsages |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    drawImageUsages |= VK_IMAGE_USAGE_STORAGE_BIT;
    drawImageUsages |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

    VkImageCreateInfo rimg_info = vkinit::image_create_info(draw_image.image_format, drawImageUsages, drawImageExtent);

    //for the draw image, we want to allocate it from gpu local memory
    VmaAllocationCreateInfo rimg_allocinfo = {};
    rimg_allocinfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
    rimg_allocinfo.requiredFlags = VkMemoryPropertyFlags(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    //allocate and create the image
    vmaCreateImage(vma_allocator, &rimg_info, &rimg_allocinfo, &draw_image.image, &draw_image.allocation, nullptr);

    //build a image-view for the draw image to use for rendering
    VkImageViewCreateInfo rview_info = vkinit::imageview_create_info(draw_image.image_format, draw_image.image, VK_IMAGE_ASPECT_COLOR_BIT);

    VK_CHECK(vkCreateImageView(vk_device, &rview_info, nullptr, &draw_image.image_view));

    //create a depth image too
    //hardcoding the draw format to 32 bit float
    depth_image.image_format = VK_FORMAT_D32_SFLOAT;
    depth_image.image_extent = drawImageExtent;

    VkImageUsageFlags depthImageUsages{};
    depthImageUsages |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;

    VkImageCreateInfo dimg_info = vkinit::image_create_info(depth_image.image_format, depthImageUsages, drawImageExtent);

    //allocate and create the image
    vmaCreateImage(vma_allocator, &dimg_info, &rimg_allocinfo, &depth_image.image, &depth_image.allocation, nullptr);

    //build a image-view for the draw image to use for rendering
    VkImageViewCreateInfo dview_info = vkinit::imageview_create_info(depth_image.image_format, depth_image.image, VK_IMAGE_ASPECT_DEPTH_BIT);

    VK_CHECK(vkCreateImageView(vk_device, &dview_info, nullptr, &depth_image.image_view));
}

void VulkanRenderer::init_command_pools_and_buffers()
{
    // create a command pool for commands submitted to the graphics queue.
    // we also want the pool to allow for resetting of individual command buffers
    VkCommandPoolCreateInfo commandPoolInfo = vkinit::command_pool_create_info(vk_graphics_queue_family, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);

    for (int i = 0; i < FRAME_NUM; i++) {

        VK_CHECK(vkCreateCommandPool(vk_device, &commandPoolInfo, nullptr, &frames[i]._commandPool));

        // allocate the default command buffer that we will use for rendering
        VkCommandBufferAllocateInfo cmdAllocInfo = vkinit::command_buffer_allocate_info(frames[i]._commandPool, 1);

        VK_CHECK(vkAllocateCommandBuffers(vk_device, &cmdAllocInfo, &frames[i]._mainCommandBuffer));
    }

    VK_CHECK(vkCreateCommandPool(vk_device, &commandPoolInfo, nullptr, &immidiate_command_pool));

    // allocate the default command buffer that we will use for rendering
    VkCommandBufferAllocateInfo cmdAllocInfo = vkinit::command_buffer_allocate_info(immidiate_command_pool, 1);

    VK_CHECK(vkAllocateCommandBuffers(vk_device, &cmdAllocInfo, &immidiate_command_buffer));

}

void VulkanRenderer::init_sync_structures()
{
    // create syncronization structures
    // one fence to control when the gpu has finished rendering the frame,
    // and 2 semaphores to syncronize rendering with swapchain
    // we want the fence to start signalled so we can wait on it on the first
    // frame
    VkFenceCreateInfo fenceCreateInfo = vkinit::fence_create_info(VK_FENCE_CREATE_SIGNALED_BIT);
    VK_CHECK(vkCreateFence(vk_device, &fenceCreateInfo, nullptr, &immidiate_fence));

    for (int i = 0; i < FRAME_NUM; i++) {

        VK_CHECK(vkCreateFence(vk_device, &fenceCreateInfo, nullptr, &frames[i]._renderFence));

        VkSemaphoreCreateInfo semaphoreCreateInfo = vkinit::semaphore_create_info();

        VK_CHECK(vkCreateSemaphore(vk_device, &semaphoreCreateInfo, nullptr, &frames[i]._swapchainSemaphore));
        VK_CHECK(vkCreateSemaphore(vk_device, &semaphoreCreateInfo, nullptr, &frames[i]._renderSemaphore));
    }
}

void VulkanRenderer::init_descriptors()
{
    // create a descriptor pool
    std::vector<DescriptorAllocator::PoolSizeRatio> sizes =
    {
        { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 3 },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 3 },
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 3 },
    };

    global_descriptor_allocator.init_pool(vk_device, 10, sizes);

    {
        DescriptorLayoutBuilder builder;
        builder.add_binding(0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
        draw_image_descriptor_layout = builder.build(vk_device, VK_SHADER_STAGE_COMPUTE_BIT);
    }
    {
        DescriptorLayoutBuilder builder;
        builder.add_binding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
        gpu_scene_data_descriptor_layout = builder.build(vk_device, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT);
    }

    draw_image_descriptors = global_descriptor_allocator.allocate(vk_device, draw_image_descriptor_layout);
    {
        DescriptorWriter writer;
        writer.write_image(0, draw_image.image_view, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_GENERAL, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
        writer.update_set(vk_device, draw_image_descriptors);
    }
    for (int i = 0; i < FRAME_NUM; i++) {
        // create a descriptor pool
        std::vector<DescriptorAllocatorGrowable::PoolSizeRatio> frame_sizes = {
            { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 3 },
            { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 3 },
            { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 3 },
            { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4 },
        };

        frames[i]._frameDescriptors = DescriptorAllocatorGrowable{};
        frames[i]._frameDescriptors.init(vk_device, 1000, frame_sizes);
    }
}

void VulkanRenderer::init_pipelines()
{
    // COMPUTE PIPELINES
    init_background_pipelines();

    rough_metal_material.build_pipelines(this);
}

void VulkanRenderer::init_background_pipelines()
{
    VkPipelineLayoutCreateInfo computeLayout{};
    computeLayout.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    computeLayout.pNext = nullptr;
    computeLayout.pSetLayouts = &draw_image_descriptor_layout;
    computeLayout.setLayoutCount = 1;

    VkPushConstantRange pushConstant{};
    pushConstant.offset = 0;
    pushConstant.size = sizeof(ComputePushConstants);
    pushConstant.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    computeLayout.pPushConstantRanges = &pushConstant;
    computeLayout.pushConstantRangeCount = 1;

    VK_CHECK(vkCreatePipelineLayout(vk_device, &computeLayout, nullptr, &gradient_pipeline_layout));

    VkShaderModule gradientShader;
    if (!vkutil::load_shader_module("../../shaders/compiled/gradient_color.comp.spv", vk_device, &gradientShader)) {
        fmt::print("Error when building the compute shader \n");
    }

    VkShaderModule skyShader;
    if (!vkutil::load_shader_module("../../shaders/compiled/sky.comp.spv", vk_device, &skyShader)) {
        fmt::print("Error when building the compute shader\n");
    }

    VkPipelineShaderStageCreateInfo stageinfo{};
    stageinfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stageinfo.pNext = nullptr;
    stageinfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    stageinfo.module = gradientShader;
    stageinfo.pName = "main";

    VkComputePipelineCreateInfo computePipelineCreateInfo{};
    computePipelineCreateInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    computePipelineCreateInfo.pNext = nullptr;
    computePipelineCreateInfo.layout = gradient_pipeline_layout;
    computePipelineCreateInfo.stage = stageinfo;


    gradient.layout = gradient_pipeline_layout;
    gradient.name = "gradient";
    gradient.push_constants_data = {};

    //default colors
    gradient.push_constants_data.data1 = glm::vec4(1, 0, 0, 1);
    gradient.push_constants_data.data2 = glm::vec4(0, 0, 1, 1);

    VK_CHECK(vkCreateComputePipelines(vk_device, VK_NULL_HANDLE, 1, &computePipelineCreateInfo, nullptr, &gradient.pipeline));

    //change the shader module only to create the sky shader
    computePipelineCreateInfo.stage.module = skyShader;


    sky.layout = gradient_pipeline_layout;
    sky.name = "sky";
    sky.push_constants_data = {};
    //default sky parameters
    sky.push_constants_data.data1 = glm::vec4(0.1, 0.2, 0.4, 0.97);

    VK_CHECK(vkCreateComputePipelines(vk_device, VK_NULL_HANDLE, 1, &computePipelineCreateInfo, nullptr, &sky.pipeline));

    //add the 2 background effects into the array
    background_effects.push_back(gradient);
    background_effects.push_back(sky);

    //destroy structures properly
    vkDestroyShaderModule(vk_device, gradientShader, nullptr);
    vkDestroyShaderModule(vk_device, skyShader, nullptr);
}

void VulkanRenderer::init_default_data()
{
    std::array<Vertex, 4> rect_vertices;

    rect_vertices[0].position = { 0.5,-0.5, 0 };
    rect_vertices[1].position = { 0.5,0.5, 0 };
    rect_vertices[2].position = { -0.5,-0.5, 0 };
    rect_vertices[3].position = { -0.5,0.5, 0 };

    rect_vertices[0].color = { 0,0, 0,1 };
    rect_vertices[1].color = { 0.5,0.5,0.5 ,1 };
    rect_vertices[2].color = { 1,0, 0,1 };
    rect_vertices[3].color = { 0,1, 0,1 };

    rect_vertices[0].uv_x = 1;
    rect_vertices[0].uv_y = 0;
    rect_vertices[1].uv_x = 0;
    rect_vertices[1].uv_y = 0;
    rect_vertices[2].uv_x = 1;
    rect_vertices[2].uv_y = 1;
    rect_vertices[3].uv_x = 0;
    rect_vertices[3].uv_y = 1;

    std::array<uint32_t, 6> rect_indices;

    rect_indices[0] = 0;
    rect_indices[1] = 1;
    rect_indices[2] = 2;

    rect_indices[3] = 2;
    rect_indices[4] = 1;
    rect_indices[5] = 3;

    rectangle = upload_mesh_immidiate(rect_indices, rect_vertices);


    //3 default textures, white, grey, black. 1 pixel each
    uint32_t white_color = glm::packUnorm4x8(glm::vec4(1, 1, 1, 1));
    white_image = *create_image_on_gpu_immidiate((void*)&white_color, VkExtent3D{ 1, 1, 1 }, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT, false);

    uint32_t grey_color = glm::packUnorm4x8(glm::vec4(0.66f, 0.66f, 0.66f, 1));
    grey_image = *create_image_on_gpu_immidiate((void*)&grey_color, VkExtent3D{ 1, 1, 1 }, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT, false);

    uint32_t black_color = glm::packUnorm4x8(glm::vec4(0, 0, 0, 0));
    black_image = *create_image_on_gpu_immidiate((void*)&black_color, VkExtent3D{ 1, 1, 1 }, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT, false);

    //checkerboard image
    uint32_t magenta_color = glm::packUnorm4x8(glm::vec4(1, 0, 1, 1));
    std::array<uint32_t, 16 * 16 > pixels; //for 16x16 checkerboard texture
    for (int x = 0; x < 16; x++)
    {
        for (int y = 0; y < 16; y++)
        {
            pixels[y * 16 + x] = ((x % 2) ^ (y % 2)) ? magenta_color : black_color;
        }
    }

    error_checkerboard_image = *create_image_on_gpu_immidiate(pixels.data(), VkExtent3D{ 16, 16, 1 }, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT, false);

    VkSamplerCreateInfo sampl = { .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };

    sampl.magFilter = VK_FILTER_NEAREST;
    sampl.minFilter = VK_FILTER_NEAREST;

    vkCreateSampler(vk_device, &sampl, nullptr, &default_sampler_nearest);

    sampl.magFilter = VK_FILTER_LINEAR;
    sampl.minFilter = VK_FILTER_LINEAR;
    vkCreateSampler(vk_device, &sampl, nullptr, &default_sampler_linear);

    // Create a buffer so we don't crash when deleting an empty buffer on the first frame
    for (int i = 0; i < FRAME_NUM; i++)
    {
        frames[i].gpuSceneDataBuffer = create_buffer(sizeof(GPUSceneData), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU, true);
    }
}

void VulkanRenderer::init_renderables()
{
    std::string structurePath = { "..\\..\\assets\\gltf\\CastleModel.glb" };
    auto structureFile = loadGltf(this, structurePath);

    assert(structureFile.has_value());

    loadedScenes["structure"] = *structureFile;
}

void VulkanRenderer::init_imgui()
{
    // 1: create descriptor pool for IMGUI
    //  the size of the pool is very oversize, but it's copied from imgui demo
    //  itself.
    VkDescriptorPoolSize pool_sizes[] = {
        { VK_DESCRIPTOR_TYPE_SAMPLER, 1000 },
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000 },
        { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000 },
        { VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000 } };

    VkDescriptorPoolCreateInfo pool_info = {};
    pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    pool_info.maxSets = 1000;
    pool_info.poolSizeCount = (uint32_t)std::size(pool_sizes);
    pool_info.pPoolSizes = pool_sizes;

    VK_CHECK(vkCreateDescriptorPool(vk_device, &pool_info, nullptr, &imgui_pool));

    // 2: initialize imgui library

    // this initializes the core structures of imgui
    ImGui::CreateContext();

    // this initializes imgui for SDL
    ImGui_ImplSDL2_InitForVulkan(_window);

    // this initializes imgui for Vulkan
    ImGui_ImplVulkan_InitInfo init_info = {};
    init_info.Instance = vk_instance;
    init_info.PhysicalDevice = vk_gpu;
    init_info.Device = vk_device;
    init_info.Queue = vk_graphics_queue;
    init_info.DescriptorPool = imgui_pool;
    init_info.MinImageCount = 3;
    init_info.ImageCount = 3;
    init_info.UseDynamicRendering = true;

    //dynamic rendering parameters for imgui to use
    init_info.PipelineRenderingCreateInfo = { .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO };
    init_info.PipelineRenderingCreateInfo.colorAttachmentCount = 1;
    init_info.PipelineRenderingCreateInfo.pColorAttachmentFormats = &vk_swapchain_image_format;


    init_info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;

    ImGui_ImplVulkan_Init(&init_info);

    ImGui_ImplVulkan_CreateFontsTexture();

}

void VulkanRenderer::update()
{
    glm::mat4 view = main_camera->getViewMatrix();

    // camera projection
    glm::mat4 projection = glm::perspective(glm::radians(45.f), (float)draw_extent.width / (float)draw_extent.height, 10000.f, 0.1f);

    // invert the Y direction on projection matrix so that we are more similar to opengl and gltf axis
    projection[1][1] *= -1;

    sceneData.view = view;
    sceneData.proj = projection;
    sceneData.viewproj = projection * view;


    loadedScenes["structure"]->Draw(glm::mat4{ 1.f }, drawCommands);

}

void VulkanRenderer::draw_main(VkCommandBuffer cmd)
{
    ComputeEffect& effect = background_effects[currentBackgroundEffect];

    // bind the background compute pipeline
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, effect.pipeline);

    // bind the descriptor set containing the draw image for the compute pipeline
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, gradient_pipeline_layout, 0, 1, &draw_image_descriptors, 0, nullptr);

    vkCmdPushConstants(cmd, gradient_pipeline_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(ComputePushConstants), &effect.push_constants_data);
    // execute the compute pipeline dispatch. We are using 16x16 workgroup size so we need to divide by it
    vkCmdDispatch(cmd, (uint32_t)std::ceil((float)_windowExtent.width / 16.0), (uint32_t)std::ceil((float)_windowExtent.height / 16.0), 1);

    //draw the triangle

    VkRenderingAttachmentInfo colorAttachment = vkinit::attachment_info(draw_image.image_view, nullptr, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    VkRenderingAttachmentInfo depthAttachment = vkinit::depth_attachment_info(depth_image.image_view, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);

    VkRenderingInfo renderInfo = vkinit::rendering_info(_windowExtent, &colorAttachment, &depthAttachment);

    vkCmdBeginRendering(cmd, &renderInfo);
    auto start = std::chrono::system_clock::now();
    draw_geometry(cmd);

    auto end = std::chrono::system_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

    renderer_stats.mesh_draw_time = elapsed.count() / 1000.f;

    vkCmdEndRendering(cmd);
}

void VulkanRenderer::draw_imgui(VkCommandBuffer cmd, VkImageView targetImageView)
{
    VkRenderingAttachmentInfo colorAttachment = vkinit::attachment_info(targetImageView, nullptr, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    VkRenderingInfo renderInfo = vkinit::rendering_info(_windowExtent, &colorAttachment, nullptr);

    vkCmdBeginRendering(cmd, &renderInfo);

    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);

    vkCmdEndRendering(cmd);
}

void VulkanRenderer::draw()
{
    //wait until the gpu has finished rendering the last frame. Timeout of 1 second
    VK_CHECK(vkWaitForFences(vk_device, 1, &get_current_frame_data()._renderFence, true, 1000000000));

    destroy_allocated_buffer(*get_current_frame_data().gpuSceneDataBuffer);
    get_current_frame_data()._frameDescriptors.clear_pools(vk_device);
    //request image from the swapchain
    uint32_t swapchainImageIndex;

    VkResult e = vkAcquireNextImageKHR(vk_device, vk_swapchain, 1000000000, get_current_frame_data()._swapchainSemaphore, nullptr, &swapchainImageIndex);
    if (e == VK_ERROR_OUT_OF_DATE_KHR) {
        resize_requested = true;
        return;
    }

    draw_extent.height = uint32_t((float)std::min(swapchain_extent.height, draw_image.image_extent.height) * renderScale);
    draw_extent.width  = uint32_t((float)std::min(swapchain_extent.width , draw_image.image_extent.width)  * renderScale);

    VK_CHECK(vkResetFences(vk_device, 1, &get_current_frame_data()._renderFence));

    //now that we are sure that the commands finished executing, we can safely reset the command buffer to begin recording again.
    VK_CHECK(vkResetCommandBuffer(get_current_frame_data()._mainCommandBuffer, 0));

    //naming it cmd for shorter writing
    VkCommandBuffer cmd = get_current_frame_data()._mainCommandBuffer;

    //begin the command buffer recording. We will use this command buffer exactly once, so we want to let vulkan know that
    VkCommandBufferBeginInfo cmdBeginInfo = vkinit::command_buffer_begin_info(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

    //> draw_first
    VK_CHECK(vkBeginCommandBuffer(cmd, &cmdBeginInfo));

    // transition our main draw image into general layout so we can write into it
    // we will overwrite it all so we dont care about what was the older layout
    vkutil::transition_image(cmd, draw_image.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
    vkutil::transition_image(cmd, depth_image.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);

    draw_main(cmd);

    //transtion the draw image and the swapchain image into their correct transfer layouts
    vkutil::transition_image(cmd, draw_image.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    vkutil::transition_image(cmd, swapchain_images[swapchainImageIndex], VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

    VkExtent2D extent;
    extent.height = _windowExtent.height;
    extent.width = _windowExtent.width;
    //< draw_first
    //> imgui_draw
    // execute a copy from the draw image into the swapchain
    vkutil::copy_image_to_image(cmd, draw_image.image, swapchain_images[swapchainImageIndex], draw_extent, swapchain_extent);

    // set swapchain image layout to Attachment Optimal so we can draw it
    vkutil::transition_image(cmd, swapchain_images[swapchainImageIndex], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

    //draw imgui into the swapchain image
    draw_imgui(cmd, swapchain_image_views[swapchainImageIndex]);

    // set swapchain image layout to Present so we can draw it
    vkutil::transition_image(cmd, swapchain_images[swapchainImageIndex], VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

    //finalize the command buffer (we can no longer add commands, but it can now be executed)
    VK_CHECK(vkEndCommandBuffer(cmd));

    //prepare the submission to the queue. 
    //we want to wait on the _presentSemaphore, as that semaphore is signaled when the swapchain is ready
    //we will signal the _renderSemaphore, to signal that rendering has finished

    VkCommandBufferSubmitInfo cmdinfo = vkinit::command_buffer_submit_info(cmd);

    VkSemaphoreSubmitInfo waitInfo = vkinit::semaphore_submit_info(VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT_KHR, get_current_frame_data()._swapchainSemaphore);
    VkSemaphoreSubmitInfo signalInfo = vkinit::semaphore_submit_info(VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT, get_current_frame_data()._renderSemaphore);

    VkSubmitInfo2 submit = vkinit::submit_info(&cmdinfo, &signalInfo, &waitInfo);

    //submit command buffer to the queue and execute it.
    // _renderFence will now block until the graphic commands finish execution
    VK_CHECK(vkQueueSubmit2(vk_graphics_queue, 1, &submit, get_current_frame_data()._renderFence));



    //prepare present
    // this will put the image we just rendered to into the visible window.
    // we want to wait on the _renderSemaphore for that, 
    // as its necessary that drawing commands have finished before the image is displayed to the user
    VkPresentInfoKHR presentInfo = vkinit::present_info();

    presentInfo.pSwapchains = &vk_swapchain;
    presentInfo.swapchainCount = 1;

    presentInfo.pWaitSemaphores = &get_current_frame_data()._renderSemaphore;
    presentInfo.waitSemaphoreCount = 1;

    presentInfo.pImageIndices = &swapchainImageIndex;

    VkResult presentResult = vkQueuePresentKHR(vk_graphics_queue, &presentInfo);
    if (e == VK_ERROR_OUT_OF_DATE_KHR) {
        resize_requested = true;
        return;
    }

    //increase the number of frames drawn
    frame_number++;


}

//> visfn
bool is_visible(const RenderObject& obj, const glm::mat4& viewproj) {
    std::array<glm::vec3, 8> corners{
        glm::vec3 { 1, 1, 1 },
        glm::vec3 { 1, 1, -1 },
        glm::vec3 { 1, -1, 1 },
        glm::vec3 { 1, -1, -1 },
        glm::vec3 { -1, 1, 1 },
        glm::vec3 { -1, 1, -1 },
        glm::vec3 { -1, -1, 1 },
        glm::vec3 { -1, -1, -1 },
    };

    glm::mat4 matrix = viewproj * obj.transform;

    glm::vec3 min = { 1.5, 1.5, 1.5 };
    glm::vec3 max = { -1.5, -1.5, -1.5 };

    for (int c = 0; c < 8; c++) {
        // project each corner into clip space
        glm::vec4 v = matrix * glm::vec4(obj.bounds.origin + (corners[c] * obj.bounds.extents), 1.f);

        // perspective correction
        v.x = v.x / v.w;
        v.y = v.y / v.w;
        v.z = v.z / v.w;

        min = glm::min(glm::vec3{ v.x, v.y, v.z }, min);
        max = glm::max(glm::vec3{ v.x, v.y, v.z }, max);
    }

    // check the clip space box is within the view
    if (min.z > 1.f || max.z < 0.f || min.x > 1.f || max.x < -1.f || min.y > 1.f || max.y < -1.f) {
        return false;
    }
    else {
        return true;
    }
}
//< visfn

void VulkanRenderer::draw_geometry(VkCommandBuffer cmd)
{
    std::vector<uint32_t> opaque_draws;
    opaque_draws.reserve(drawCommands.OpaqueSurfaces.size());

    for (int i = 0; i < drawCommands.OpaqueSurfaces.size(); i++)
    {
        if (is_visible(drawCommands.OpaqueSurfaces[i], sceneData.viewproj))
        {
            opaque_draws.push_back(i);
        }
    }

    // sort the opaque surfaces by material and mesh
    std::sort(opaque_draws.begin(), opaque_draws.end(), [&](const auto& iA, const auto& iB)
    {
    const RenderObject& A = drawCommands.OpaqueSurfaces[iA];
    const RenderObject& B = drawCommands.OpaqueSurfaces[iB];
    if (A.material == B.material)
    {
        return A.indexBuffer < B.indexBuffer;
    }
    else
    {
        return A.material < B.material;
    }
    });


    //allocate a new uniform buffer for the scene data
    get_current_frame_data().gpuSceneDataBuffer = create_buffer(sizeof(GPUSceneData), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU, true);

    //write the buffer
    GPUSceneData* sceneUniformData = (GPUSceneData*)get_current_frame_data().gpuSceneDataBuffer->allocation->GetMappedData();
    *sceneUniformData = sceneData;

    //create a descriptor set that binds that buffer and update it
    VkDescriptorSet globalDescriptor = get_current_frame_data()._frameDescriptors.allocate(vk_device, gpu_scene_data_descriptor_layout);

    DescriptorWriter writer;
    writer.write_buffer(0, get_current_frame_data().gpuSceneDataBuffer->buffer, sizeof(GPUSceneData), 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
    writer.update_set(vk_device, globalDescriptor);

    MaterialPipeline* lastPipeline = nullptr;
    MaterialInstance* lastMaterial = nullptr;
    VkBuffer lastIndexBuffer = VK_NULL_HANDLE;

    auto draw = [&](const RenderObject& r)
    {
        if (r.material != lastMaterial) {
            lastMaterial = r.material;
            if (r.material->pipeline != lastPipeline) {

                lastPipeline = r.material->pipeline;
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, r.material->pipeline->pipeline);
                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, r.material->pipeline->layout, 0, 1,
                    &globalDescriptor, 0, nullptr);

                VkViewport viewport = {};
                viewport.x = 0;
                viewport.y = 0;
                viewport.width = (float)_windowExtent.width;
                viewport.height = (float)_windowExtent.height;
                viewport.minDepth = 0.f;
                viewport.maxDepth = 1.f;

                vkCmdSetViewport(cmd, 0, 1, &viewport);

                VkRect2D scissor = {};
                scissor.offset.x = 0;
                scissor.offset.y = 0;
                scissor.extent.width = _windowExtent.width;
                scissor.extent.height = _windowExtent.height;

                vkCmdSetScissor(cmd, 0, 1, &scissor);
            }

            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, r.material->pipeline->layout, 1, 1,
                &r.material->materialSet, 0, nullptr);
        }
        if (r.indexBuffer != lastIndexBuffer) {
            lastIndexBuffer = r.indexBuffer;
            vkCmdBindIndexBuffer(cmd, r.indexBuffer, 0, VK_INDEX_TYPE_UINT32);
        }
        // calculate final mesh matrix
        GPUDrawPushConstants push_constants;
        push_constants.world_matrix = r.transform;
        push_constants.vertex_buffer = r.vertexBufferAddress;

        vkCmdPushConstants(cmd, r.material->pipeline->layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(GPUDrawPushConstants), &push_constants);

        renderer_stats.drawcall_count++;
        renderer_stats.triangle_count += r.indexCount / 3;
        vkCmdDrawIndexed(cmd, r.indexCount, 1, r.firstIndex, 0, 0);
    };

    renderer_stats.drawcall_count = 0;
    renderer_stats.triangle_count = 0;

    for (auto& r : opaque_draws) {
        draw(drawCommands.OpaqueSurfaces[r]);
    }

    for (auto& r : drawCommands.TransparentSurfaces) {
        draw(r);
    }


    // we delete the draw commands now that we processed them
    drawCommands.OpaqueSurfaces.clear();
    drawCommands.TransparentSurfaces.clear();
}


void VulkanRenderer::create_swapchain(uint32_t width, uint32_t height)
{
    vkb::SwapchainBuilder swapchainBuilder{ vk_gpu,vk_device,vk_surface };

    vk_swapchain_image_format = VK_FORMAT_B8G8R8A8_UNORM;

    vkb::Swapchain vkbSwapchain = swapchainBuilder
        //.use_default_format_selection()
        .set_desired_format(VkSurfaceFormatKHR{ .format = vk_swapchain_image_format, .colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR })
        //use vsync present mode
        .set_desired_present_mode(VK_PRESENT_MODE_FIFO_KHR)
        .set_desired_extent(width, height)
        .add_image_usage_flags(VK_IMAGE_USAGE_TRANSFER_DST_BIT)
        .build()
        .value();

    swapchain_extent = vkbSwapchain.extent;

    //store swapchain and its related images
    vk_swapchain = vkbSwapchain.swapchain;
    swapchain_images = vkbSwapchain.get_images().value();
    swapchain_image_views = vkbSwapchain.get_image_views().value();
}


BufferAllocation* VulkanRenderer::create_buffer(size_t allocSize, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage, bool is_temporal)
{
    // allocate buffer
    VkBufferCreateInfo bufferInfo = {};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.pNext = nullptr;
    bufferInfo.size = allocSize;

    bufferInfo.usage = usage;

    VmaAllocationCreateInfo vmaallocInfo = {};
    vmaallocInfo.usage = memoryUsage;
    vmaallocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;

    BufferAllocation* new_buffer = new BufferAllocation;
    if (!is_temporal)
    {
        allocated_buffers.push_back(new_buffer);
    }

    // allocate the buffer
    VK_CHECK(vmaCreateBuffer(vma_allocator, &bufferInfo, &vmaallocInfo, &new_buffer->buffer, &new_buffer->allocation, &new_buffer->info));

    return new_buffer;
}

bool VulkanRenderer::create_image(ImageAllocation& outImage, VkExtent3D size, VkFormat format, VkImageUsageFlags usage, bool mipmapped)
{
    outImage.image_format = format;
    outImage.image_extent = size;

    VkImageCreateInfo img_info = vkinit::image_create_info(format, usage, size);
    if (mipmapped)
    {
        img_info.mipLevels = static_cast<uint32_t>(std::floor(std::log2(std::max(size.width, size.height)))) + 1;
    }

    // always allocate images on dedicated GPU memory
    VmaAllocationCreateInfo allocinfo = {};
    allocinfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
    allocinfo.requiredFlags = VkMemoryPropertyFlags(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    // allocate and create the image
    VK_CHECK(vmaCreateImage(vma_allocator, &img_info, &allocinfo, &outImage.image, &outImage.allocation, nullptr));

    // if the format is a depth format, we will need to have it use the correct
    // aspect flag
    VkImageAspectFlags aspectFlag = VK_IMAGE_ASPECT_COLOR_BIT;
    if (format == VK_FORMAT_D32_SFLOAT) {
        aspectFlag = VK_IMAGE_ASPECT_DEPTH_BIT;
    }

    // build a image-view for Athe image
    VkImageViewCreateInfo view_info = vkinit::imageview_create_info(format, outImage.image, aspectFlag);
    view_info.subresourceRange.levelCount = img_info.mipLevels;

    VK_CHECK(vkCreateImageView(vk_device, &view_info, nullptr, &outImage.image_view));

    return true;
}

ImageAllocation* VulkanRenderer::create_image_on_gpu_immidiate(void* data, VkExtent3D size, VkFormat format, VkImageUsageFlags usage, bool should_mipmap)
{
    ImageAllocation *new_image = allocated_images.emplace_back(new ImageAllocation);

    size_t data_size = size.depth * size.width * size.height * 4;
    BufferAllocation *uploadbuffer = create_buffer(data_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU, true);

    memcpy(uploadbuffer->info.pMappedData, data, data_size);

    create_image(*new_image, size, format, usage | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, should_mipmap);

    prepare_immidate_command_buffer_submit();

    vkutil::transition_image(immidiate_command_buffer, new_image->image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

    VkBufferImageCopy copyRegion = {};
    copyRegion.bufferOffset = 0;
    copyRegion.bufferRowLength = 0;
    copyRegion.bufferImageHeight = 0;

    copyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copyRegion.imageSubresource.mipLevel = 0;
    copyRegion.imageSubresource.baseArrayLayer = 0;
    copyRegion.imageSubresource.layerCount = 1;
    copyRegion.imageExtent = size;

    // copy the buffer into the image
    vkCmdCopyBufferToImage(immidiate_command_buffer, uploadbuffer->buffer, new_image->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);

    if (should_mipmap)
    {
        vkutil::generate_mipmaps(immidiate_command_buffer, new_image->image, VkExtent2D{ new_image->image_extent.width, new_image->image_extent.height });
    }
    else
    {
        vkutil::transition_image(immidiate_command_buffer, new_image->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }

    submit_immidiate_command_buffer();
    destroy_allocated_buffer(*uploadbuffer);

   
    return new_image;
}

GPUMeshBuffers VulkanRenderer::upload_mesh_immidiate(std::span<uint32_t> indices, std::span<Vertex> vertices)
{
    const size_t vertexBufferSize = vertices.size() * sizeof(Vertex);
    const size_t indexBufferSize = indices.size() * sizeof(uint32_t);

    GPUMeshBuffers newSurface;

    newSurface.vertex_buffer = create_buffer(vertexBufferSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, VMA_MEMORY_USAGE_GPU_ONLY);

    VkBufferDeviceAddressInfo deviceAdressInfo{ .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,.buffer = newSurface.vertex_buffer->buffer };
    newSurface.vertex_buffer_address = vkGetBufferDeviceAddress(vk_device, &deviceAdressInfo);

    newSurface.index_buffer = create_buffer(indexBufferSize, VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_GPU_ONLY);

    BufferAllocation *staging = create_buffer(vertexBufferSize + indexBufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_ONLY, true);

    void* data = staging->allocation->GetMappedData();

    // copy vertex buffer
    memcpy(data, vertices.data(), vertexBufferSize);
    // copy index buffer
    memcpy((char*)data + vertexBufferSize, indices.data(), indexBufferSize);

    prepare_immidate_command_buffer_submit();

    VkBufferCopy vertexCopy{ 0 };
    vertexCopy.dstOffset = 0;
    vertexCopy.srcOffset = 0;
    vertexCopy.size = vertexBufferSize;

    vkCmdCopyBuffer(immidiate_command_buffer, staging->buffer, newSurface.vertex_buffer->buffer, 1, &vertexCopy);

    VkBufferCopy indexCopy{ 0 };
    indexCopy.dstOffset = 0;
    indexCopy.srcOffset = vertexBufferSize;
    indexCopy.size = indexBufferSize;

    vkCmdCopyBuffer(immidiate_command_buffer, staging->buffer, newSurface.index_buffer->buffer, 1, &indexCopy);

    submit_immidiate_command_buffer();

    destroy_allocated_buffer(*staging);

    return newSurface;
}

FrameData& VulkanRenderer::get_current_frame_data()
{
    return frames[frame_number % FRAME_NUM];
}

FrameData& VulkanRenderer::get_last_frame_data()
{
    return frames[(frame_number - 1) % FRAME_NUM];
}

void VulkanRenderer::prepare_immidate_command_buffer_submit()
{
    VK_CHECK(vkResetFences(vk_device, 1, &immidiate_fence));
    VK_CHECK(vkResetCommandBuffer(immidiate_command_buffer, 0));

    // begin the command buffer recording. We will use this command buffer exactly once, so we want to let vulkan know that
    VkCommandBufferBeginInfo cmdBeginInfo = vkinit::command_buffer_begin_info(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

    VK_CHECK(vkBeginCommandBuffer(immidiate_command_buffer, &cmdBeginInfo));
}

void VulkanRenderer::submit_immidiate_command_buffer()
{
    VK_CHECK(vkEndCommandBuffer(immidiate_command_buffer));

    VkCommandBufferSubmitInfo cmdinfo = vkinit::command_buffer_submit_info(immidiate_command_buffer);
    VkSubmitInfo2 submit = vkinit::submit_info(&cmdinfo, nullptr, nullptr);

    // submit command buffer to the queue and execute it.
    //  _renderFence will now block until the graphic commands finish execution
    VK_CHECK(vkQueueSubmit2(vk_graphics_queue, 1, &submit, immidiate_fence));

    VK_CHECK(vkWaitForFences(vk_device, 1, &immidiate_fence, true, 9999999999));

    vkResetCommandBuffer(immidiate_command_buffer, 0);

}

void VulkanRenderer::destroy_allocated_image(const ImageAllocation& img)
{
    auto it = std::find(allocated_images.begin(), allocated_images.end(), &img);
    //allocated_images.erase(it);

    vkDestroyImageView(vk_device, img.image_view, nullptr);
    vmaDestroyImage(vma_allocator, img.image, img.allocation);
}

void VulkanRenderer::destroy_allocated_buffer(const BufferAllocation& buffer)
{
    auto it = std::find(allocated_buffers.begin(), allocated_buffers.end(), &buffer);
    //allocated_buffers.erase(it);

    vmaDestroyBuffer(vma_allocator, buffer.buffer, buffer.allocation);

}

void VulkanRenderer::resize_swapchain()
{
    vkDeviceWaitIdle(vk_device);

    destroy_swapchain();

    int w, h;
    SDL_GetWindowSize(_window, &w, &h);
    _windowExtent.width = w;
    _windowExtent.height = h;

    create_swapchain(_windowExtent.width, _windowExtent.height);

    resize_requested = false;
}

void GLTFMetallic_Roughness::build_pipelines(VulkanRenderer* renderer)
{
    VkShaderModule meshFragShader;
    if (!vkutil::load_shader_module("../../shaders/compiled/mesh.frag.spv", renderer->vk_device, &meshFragShader)) {
        fmt::println("Error when building the triangle fragment shader module");
    }

    VkShaderModule meshVertexShader;
    if (!vkutil::load_shader_module("../../shaders/compiled/mesh.vert.spv", renderer->vk_device, &meshVertexShader)) {
        fmt::println("Error when building the triangle vertex shader module");
    }

    VkPushConstantRange matrixRange{};
    matrixRange.offset = 0;
    matrixRange.size = sizeof(GPUDrawPushConstants);
    matrixRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    DescriptorLayoutBuilder layoutBuilder;
    layoutBuilder.add_binding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
    layoutBuilder.add_binding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
    layoutBuilder.add_binding(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);

    materialLayout = layoutBuilder.build(renderer->vk_device, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT);

    VkDescriptorSetLayout layouts[] = { renderer->gpu_scene_data_descriptor_layout, materialLayout };

    VkPipelineLayoutCreateInfo mesh_layout_info = vkinit::pipeline_layout_create_info();
    mesh_layout_info.setLayoutCount = 2;
    mesh_layout_info.pSetLayouts = layouts;
    mesh_layout_info.pPushConstantRanges = &matrixRange;
    mesh_layout_info.pushConstantRangeCount = 1;

    VkPipelineLayout newLayout;
    VK_CHECK(vkCreatePipelineLayout(renderer->vk_device, &mesh_layout_info, nullptr, &newLayout));

    opaquePipeline.layout = newLayout;
    transparentPipeline.layout = newLayout;

    // build the stage-create-info for both vertex and fragment stages. This lets
    // the pipeline know the shader modules per stage
    PipelineBuilder pipelineBuilder;

    pipelineBuilder.set_shaders(meshVertexShader, meshFragShader);

    pipelineBuilder.set_input_topology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);

    pipelineBuilder.set_polygon_mode(VK_POLYGON_MODE_FILL);

    pipelineBuilder.set_cull_mode(VK_CULL_MODE_NONE, VK_FRONT_FACE_CLOCKWISE);

    pipelineBuilder.set_multisampling_none();

    pipelineBuilder.disable_blending();

    pipelineBuilder.enable_depthtest(true, VK_COMPARE_OP_GREATER_OR_EQUAL);

    //render format
    pipelineBuilder.set_color_attachment_format(renderer->draw_image.image_format);
    pipelineBuilder.set_depth_format(renderer->depth_image.image_format);

    // use the triangle layout we created
    pipelineBuilder._pipelineLayout = newLayout;

    // finally build the pipeline
    opaquePipeline.pipeline = pipelineBuilder.build_pipeline(renderer->vk_device);

    // create the transparent variant
    pipelineBuilder.enable_blending_additive();

    pipelineBuilder.enable_depthtest(false, VK_COMPARE_OP_GREATER_OR_EQUAL);

    transparentPipeline.pipeline = pipelineBuilder.build_pipeline(renderer->vk_device);

    vkDestroyShaderModule(renderer->vk_device, meshFragShader, nullptr);
    vkDestroyShaderModule(renderer->vk_device, meshVertexShader, nullptr);

}

void GLTFMetallic_Roughness::clear_resources(VkDevice device)
{

}

MaterialInstance GLTFMetallic_Roughness::write_material(VkDevice device, MaterialPass pass, const MaterialResources& resources, DescriptorAllocatorGrowable& descriptorAllocator)
{
    MaterialInstance matData;
    matData.passType = pass;
    if (pass == MaterialPass::Transparent) {
        matData.pipeline = &transparentPipeline;
    }
    else {
        matData.pipeline = &opaquePipeline;
    }

    matData.materialSet = descriptorAllocator.allocate(device, materialLayout);


    writer.clear();
    writer.write_buffer(0, resources.dataBuffer, sizeof(MaterialConstants), resources.dataBufferOffset, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
    writer.write_image(1, resources.colorImage.image_view, resources.colorSampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
    writer.write_image(2, resources.metalRoughImage.image_view, resources.metalRoughSampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);

    writer.update_set(device, matData.materialSet);

    return matData;
}

void VulkanRenderer::cleanup()
{
    if (!is_initialized) return;

    // make sure the gpu has stopped doing its things
    vkDeviceWaitIdle(vk_device);

    // Cleanup imgui
    ImGui_ImplVulkan_Shutdown();
    vkDestroyDescriptorPool(vk_device, imgui_pool, nullptr);

    loadedScenes.clear();


    destroy_images();

    deleteVulkanObjects();



    destroy_swapchain();

    vkDestroySurfaceKHR(vk_instance, vk_surface, nullptr);

    //char* statsString;
    //vmaBuildStatsString(vma_allocator, &statsString, VK_TRUE);
    //printf("%s\n", statsString); // Print the report to see details about the allocations.
    //vmaFreeStatsString(vma_allocator, statsString);

    vmaDestroyAllocator(vma_allocator);

    vkDestroyDevice(vk_device, nullptr);
    vkb::destroy_debug_utils_messenger(vk_instance, vk_debug_messenger);
    vkDestroyInstance(vk_instance, nullptr);

    SDL_DestroyWindow(_window);

}

void VulkanRenderer::deleteVulkanObjects()
{
    for (int i = 0; i < FRAME_NUM; i++)
    {
        destroy_allocated_buffer(*frames[i].gpuSceneDataBuffer);
        frames[i]._frameDescriptors.destroy_pools(vk_device);

        vkDestroyFence(vk_device, frames[i]._renderFence, nullptr);
        vkDestroySemaphore(vk_device, frames[i]._swapchainSemaphore, nullptr);
        vkDestroySemaphore(vk_device, frames[i]._renderSemaphore, nullptr);

        vkDestroyCommandPool(vk_device, frames[i]._commandPool, nullptr);
    }

    vkDestroyCommandPool(vk_device, immidiate_command_pool, nullptr);

    //destroy_allocated_buffer(*rectangle.indexBuffer);
    //destroy_allocated_buffer(*rectangle.vertexBuffer);

    for (BufferAllocation* buffer : allocated_buffers)
    {
        destroy_allocated_buffer(*buffer);
    }


    for (ImageAllocation* image : allocated_images)
    {
        destroy_allocated_image(*image);
    }
    allocated_images.clear();
    //
    destroy_allocated_image(draw_image);
    destroy_allocated_image(depth_image);




    vkDestroyDescriptorPool(vk_device, global_descriptor_allocator.pool, nullptr);
    vkDestroyDescriptorSetLayout(vk_device, draw_image_descriptor_layout, nullptr);
    vkDestroyDescriptorSetLayout(vk_device, gpu_scene_data_descriptor_layout, nullptr);

    vkDestroyPipelineLayout(vk_device, gradient_pipeline_layout, nullptr);
    vkDestroyPipeline(vk_device, sky.pipeline, nullptr);
    vkDestroyPipeline(vk_device, gradient.pipeline, nullptr);


    vkDestroyFence(vk_device, immidiate_fence, nullptr);




    vkDestroySampler(vk_device, default_sampler_nearest, nullptr);
    vkDestroySampler(vk_device, default_sampler_linear, nullptr);



    vkDestroyDescriptorSetLayout(vk_device, rough_metal_material.materialLayout, nullptr);
    vkDestroyPipelineLayout(vk_device, rough_metal_material.opaquePipeline.layout, nullptr);
    // transparentPipeline layout is the same as opaquePipeline, so no don't destroy it
    //vkDestroyPipelineLayout(_device, metalRoughMaterial.transparentPipeline.layout, nullptr);
    vkDestroyPipeline(vk_device, rough_metal_material.opaquePipeline.pipeline, nullptr);
    vkDestroyPipeline(vk_device, rough_metal_material.transparentPipeline.pipeline, nullptr);



}

void VulkanRenderer::destroy_swapchain()
{
    vkDestroySwapchainKHR(vk_device, vk_swapchain, nullptr);

    // destroy swapchain resources
    for (int i = 0; i < swapchain_image_views.size(); i++) {

        vkDestroyImageView(vk_device, swapchain_image_views[i], nullptr);
    }
}

void VulkanRenderer::destroy_buffers()
{
}

void VulkanRenderer::destroy_images()
{
}
