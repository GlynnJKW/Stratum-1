#include <Input/MouseKeyboardInput.hpp>
#include <Core/Window.hpp>

MouseKeyboardInput::MouseKeyboardInput(){
	memset(&mMousePointer, 0, sizeof(InputPointer));
	memset(&mMousePointerLast, 0, sizeof(InputPointer));
	mMousePointer.mDevice = this;
	mMousePointerLast.mDevice = this;

	strcpy(mMousePointer.mName, "Mouse");
	strcpy(mMousePointerLast.mName, "Mouse");

	mLockMouse = false;
	mCurrent.mCursorPos = mLast.mCursorPos = 0;
	mCurrent.mCursorDelta = mLast.mCursorDelta = 0;
	mCurrent.mCursorDelta = mLast.mScrollDelta = 0;
	mMousePointer.mGuiHitT = -1.f;
	mCurrent.mKeys = {};
	mLast.mKeys = {};
}

void MouseKeyboardInput::LockMouse(bool l) {
	#ifdef WINDOWS
	if (mLockMouse && !l)
		ShowCursor(TRUE);
	else if (!mLockMouse && l)
		ShowCursor(FALSE);
	#else
	// TODO: hide cursor on linux
	#endif

	mLockMouse = l;
}

void MouseKeyboardInput::NextFrame() {
	memcpy(&mMousePointerLast, &mMousePointer, sizeof(InputPointer));
	mMousePointer.mGuiHitT = -1.f;
	mMousePointer.mPrimaryAxis = 0;
	mMousePointer.mSecondaryAxis = 0;
	mMousePointer.mScrollDelta = 0;

	mLast = mCurrent;
	mCurrent.mScrollDelta = 0;
	mCurrent.mCursorDelta = 0;
}