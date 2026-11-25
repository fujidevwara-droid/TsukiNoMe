#!/bin/bash
# Runs the real Android video player classes on a desktop JVM against fakes (needs a JDK).
set -e
here="$(cd "$(dirname "$0")" && pwd)"
real="${REAL:-$here/../../../../proj.android-studio/app/src/main/java/com/proj/narsen/video}"
out="$(mktemp -d)"
javac --release 8 -Xlint:-options -d "$out" $(find "$here/stubs" -name '*.java') "$real"/*.java "$here/VideoCoreTest.java"
java -cp "$out" VideoCoreTest
