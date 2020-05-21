#include <Core/Buffer.hpp>
#include <Core/Device.hpp>
#include <Core/Instance.hpp>
#include <Core/CommandBuffer.hpp>
#include <Core/Window.hpp>
#include <Util/Profiler.hpp>
#include <Util/Util.hpp>


//#define PRINT_VK_ALLOCATIONS

// 4kb blocks
#define MEM_BLOCK_SIZE (4*1024)
// 4mb min allocation
#define MEM_MIN_ALLOC (4*1024*1024)

using namespace std;

/*static*/ bool Device::FindQueueFamilies(VkPhysicalDevice device, VkSurfaceKHR surface, uint32_t& graphicsFamily, uint32_t& presentFamily) {
	uint32_t queueFamilyCount = 0;
	vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);
	std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
	vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies.data());

	bool g = false;
	bool p = false;

	uint32_t i = 0;
	for (const auto& queueFamily : queueFamilies) {
		if (queueFamily.queueCount > 0 && queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT) {
			graphicsFamily = i;
			g = true;
		}

		VkBool32 presentSupport = false;
		vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface, &presentSupport);

		if (queueFamily.queueCount > 0 && presentSupport) {
			presentFamily = i;
			p = true;
		}

		i++;
	}

	return g && p;
}

void Device::FrameContext::Reset() {
	if (mFences.size()) {
		PROFILER_BEGIN("Wait for GPU");
		vector<VkFence> fences(mFences.size());
		for (uint32_t i = 0; i < mFences.size(); i++)
			fences[i] = *mFences[i];
		ThrowIfFailed(vkWaitForFences(*mDevice, fences.size(), fences.data(), true, numeric_limits<uint64_t>::max()), "vkWaitForFences failed");
		PROFILER_END;
	}

	mFences.clear();
	mSemaphores.clear();

	PROFILER_BEGIN("Clear old buffers");
	for (auto it = mTempBuffers.begin(); it != mTempBuffers.end();) {
		if (it->second == 1) {
			safe_delete(it->first);
			it = mTempBuffers.erase(it);
		}

		it->second--;
		it++;
	}
	PROFILER_END;

	for (Buffer* b : mTempBuffersInUse)
		mTempBuffers.push_back(make_pair(b, 8));
	for (DescriptorSet* ds : mTempDescriptorSetsInUse)
		mTempDescriptorSets[ds->Layout()].push_back(make_pair(ds, 8));

	mTempBuffersInUse.clear();
	mTempDescriptorSetsInUse.clear();
}
Device::FrameContext::~FrameContext() {
	Reset();
	for (auto b : mTempBuffers)
		safe_delete(b.first);
	for (auto kp : mTempDescriptorSets)
		while (kp.second.size()) {
			auto front = kp.second.begin();
			safe_delete(front->first);
			kp.second.erase(front);
		}
}

Device::Device(::Instance* instance, VkPhysicalDevice physicalDevice, uint32_t physicalDeviceIndex, uint32_t graphicsQueueFamily, uint32_t presentQueueFamily, const set<string>& deviceExtensions, vector<const char*> validationLayers)
	: mInstance(instance), mFrameContexts(nullptr), mGraphicsQueueFamilyIndex(graphicsQueueFamily), mPresentQueueFamilyIndex(presentQueueFamily),
	mFrameContextIndex(0), mDescriptorSetCount(0), mMemoryAllocationCount(0), mMemoryUsage(0) {

	#ifdef ENABLE_DEBUG_LAYERS
	SetDebugUtilsObjectNameEXT = (PFN_vkSetDebugUtilsObjectNameEXT)vkGetInstanceProcAddr(*instance, "vkSetDebugUtilsObjectNameEXT");
	CmdBeginDebugUtilsLabelEXT = (PFN_vkCmdBeginDebugUtilsLabelEXT)vkGetInstanceProcAddr(*instance, "vkCmdBeginDebugUtilsLabelEXT");
	CmdEndDebugUtilsLabelEXT   = (PFN_vkCmdEndDebugUtilsLabelEXT)  vkGetInstanceProcAddr(*instance, "vkCmdEndDebugUtilsLabelEXT");
	#endif

	mPhysicalDevice = physicalDevice;
	mMaxMSAASamples = GetMaxUsableSampleCount();
	mPhysicalDeviceIndex = physicalDeviceIndex;

	vector<const char*> deviceExts;
	for (const string& s : deviceExtensions)
		deviceExts.push_back(s.c_str());

	#pragma region get queue info
	set<uint32_t> uniqueQueueFamilies{ mGraphicsQueueFamilyIndex, mPresentQueueFamilyIndex };
	vector<VkDeviceQueueCreateInfo> queueCreateInfos;
	float queuePriority = 1.0f;
	for (uint32_t queueFamily : uniqueQueueFamilies) {
		VkDeviceQueueCreateInfo queueCreateInfo = {};
		queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
		queueCreateInfo.queueFamilyIndex = queueFamily;
		queueCreateInfo.queueCount = 1;
		queueCreateInfo.pQueuePriorities = &queuePriority;
		queueCreateInfos.push_back(queueCreateInfo);
	}
	#pragma endregion

	#pragma region create logical device and queues
	VkPhysicalDeviceFeatures deviceFeatures = {};
	deviceFeatures.samplerAnisotropy = VK_TRUE;
	deviceFeatures.fillModeNonSolid = VK_TRUE;
	deviceFeatures.wideLines = VK_TRUE;
	deviceFeatures.shaderStorageImageExtendedFormats = VK_TRUE;
	deviceFeatures.sparseBinding = VK_TRUE;
	deviceFeatures.shaderImageGatherExtended = VK_TRUE;

	VkPhysicalDeviceDescriptorIndexingFeaturesEXT indexingFeatures = {};
	indexingFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES_EXT;
	indexingFeatures.pNext = nullptr;
	indexingFeatures.descriptorBindingPartiallyBound = VK_TRUE;
	indexingFeatures.runtimeDescriptorArray = VK_TRUE;

	VkDeviceCreateInfo createInfo = {};
	createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
	createInfo.queueCreateInfoCount = (uint32_t)queueCreateInfos.size();
	createInfo.pQueueCreateInfos = queueCreateInfos.data();
	createInfo.pEnabledFeatures = &deviceFeatures;
	createInfo.enabledExtensionCount = (uint32_t)deviceExts.size();
	createInfo.ppEnabledExtensionNames = deviceExts.data();
	createInfo.enabledLayerCount = (uint32_t)validationLayers.size();
	createInfo.ppEnabledLayerNames = validationLayers.data();
	createInfo.pNext = &indexingFeatures;
	ThrowIfFailed(vkCreateDevice(mPhysicalDevice, &createInfo, nullptr, &mDevice), "vkCreateDevice failed");

	VkPhysicalDeviceProperties properties = {};
	vkGetPhysicalDeviceProperties(mPhysicalDevice, &properties);
	string name = "Device " + to_string(properties.deviceID) + ": " + properties.deviceName;
	SetObjectName(mDevice, name, VK_OBJECT_TYPE_DEVICE);
	mLimits = properties.limits;

	mGraphicsQueueIndex = 0;
	mPresentQueueIndex = 0;

	vkGetDeviceQueue(mDevice, mGraphicsQueueFamilyIndex, mGraphicsQueueIndex, &mGraphicsQueue);
	vkGetDeviceQueue(mDevice, mPresentQueueFamilyIndex, mPresentQueueIndex, &mPresentQueue);
	SetObjectName(mGraphicsQueue, name + " Graphics Queue", VK_OBJECT_TYPE_QUEUE);
	SetObjectName(mPresentQueue, name + " Present Queue", VK_OBJECT_TYPE_QUEUE);
	#pragma endregion

	#pragma region PipelineCache and DesriptorPool
	char* cacheData = nullptr;
	ifstream cacheFile("./pcache", ios::binary | ios::ate);

	VkPipelineCacheCreateInfo cacheInfo = {};
	cacheInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
	if (cacheFile.is_open()) {
		size_t size = cacheFile.tellg();
		cacheData = new char[size];
		cacheFile.seekg(0, ios::beg);
		cacheFile.read(cacheData, size);

		cacheInfo.pInitialData = cacheData;
		cacheInfo.initialDataSize = size;
	}
	vkCreatePipelineCache(mDevice, &cacheInfo, nullptr, &mPipelineCache);
	safe_delete_array(cacheData);
	
	VkDescriptorPoolSize type_count[5] {
		{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,			min(4096u, mLimits.maxDescriptorSetUniformBuffers) },
		{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,	min(4096u, mLimits.maxDescriptorSetSampledImages) },
		{ VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,				min(4096u, mLimits.maxDescriptorSetSampledImages) },
		{ VK_DESCRIPTOR_TYPE_SAMPLER,					min(4096u, mLimits.maxDescriptorSetSamplers) },
		{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,			min(4096u, mLimits.maxDescriptorSetStorageBuffers) },
	};

	VkDescriptorPoolCreateInfo poolInfo = {};
	poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
	poolInfo.poolSizeCount = 5;
	poolInfo.pPoolSizes = type_count;
	poolInfo.maxSets = 8192;

	ThrowIfFailed(vkCreateDescriptorPool(mDevice, &poolInfo, nullptr, &mDescriptorPool), "vkCreateDescriptorPool failed");
	SetObjectName(mDescriptorPool, name, VK_OBJECT_TYPE_DESCRIPTOR_POOL);
	#pragma endregion

	vkGetPhysicalDeviceMemoryProperties(mPhysicalDevice, &mMemoryProperties);
}
Device::~Device() {
	Flush();
	safe_delete_array(mFrameContexts);
	vkDestroyDescriptorPool(mDevice, mDescriptorPool, nullptr);

	size_t size = 0;
	vkGetPipelineCacheData(mDevice, mPipelineCache, &size, nullptr);
	char* cacheData = new char[size];
	vkGetPipelineCacheData(mDevice, mPipelineCache, &size, cacheData);
	ofstream output("./pcache", ios::binary);
	output.write(cacheData, size);
	delete[] cacheData;

	vkDestroyPipelineCache(mDevice, mPipelineCache, nullptr);
	for (auto& p : mCommandBuffers)
		vkDestroyCommandPool(mDevice, p.first, nullptr);
	
	for (auto kp : mMemoryAllocations) {
		for (uint32_t i = 0; i < kp.second.size(); i++) {
			for (auto it = kp.second[i].mAllocations.begin(); it != kp.second[i].mAllocations.begin(); it++)
				fprintf_color(COLOR_RED, stderr, "Device memory leak detected. Tag: %s\n", it->mTag.c_str());
			vkFreeMemory(mDevice, kp.second[i].mMemory, nullptr);
		}
	}

	vkDestroyDevice(mDevice, nullptr);
}

VkSampleCountFlagBits Device::GetMaxUsableSampleCount() {
	VkPhysicalDeviceProperties physicalDeviceProperties;
	vkGetPhysicalDeviceProperties(mPhysicalDevice, &physicalDeviceProperties);

	VkSampleCountFlags counts = std::min(physicalDeviceProperties.limits.framebufferColorSampleCounts, physicalDeviceProperties.limits.framebufferDepthSampleCounts);
	if (counts & VK_SAMPLE_COUNT_64_BIT) { return VK_SAMPLE_COUNT_64_BIT; }
	if (counts & VK_SAMPLE_COUNT_32_BIT) { return VK_SAMPLE_COUNT_32_BIT; }
	if (counts & VK_SAMPLE_COUNT_16_BIT) { return VK_SAMPLE_COUNT_16_BIT; }
	if (counts & VK_SAMPLE_COUNT_8_BIT) { return VK_SAMPLE_COUNT_8_BIT; }
	if (counts & VK_SAMPLE_COUNT_4_BIT) { return VK_SAMPLE_COUNT_4_BIT; }
	if (counts & VK_SAMPLE_COUNT_2_BIT) { return VK_SAMPLE_COUNT_2_BIT; }

	return VK_SAMPLE_COUNT_1_BIT;
}

void Device::Flush() {
	//vkDeviceWaitIdle(mDevice);
	lock_guard<mutex> lock(mCommandPoolMutex);
	for (auto& p : mCommandBuffers) {
		while (p.second.size()) {
			p.second.front()->mSignalFence->Wait();
			p.second.pop();
		}
	}
	for (uint32_t i = 0; i < MaxFramesInFlight(); i++)
		mFrameContexts[i].Reset();
}

void Device::SetObjectName(void* object, const string& name, VkObjectType type) const {
	#ifdef ENABLE_DEBUG_LAYERS
	VkDebugUtilsObjectNameInfoEXT info = {};
	info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
	info.objectHandle = (uint64_t)object;
	info.objectType = type;
	info.pObjectName = name.c_str();
	SetDebugUtilsObjectNameEXT(mDevice, &info);
	#endif
}

void Device::PrintAllocations() {
	VkDeviceSize used = 0;
	VkDeviceSize available = 0;
	VkDeviceSize total = 0;

	for (uint32_t i = 0; i < mMemoryProperties.memoryHeapCount; i++)
		total += mMemoryProperties.memoryHeaps[i].size;

	for (auto kp : mMemoryAllocations)
		for (auto a : kp.second) {
			used += a.mSize;
			for (auto av : a.mAvailable) available += av.second;
		}

	if (used == 0) {
		printf_color(COLOR_YELLOW, "%s", "Using 0 B");
		return;
	}

	float percentTotal = 100.f * (float)used / (float)total;
	float percentWasted = 100.f * (float)available / (float)used;

	if (used < 1024)
		printf_color(COLOR_YELLOW, "Using %lu B (%.1f%%) - %.1f%% wasted", used, percentTotal, percentWasted);
	else if (used < 1024 * 1024)
		printf_color(COLOR_YELLOW, "Using %.3f KiB (%.1f%%) - %.1f%%wasted", used / 1024.f, percentTotal, percentWasted);
	else
		printf_color(COLOR_YELLOW, "Using %.3f MiB (%.1f%%) - %.1f%% wasted", used / (1024.f * 1024.f), percentTotal, percentWasted);
}

bool Device::Allocation::SubAllocate(const VkMemoryRequirements& requirements, DeviceMemoryAllocation& allocation, const string& tag) {
if (mAvailable.empty()) return false;

	VkDeviceSize blockSize = 0;
	VkDeviceSize memLocation = 0;
	VkDeviceSize memSize = 0;

	// find smallest block that can fit the allocation
	auto block = mAvailable.end();
	for (auto it = mAvailable.begin(); it != mAvailable.end(); it++) {
		VkDeviceSize offset = it->first ? AlignUp(it->first, requirements.alignment) : 0;
		VkDeviceSize blockEnd = AlignUp(offset + requirements.size, MEM_BLOCK_SIZE);

		if (blockEnd > it->first + it->second) continue;

		if (block == mAvailable.end() || it->second < block->second) {
			memLocation = offset;
			memSize = blockEnd - offset;
			blockSize = blockEnd - it->first;
			block = it;
		}
	}
	if (block == mAvailable.end()) return false;

	allocation.mDeviceMemory = mMemory;
	allocation.mOffset = memLocation;
	allocation.mSize = memSize;
	allocation.mMapped = ((uint8_t*)mMapped) + memLocation;
	allocation.mTag = tag;

	if (block->second > blockSize) {
		// still room left after this allocation, shift this block over
		block->first += blockSize;
		block->second -= blockSize;
	} else
		mAvailable.erase(block);

	mAllocations.push_back(allocation);

	return true;
}
void Device::Allocation::Deallocate(const DeviceMemoryAllocation& allocation) {
	if (allocation.mDeviceMemory != mMemory) return;

	for (auto it = mAllocations.begin(); it != mAllocations.end(); it++)
		if (it->mOffset == allocation.mOffset) {
			mAllocations.erase(it);
			break;
		}

	VkDeviceSize end = allocation.mOffset + allocation.mSize;

	auto firstAfter = mAvailable.end();
	auto startBlock = mAvailable.end();
	auto endBlock = mAvailable.end();

	for (auto it = mAvailable.begin(); it != mAvailable.end(); it++) {
		if (it->first > allocation.mOffset && (firstAfter == mAvailable.end() || it->first < firstAfter->first)) firstAfter = it;

		if (it->first == end)
			endBlock = it;
		if (it->first + it->second == allocation.mOffset)
			startBlock = it;
	}

	if (startBlock == endBlock && startBlock != mAvailable.end()) throw; // this should NOT happen

	// merge blocks

	if (startBlock == mAvailable.end() && endBlock == mAvailable.end())
		// block isn't adjacent to any other blocks
		mAvailable.insert(firstAfter, make_pair(allocation.mOffset, allocation.mSize));
	else if (startBlock == mAvailable.end()) {
		//  --------     |---- allocation ----|---- endBlock ----|
		endBlock->first = allocation.mOffset;
		endBlock->second += allocation.mSize;
	} else if (endBlock == mAvailable.end()) {
		//  |---- startBlock ----|---- allocation ----|     --------
		startBlock->second += allocation.mSize;
	} else {
		//  |---- startBlock ----|---- allocation ----|---- endBlock ----|
		startBlock->second += allocation.mSize + endBlock->second;
		mAvailable.erase(endBlock);
	}
}

DeviceMemoryAllocation Device::AllocateMemory(const VkMemoryRequirements& requirements, VkMemoryPropertyFlags properties, const string& tag) {
	lock_guard<mutex> lock(mMemoryMutex);

	int32_t memoryType = -1;
	for (uint32_t i = 0; i < mMemoryProperties.memoryTypeCount; i++) {
		if ((requirements.memoryTypeBits & (1 << i)) && ((mMemoryProperties.memoryTypes[i].propertyFlags & properties) == properties)) {
			memoryType = i;
			break;
		}
	}
	if (memoryType == -1) {
		fprintf_color(COLOR_RED, stderr, "%s", "Failed to find suitable memory type!");
		throw;
	}

	DeviceMemoryAllocation alloc = {};
	alloc.mMemoryType = memoryType;

	vector<Allocation>& allocations = mMemoryAllocations[memoryType];

	for (uint32_t i = 0; i < allocations.size(); i++)
		if (allocations[i].SubAllocate(requirements, alloc, tag))
			return alloc;


	// Failed to sub-allocate, make a new allocation

	allocations.push_back({});
	Allocation& allocation = allocations.back();
	
	VkMemoryAllocateInfo info = {};
	info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	info.memoryTypeIndex = memoryType;
	info.allocationSize = max((uint64_t)MEM_MIN_ALLOC, AlignUp(requirements.size, MEM_BLOCK_SIZE));
	if (VkResult err = vkAllocateMemory(mDevice, &info, nullptr, &allocation.mMemory)) {
		VkDeviceSize deviceMemSize = 0;
		for (uint32_t i = 0; i < mMemoryProperties.memoryHeapCount; i++)
			if (mMemoryProperties.memoryHeaps[i].flags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)
				deviceMemSize += mMemoryProperties.memoryHeaps[i].size;

		switch (err) {
		case VK_ERROR_OUT_OF_HOST_MEMORY:
			fprintf_color(COLOR_RED, stderr, "vkAllocateMemory failed: VK_ERROR_OUT_OF_HOST_MEMORY\n");
			throw;
		case VK_ERROR_OUT_OF_DEVICE_MEMORY:
			fprintf_color(COLOR_RED, stderr, "vkAllocateMemory failed: VK_ERROR_OUT_OF_DEVICE_MEMORY (%.3f / %.3f)\n", (mMemoryUsage + info.allocationSize) / (1024.f * 1024.f), deviceMemSize / (1024.f * 1024.f));
			throw;
		case VK_ERROR_TOO_MANY_OBJECTS:
			fprintf_color(COLOR_RED, stderr, "vkAllocateMemory failed: VK_ERROR_TOO_MANY_OBJECTS\n");
			throw;
		case VK_ERROR_INVALID_EXTERNAL_HANDLE:
			fprintf_color(COLOR_RED, stderr, "vkAllocateMemory failed: VK_ERROR_INVALID_EXTERNAL_HANDLE\n");
			throw;
		default:
			break;
		}
	}
	allocation.mSize = info.allocationSize;
	allocation.mAvailable = { make_pair((VkDeviceSize)0, allocation.mSize) };
	mMemoryAllocationCount++;

	if (mMemoryProperties.memoryTypes[memoryType].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)
		mMemoryUsage += allocation.mSize;

	if (properties & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)
		vkMapMemory(mDevice, allocation.mMemory, 0, allocation.mSize, 0, &allocation.mMapped);

	if (!allocation.SubAllocate(requirements, alloc, tag)) {
		fprintf_color(COLOR_RED, stderr, "%s", "Failed to allocate memory\n");
		throw;
	}

	#ifdef PRINT_VK_ALLOCATIONS
	if (info.allocationSize < 1024)
		printf_color(COLOR_YELLOW, "Allocated %llu B of type %u\t-- ", info.allocationSize, info.memoryTypeIndex);
	else if (info.allocationSize < 1024 * 1024)
		printf_color(COLOR_YELLOW, "Allocated %.3f KiB of type %u\t-- ", info.allocationSize / 1024, info.memoryTypeIndex);
	else if (info.allocationSize < 1024 * 1024 * 1024)
		printf_color(COLOR_YELLOW, "Allocated %.3f MiB of type %u\t-- ", info.allocationSize / (1024.f * 1024.f), info.memoryTypeIndex);
	else
		printf_color(COLOR_YELLOW, "Allocated %.3f GiB of type %u\t-- ", info.allocationSize / (1024.f * 1024.f * 1024.f), info.memoryTypeIndex);
	PrintAllocations();
	printf_color(COLOR_YELLOW, "\n");
	#endif

	return alloc;
}
void Device::FreeMemory(const DeviceMemoryAllocation& allocation) {
	lock_guard<mutex> lock(mMemoryMutex);

	vector<Allocation>& allocations = mMemoryAllocations[allocation.mMemoryType];
	for (auto it = allocations.begin(); it != allocations.end();){
		if (it->mMemory == allocation.mDeviceMemory) {
			it->Deallocate(allocation);
			if (it->mAvailable.size() == 1 && it->mAvailable.begin()->second == it->mSize) {
				vkFreeMemory(mDevice, it->mMemory, nullptr);
				mMemoryAllocationCount--;
				if (mMemoryProperties.memoryTypes[allocation.mMemoryType].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)
					mMemoryUsage -= it->mSize;
				#ifdef PRINT_VK_ALLOCATIONS
				if (allocation.mSize < 1024)
					printf_color(COLOR_YELLOW, "Freed %lu B of type %u\t- ", allocation.mSize, allocation.mMemoryType);
				else if (allocation.mSize < 1024 * 1024)
					printf_color(COLOR_YELLOW, "Freed %.3f KiB of type %u\t-- ", allocation.mSize / 1024.f, allocation.mMemoryType);
				else if (allocation.mSize < 1024 * 1024 * 1024)
					printf_color(COLOR_YELLOW, "Freed %.3f MiB of type %u\t-- ", allocation.mSize / (1024.f * 1024.f), allocation.mMemoryType);
				else
					printf_color(COLOR_YELLOW, "Freed %.3f GiB of type %u\t-- ", allocation.mSize / (1024.f * 1024.f * 1024), allocation.mMemoryType);
				PrintAllocations();
				printf_color(COLOR_YELLOW, "\n");
				#endif
				it = allocations.erase(it);
				continue;
			}
		}
		it++;
	}
}

shared_ptr<CommandBuffer> Device::GetCommandBuffer(const std::string& name) {
	// get a commandpool for the current thread
	lock_guard<mutex> lock(mCommandPoolMutex);
	VkCommandPool& commandPool = mCommandPools[this_thread::get_id()];
	if (!commandPool) {
		VkCommandPoolCreateInfo poolInfo = {};
		poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
		poolInfo.queueFamilyIndex = mGraphicsQueueFamilyIndex;
		poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
		ThrowIfFailed(vkCreateCommandPool(mDevice, &poolInfo, nullptr, &commandPool), "vkCreateCommandPool failed");
		SetObjectName(commandPool, name + " Graphics Command Pool", VK_OBJECT_TYPE_COMMAND_POOL);
	}

	auto& commandBufferQueue = mCommandBuffers[commandPool];

	shared_ptr<CommandBuffer> commandBuffer;
	// see if the command buffer at the front of the queue is done
	if (commandBufferQueue.size()) {
		commandBuffer = commandBufferQueue.front();
		if (commandBuffer->mSignalFence->Signaled()) {
			// reset and reuse the command buffer at the front of the queue
			commandBufferQueue.pop();
			commandBuffer->Reset(name);
		} else
			commandBuffer.reset();
	}
	
	if (!commandBuffer) commandBuffer = shared_ptr<CommandBuffer>(new CommandBuffer(this, commandPool, name));

	// begin recording commands
	VkCommandBufferBeginInfo beginInfo = {};
	beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	beginInfo.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT | VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	ThrowIfFailed(vkBeginCommandBuffer(commandBuffer->mCommandBuffer, &beginInfo), "vkBeginCommandBuffer failed");

	return commandBuffer;
}
shared_ptr<Fence> Device::Execute(shared_ptr<CommandBuffer> commandBuffer, bool frameContext) {
	lock_guard<mutex> lock(mCommandPoolMutex);
	ThrowIfFailed(vkEndCommandBuffer(commandBuffer->mCommandBuffer), "vkEndCommandBuffer failed");

	VkSemaphore semaphore = VK_NULL_HANDLE;
	VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
	if (frameContext) {		
		CurrentFrameContext()->mFences.push_back(commandBuffer->mSignalFence);

		if (!commandBuffer->mSignalSemaphore) {
			commandBuffer->mSignalSemaphore = make_shared<Semaphore>(this);
			SetObjectName(*commandBuffer->mSignalSemaphore, "CommandBuffer Semaphore", VK_OBJECT_TYPE_SEMAPHORE);
		}
		
		semaphore = *commandBuffer->mSignalSemaphore;

		CurrentFrameContext()->mSemaphores.push_back(commandBuffer->mSignalSemaphore);
	}

	VkSubmitInfo submitInfo = {};
	submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submitInfo.pWaitDstStageMask = semaphore ? &waitStage : nullptr;
	submitInfo.signalSemaphoreCount = semaphore ? 1 : 0;
	submitInfo.pSignalSemaphores = semaphore ? &semaphore : nullptr;
	submitInfo.waitSemaphoreCount = 0;
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &commandBuffer->mCommandBuffer;
	vkQueueSubmit(mGraphicsQueue, 1, &submitInfo, *commandBuffer->mSignalFence);

	// store the command buffer in the queue
	mCommandBuffers[commandBuffer->mCommandPool].push(commandBuffer);
	return commandBuffer->mSignalFence;
}

Buffer* Device::GetTempBuffer(const std::string& name, VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties) {
	lock_guard<mutex> lock(mTmpBufferMutex);
	FrameContext* frame = CurrentFrameContext();

	auto closest = frame->mTempBuffers.end();
	for (auto it = frame->mTempBuffers.begin(); it != frame->mTempBuffers.end(); it++) {
		if (((it->first->Usage() & usage) == usage) && ((it->first->MemoryProperties() & properties) == properties) && it->first->Size() >= size) {
			if (closest == frame->mTempBuffers.end() || it->first->Size() < it->first->Size())
				closest = it;
			if (it->first->Size() == size) break;
		}
	}

	Buffer* b;
	if (closest != frame->mTempBuffers.end()) {
		b = closest->first;
		frame->mTempBuffers.erase(closest);
	} else
		b = new Buffer(name, this, size, usage, properties);
	frame->mTempBuffersInUse.push_back(b);
	return b;
}
DescriptorSet* Device::GetTempDescriptorSet(const std::string& name, VkDescriptorSetLayout layout) {
	lock_guard<mutex> lock(mTmpDescriptorSetMutex);
	FrameContext* frame = CurrentFrameContext();

	auto& sets = frame->mTempDescriptorSets[layout];

	DescriptorSet* ds;
	if (sets.size()) {
		auto front = sets.begin();
		ds = front->first;
		sets.erase(front);
	} else
		ds = new DescriptorSet(name, this, layout);

	frame->mTempDescriptorSetsInUse.push_back(ds);
	return ds;
}