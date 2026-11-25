#pragma once
#include <functional>
#include <memory>
#include <string>

#include "IVideoBackend.h"
#include "VideoPlayback.h"
#include "Defines.h"

// A video that behaves like a CCSprite.
//
// The picture is drawn as a plain GL quad: position, scale, rotation, anchor point,
// z-order, visibility, actions, colour tint, opacity and the blend function all work
// as they do for any sprite, so the video can be parented anywhere. To keep it free of
// the camera zoom and the screen veil, hang it on the HUD layer (or on the scene above
// kHudLayerOrder), not on the game layer:
//
//     auto video = CCVideoSprite::create("Video/intro.mp4");
//     if (video)
//     {
//         video->setPosition(ccp(winSize.width * 0.5f, winSize.height * 0.5f));
//         video->setScale(winSize.width / video->getContentSize().width);
//         video->onFinished = [video]() { video->removeFromParent(); };
//         _hudLayer->addChild(video, 100); // HudLayer is at kHudLayerOrder (2), above the veil layer (1)
//         video->play();
//     }
//
// There is no native overlay, no touch or keyboard handling and no playback controls:
// everything is driven through this class.
//
// The content size is the size of the video in pixels. Opacity is applied by the
// shader (see VideoShader.hpp) from the displayed opacity, so it cascades from the
// parents like for any other sprite.
//
// Cleanup: removeFromParent() (with cleanup, the default) and the destructor stop the
// decoders and the audio and free the GL texture. A video that was cleaned up cannot be
// played again, create a new one. removeFromParentAndCleanup(false) keeps it alive.
//
// Everything is called from the GL (main) thread, and so are the callbacks.
class CCVideoSprite : public CCSprite
{
public:
	// Opens a video file. Returns nullptr if it cannot be opened (the reason is in
	// getLastCreateError() and in the log). The container is read right here so the
	// content size and the duration are valid on return; decoding starts in the background.
	static CCVideoSprite *create(const char *path);

	// Opens a video from a file path, descriptor, handle or memory buffer (see
	// VideoSource). offset and length select a byte range of it; a length of 0 or less
	// means "from offset to the end". The source has to stay valid while the video exists.
	static CCVideoSprite *create(const VideoSource &source, int64_t offset, int64_t length);

	// Same, with a backend of your choosing (e.g. NullVideoBackend for testing).
	static CCVideoSprite *createWithBackend(std::unique_ptr<IVideoBackend> backend, const VideoSource &source, int64_t offset, int64_t length);

	static const std::string &getLastCreateError();

	// Playback. play() starts from the beginning when the video is new or has finished,
	// and resumes when it is paused. Nothing happens in the other states.
	void play();
	void pause();
	void resume();

	// Frame-accurate: the video jumps to the previous keyframe, decodes forward and
	// discards frames until the one that is on screen at ms is reached. Playing stays
	// playing, paused stays paused (and shows that frame). A finished video becomes
	// paused at the target. onSeekDone follows when the frame is ready.
	void seekTo(int64_t milliseconds);

	int64_t getCurrentTimeMs() const; // the seek target while seeking
	int64_t getDurationMs() const;
	bool isPlaying() const;           // playing and not paused (also true while seeking)
	bool isSeeking() const;

	void setLoop(bool loop);
	bool isLoop() const { return _playback.isLoop(); }

	// 0 (silent) to 1 (full). Independent of SimpleAudioEngine.
	void setVolume(float volume);
	float getVolume() const { return _playback.getVolume(); }

	// Called from the GL thread. A callback may remove the video from its parent.
	std::function<void()> onFinished;
	std::function<void()> onSeekDone;
	std::function<void(const std::string &)> onError;

	// App lifecycle. Every video that is playing is held paused while the app is in the
	// background, while it is not in a running scene (removed without cleanup, or its scene
	// is not the running one), while the window has lost focus (Windows) and while the GL
	// context is being recreated, and carries on from where it stopped when the last of
	// these ends. A video that is paused or not started stays that way.
	//
	// AppDelegate calls onAppPause/onAppResume (from applicationDidEnterBackground and
	// applicationWillEnterForeground); the Android GL context recreation calls
	// onGLContextLost/onGLContextRestored (jni/main.cpp). On Windows the focus is picked up
	// by a window-message hook installed with the first video.
	static void onAppPause();
	static void onAppResume();
	static void onGLContextLost();
	static void onGLContextRestored();

	// Windows: pause the playing videos while the window does not have the focus
	// (default: yes; the minimized window counts, as it loses the focus).
	static void onWindowFocusChanged(bool focused);
	static void setPauseOnFocusLoss(bool pause);
	static bool isPauseOnFocusLoss();

	virtual ~CCVideoSprite();

	virtual void draw(void);
	virtual void update(float delta);
	virtual void onEnter();
	virtual void onExit();
	virtual void cleanup(void);

protected:
	CCVideoSprite();

	// The sprite's quad always shows the whole texture.
	virtual void setTextureCoords(CCRect rect);

private:
	bool initWithBackend(std::unique_ptr<IVideoBackend> backend, const VideoSource &source, int64_t offset, int64_t length);
	void unregisterVideo();
	void refreshQuadColor();
	void contextLost();
	void contextRestored();
	static void applyFocus();

	VideoPlayback _playback; // what play(), pause(), ... mean, and the lifecycle rules

	// uniform locations of the program in use
	CCGLProgram *_program = nullptr;
	GLint _locOpacity = -1;
	GLint _locPremultiply = -1;
	GLint _locTexMatrix = -1;
	bool _shaderFailed = false;
	std::string _pendingError; // an error found while drawing, reported from update()
};
