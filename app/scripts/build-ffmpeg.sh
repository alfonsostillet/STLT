#!/bin/sh
# Builds the small, decode-only FFmpeg that STLT-VJ uses for everything that is not HAP
# (H.264, HEVC, ProRes, DXV, PNG, JPEG, GIF, ..., and the clips' sound), as static libraries with no outside
# dependencies but the system's own: on macOS, VideoToolbox decodes H.264, HEVC and ProRes on the graphics chip.
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

# The system's hardware video decoder, where there is one.
HWACCEL=""
if [ "$(uname)" = "Darwin" ]; then
	HWACCEL="--enable-videotoolbox --enable-hwaccel=h264_videotoolbox,hevc_videotoolbox,prores_videotoolbox,vp9_videotoolbox,mpeg2_videotoolbox,mpeg4_videotoolbox"
fi

./configure \
	--prefix="$PREFIX" \
	--enable-static --disable-shared --enable-pic \
	--disable-programs --disable-doc --disable-network --disable-autodetect --disable-everything \
	--disable-avdevice --disable-avfilter --disable-postproc --enable-swresample \
	--enable-zlib \
	--enable-protocol=file \
	--enable-demuxer=mov,matroska,avi,mxf,mpegts,m4v,h264,hevc,gif,image2,image_png_pipe,image_jpeg_pipe,image_bmp_pipe,image_tiff_pipe,image_webp_pipe \
	--enable-decoder=h264,hevc,prores,dxv,dnxhd,mjpeg,png,gif,bmp,tiff,webp,vp8,vp9,mpeg4,mpeg2video,qtrle,rawvideo,v210 \
	--enable-decoder=aac,aac_latm,mp3,mp3float,mp2,ac3,eac3,alac,flac,opus,vorbis,pcm_s16le,pcm_s16be,pcm_s24le,pcm_s24be,pcm_s32le,pcm_f32le,pcm_f32be \
	--enable-parser=h264,hevc,mpeg4video,mpegvideo,mjpeg,png,gif,bmp,webp,vp8,vp9,aac,mpegaudio,ac3,flac,opus,vorbis \
	$HWACCEL \
	"$@"
make -j"$JOBS"
make install
echo "FFmpeg $VERSION installed in $PREFIX"
