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

	// Helpers to get common values

	bool mPrimaryButton;
	bool mSecondaryButton;
	float mPrimaryAxis;
	float mSecondaryAxis;
	float mScrollDelta;

	// All axis values of this input pointer
	float mAxis[16];
};

class InputDevice {
public:
	inline virtual uint32_t PointerCount() const = 0;
	// Get info about a pointer
	inline virtual const InputPointer* GetPointer(uint32_t index) const = 0;
	// Get info about a pointer, one frame ago
	inline virtual const InputPointer* GetPointerLast(uint32_t index) const = 0;

	inline virtual void NextFrame() = 0;
};