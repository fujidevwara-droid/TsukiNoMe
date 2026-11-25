#include "IVideoBackend.h"
#include "NullVideoBackend.h"
#include "Defines.h"

#if CC_TARGET_PLATFORM == CC_PLATFORM_WIN32
#include "Win32/MFVideoBackend.h"
#elif CC_TARGET_PLATFORM == CC_PLATFORM_ANDROID
#include "Android/AndroidVideoBackend.h"
#endif

std::unique_ptr<IVideoBackend> createPlatformVideoBackend()
{
#if CC_TARGET_PLATFORM == CC_PLATFORM_WIN32
	return std::make_unique<MFVideoBackend>();
#elif CC_TARGET_PLATFORM == CC_PLATFORM_ANDROID
	return std::make_unique<AndroidVideoBackend>();
#else
	// No real backend on this platform: the null backend plays a test pattern without
	// sound, which keeps the game building and the scene graph side of CCVideoSprite
	// testable.
	CCLOG("CCVideoSprite: no video backend for this platform yet, using the null backend (test pattern, no audio)");
	return std::make_unique<NullVideoBackend>();
#endif
}
