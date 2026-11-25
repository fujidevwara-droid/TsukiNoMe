#include <unistd.h>

#include "AndroidVideoBackend.h"
#include "Defines.h"
#include "platform/android/jni/JniHelper.h"

#include <GLES2/gl2ext.h>

#ifndef GL_TEXTURE_EXTERNAL_OES
#define GL_TEXTURE_EXTERNAL_OES 0x8D65
#endif

namespace
{
	const char *kPlayerClass = "com/proj/narsen/video/VideoPlayerCore";

	// VideoPlayerCore.EVENT_*
	const int kEventFinished = 1;
	const int kEventSeekDone = 2;
	const int kEventError = 3;
}

AndroidVideoBackend::AndroidVideoBackend()
{
}

AndroidVideoBackend::~AndroidVideoBackend()
{
	close();
}

// ---------------------------------------------------------------------------
// JNI plumbing
// ---------------------------------------------------------------------------

// Everything runs on the GL thread, which is a Java thread and so already attached.
JNIEnv *AndroidVideoBackend::env() const
{
	JavaVM *vm = cocos2d::JniHelper::getJavaVM();
	if (!vm)
		return nullptr;

	JNIEnv *environment = nullptr;
	if (vm->GetEnv((void **)&environment, JNI_VERSION_1_4) != JNI_OK)
		return nullptr;
	return environment;
}

bool AndroidVideoBackend::checkException(JNIEnv *environment, const char *what) const
{
	if (!environment->ExceptionCheck())
		return false;

	CCLOG("AndroidVideoBackend: exception in %s", what);
	environment->ExceptionDescribe();
	environment->ExceptionClear();
	return true;
}

std::string AndroidVideoBackend::toString(JNIEnv *environment, jstring text) const
{
	if (!text)
		return std::string();

	const char *chars = environment->GetStringUTFChars(text, nullptr);
	std::string result = chars ? chars : "";
	if (chars)
		environment->ReleaseStringUTFChars(text, chars);
	return result;
}

bool AndroidVideoBackend::resolveMethods(JNIEnv *environment, jclass type)
{
	struct Entry
	{
		jmethodID *id;
		const char *name;
		const char *signature;
	};

	const Entry entries[] = {
		{ &_m.open, "open", "(Ljava/lang/String;IJJ[B)Ljava/lang/String;" },
		{ &_m.getWidth, "getWidth", "()I" },
		{ &_m.getHeight, "getHeight", "()I" },
		{ &_m.getDurationUs, "getDurationUs", "()J" },
		{ &_m.hasAudio, "hasAudio", "()Z" },
		{ &_m.attachGl, "attachGl", "(I)Z" },
		{ &_m.onContextLost, "onContextLost", "()V" },
		{ &_m.detachGl, "detachGl", "()V" },
		{ &_m.start, "start", "()V" },
		{ &_m.pause, "pause", "()V" },
		{ &_m.resume, "resume", "()V" },
		{ &_m.seek, "seek", "(JI)V" },
		{ &_m.setLoop, "setLoop", "(Z)V" },
		{ &_m.setVolume, "setVolume", "(F)V" },
		{ &_m.getPositionUs, "getPositionUs", "()J" },
		{ &_m.uploadFrame, "uploadFrame", "([F)Z" },
		{ &_m.pollEvent, "pollEvent", "()I" },
		{ &_m.eventSeekId, "eventSeekId", "()I" },
		{ &_m.eventMessage, "eventMessage", "()Ljava/lang/String;" },
		{ &_m.close, "close", "()V" },
	};

	for (const Entry &entry : entries)
	{
		*entry.id = environment->GetMethodID(type, entry.name, entry.signature);
		if (!*entry.id)
		{
			CCLOG("AndroidVideoBackend: VideoPlayerCore.%s%s not found", entry.name, entry.signature);
			environment->ExceptionClear();
			return false;
		}
	}
	return true;
}

// ---------------------------------------------------------------------------
// Opening and closing
// ---------------------------------------------------------------------------

bool AndroidVideoBackend::open(const VideoSource &source, int64_t offset, int64_t length, VideoInfo &info, std::string &error)
{
	JNIEnv *environment = env();
	if (!environment)
	{
		error = "no JNI environment on this thread";
		return false;
	}

	if (_core)
	{
		error = "the video is already open";
		return false;
	}

	jclass type = cocos2d::JniHelper::getClassID(kPlayerClass, environment);
	if (!type)
	{
		error = "cannot find the Java class VideoPlayerCore (is com.proj.narsen.video in the APK?)";
		return false;
	}

	if (!resolveMethods(environment, type))
	{
		error = "the Java class VideoPlayerCore does not match this build";
		environment->DeleteLocalRef(type);
		return false;
	}

	jmethodID constructor = environment->GetMethodID(type, "<init>", "()V");
	jobject core = constructor ? environment->NewObject(type, constructor) : nullptr;
	environment->DeleteLocalRef(type);
	if (!core || checkException(environment, "VideoPlayerCore()"))
	{
		error = "cannot create the Java video player";
		return false;
	}

	_core = environment->NewGlobalRef(core);
	environment->DeleteLocalRef(core);

	jfloatArray matrix = environment->NewFloatArray(16);
	_matrixArray = (jfloatArray)environment->NewGlobalRef(matrix);
	environment->DeleteLocalRef(matrix);

	// What Java is given: a path (or "assets/..."), or a descriptor, or the bytes. The
	// range of a buffer is cut out right here, so Java gets exactly the video.
	jstring path = nullptr;
	jbyteArray bytes = nullptr;
	jint fd = -1;
	jlong javaOffset = (jlong)offset;
	jlong javaLength = (jlong)length;

	switch (source.kind)
	{
	case VideoSource::Kind::File:
		path = environment->NewStringUTF(source.path.c_str());
		break;
	case VideoSource::Kind::Fd:
		// Java takes the copy over and closes it; the caller's descriptor stays theirs.
		fd = dup(source.fd);
		if (fd < 0)
		{
			error = "cannot duplicate the file descriptor";
			release(environment);
			return false;
		}
		break;
	case VideoSource::Kind::Memory:
	{
		if (!source.data || offset < 0 || (uint64_t)offset > source.size || (length > 0 && (uint64_t)length > source.size - (uint64_t)offset))
		{
			error = "the offset and length do not fit in the buffer";
			release(environment);
			return false;
		}

		const size_t count = length > 0 ? (size_t)length : source.size - (size_t)offset;
		bytes = environment->NewByteArray((jsize)count);
		if (!bytes)
		{
			error = "out of memory for the video buffer";
			checkException(environment, "NewByteArray");
			release(environment);
			return false;
		}
		environment->SetByteArrayRegion(bytes, 0, (jsize)count, (const jbyte *)source.data + offset);
		javaOffset = 0;
		javaLength = 0;
		break;
	}
	case VideoSource::Kind::Handle:
		error = "Windows handles cannot be played on Android, pass a path or a file descriptor";
		release(environment);
		return false;
	}

	jstring failure = (jstring)environment->CallObjectMethod(_core, _m.open, path, fd, javaOffset, javaLength, bytes);
	const bool threw = checkException(environment, "VideoPlayerCore.open");
	if (path)
		environment->DeleteLocalRef(path);
	if (bytes)
		environment->DeleteLocalRef(bytes);

	if (threw || failure)
	{
		error = threw ? "the Java video player failed to open the video" : toString(environment, failure);
		if (failure)
			environment->DeleteLocalRef(failure);
		release(environment);
		return false;
	}

	info.width = environment->CallIntMethod(_core, _m.getWidth);
	info.height = environment->CallIntMethod(_core, _m.getHeight);
	info.durationMs = (int64_t)environment->CallLongMethod(_core, _m.getDurationUs) / 1000;
	info.hasAudio = environment->CallBooleanMethod(_core, _m.hasAudio) == JNI_TRUE;
	if (checkException(environment, "VideoPlayerCore.getInfo"))
	{
		error = "cannot read the video information";
		release(environment);
		return false;
	}
	return true;
}

void AndroidVideoBackend::release(JNIEnv *environment)
{
	if (_core)
	{
		environment->CallVoidMethod(_core, _m.close);
		checkException(environment, "VideoPlayerCore.close");
		environment->DeleteGlobalRef(_core);
		_core = nullptr;
	}
	if (_matrixArray)
	{
		environment->DeleteGlobalRef(_matrixArray);
		_matrixArray = nullptr;
	}
}

void AndroidVideoBackend::close()
{
	JNIEnv *environment = env();
	if (environment)
		release(environment);

	_core = nullptr;
	_matrixArray = nullptr;
}

// ---------------------------------------------------------------------------
// GL
// ---------------------------------------------------------------------------

bool AndroidVideoBackend::createGLResources()
{
	JNIEnv *environment = env();
	if (!environment || !_core)
		return false;

	if (_texture != 0)
		return true;

	glGenTextures(1, &_texture);
	if (_texture == 0)
		return false;

	// An external texture is a different target than GL_TEXTURE_2D: the engine's
	// bound-texture cache does not know about it, and does not have to.
	glBindTexture(GL_TEXTURE_EXTERNAL_OES, _texture);
	glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

	// Java makes the SurfaceTexture for it and starts decoding.
	const jboolean attached = environment->CallBooleanMethod(_core, _m.attachGl, (jint)_texture);
	if (checkException(environment, "VideoPlayerCore.attachGl") || !attached)
	{
		glDeleteTextures(1, &_texture);
		_texture = 0;
		return false;
	}
	return true;
}

void AndroidVideoBackend::destroyGLResources()
{
	JNIEnv *environment = env();
	if (environment && _core)
	{
		environment->CallVoidMethod(_core, _m.detachGl);
		checkException(environment, "VideoPlayerCore.detachGl");
	}

	if (_texture != 0)
	{
		glDeleteTextures(1, &_texture);
		_texture = 0;
	}
}

bool AndroidVideoBackend::uploadFrame()
{
	JNIEnv *environment = env();
	if (!environment || !_core || _texture == 0)
		return false;

	const jboolean changed = environment->CallBooleanMethod(_core, _m.uploadFrame, _matrixArray);
	if (checkException(environment, "VideoPlayerCore.uploadFrame"))
		return false;

	if (changed)
		environment->GetFloatArrayRegion(_matrixArray, 0, 16, _matrix);
	return changed == JNI_TRUE;
}

void AndroidVideoBackend::onContextLost()
{
	JNIEnv *environment = env();
	if (environment && _core)
	{
		// The Java side drops the SurfaceTexture and the video decoder and remembers where
		// it was; the texture itself went away with the context.
		environment->CallVoidMethod(_core, _m.onContextLost);
		checkException(environment, "VideoPlayerCore.onContextLost");
	}

	_texture = 0;
}

bool AndroidVideoBackend::onContextRestored()
{
	_texture = 0;
	return createGLResources(); // Java resumes at the position it remembered
}

// ---------------------------------------------------------------------------
// Playback
// ---------------------------------------------------------------------------

void AndroidVideoBackend::start()
{
	JNIEnv *environment = env();
	if (environment && _core)
	{
		environment->CallVoidMethod(_core, _m.start);
		checkException(environment, "VideoPlayerCore.start");
	}
}

void AndroidVideoBackend::pause()
{
	JNIEnv *environment = env();
	if (environment && _core)
	{
		environment->CallVoidMethod(_core, _m.pause);
		checkException(environment, "VideoPlayerCore.pause");
	}
}

void AndroidVideoBackend::resume()
{
	JNIEnv *environment = env();
	if (environment && _core)
	{
		environment->CallVoidMethod(_core, _m.resume);
		checkException(environment, "VideoPlayerCore.resume");
	}
}

void AndroidVideoBackend::seek(int64_t ms, uint32_t seekId)
{
	JNIEnv *environment = env();
	if (environment && _core)
	{
		environment->CallVoidMethod(_core, _m.seek, (jlong)(ms * 1000), (jint)seekId);
		checkException(environment, "VideoPlayerCore.seek");
	}
}

void AndroidVideoBackend::setLoop(bool loop)
{
	JNIEnv *environment = env();
	if (environment && _core)
	{
		environment->CallVoidMethod(_core, _m.setLoop, (jboolean)(loop ? JNI_TRUE : JNI_FALSE));
		checkException(environment, "VideoPlayerCore.setLoop");
	}
}

void AndroidVideoBackend::setVolume(float volume)
{
	JNIEnv *environment = env();
	if (environment && _core)
	{
		environment->CallVoidMethod(_core, _m.setVolume, (jfloat)volume);
		checkException(environment, "VideoPlayerCore.setVolume");
	}
}

int64_t AndroidVideoBackend::getPositionMs() const
{
	JNIEnv *environment = env();
	if (!environment || !_core)
		return 0;

	const jlong microseconds = environment->CallLongMethod(_core, _m.getPositionUs);
	if (checkException(environment, "VideoPlayerCore.getPositionUs"))
		return 0;
	return (int64_t)microseconds / 1000;
}

bool AndroidVideoBackend::pollEvent(VideoEvent &event)
{
	JNIEnv *environment = env();
	if (!environment || !_core)
		return false;

	const jint type = environment->CallIntMethod(_core, _m.pollEvent);
	if (checkException(environment, "VideoPlayerCore.pollEvent") || type == 0)
		return false;

	event = VideoEvent();
	switch (type)
	{
	case kEventFinished:
		event.type = VideoEventType::Finished;
		break;
	case kEventSeekDone:
		event.type = VideoEventType::SeekDone;
		event.seekId = (uint32_t)environment->CallIntMethod(_core, _m.eventSeekId);
		break;
	case kEventError:
	default:
	{
		event.type = VideoEventType::Error;
		jstring message = (jstring)environment->CallObjectMethod(_core, _m.eventMessage);
		event.message = toString(environment, message);
		if (message)
			environment->DeleteLocalRef(message);
		break;
	}
	}

	checkException(environment, "VideoPlayerCore.event");
	return true;
}
