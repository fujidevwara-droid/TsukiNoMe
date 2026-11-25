# CCVideoSprite tests

These run on a desktop, in real time, without Windows or Android. They are not part of any
game build. What they check is the logic of playback: audio/video sync, frame- and
sample-accurate seeking, pause/resume, looping, the end of the video, context loss and
closing. That Media Foundation, OpenAL, MediaCodec or AudioTrack behave like the fakes can
only be proven on a device.

## native/ (needs g++ with C++20 and libglew-dev)

    native/run.sh [all|playback|pipeline|null|sink]

- `playback`: `VideoPlayback`, the state machine behind `CCVideoSprite` and its lifecycle
  rules (what holds a video paused and what lets it go on; callbacks; errors; context loss;
  releasing from inside a callback): first against a scripted mock backend, then against the
  real `PipelineBackend` on fake media, in real time. About 100 checks, 6 s.
- `pipeline`: the real `PipelineBackend` (the platform-neutral half of the Windows backend:
  threads, clocks, pacing, seeking, GL upload) against a fake media source and a fake audio
  output. About 170 checks, 18 s.
- `null`: `NullVideoBackend`.
- `sink`: `ALPcmSink` (the Windows audio clock) against a simulated OpenAL.

## jvm/ (needs a JDK, 8 or newer)

    jvm/run.sh

The real `VideoPlayerCore`, `VideoDecoder` and `AudioRenderer` (Android) against fake
`MediaExtractor`, `MediaCodec`, `AudioTrack` and `SurfaceTexture` classes in `jvm/stubs/`.
About 195 checks, 20 s. The fake media is described by the string passed to `open`, e.g.
`fake:duration=3000000;audio=1;astart=40000;aseekdelay=300`.

## Both suites check

- the position follows real time and the pictures are shown within a few milliseconds of
  the audio clock, none early, none more than a frame late;
- a seek shows exactly the frame that is on screen at the target and starts the sound at
  exactly the target sample, playing or paused, at keyframes, between them, at the very
  start and end, and far past the end;
- a newer seek replaces one in progress (one SeekDone, for the last);
- a seek is not completed before the slower of picture and sound is ready;
- pause freezes everything, resume continues; the end is reported once; looping never
  reports it; sound that ends early or never starts hands the clock to the system clock
  without the position going backwards; a video without sound works;
- after the GL context is lost, the picture and position come back; the app going to the
  background, leaving the scene or losing the window focus holds a video exactly where it is
  and lets it go on from there (also with a seek in progress);
- closing, also in the middle of a seek, returns promptly.

The tests were checked by breaking the code on purpose (committing a seek without the audio
or without the picture, not trimming samples, stopping at an earlier frame, keeping old
frames across a seek, never taking over with the system clock, ...): every such change makes
them fail.
