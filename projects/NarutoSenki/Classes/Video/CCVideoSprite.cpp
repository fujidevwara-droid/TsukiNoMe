#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

#include "CCVideoSprite.h"
#include "VideoShader.hpp"

#include "CCLuaEngine.h"

#ifndef GL_TEXTURE_EXTERNAL_OES
#define GL_TEXTURE_EXTERNAL_OES 0x8D65
#endif

namespace
{
	// Every live video, for the app lifecycle hooks. GL thread only.
	std::vector<CCVideoSprite *> &registry()
	{
		static std::vector<CCVideoSprite *> videos;
		return videos;
	}

	std::string &lastCreateError()
	{
		static std::string error;
		return error;
	}

	// Calls a Lua handler as function(video) or, with a message, function(video, message).
	// The video stays alive until the end of the frame, so the handler may remove it.
	void callLuaHandler(int handler, CCVideoSprite *video, const std::string *message)
	{
		CCLuaEngine *engine = CCLuaEngine::defaultEngine();
		if (!engine || !handler)
			return;

		CCLuaStack *stack = engine->getLuaStack();
		video->retain();
		video->autorelease();

		stack->pushCCObject(video, "CCVideoSprite");
		int argCount = 1;
		if (message)
		{
			stack->pushString(message->c_str(), static_cast<int>(message->size()));
			argCount = 2;
		}
		stack->executeFunctionByHandler(handler, argCount);
		stack->clean();
	}

	void releaseLuaHandler(int &handler)
	{
		if (!handler)
			return;

		if (CCScriptEngineProtocol *engine = CCScriptEngineManager::sharedManager()->getScriptEngine())
			engine->removeScriptHandler(handler);
		handler = 0;
	}

	// What the lifecycle hooks last said, for videos that are created afterwards.
	bool s_appPaused = false;
	bool s_windowFocused = true;
	bool s_pauseOnFocusLoss = true;

	// The lifecycle hooks call back into videos that might be gone by then (a failing
	// video reports through onError, whose handler may remove it), so they walk a copy.
	template <typename Function>
	void forEachVideo(Function function)
	{
		const std::vector<CCVideoSprite *> videos = registry();
		for (CCVideoSprite *video : videos)
		{
			const auto &live = registry();
			if (std::find(live.begin(), live.end(), video) != live.end())
				function(video);
		}
	}
}

#if CC_TARGET_PLATFORM == CC_PLATFORM_WIN32
namespace
{
	// The engine does not tell about the focus. Its window procedure passes every message
	// it does not handle itself to this hook, WM_ACTIVATE among them, and then runs the
	// default handling (we leave *processed alone).
	LRESULT focusWindowProc(UINT message, WPARAM wParam, LPARAM, BOOL *)
	{
		if (message == WM_ACTIVATE)
			CCVideoSprite::onWindowFocusChanged(LOWORD(wParam) != WA_INACTIVE);
		return 0;
	}

	void installFocusHook()
	{
		static bool installed = false;
		if (installed)
			return;

		if (CCEGLView *view = CCEGLView::sharedOpenGLView())
		{
			view->setWndProc(focusWindowProc);
			installed = true;
		}
	}
}
#else
namespace
{
	void installFocusHook() {}
}
#endif

CCVideoSprite::CCVideoSprite()
{
	_playback.setCallbackSources(&onFinished, &onSeekDone, &onError);
}

CCVideoSprite::~CCVideoSprite()
{
	unregisterVideo();
	_playback.release();
	unregisterLuaHandlers();
}

CCVideoSprite *CCVideoSprite::create(const char *path)
{
	if (!path || !*path)
	{
		lastCreateError() = "empty video path";
		return nullptr;
	}

	const std::string fullPath = CCFileUtils::sharedFileUtils()->fullPathForFilename(path);
	return create(VideoSource::fromFile(fullPath), 0, 0);
}

CCVideoSprite *CCVideoSprite::create(const VideoSource &source, int64_t offset, int64_t length)
{
	return createWithBackend(createPlatformVideoBackend(), source, offset, length);
}

CCVideoSprite *CCVideoSprite::createWithBackend(std::unique_ptr<IVideoBackend> backend, const VideoSource &source, int64_t offset, int64_t length)
{
	auto sprite = new CCVideoSprite();
	if (sprite && sprite->initWithBackend(std::move(backend), source, offset, length))
	{
		sprite->autorelease();
		return sprite;
	}

	CCLOG("CCVideoSprite: cannot create the video: %s", lastCreateError().c_str());
	CC_SAFE_DELETE(sprite);
	return nullptr;
}

const std::string &CCVideoSprite::getLastCreateError()
{
	return lastCreateError();
}

bool CCVideoSprite::initWithBackend(std::unique_ptr<IVideoBackend> backend, const VideoSource &source, int64_t offset, int64_t length)
{
	std::string &error = lastCreateError();
	error.clear();

	if (!_playback.init(std::move(backend), source, offset, length, error))
		return false;

	const VideoInfo &info = _playback.info();

	// The sprite needs a texture to have a size and a rect. The video's own texture is
	// not a CCTexture2D (on Android it is not even a GL_TEXTURE_2D), so a 1x1 stand-in
	// is used and the rect is given the size of the video; draw() binds the real one.
	static const unsigned char pixel[4] = { 0, 0, 0, 255 };
	auto placeholder = new CCTexture2D();
	const bool placeholderOk = placeholder->initWithData(pixel, kCCTexture2DPixelFormat_RGBA8888, 1, 1, CCSize(1, 1));
	const bool spriteOk = placeholderOk && initWithTexture(placeholder, CCRect(0, 0, (float)info.width, (float)info.height));
	placeholder->release();
	if (!spriteOk)
	{
		error = "cannot create the video quad";
		_playback.release();
		return false;
	}

	// The picture has no alpha and is not premultiplied.
	setOpacityModifyRGB(false);
	ccBlendFunc blend = { GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA };
	setBlendFunc(blend);

	// Not in a running scene yet: a video that is started before it is added (or while it
	// is removed without cleanup) waits, and continues once onEnter() runs. The same goes
	// for a video that is created while the app is in the background or the window has
	// no focus.
	int suspended = VideoPlayback::kSuspendExit;
	if (s_appPaused)
		suspended |= VideoPlayback::kSuspendApp;
	if (!s_windowFocused && s_pauseOnFocusLoss)
		suspended |= VideoPlayback::kSuspendFocus;
	_playback.suspend(suspended);

	registry().push_back(this);
	scheduleUpdate();
	installFocusHook();
	return true;
}

void CCVideoSprite::unregisterVideo()
{
	auto &videos = registry();
	videos.erase(std::remove(videos.begin(), videos.end(), this), videos.end());
}

void CCVideoSprite::cleanup(void)
{
	unregisterVideo();
	_playback.release();

	// Drop whatever the callbacks captured.
	onFinished = nullptr;
	onSeekDone = nullptr;
	onError = nullptr;
	unregisterLuaHandlers();

	CCSprite::cleanup();
}

// ---------------------------------------------------------------------------
// Lua handlers
// ---------------------------------------------------------------------------

void CCVideoSprite::registerFinishedHandler(int handler)
{
	releaseLuaHandler(_luaFinishedHandler);
	onFinished = nullptr;
	if (!handler)
		return;

	_luaFinishedHandler = handler;
	onFinished = [this, handler]() { callLuaHandler(handler, this, nullptr); };
}

void CCVideoSprite::registerSeekDoneHandler(int handler)
{
	releaseLuaHandler(_luaSeekDoneHandler);
	onSeekDone = nullptr;
	if (!handler)
		return;

	_luaSeekDoneHandler = handler;
	onSeekDone = [this, handler]() { callLuaHandler(handler, this, nullptr); };
}

void CCVideoSprite::registerErrorHandler(int handler)
{
	releaseLuaHandler(_luaErrorHandler);
	onError = nullptr;
	if (!handler)
		return;

	_luaErrorHandler = handler;
	onError = [this, handler](const std::string &message) { callLuaHandler(handler, this, &message); };
}

void CCVideoSprite::unregisterLuaHandlers()
{
	if (_luaFinishedHandler)
		onFinished = nullptr;
	if (_luaSeekDoneHandler)
		onSeekDone = nullptr;
	if (_luaErrorHandler)
		onError = nullptr;

	releaseLuaHandler(_luaFinishedHandler);
	releaseLuaHandler(_luaSeekDoneHandler);
	releaseLuaHandler(_luaErrorHandler);
}

// ---------------------------------------------------------------------------
// Playback (see VideoPlayback)
// ---------------------------------------------------------------------------

void CCVideoSprite::play()
{
	_playback.play();
}

void CCVideoSprite::pause()
{
	_playback.pause();
}

void CCVideoSprite::resume()
{
	_playback.resume();
}

void CCVideoSprite::seekTo(int64_t milliseconds)
{
	_playback.seekTo(milliseconds);
}

int64_t CCVideoSprite::getCurrentTimeMs() const
{
	return _playback.getCurrentTimeMs();
}

int64_t CCVideoSprite::getDurationMs() const
{
	return _playback.getDurationMs();
}

bool CCVideoSprite::isPlaying() const
{
	return _playback.isPlaying();
}

bool CCVideoSprite::isSeeking() const
{
	return _playback.isSeeking();
}

void CCVideoSprite::setLoop(bool loop)
{
	_playback.setLoop(loop);
}

void CCVideoSprite::setVolume(float volume)
{
	_playback.setVolume(volume);
}

void CCVideoSprite::update(float delta)
{
	if (_playback.released())
		return;

	// A callback may remove the video from its parent. Keep it alive until the end of
	// the frame so that this function can finish safely.
	retain();
	autorelease();

	if (!_pendingError.empty())
	{
		const std::string message = std::move(_pendingError);
		_pendingError.clear();
		_playback.fail(message);
		return;
	}

	_playback.update();
}

void CCVideoSprite::onEnter()
{
	CCSprite::onEnter();
	_playback.unsuspend(VideoPlayback::kSuspendExit);
}

void CCVideoSprite::onExit()
{
	_playback.suspend(VideoPlayback::kSuspendExit);
	CCSprite::onExit();
}

// ---------------------------------------------------------------------------
// Rendering
// ---------------------------------------------------------------------------

// The quad always shows the whole texture (flipX/flipY still work). Normal
// orientation: row 0 of the picture at the top, v = 0 at the top.
void CCVideoSprite::setTextureCoords(CCRect rect)
{
	float left = 0.0f;
	float right = 1.0f;
	float top = 0.0f;
	float bottom = 1.0f;

	if (m_bFlipX)
		std::swap(left, right);
	if (m_bFlipY)
		std::swap(top, bottom);

	m_sQuad.bl.texCoords.u = left;
	m_sQuad.bl.texCoords.v = bottom;
	m_sQuad.br.texCoords.u = right;
	m_sQuad.br.texCoords.v = bottom;
	m_sQuad.tl.texCoords.u = left;
	m_sQuad.tl.texCoords.v = top;
	m_sQuad.tr.texCoords.u = right;
	m_sQuad.tr.texCoords.v = top;
}

// Opacity is the shader's job (u_opacity), so the quad's vertex colour carries the
// tint only. CCSprite writes the displayed opacity into it whenever opacity or colour
// change, which is why it is put right again before every draw.
void CCVideoSprite::refreshQuadColor()
{
	const ccColor3B &tint = getDisplayedColor();
	const ccColor4B color = { tint.r, tint.g, tint.b, 255 };
	m_sQuad.bl.colors = color;
	m_sQuad.br.colors = color;
	m_sQuad.tl.colors = color;
	m_sQuad.tr.colors = color;
}

void CCVideoSprite::draw(void)
{
	IVideoBackend *backend = _playback.backend();
	if (!backend || _shaderFailed)
		return;

	const unsigned int textureId = backend->getTextureId();
	if (textureId == 0)
		return;

	const VideoTextureKind kind = backend->getTextureKind();
	CCGLProgram *program = VideoShader::program(kind);
	if (!program)
	{
		// Reported from update(): a callback must not run in the middle of drawing.
		_shaderFailed = true;
		_pendingError = "the video shader does not compile on this device";
		return;
	}

	if (program != _program)
	{
		_program = program;
		_locOpacity = program->getUniformLocationForName("u_opacity");
		_locPremultiply = program->getUniformLocationForName("u_premultiply");
		_locTexMatrix = program->getUniformLocationForName("u_texMatrix");
		setShaderProgram(program);
	}

	refreshQuadColor();

	CC_NODE_DRAW_SETUP();

	program->setUniformLocationWith1f(_locOpacity, getDisplayedOpacity() / 255.0f);
	program->setUniformLocationWith1f(_locPremultiply, m_sBlendFunc.src == GL_ONE ? 1.0f : 0.0f);

	if (kind == VideoTextureKind::ExternalOES)
	{
		GLfloat matrix[16];
		std::memcpy(matrix, backend->getTexMatrix(), sizeof(matrix));
		program->setUniformLocationWithMatrix4fv(_locTexMatrix, matrix, 1);
	}

	ccGLBlendFunc(m_sBlendFunc.src, m_sBlendFunc.dst);

	// The engine remembers which 2D texture is bound, so 2D goes through its cache.
	// External textures are a different target and are not part of that cache.
	if (kind == VideoTextureKind::ExternalOES)
		glBindTexture(GL_TEXTURE_EXTERNAL_OES, textureId);
	else
		ccGLBindTexture2D(textureId);

	ccGLEnableVertexAttribs(kCCVertexAttribFlag_PosColorTex);

#define kQuadSize sizeof(m_sQuad.bl)
	const uintptr_t offset = (uintptr_t)&m_sQuad;

	int diff = offsetof(ccV3F_C4B_T2F, vertices);
	glVertexAttribPointer(kCCVertexAttrib_Position, 3, GL_FLOAT, GL_FALSE, kQuadSize, (void *)(offset + diff));

	diff = offsetof(ccV3F_C4B_T2F, texCoords);
	glVertexAttribPointer(kCCVertexAttrib_TexCoords, 2, GL_FLOAT, GL_FALSE, kQuadSize, (void *)(offset + diff));

	diff = offsetof(ccV3F_C4B_T2F, colors);
	glVertexAttribPointer(kCCVertexAttrib_Color, 4, GL_UNSIGNED_BYTE, GL_TRUE, kQuadSize, (void *)(offset + diff));

	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
#undef kQuadSize

	CHECK_GL_ERROR_DEBUG();
}

// ---------------------------------------------------------------------------
// App lifecycle
// ---------------------------------------------------------------------------

void CCVideoSprite::onAppPause()
{
	s_appPaused = true;
	forEachVideo([](CCVideoSprite *video) { video->_playback.suspend(VideoPlayback::kSuspendApp); });
}

void CCVideoSprite::onAppResume()
{
	s_appPaused = false;
	forEachVideo([](CCVideoSprite *video) { video->_playback.unsuspend(VideoPlayback::kSuspendApp); });
}

void CCVideoSprite::onGLContextLost()
{
	forEachVideo([](CCVideoSprite *video) { video->contextLost(); });
}

void CCVideoSprite::onGLContextRestored()
{
	// Once for everybody: the programs are shared.
	VideoShader::rebuild();
	forEachVideo([](CCVideoSprite *video) { video->contextRestored(); });
}

void CCVideoSprite::contextLost()
{
	_playback.contextLost();
	_program = nullptr;
}

void CCVideoSprite::contextRestored()
{
	_program = nullptr;
	_shaderFailed = false;
	_playback.contextRestored();
}

void CCVideoSprite::onWindowFocusChanged(bool focused)
{
	s_windowFocused = focused;
	applyFocus();
}

void CCVideoSprite::setPauseOnFocusLoss(bool pause)
{
	s_pauseOnFocusLoss = pause;
	applyFocus();
}

bool CCVideoSprite::isPauseOnFocusLoss()
{
	return s_pauseOnFocusLoss;
}

void CCVideoSprite::applyFocus()
{
	const bool hold = !s_windowFocused && s_pauseOnFocusLoss;
	forEachVideo([hold](CCVideoSprite *video)
	{
		if (hold)
			video->_playback.suspend(VideoPlayback::kSuspendFocus);
		else
			video->_playback.unsuspend(VideoPlayback::kSuspendFocus);
	});
}
