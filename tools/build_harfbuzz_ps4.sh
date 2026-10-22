#!/bin/bash
# Builds HarfBuzz (text shaping for Arabic, Indic, Thai... subtitles) for the
# PS4 (OpenOrbis) as a static library. Only the core is built: fonts are
# read by HarfBuzz itself (no FreeType, ICU or GLib integration), and the app
# shapes on one thread, so it's built without thread safety.
#
#   tools/build_harfbuzz_ps4.sh     -> harfbuzz_ps4/install/{include,lib}
# (outside build_ps4, so `make -f Makefile.ps4 clean` keeps it)
#
# Needs OO_PS4_TOOLCHAIN, clang and llvm-ar.
set -e
VERSION=14.6.0
SHA256=d07a007327277708a2a73ae437887cdbaf282937f6d03ca5467723e9099af586
HERE="$(cd "$(dirname "$0")/.." && pwd)"
WORK="$HERE/harfbuzz_ps4/src"
PREFIX="$HERE/harfbuzz_ps4/install"
OO=$OO_PS4_TOOLCHAIN
mkdir -p "$WORK" "$PREFIX/lib" "$PREFIX/include/harfbuzz"
cd "$WORK"
if [ ! -d harfbuzz-$VERSION ]; then
  curl -fsSL -o harfbuzz-$VERSION.tar.xz https://github.com/harfbuzz/harfbuzz/releases/download/$VERSION/harfbuzz-$VERSION.tar.xz
  echo "$SHA256  harfbuzz-$VERSION.tar.xz" | sha256sum -c -
  tar xf harfbuzz-$VERSION.tar.xz
fi
cd harfbuzz-$VERSION/src
clang++ --target=x86_64-pc-freebsd12-elf -fPIC -funwind-tables -O2 -std=c++17 \
  -isysroot "$OO" -isystem "$OO/include/c++/v1" -isystem "$OO/include" \
  -fno-exceptions -fno-rtti -fno-threadsafe-statics -DHB_NO_MT \
  -c harfbuzz.cc -o "$WORK/harfbuzz.o"
rm -f "$PREFIX/lib/libharfbuzz.a"
llvm-ar rcs "$PREFIX/lib/libharfbuzz.a" "$WORK/harfbuzz.o"
# The public headers (hb.h includes the ones the app uses).
cp hb.h hb-*.h "$PREFIX/include/harfbuzz/"
echo "HarfBuzz for PS4 installed in $PREFIX"
