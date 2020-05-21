#pragma once

#include <Util/Util.hpp>

class InputDevice;

#define INPUT_POINTER_NAME_LENGTH 64

// Represents a device capable of "pointing" into the world
// i.e. a mouse or Vive controller
// an InputDevice might have multiple pointers (i.e. multiple fingers)
class InputPointer {
public:
	InputDevice* mDevice;
	// Name should be consistent and unique to this pointer
	char mName[INPUT_POINTER_NAME_LENGTH];

	Ray mWorldRay;
	float mGuiHitT;

	// Generalizable values
	bool mPrimaryButton;
	bool mSecondaryButton;
	float mPrimaryAxis;
	float mSecondaryAxis;
	float2 mScrollDelta;
};

class InputDevice {
public:
	virtual uint32_t PointerCount() const = 0;
	// Get info about a pointer
	virtual const InputPointer* GetPointer(uint32_t index) const = 0;
	// Get info about a pointer, one frame ago
	virtual const InputPointer* GetPointerLast(uint32_t index) const = 0;

	virtual void NextFrame() = 0;
};