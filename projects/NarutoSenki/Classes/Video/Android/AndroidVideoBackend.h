#pragma once
#include <jni.h>

#include "../IVideoBackend.h"

// The Android backend: a thin proxy for the Java class com.proj.narsen.video.VideoPlayerCore,
// which does the work (MediaExtractor, MediaCodec, SurfaceTexture, AudioTrack).
//
// The picture reaches GL through a SurfaceTexture bound to a GL_TEXTURE_EXTERNAL_OES
// texture that this class creates on the GL thread. Java never calls back into C++ (so
// there are no native methods to register): events are polled from pollEvent().
class AndroidVideoBackend : public IVideoBackend
{
public:
	AndroidVideoBackend();
	virtual ~AndroidVideoBackend();

	virtual bool open(const VideoSource &source, int64_t offset, int64_t length, VideoInfo &info, std::string &error);
	virtual void close();

	virtual bool createGLResources();
	virtual void destroyGLResources();

	virtual void start();
	virtual void pause();
	virtual void resume();
	virtual void seek(int64_t ms, uint32_t seekId);
	virtual void setLoop(bool loop);
	virtual void setVolume(float volume);

	virtual int64_t getPositionMs() const;

	virtual bool uploadFrame();
	virtual unsigned int getTextureId() const { return _texture; }
	virtual VideoTextureKind getTextureKind() const { return VideoTextureKind::ExternalOES; }
	virtual const float *getTexMatrix() const { return _matrix; }

	virtual bool pollEvent(VideoEvent &event);

	virtual void onContextLost();
	virtual bool onContextRestored();

private:
	struct Methods
	{
		jmethodID open = nullptr;
		jmethodID getWidth = nullptr;
		jmethodID getHeight = nullptr;
		jmethodID getDurationUs = nullptr;
		jmethodID hasAudio = nullptr;
		jmethodID attachGl = nullptr;
		jmethodID onContextLost = nullptr;
		jmethodID detachGl = nullptr;
		jmethodID start = nullptr;
		jmethodID pause = nullptr;
		jmethodID resume = nullptr;
		jmethodID seek = nullptr;
		jmethodID setLoop = nullptr;
		jmethodID setVolume = nullptr;
		jmethodID getPositionUs = nullptr;
		jmethodID uploadFrame = nullptr;
		jmethodID pollEvent = nullptr;
		jmethodID eventSeekId = nullptr;
		jmethodID eventMessage = nullptr;
		jmethodID close = nullptr;
	};

	JNIEnv *env() const;
	bool resolveMethods(JNIEnv *env, jclass type);
	bool checkException(JNIEnv *env, const char *what) const; // logs and clears a pending Java exception
	std::string toString(JNIEnv *env, jstring text) const;
	void release(JNIEnv *env);

	jobject _core = nullptr;          // the VideoPlayerCore (global reference)
	jfloatArray _matrixArray = nullptr; // for the texture matrix (global reference)
	Methods _m;

	unsigned int _texture = 0;
	float _matrix[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
};
