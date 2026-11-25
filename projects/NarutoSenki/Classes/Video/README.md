# CCVideoSprite

A video as a `CCSprite`: the picture is a plain GL quad, so position, scale, rotation,
anchor point, z-order, visibility, actions, colour tint, opacity (by shader uniform) and the
blend function work as for any sprite. No native overlay, no touch or keyboard handling, no
playback controls: everything is driven from C++.

## Using it

```cpp
#include "Video/CCVideoSprite.h"

auto video = CCVideoSprite::create("Video/intro.mp4");   // nullptr if it cannot be opened
if (video)                                               // (reason: CCVideoSprite::getLastCreateError())
{
	video->setPosition(ccp(winSize.width * 0.5f, winSize.height * 0.5f));
	video->setScale(winSize.width / video->getContentSize().width);
	video->onFinished = [video]() { video->removeFromParent(); };
	video->onError = [video](const std::string &why) { video->removeFromParent(); };
	_hudLayer->addChild(video, 100);   // HudLayer is at kHudLayerOrder (2): above the veil layer (1),
	video->play();                     // and not scaled by setZoom
}
```

Hang it on the HUD layer (or on the scene above `kHudLayerOrder`), not on the game layer, to
keep it free of the camera zoom and the screen veil.

| | |
|---|---|
| `create(path)` | a file; on Android also `assets/...` as the file utils spell it |
| `create(source, offset, length)` | a `VideoSource` (path, file descriptor (Android), HANDLE (Windows), memory) and a byte range of it; length <= 0 means to the end |
| `play() pause() resume()` | `play()` starts, restarts a finished video, or resumes a paused one |
| `seekTo(ms)` | frame-accurate (the frame that is on screen at that time, the sound from exactly that sample); playing stays playing, paused stays paused (and shows that frame), a finished video becomes paused there |
| `getCurrentTimeMs() getDurationMs() isPlaying() isSeeking()` | the time is the seek target while seeking |
| `setLoop(bool) setVolume(0..1)` | the volume is independent of SimpleAudioEngine |
| `onFinished onSeekDone onError` | `std::function` members, called from the GL thread; a callback may remove the video |

Opening reads the container right away, so size and duration are valid on return (the content
size is the video's size in pixels); decoding starts in the background. The first frame is
shown as soon as the video is in a running scene, before `play()`.

Removing the video from its parent with cleanup (the default) or destroying it stops the
decoders and the sound and frees the texture. A cleaned-up video cannot be played again:
create a new one. `removeFromParentAndCleanup(false)` keeps it.

## What holds a video paused

A playing video is held paused, and goes on from where it stopped when the last reason ends,
while: the app is in the background, the node is not in a running scene, the window has lost
the focus (Windows; `CCVideoSprite::setPauseOnFocusLoss(false)` turns this off), or the GL
context is being recreated. A video that is paused or not started stays that way.

## How it is built

```
CCVideoSprite        the node: GL quad, shaders, scene-graph and lifecycle hooks
  VideoPlayback      the state machine: play/pause/seek, suspend reasons, events -> callbacks, errors
    IVideoBackend    the platform
      PipelineBackend        platform-neutral half of Windows: threads, audio master clock, pacing,
        MFVideoBackend         frame-accurate seeking, looping, GL upload
          MFPipelineSource   Media Foundation Source Readers (video, audio)  [Win32/]
          ALPcmSink          OpenAL PCM queue and clock                      [Win32/]
          MFRangeStream      IStream over a file / handle / memory range     [Win32/]
      AndroidVideoBackend    JNI proxy to the Java VideoPlayerCore            [Android/]
        VideoPlayerCore, VideoDecoder, AudioRenderer, VideoSourceSpec        [.../com/proj/narsen/video/]
      NullVideoBackend       a test pattern, no sound (other platforms, and for testing)
```

Audio is the master clock on both platforms; a video without sound, and the stretch after the
sound has ended, run on the system clock.

## Hooks into the game

- `AppDelegate.cpp`: `CCVideoSprite::onAppPause()` / `onAppResume()` in
  `applicationDidEnterBackground` / `applicationWillEnterForeground` (Windows and Android).
- `jni/main.cpp`: `onGLContextLost()` + `onGLContextRestored()` in the branch of
  `Cocos2dxRenderer_nativeInit` that runs when the GL context was recreated.
- Windows: the first video installs a window-message hook (`CCEGLView::setWndProc`) that
  reads `WM_ACTIVATE`. The engine has one hook slot: if you use it for something else,
  chain to this one (`CCVideoSprite::onWindowFocusChanged(bool)`).

## Platform notes

- **Windows**: Media Foundation decodes (H.264/AAC in MP4 and whatever else the PC has a
  decoder for; Windows N editions need the Media Feature Pack), on the CPU, into RGB32 and
  a plain GL texture. Sound is OpenAL, on the context SimpleAudioEngine owns. Links
  `mfplat.lib mfreadwrite.lib mfuuid.lib OpenAL32.lib`.
- **Android** (minSdk 21): `MediaCodec` into a `SurfaceTexture` (`GL_TEXTURE_EXTERNAL_OES`),
  `AudioTrack` for the sound. A video in the APK must be stored uncompressed
  (`androidResources { noCompress 'mp4' }` in `app/build.gradle`, already added). Memory
  sources use `MediaDataSource` on API 23+, a temporary file below.
- Rotation metadata and non-square pixels are not applied on Windows; the size of the video
  must not change while it plays.

See `tests/README.md` for the tests.
