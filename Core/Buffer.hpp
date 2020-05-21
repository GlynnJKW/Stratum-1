#pragma once

#include <Core/Device.hpp>
#include <Util/Util.hpp>

class Buffer {
public:
	const std::string mName;

	ENGINE_EXPORT Buffer(const std::string& name, ::Device* device, const void* data, VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags memoryFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	ENGINE_EXPORT Buffer(const std::string& name, ::Device* device, const void* data, VkDeviceSize size, VkBufferUsageFlags usage, VkFormat viewFormat, VkMemoryPropertyFlags memoryFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	ENGINE_EXPORT Buffer(const std::string& name, ::Device* device, VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags memoryFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	ENGINE_EXPORT Buffer(const std::string& name, ::Device* device, VkDeviceSize size, VkBufferUsageFlags usage, VkFormat viewFormat, VkMemoryPropertyFlags memoryFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	ENGINE_EXPORT Buffer(const Buffer& src);
	ENGINE_EXPORT ~Buffer();

	// Upload data from the host to the device
	// If this buffer is not host visible, then a staging buffer will be created and the data will be copied with Buffer::CopyFrom()
	// If this buffer is not host visible and does not have the VK_BUFFER_USAGE_TRANSFER_DST_BIT flag, then the buffer will be re-created with VK_BUFFER_USAGE_TRANSFER_DST_BIT
	// If this buffer is host visible, then the data is immediately memcpy'd
	ENGINE_EXPORT void Upload(const void* data, VkDeviceSize size);

	inline void* MappedData() const { return mMemory.mMapped; }

	inline VkDeviceSize Size() const { return mSize; }
	inline VkBufferUsageFlags Usage() const { return mUsageFlags; }
	inline VkMemoryPropertyFlags MemoryProperties() const { return mMemoryProperties; }
	inline const DeviceMemoryAllocation& Memory() const { return mMemory; }

	ENGINE_EXPORT void CopyFrom(const Buffer& other);
	Buffer& operator=(const Buffer& other) = delete;

	// The view used for a texel buffer. Can be VK_NULL_HANDLE if the buffer is not a texel buffer.
	inline const VkBufferView& View() const { return mView; }

	inline ::Device* Device() const { return mDevice; }
	inline operator VkBuffer() const { return mBuffer; }

private:
	::Device* mDevice;
	VkBuffer mBuffer;
	DeviceMemoryAllocation mMemory;

	VkBufferView mView;
	VkFormat mViewFormat;

	VkDeviceSize mSize;

	VkBufferUsageFlags mUsageFlags;
	VkMemoryPropertyFlags mMemoryProperties;

	ENGINE_EXPORT void Allocate();
};