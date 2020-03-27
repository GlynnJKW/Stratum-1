#pragma once

#include <Util/Util.hpp>

class Instance;
class Scene;

class XRRuntime {
public:
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

    inline virtual void PollEvents() {}
};