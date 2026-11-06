#!/usr/bin/env bash
# JVM unit test for GameFiles (pure Java, no Android SDK).
set -euo pipefail
cd "$(dirname "$0")"
out=$(mktemp -d)
javac -d "$out" ../app/src/main/java/com/awkto/zeliard/port/GameFiles.java GameFilesTest.java
java -cp "$out" GameFilesTest
