// vulkan_guide.h : Include file for standard system include files,
// or project specific include files.

#pragma once

#include <vk_types.h>

#include <deque>
#include <functional>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

#include <vk_mem_alloc.h>

#include <camera.h>
#include <vk_descriptors.h>
#include <vk_loader.h>
#include <vk_pipelines.h>
struct MeshAsset;
namespace fastgltf {
    struct Mesh;
}

struct ComputePushConstants
{
    glm::vec4 data1;
    glm::vec4 data2;
    glm::vec4 data3;
    glm::vec4 data4;
};

struct ComputeEffect
{
    const char* name;

    VkPipeline pipeline;
    VkPipelineLayout layout;

    ComputePushConstants push_constants_data;
};

struct RenderObject
{
    uint32_t indexCount;
    uint32_t firstIndex;
    VkBuffer indexBuffer;

    MaterialInstance* material;
    Bounds bounds;
    glm::mat4 transform;
    VkDeviceAddress vertexBufferAddress;
};

struct FrameData
{
    VkSemaphore _swapchainSemaphore, _renderSemaphore;
    VkFence _renderFence;

    DescriptorAllocatorGrowable _frameDescriptors;

    VkCommandPool _commandPool;
    VkCommandBuffer _mainCommandBuffer;

    AllocatedBuffer *gpuSceneDataBuffer;
};

constexpr unsigned int FRAME_NUM = 2;


struct DrawContext
{
    std::vector<RenderObject> OpaqueSurfaces;
    std::vector<RenderObject> TransparentSurfaces;
};

struct RendererStats
{
    float frametime;
    int triangle_count;
    int drawcall_count;
    float mesh_draw_time;
};

struct GLTFMetallic_Roughness
{
    MaterialPipeline opaquePipeline;
    MaterialPipeline transparentPipeline;

    VkDescriptorSetLayout materialLayout;

    struct MaterialConstants
    {
        glm::vec4 colorFactors;
        glm::vec4 metal_rough_factors;
        //padding, we need it anyway for uniform buffers
        glm::vec4 extra[14];
    };

    struct MaterialResources
    {
        AllocatedImage colorImage;
        VkSampler colorSampler;
        AllocatedImage metalRoughImage;
        VkSampler metalRoughSampler;
        VkBuffer dataBuffer;
        uint32_t dataBufferOffset;
    };

    DescriptorWriter writer;

    void build_pipelines(VulkanRenderer* engine);
    void clear_resources(VkDevice device);

    MaterialInstance write_material(VkDevice device, MaterialPass pass, const MaterialResources& resources, DescriptorAllocatorGrowable& descriptorAllocator);
};

//
class VulkanRenderer
{
public:
    // initializes window and vulkan
    void init();
    // run main loop
    void run();
    // shuts down the engine
    void cleanup();
    
    // Singleton
    static VulkanRenderer& Get();

private:
    // init
    void init_sdl();
    void init_vulkan();
    void init_swapchain();
    void init_command_pools_and_buffers();
    void init_sync_structures();
    void init_descriptors();
    void init_pipelines();
    void init_background_pipelines();
    void init_default_data();
    void init_renderables();
    void init_imgui();

    void create_swapchain(uint32_t width, uint32_t height);
    void resize_swapchain();

public:
    bool is_initialized = false;
    int frame_number = 0;

    VkExtent2D _windowExtent{ 1700, 900 };

    // SDL
    struct SDL_Window* _window = nullptr;

    // Vulkan
    VkInstance vk_instance;
    VkDebugUtilsMessengerEXT vk_debug_messenger;
    VkPhysicalDevice vk_gpu; // Chosen gpu
    VkDevice vk_device;
    VkQueue vk_graphics_queue;
    uint32_t vk_graphics_queue_family;
    VkSurfaceKHR vk_surface;
    VkSwapchainKHR vk_swapchain;
    VkFormat vk_swapchain_image_format;
    VkExtent2D swapchain_extent;
    VkExtent2D draw_extent;

    // Descriptors
    DescriptorAllocator global_descriptor_allocator;

    VkDescriptorSet draw_image_descriptors;
    VkDescriptorSetLayout draw_image_descriptor_layout;

    VkDescriptorSetLayout gpu_scene_data_descriptor_layout;

    VkDescriptorPool imgui_pool;

    // Immediate command structures
    VkFence         immidiate_fence;
    VkCommandBuffer immidiate_command_buffer;
    VkCommandPool   immidiate_command_pool;
    void prepare_immidate_command_buffer_submit();
    void submit_immidiate_command_buffer();

    // Pipelines
    VkPipeline gradient_pipeline;
    VkPipelineLayout gradient_pipeline_layout;

    // Allocations
    VmaAllocator vma_allocator;

    AllocatedImage white_image;
    AllocatedImage black_image;
    AllocatedImage grey_image;
    AllocatedImage error_checkerboard_image;

    AllocatedImage draw_image;
    AllocatedImage depth_image;

    // Samplers
    VkSampler default_sampler_linear;
    VkSampler default_sampler_nearest;

    // Swapchain
    std::vector<VkImage> swapchain_images;
    std::vector<VkImageView> swapchain_image_views;

    // Background
    ComputeEffect sky;
    ComputeEffect gradient;
    std::vector<ComputeEffect> background_effects;
    int currentBackgroundEffect = 0;

    // Materials
    GLTFMetallic_Roughness rough_metal_material;

    //Misc
    float renderScale = 1;
    bool resize_requested = false;
    bool freeze_rendering = false;

    // Frames
    FrameData frames[FRAME_NUM];
    FrameData& get_current_frame_data();
    FrameData& get_last_frame_data();

    // Draw
    void draw();
    void draw_main(VkCommandBuffer cmd);
    void draw_imgui(VkCommandBuffer cmd, VkImageView targetImageView);
    void draw_geometry(VkCommandBuffer cmd);

    // Allocations
    std::vector<AllocatedImage*>  allocated_images;
    std::vector<AllocatedBuffer*> allocated_buffers;

    void destroy_allocated_image(const AllocatedImage& img);
    void destroy_allocated_buffer(const AllocatedBuffer& buffer);

    //Create
    AllocatedBuffer* create_buffer(size_t allocSize, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage);
    bool create_image(AllocatedImage& outImage, VkExtent3D size, VkFormat format, VkImageUsageFlags usage, bool mipmapped = false);
    AllocatedImage* create_image_on_gpu_immidiate(void* data, VkExtent3D size, VkFormat format, VkImageUsageFlags usage, bool mipmapped = false);
    // upload a mesh into a pair of gpu buffers. If descriptor allocator is not
    // null, it will also create a descriptor that points to the vertex buffer
    GPUMeshBuffers upload_mesh_immidiate(std::span<uint32_t> indices, std::span<Vertex> vertices);


    std::unordered_map<std::string, std::shared_ptr<LoadedGLTF>> loadedScenes;

    RendererStats stats;

    Camera mainCamera;
    void update_scene();
    GPUMeshBuffers rectangle;
    DrawContext drawCommands;
    GPUSceneData sceneData;

private:
    // destroy

    void destroy_buffers();
    void destroy_images();


    void destroy_swapchain();


    void deleteVulkanObjects();
};