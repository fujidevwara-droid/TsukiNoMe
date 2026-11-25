#pragma once
#include <functional>
#include <memory>
#include <string>

#include "IVideoBackend.h"

// The playback state machine of a video, without the scene graph: what play(), pause(),
// seekTo() and the rest mean in each state, how app lifecycle events suspend and resume the
// backend, how backend events become callbacks, and what happens on errors and GL context
// loss. CCVideoSprite owns one and adds the drawing; keeping this apart is what lets the
// lifecycle rules be tested without the engine (see tests/native/VideoPlaybackTest.cpp).
//
// States: Ready (opened, not started), Playing, Paused, Finished, Error, Released.
// "Suspended" is separate from them: the backend is held paused while any suspend reason
// is set (the app is in the background, the node is not in a running scene, the window has
// lost focus, the GL context is being recreated), and the state still says Playing, so
// isPlaying() stays true and playback continues where it stopped when the last reason ends.
//
// GL thread only, including the callbacks.
class VideoPlayback
{
public:
	enum class State
	{
		Ready,
		Playing,
		Paused,
		Finished,
		Error,
		Released
	};

	enum SuspendReason
	{
		kSuspendApp = 1,      // the app is in the background
		kSuspendExit = 2,     // the node is not in a running scene
		kSuspendFocus = 4,    // the window has lost focus (Windows)
		kSuspendContext = 8   // the GL context was lost and is being recreated
	};

	VideoPlayback();
	~VideoPlayback();

	// Opens the video and creates its GL resources. On failure the backend is closed
	// and error says why.
	bool init(std::unique_ptr<IVideoBackend> backend, const VideoSource &source, int64_t offset, int64_t length, std::string &error);

	// Where the callbacks live (they may be replaced at any time, also from inside a callback).
	void setCallbackSources(const std::function<void()> *onFinished, const std::function<void()> *onSeekDone, const std::function<void(const std::string &)> *onError);

	IVideoBackend *backend() { return _backend.get(); }
	const VideoInfo &info() const { return _info; }
	State state() const { return _state; }
	bool released() const { return _state == State::Released; }

	// Same meaning as on CCVideoSprite.
	void play();
	void pause();
	void resume();
	void seekTo(int64_t milliseconds);
	int64_t getCurrentTimeMs() const;
	int64_t getDurationMs() const { return _info.durationMs; }
	bool isPlaying() const { return _state == State::Playing; }
	bool isSeeking() const { return _seeking; }
	void setLoop(bool loop);
	bool isLoop() const { return _loop; }
	void setVolume(float volume);
	float getVolume() const { return _volume; }

	// Shows the newest picture and delivers the backend's events. Call every frame.
	void update();

	// Holds the backend paused while a reason is set (see SuspendReason).
	void suspend(int reason);
	void unsuspend(int reason);
	int suspendMask() const { return _suspendMask; }

	// The GL context was lost / is back. Between the two the video is suspended.
	void contextLost();
	void contextRestored();

	// Reports an error from outside (e.g. the shader does not compile): becomes the Error state.
	void fail(const std::string &message);

	// Stops everything and frees the backend. The video cannot be used afterwards.
	void release();

private:
	void handleEvent(const VideoEvent &event);
	void syncBackend();

	std::unique_ptr<IVideoBackend> _backend;
	VideoInfo _info;
	State _state = State::Ready;
	bool _backendRunning = false; // what the backend has been told (start/resume vs pause)
	int _suspendMask = 0;
	bool _seeking = false;
	int64_t _seekTargetMs = 0;
	uint32_t _seekSerial = 0;
	bool _loop = false;
	float _volume = 1.0f;

	const std::function<void()> *_onFinished = nullptr;
	const std::function<void()> *_onSeekDone = nullptr;
	const std::function<void(const std::string &)> *_onError = nullptr;
};
