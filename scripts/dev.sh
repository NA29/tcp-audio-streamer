#!/usr/bin/env bash
# Every build/run goes through the Linux container.
set -euo pipefail
cd "$(dirname "$0")/.."
IMAGE=tcp-audio-dev
case "${1:-help}" in
  image) docker build -t "$IMAGE" . ;;
  build) docker run --rm -v "$PWD:/work" "$IMAGE" \
           bash -c "cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo && cmake --build build -j" ;;
  run)   docker run --rm -it -v "$PWD:/work" -p 12345:12345 "$IMAGE" ./build/serverApp "${@:2}" ;;
  sh)    docker run --rm -it -v "$PWD:/work" -p 12345:12345 "$IMAGE" bash ;;
  *)     echo "usage: dev.sh {image|build|run|sh}" ;;
esac
