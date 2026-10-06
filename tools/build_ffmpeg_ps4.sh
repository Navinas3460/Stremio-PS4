#!/bin/bash
# Builds FFmpeg for the PS4 (OpenOrbis) as static libraries with just what
# Stremio TV needs: common containers (MKV, MP4, TS...), video and audio
# decoders, subtitles, swscale/swresample. Input comes through our own
# AVIOContext (HTTPS via libSceHttp), so there are no network protocols.
# x86 assembly (nasm) matters: HEVC is several times slower without it.
#
#   tools/build_ffmpeg_ps4.sh     -> ffmpeg_ps4/install/{include,lib}
# (outside build_ps4, so `make -f Makefile.ps4 clean` keeps it)
#
# Needs OO_PS4_TOOLCHAIN, clang/lld/llvm-ar and nasm.
set -e
VERSION=7.1
SHA256=40973d44970dbc83ef302b0609f2e74982be2d85916dd2ee7472d30678a7abe6
HERE="$(cd "$(dirname "$0")/.." && pwd)"
WORK="$HERE/ffmpeg_ps4/src"
PREFIX="$HERE/ffmpeg_ps4/install"
OO=$OO_PS4_TOOLCHAIN
mkdir -p "$WORK"
cd "$WORK"
if [ ! -d ffmpeg-$VERSION ]; then
  curl -fsSL -o ffmpeg-$VERSION.tar.xz https://ffmpeg.org/releases/ffmpeg-$VERSION.tar.xz
  echo "$SHA256  ffmpeg-$VERSION.tar.xz" | sha256sum -c -
  tar xf ffmpeg-$VERSION.tar.xz
fi
cd ffmpeg-$VERSION
ARCH="--target=x86_64-pc-freebsd12-elf"
CFLAGS="$ARCH -fPIC -funwind-tables -D__PS4__ -D__OPENORBIS__ -D__BSD_VISIBLE -D_BSD_SOURCE -isysroot $OO -isystem $OO/include"
LDFLAGS="$ARCH -fuse-ld=lld -nostdlib -Wl,-m,elf_x86_64 -Wl,-pie -Wl,--script=$OO/link.x -Wl,--eh-frame-hdr -L$OO/lib $OO/lib/crt1.o"
LIBS="-lc -lkernel -lclang_rt.builtins-x86_64"
./configure --prefix="$PREFIX" \
  --enable-cross-compile --arch=x86_64 --target-os=freebsd \
  --cc=clang --cxx=clang++ --host-cc=clang --ar=llvm-ar --ranlib=llvm-ranlib --nm=llvm-nm --strip=llvm-strip \
  --extra-cflags="$CFLAGS" --extra-ldflags="$LDFLAGS" --extra-libs="$LIBS" \
  --disable-shared --enable-static --enable-pic \
  --disable-programs --disable-doc --disable-debug --disable-autodetect \
  --disable-avdevice --disable-avfilter --disable-network \
  --disable-everything \
  --enable-protocol=file \
  --enable-demuxer=matroska,mov,mpegts,avi,flv,ogg,wav,mp3,aac,ac3,eac3,hls,webm_dash_manifest \
  --enable-decoder=h264,hevc,mpeg4,mpeg2video,vp8,vp9,av1 \
  --enable-decoder=aac,aac_latm,ac3,eac3,mp3,mp2,opus,vorbis,flac,dca,truehd,mlp,alac,pcm_s16le,pcm_s24le,pcm_s32le,pcm_f32le \
  --enable-decoder=subrip,ass,ssa,webvtt,mov_text,dvd_subtitle,hdmv_pgs_subtitle \
  --enable-parser=h264,hevc,mpeg4video,mpegvideo,vp8,vp9,av1,aac,aac_latm,ac3,mpegaudio,opus,vorbis,flac,dca,mlp \
  --enable-bsf=h264_mp4toannexb,hevc_mp4toannexb,aac_adtstoasc,extract_extradata \
  --enable-swscale --enable-swresample \
  --x86asmexe=nasm

# av_cpu_count's sysctl path isn't usable on PS4 (same fix as PacBrew's
# recipe); the player sets its thread count itself.
sed -i 's|^#define HAVE_SYSCTL 1|#define HAVE_SYSCTL 0|' config.h
make -j"$(nproc)"
make install
echo "FFmpeg for PS4 installed in $PREFIX"
