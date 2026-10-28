#!/bin/sh
# Builds the small, decode-only FFmpeg that STLT-VJ uses for everything that is not HAP
# (H.264, HEVC, ProRes, DXV, PNG, JPEG, GIF, ...), as static libraries with no outside dependencies.
#   app/scripts/build-ffmpeg.sh <install prefix> [extra configure options]
# Then configure the app with -DFFMPEG_ROOT=<install prefix>.
# Windows builds use a prebuilt FFmpeg instead (see app/CMakeLists.txt).
set -e

PREFIX="$1"
if [ -z "$PREFIX" ]; then
	echo "usage: $0 <install prefix> [extra configure options]" >&2
	exit 2
fi
shift
VERSION=n7.1.5
WORK="${FFMPEG_BUILD_DIR:-$PREFIX/src}"
JOBS=$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)

mkdir -p "$WORK"
if [ ! -d "$WORK/FFmpeg" ]; then
	git clone --depth 1 --branch "$VERSION" https://github.com/FFmpeg/FFmpeg.git "$WORK/FFmpeg"
fi
cd "$WORK/FFmpeg"

./configure \
	--prefix="$PREFIX" \
	--enable-static --disable-shared --enable-pic \
	--disable-programs --disable-doc --disable-network --disable-autodetect --disable-everything \
	--disable-avdevice --disable-avfilter --disable-postproc --disable-swresample \
	--enable-zlib \
	--enable-protocol=file \
	--enable-demuxer=mov,matroska,avi,mxf,mpegts,m4v,h264,hevc,gif,image2,image_png_pipe,image_jpeg_pipe,image_bmp_pipe,image_tiff_pipe,image_webp_pipe \
	--enable-decoder=h264,hevc,prores,dxv,dnxhd,mjpeg,png,gif,bmp,tiff,webp,vp8,vp9,mpeg4,mpeg2video,qtrle,rawvideo,v210 \
	--enable-parser=h264,hevc,mpeg4video,mpegvideo,mjpeg,png,gif,bmp,webp,vp8,vp9 \
	"$@"
make -j"$JOBS"
make install
echo "FFmpeg $VERSION installed in $PREFIX"
