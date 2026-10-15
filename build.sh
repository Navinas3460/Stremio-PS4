#!/bin/bash
# Stremio PS4: one entry point for all builds. See BUILDING.md.
#
#   ./build.sh ps4            PS4 package, built inside Docker/Podman (reproducible)
#   ./build.sh ps4 --local    PS4 package with a locally installed toolchain
#   ./build.sh desktop        desktop build (SDL2 + FFmpeg), for development
#   ./build.sh desktop --dev  same, with developer tools (demo session, scripts)
#   ./build.sh test           unit tests (no window, network or PS4 needed)
#   ./build.sh clean          remove build outputs (keeps the FFmpeg build)
set -euo pipefail
cd "$(dirname "$0")"

target=${1:-ps4}
mode=${2:-}

case "$target" in
ps4)
	if [ "$mode" = "--local" ]; then
		: "${OO_PS4_TOOLCHAIN:?Set OO_PS4_TOOLCHAIN to the OpenOrbis PS4Toolchain folder}"
		export DOTNET_SYSTEM_GLOBALIZATION_INVARIANT=1
		# (Re)build FFmpeg when it's missing or its script changed.
		if [ ! -f ffmpeg_ps4/install/lib/libavcodec.a ] || [ tools/build_ffmpeg_ps4.sh -nt ffmpeg_ps4/install/lib/libavcodec.a ]; then
			tools/build_ffmpeg_ps4.sh
		fi
		make -f Makefile.ps4 -j"$(nproc)"
		echo "Package: build_ps4/stremio-ps4-$(cat VERSION).pkg"
	else
		engine=$(command -v docker || command -v podman || true)
		if [ -z "$engine" ]; then
			echo "Docker or Podman is needed (or use: ./build.sh ps4 --local)" >&2
			exit 1
		fi
		"$engine" build -t stremio-ps4-build tools/docker
		"$engine" run --rm --user "$(id -u):$(id -g)" -v "$PWD":/src stremio-ps4-build ./build.sh ps4 --local
	fi
	;;
desktop)
	dev=OFF
	[ "$mode" = "--dev" ] && dev=ON
	cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo -DSTREMIO_DEV="$dev"
	cmake --build build -j"$(nproc)"
	echo "Run: build/stremio_ps4"
	;;
test)
	cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
	cmake --build build -j"$(nproc)" --target stremio_ps4_tests
	build/stremio_ps4_tests
	;;
clean)
	rm -rf build build_ps4
	;;
*)
	echo "usage: $0 ps4 [--local] | desktop [--dev] | test | clean" >&2
	exit 1
	;;
esac
