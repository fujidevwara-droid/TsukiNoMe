#!/bin/bash
# Builds and runs the native CCVideoSprite tests on Linux. They need g++ (C++20), the GLEW
# development headers (libglew-dev) and a checkout of the engine; run from anywhere in the repo.
#
#   ./run.sh            all four
#   ./run.sh pipeline   PipelineBackend (set PIPELINE=/path/PipelineBackend.cpp to test a modified copy)
#   ./run.sh null       NullVideoBackend
#   ./run.sh playback   VideoPlayback (the state machine behind CCVideoSprite and its lifecycle rules)
#   ./run.sh sink       ALPcmSink
set -e
here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here" && git rev-parse --show-toplevel)"
classes="$root/projects/NarutoSenki/Classes"
video="$classes/Video"
inc="-I$root/cocos2dx -I$root/cocos2dx/include -I$root/cocos2dx/kazmath/include -I$root/cocos2dx/platform/linux \
 -I$root/cocos2dx/platform/third_party/linux -I$root/cocos2dx/platform/third_party/linux/libfreetype2 -I$root/CocosDenshion/include \
 -I$root/extensions -I$root/external -I$root/external/fmt/include -I$root/external/toml -I$classes \
 -I$classes/Core -I$root/scripting/lua/cocos2dx_support -I$root/scripting/lua/luajit/include \
 -I$root/scripting/lua/tolua -I$root/external/glfw3/include/linux -I$root/cocos2dx/platform/third_party/win32/OpenalSoft"
out="$(mktemp -d)"

run() { # name, sources...
  local name="$1"; shift
  echo "== $name"
  g++ -std=c++20 -pthread -w -DLINUX $inc -o "$out/$name" "$@"
  "$out/$name"
}

what="${1:-all}"
[ "$what" = all -o "$what" = null ]     && run null     "$here/NullVideoBackendTest.cpp" "$video/NullVideoBackend.cpp"
[ "$what" = all -o "$what" = sink ]     && run sink     "$here/ALPcmSinkTest.cpp" "$video/Win32/ALPcmSink.cpp"
[ "$what" = all -o "$what" = playback ] && run playback "$here/VideoPlaybackTest.cpp" "${PLAYBACK:-$video/VideoPlayback.cpp}" "$video/PipelineBackend.cpp"
[ "$what" = all -o "$what" = pipeline ] && run pipeline "$here/PipelineBackendTest.cpp" "${PIPELINE:-$video/PipelineBackend.cpp}"
exit 0
