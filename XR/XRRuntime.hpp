#pragma once

#include <Util/Util.hpp>

class CommandBuffer;
class Instance;
class Scene;

class XRRuntime {
public:
    inline virtual ~XRRuntime() {};

    // Called before the Vulkan instance and device are created
    virtual bool Init() = 0;

    // Called before vkCreateInstance
    // Use to request any Vulkan instance extensions
    inline virtual void PreInstanceInit(Instance* instance) {}
    
    // Called before vkCreateDevice
    // Use to request any Vulkan device extensions
    inline virtual void PreDeviceInit(Instance* instance, VkPhysicalDevice device) {}

    // Called after the Vulkan instance and device are created, before the scene loop starts
    virtual bool InitScene(Scene* scene) = 0;

    inline virtual void BeginFrame() {}
    // Called after the scene has rendered, including resolve/copy to the target window.
    // Scene camera resolve buffers are in VK_IMAGE_LAYOUT_GENERAL
    inline virtual void PostRender(CommandBuffer* commandBuffer) {}
    // Called after command buffers are executed, just before the window presents
    inline virtual void EndFrame() {}
};