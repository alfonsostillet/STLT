// Every format that is not HAP, through FFmpeg: H.264, HEVC, ProRes, DXV, DNxHD, VP8/9, PNG, JPEG, GIF, ...
// H.264, HEVC and the other codecs the computer's video hardware knows decode there when it can (VideoToolbox,
// D3D11, DXVA2, VA-API, VDPAU), falling back to the CPU. Planar YUV and NV12 frames go to the GPU as they are
// (single and two-channel textures, converted in the shader); anything else is converted to RGBA on the CPU.
//
// Jumping (cue points, scratching) costs what decoding from the keyframe before the frame costs, which in an
// H.264 file can be hundreds of frames. To keep that down: the keyframes are known from the file's index, so a
// jump ahead decodes on when no keyframe lies between; recent frames and those just before a jump's target stay
// in memory (FrameCacheMegabytes), so scratching back over them is instant; and while the playhead is scratched
// the decoder gives up on frames left behind and shows the nearest it has (the keyframe after a seek, or how far
// it got), the exact frame following once the playhead rests.
#ifdef STLTVJ_WITH_FFMPEG
#include <algorithm>
#include <cctype>
#include <chrono>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <map>
#include <mutex>
#include <vector>
#include "media/FrameSource.h"

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/hwcontext.h>
#include <libavutil/imgutils.h>
#include <libavutil/pixdesc.h>
#include <libswscale/swscale.h>
}

namespace stltvj
{
namespace
{
std::string ErrorText( int code )
{
	char text[ AV_ERROR_MAX_STRING_SIZE ] = {};
	av_strerror( code, text, sizeof( text ) );
	return text;
}

std::string FriendlyCodecName( const AVCodec* codec )
{
	struct Name
	{
		const char* id;
		const char* name;
	};
	static const Name names[] = { { "h264", "H.264" }, { "hevc", "HEVC" },  { "prores", "ProRes" }, { "dxv", "DXV" },   { "dnxhd", "DNxHD" },
		                          { "mjpeg", "JPEG" },  { "png", "PNG" },    { "gif", "GIF" },       { "vp8", "VP8" },   { "vp9", "VP9" },
		                          { "mpeg4", "MPEG-4" }, { "mpeg2video", "MPEG-2" }, { "webp", "WebP" }, { "tiff", "TIFF" }, { "bmp", "BMP" } };
	for( const Name& n : names )
		if( std::strcmp( codec->name, n.id ) == 0 )
			return n.name;
	std::string upper = codec->name;
	std::transform( upper.begin(), upper.end(), upper.begin(), []( unsigned char c ) { return (char)std::toupper( c ); } );
	return upper;
}

// The 8-bit planar YUV format a frame can be uploaded as, or NONE for RGBA.
AVPixelFormat YuvTarget( AVPixelFormat format )
{
	switch( format )
	{
	case AV_PIX_FMT_YUV420P:
	case AV_PIX_FMT_YUVJ420P:
	case AV_PIX_FMT_YUV422P:
	case AV_PIX_FMT_YUVJ422P:
	case AV_PIX_FMT_YUV444P:
	case AV_PIX_FMT_YUVJ444P:
	case AV_PIX_FMT_NV12: return format;
	default: break;
	}
	const AVPixFmtDescriptor* d = av_pix_fmt_desc_get( format );
	if( !d || ( d->flags & ( AV_PIX_FMT_FLAG_RGB | AV_PIX_FMT_FLAG_ALPHA | AV_PIX_FMT_FLAG_PAL | AV_PIX_FMT_FLAG_HWACCEL ) ) || d->nb_components != 3 )
		return AV_PIX_FMT_NONE;
	// Deeper or semi-planar YUV (10-bit ProRes, HEVC Main 10, NV12): reduce to 8-bit planes with the same chroma layout.
	if( d->log2_chroma_w == 1 && d->log2_chroma_h == 1 )
		return AV_PIX_FMT_YUV420P;
	if( d->log2_chroma_w == 1 && d->log2_chroma_h == 0 )
		return AV_PIX_FMT_YUV422P;
	if( d->log2_chroma_w == 0 && d->log2_chroma_h == 0 )
		return AV_PIX_FMT_YUV444P;
	return AV_PIX_FMT_NONE;
}

// The hardware decoders worth trying here, best first.
const AVHWDeviceType kHardware[] = {
#if defined( _WIN32 )
	AV_HWDEVICE_TYPE_D3D11VA, AV_HWDEVICE_TYPE_DXVA2,
#elif defined( __APPLE__ )
	AV_HWDEVICE_TYPE_VIDEOTOOLBOX,
#else
	AV_HWDEVICE_TYPE_VAAPI, AV_HWDEVICE_TYPE_VDPAU,
#endif
};

// Long-GOP codecs gain from the hardware; intra codecs (ProRes aside, on Apple) and images decode fine on the CPU.
bool WorthHardware( AVCodecID id )
{
	switch( id )
	{
	case AV_CODEC_ID_H264:
	case AV_CODEC_ID_HEVC:
	case AV_CODEC_ID_VP9:
	case AV_CODEC_ID_AV1:
	case AV_CODEC_ID_MPEG2VIDEO:
	case AV_CODEC_ID_MPEG4: return true;
#ifdef __APPLE__
	case AV_CODEC_ID_PRORES: return true;
#endif
	default: return false;
	}
}

std::string HardwareName( AVHWDeviceType type )
{
	switch( type )
	{
	case AV_HWDEVICE_TYPE_D3D11VA: return "D3D11";
	case AV_HWDEVICE_TYPE_DXVA2: return "DXVA2";
	case AV_HWDEVICE_TYPE_VIDEOTOOLBOX: return "VideoToolbox";
	case AV_HWDEVICE_TYPE_VAAPI: return "VA-API";
	case AV_HWDEVICE_TYPE_VDPAU: return "VDPAU";
	default: return av_hwdevice_get_type_name( type );
	}
}

// One device of each kind for the whole program: making one takes a moment, and a missing one (no such
// hardware, no driver) is not looked for again. Returns a new reference, or null.
AVBufferRef* SharedDevice( AVHWDeviceType type )
{
	static std::mutex mutex;
	static std::map< int, AVBufferRef* > devices; // null: not available
	std::lock_guard< std::mutex > lock( mutex );
	auto known = devices.find( (int)type );
	if( known == devices.end() )
	{
		int level = av_log_get_level();
		av_log_set_level( AV_LOG_QUIET ); // its absence is no error worth printing
		AVBufferRef* device = nullptr;
		if( av_hwdevice_ctx_create( &device, type, nullptr, nullptr, 0 ) < 0 )
			device = nullptr;
		av_log_set_level( level );
		known = devices.emplace( (int)type, device ).first;
	}
	return known->second ? av_buffer_ref( known->second ) : nullptr;
}

bool IsFullRangeFormat( AVPixelFormat f )
{
	return f == AV_PIX_FMT_YUVJ420P || f == AV_PIX_FMT_YUVJ422P || f == AV_PIX_FMT_YUVJ444P;
}
}

class FfmpegSource : public FrameSource
{
public:
	~FfmpegSource() override
	{
		sws_freeContext( sws );
		av_frame_free( &transfer );
		av_frame_free( &frame );
		av_packet_free( &packet );
		avcodec_free_context( &codec );
		avformat_close_input( &format );
	}

	bool Open( const std::string& path, std::string& error )
	{
		int result = avformat_open_input( &format, path.c_str(), nullptr, nullptr );
		if( result < 0 )
		{
			error = "Cannot open: " + ErrorText( result );
			return false;
		}
		if( ( result = avformat_find_stream_info( format, nullptr ) ) < 0 )
		{
			error = "Cannot read the file: " + ErrorText( result );
			return false;
		}
		const AVCodec* decoder = nullptr;
		stream                 = av_find_best_stream( format, AVMEDIA_TYPE_VIDEO, -1, -1, &decoder, 0 );
		if( stream < 0 || !decoder )
		{
			error = stream == AVERROR_DECODER_NOT_FOUND ? "No decoder for this video codec in this build" : "No video in this file";
			return false;
		}
		AVStream* st = format->streams[ stream ];
		bool hardware = HardwareDecoding() && WorthHardware( decoder->id );
		if( !StartDecoder( decoder, st, hardware, error ) )
			return false;
		codecName = FriendlyCodecName( decoder );
		timeBase  = st->time_base;
		AVRational rate = st->avg_frame_rate.num && st->avg_frame_rate.den ? st->avg_frame_rate : st->r_frame_rate;
		fps             = rate.num && rate.den ? av_q2d( rate ) : 25.0;
		if( !( fps >= 1 && fps <= 1000 ) )
			fps = 25.0;
		startPts = st->start_time != AV_NOPTS_VALUE ? st->start_time : 0;
		if( st->duration != AV_NOPTS_VALUE && st->duration > 0 )
			duration = st->duration * av_q2d( timeBase );
		else if( format->duration != AV_NOPTS_VALUE && format->duration > 0 )
			duration = format->duration / (double)AV_TIME_BASE;
		const char* demuxer = format->iformat->name;
		still               = std::strstr( demuxer, "image2" ) || std::strstr( demuxer, "_pipe" );
		frameCount          = still ? 1 : st->nb_frames > 0 ? (size_t)st->nb_frames : (size_t)std::max< long long >( 1, std::llround( duration * fps ) );
		if( still )
			duration = 0;
		else if( duration <= 0 )
			duration = frameCount / fps;

		frame    = av_frame_alloc();
		transfer = av_frame_alloc();
		packet   = av_packet_alloc();
		bool first = ReadFrame() && Materialize();
		if( !first && hardwareName.size() )
		{
			// The hardware took the file but cannot decode it (a profile it lacks): the CPU, from the start.
			avcodec_free_context( &codec );
			if( !StartDecoder( decoder, st, false, error ) )
				return false;
			av_seek_frame( format, stream, startPts, AVSEEK_FLAG_BACKWARD );
			draining = ended = false;
			first            = ReadFrame() && Materialize();
		}
		if( !first )
		{
			error = "Cannot decode the first frame";
			return false;
		}
		BuildLayout();
		lastIndex = IndexOf( frame );
		IndexKeyframes( st );
		NoteKeyframe();
		if( still )
		{
			cache.resize( layout.totalBytes );
			Store( cache.data() );
		}
		return true;
	}

	const FrameLayout& Layout() const override { return layout; }
	size_t FrameCount() const override { return frameCount; }
	double Duration() const override { return duration; }
	double FrameRate() const override { return fps; }
	size_t FrameAt( double seconds ) const override
	{
		if( still || seconds <= 0 )
			return 0;
		return std::min( frameCount - 1, (size_t)std::floor( seconds * fps + 1e-6 ) );
	}
	std::string CodecName() const override { return codecName; }
	std::string Details() const override
	{
		if( still )
			return "still image";
		return hardwareName.empty() ? "decoded by FFmpeg" : "decoded by the graphics chip (" + hardwareName + ")";
	}
	bool IsHardwareDecoded() const override { return !hardwareName.empty(); }
	bool IsStill() const override { return still; }

	bool Decode( size_t target, uint8_t* destination ) override
	{
		int64_t decoded = 0;
		return DecodeFrame( target, destination, DecodeRequest{}, decoded ) == DecodeResult::Exact;
	}

	void ReleaseMemory() override
	{
		std::lock_guard< std::mutex > lock( mutex );
		kept.clear();
	}

	DecodeResult DecodeFrame( size_t targetIndex, uint8_t* destination, const DecodeRequest& request, int64_t& decoded ) override
	{
		std::lock_guard< std::mutex > lock( mutex );
		if( still )
		{
			std::memcpy( destination, cache.data(), cache.size() );
			decoded = 0;
			return DecodeResult::Exact;
		}
		int64_t t = (int64_t)targetIndex;
		decoded   = t;
		if( CacheGet( t, destination ) )
			return DecodeResult::Exact;
		auto scratching = [ & ] { return request.approximate && request.approximate(); };
		// An approximate answer only when it gets closer to the target than what is on show.
		auto closer = [ & ]( int64_t candidate ) {
			return request.shown < 0 || std::llabs( candidate - t ) < std::llabs( request.shown - t );
		};
		if( scratching() )
		{
			int64_t near = CacheNearest( t, std::max< int64_t >( 2, (int64_t)( fps / 4 ) ) );
			if( near >= 0 && closer( near ) && CacheGet( near, destination ) )
			{
				decoded = near;
				return DecodeResult::Approximate;
			}
		}
		if( !Reachable( t ) )
			Seek( (size_t)t );
		bool justSought                 = lastIndex == kUnknown;
		std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
		for( int guard = 0; guard < 100000; ++guard )
		{
			if( !ReadFrame() )
				return DecodeResult::Failed;
			lastIndex = IndexOf( frame );
			NoteKeyframe();
			if( lastIndex >= t )
			{
				decoded = lastIndex;
				return Emit( lastIndex, destination ) ? DecodeResult::Exact : DecodeResult::Failed;
			}
			// The frames just before the target are where a scratch goes next: keep them (no more than fit).
			if( t - lastIndex <= std::min< int64_t >( kKeepBefore, (int64_t)CacheCapacity() ) )
				CachePut( lastIndex );
			bool givingUp = request.abandon && request.abandon();
			bool showNow  = scratching() && closer( lastIndex )
			               && ( justSought || givingUp
			                    || std::chrono::steady_clock::now() - start > std::chrono::milliseconds( kProgressMs ) );
			justSought = false;
			if( showNow )
			{
				decoded = lastIndex;
				return Emit( lastIndex, destination ) ? DecodeResult::Approximate : DecodeResult::Failed;
			}
			if( givingUp )
				return DecodeResult::Abandoned;
		}
		return DecodeResult::Failed;
	}

private:
	static constexpr int64_t kUnknown = INT64_MIN;
	static constexpr int64_t kKeepBefore = 90; // frames before a jump's target kept in memory
	static constexpr int kProgressMs     = 40; // while scratching, show how far the decoder got this often

	// Whether `t` can be had by decoding on from where the decoder is, rather than seeking: ahead of it, with no
	// keyframe in between worth seeking to (without an index, not too far ahead).
	bool Reachable( int64_t t ) const
	{
		if( lastIndex == kUnknown || ended || t <= lastIndex )
			return false;
		if( keyframes.empty() )
			return t <= lastIndex + (int64_t)std::max( 30.0, fps * 2 );
		auto after = std::upper_bound( keyframes.begin(), keyframes.end(), t - keyShift );
		if( after == keyframes.begin() )
			return true;
		int64_t key = *( after - 1 ) + keyShift; // the last keyframe at or before t
		return key <= lastIndex + 4;
	}

	// The index gives decoding times, the frames presentation times: with B-frames they differ by a frame or two.
	// A decoded keyframe tells by how much, so the keyframes are placed where the frames number them.
	void NoteKeyframe()
	{
#ifdef AV_FRAME_FLAG_KEY
		bool key = ( frame->flags & AV_FRAME_FLAG_KEY ) != 0;
#else
		bool key = frame->key_frame != 0;
#endif
		if( !key || keyframes.empty() || lastIndex == kUnknown )
			return;
		int64_t indexed = lastIndex - keyShift;
		auto near       = std::lower_bound( keyframes.begin(), keyframes.end(), indexed - 8 );
		bool found      = false;
		int64_t best    = 0;
		for( ; near != keyframes.end() && *near <= indexed + 8; ++near )
			if( !found || std::llabs( *near - indexed ) < std::llabs( best - indexed ) )
			{
				best  = *near;
				found = true;
			}
		if( found )
			keyShift = lastIndex - best;
	}

	// The keyframes' frame numbers, from the file's index (MP4 and MOV have one; otherwise none are known).
	void IndexKeyframes( AVStream* st )
	{
		keyframes.clear();
		int count = avformat_index_get_entries_count( st );
		for( int i = 0; i < count; ++i )
		{
			const AVIndexEntry* e = avformat_index_get_entry( st, i );
			if( e && ( e->flags & AVINDEX_KEYFRAME ) && e->timestamp != AV_NOPTS_VALUE )
				keyframes.push_back( (int64_t)std::floor( ( e->timestamp - startPts ) * av_q2d( timeBase ) * fps + 0.5 ) );
		}
		std::sort( keyframes.begin(), keyframes.end() );
		keyframes.erase( std::unique( keyframes.begin(), keyframes.end() ), keyframes.end() );
		// An index with every frame a keyframe tells nothing a seek would not.
		if( keyframes.size() <= 1 )
			keyframes.clear();
	}

	// ---- Frames kept in memory ----

	struct Kept
	{
		std::vector< uint8_t > bytes;
		uint64_t used = 0;
	};

	size_t CacheCapacity() const
	{
		size_t bytes = (size_t)FrameCacheMegabytes() * 1024 * 1024;
		return layout.totalBytes ? bytes / layout.totalBytes : 0;
	}

	bool CacheGet( int64_t index, uint8_t* destination )
	{
		auto at = kept.find( index );
		if( at == kept.end() )
			return false;
		at->second.used = ++useCounter;
		std::memcpy( destination, at->second.bytes.data(), at->second.bytes.size() );
		return true;
	}

	// The kept frame nearest `t` within `range`, or -1.
	int64_t CacheNearest( int64_t t, int64_t range ) const
	{
		int64_t best = -1;
		auto above   = kept.lower_bound( t );
		if( above != kept.end() && above->first - t <= range )
			best = above->first;
		if( above != kept.begin() )
		{
			int64_t below = std::prev( above )->first;
			if( t - below <= range && ( best < 0 || t - below <= best - t ) )
				best = below;
		}
		return best;
	}

	// Room for frame `index`: a new buffer, or the least recently used one's; null when nothing may be kept.
	std::vector< uint8_t >* CacheSlot( int64_t index )
	{
		size_t capacity = CacheCapacity();
		if( capacity == 0 )
		{
			kept.clear();
			return nullptr;
		}
		std::vector< uint8_t > bytes;
		while( kept.size() >= capacity )
		{
			auto oldest = std::min_element( kept.begin(), kept.end(), []( const auto& a, const auto& b ) { return a.second.used < b.second.used; } );
			bytes.swap( oldest->second.bytes );
			kept.erase( oldest );
		}
		Kept& k = kept[ index ];
		k.bytes.swap( bytes );
		k.bytes.resize( layout.totalBytes );
		k.used = ++useCounter;
		return &k.bytes;
	}

	// Keeps the current frame (frame `index`).
	void CachePut( int64_t index )
	{
		if( kept.count( index ) )
			return;
		if( std::vector< uint8_t >* bytes = CacheSlot( index ) )
			if( !Store( bytes->data() ) )
				kept.erase( index );
	}

	// Writes the current frame (frame `index`) to `destination`, keeping a copy. The copy is made first: the
	// destination is a mapped pixel buffer, which is slow to read back from.
	bool Emit( int64_t index, uint8_t* destination )
	{
		CachePut( index );
		auto at = kept.find( index );
		if( at == kept.end() )
			return Store( destination );
		std::memcpy( destination, at->second.bytes.data(), at->second.bytes.size() );
		return true;
	}

	// Opens the decoder, on the hardware when asked and available.
	bool StartDecoder( const AVCodec* decoder, AVStream* st, bool hardware, std::string& error )
	{
		hardwareName.clear();
		hardwareFormat = AV_PIX_FMT_NONE;
		codec          = avcodec_alloc_context3( decoder );
		avcodec_parameters_to_context( codec, st->codecpar );
		codec->thread_count = 0; // one per core
		codec->thread_type  = FF_THREAD_FRAME | FF_THREAD_SLICE;
		if( hardware )
			for( AVHWDeviceType type : kHardware )
			{
				for( int i = 0; const AVCodecHWConfig* config = avcodec_get_hw_config( decoder, i ); ++i )
				{
					if( !( config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX ) || config->device_type != type )
						continue;
					AVBufferRef* device = SharedDevice( type );
					if( !device )
						break;
					codec->hw_device_ctx = device; // the codec owns it now
					codec->opaque        = this;
					codec->get_format    = &FfmpegSource::PickFormat;
					codec->thread_count  = 1; // the hardware does the work
					hardwareFormat       = config->pix_fmt;
					hardwareName         = HardwareName( type );
					break;
				}
				if( !hardwareName.empty() )
					break;
			}
		int result = avcodec_open2( codec, decoder, nullptr );
		if( result < 0 && !hardwareName.empty() )
		{
			avcodec_free_context( &codec );
			return StartDecoder( decoder, st, false, error );
		}
		if( result < 0 )
		{
			error = "Cannot start the decoder: " + ErrorText( result );
			return false;
		}
		return true;
	}

	// The decoder asks which output it may use: the hardware's when offered, else the first software one.
	static AVPixelFormat PickFormat( AVCodecContext* context, const AVPixelFormat* formats )
	{
		FfmpegSource* self = (FfmpegSource*)context->opaque;
		for( const AVPixelFormat* f = formats; *f != AV_PIX_FMT_NONE; ++f )
			if( *f == self->hardwareFormat )
				return *f;
		self->hardwareName.clear(); // this stream is not for the hardware (profile, size...)
		for( const AVPixelFormat* f = formats; *f != AV_PIX_FMT_NONE; ++f )
		{
			const AVPixFmtDescriptor* d = av_pix_fmt_desc_get( *f );
			if( d && !( d->flags & AV_PIX_FMT_FLAG_HWACCEL ) )
				return *f;
		}
		return AV_PIX_FMT_NONE;
	}

	// The next decoded frame into `frame`; false at the end of the stream or on an error.
	bool ReadFrame()
	{
		for( ;; )
		{
			int result = avcodec_receive_frame( codec, frame );
			if( result == 0 )
				return true; // a hardware frame stays in video memory until it is stored (Materialize)
			if( result == AVERROR_EOF )
			{
				ended = true;
				return false;
			}
			if( result != AVERROR( EAGAIN ) || draining )
				return false;
			for( ;; )
			{
				if( av_read_frame( format, packet ) < 0 )
				{
					avcodec_send_packet( codec, nullptr ); // flush what the decoder still holds
					draining = true;
					break;
				}
				bool ours = packet->stream_index == stream;
				if( ours )
					avcodec_send_packet( codec, packet );
				av_packet_unref( packet );
				if( ours )
					break;
			}
		}
	}

	// A hardware frame copied from video memory to ours (NV12, or P010 for 10-bit), with its timing and colour
	// information; frames skipped on the way to a jump's target never need it.
	bool Materialize()
	{
		if( hardwareFormat == AV_PIX_FMT_NONE || frame->format != hardwareFormat )
			return true;
		av_frame_unref( transfer );
		if( av_hwframe_transfer_data( transfer, frame, 0 ) < 0 )
			return false;
		av_frame_copy_props( transfer, frame );
		av_frame_unref( frame );
		av_frame_move_ref( frame, transfer );
		return true;
	}

	void Seek( size_t target )
	{
		int64_t ts = startPts + (int64_t)std::llround( target / fps / av_q2d( timeBase ) );
		if( av_seek_frame( format, stream, ts, AVSEEK_FLAG_BACKWARD ) < 0 )
			av_seek_frame( format, stream, startPts, AVSEEK_FLAG_BACKWARD );
		avcodec_flush_buffers( codec );
		draining  = false;
		ended     = false;
		lastIndex = kUnknown;
	}

	int64_t IndexOf( const AVFrame* f ) const
	{
		int64_t ts = f->best_effort_timestamp != AV_NOPTS_VALUE ? f->best_effort_timestamp : f->pts;
		if( ts == AV_NOPTS_VALUE )
			return lastIndex == kUnknown ? 0 : lastIndex + 1;
		return (int64_t)std::floor( ( ts - startPts ) * av_q2d( timeBase ) * fps + 0.5 );
	}

	void BuildLayout()
	{
		layout            = FrameLayout{};
		int w             = frame->width, h = frame->height;
		layout.width      = w;
		layout.height     = h;
		AVPixelFormat src = (AVPixelFormat)frame->format;
		target            = YuvTarget( src );
		if( target != AV_PIX_FMT_NONE )
		{
			const AVPixFmtDescriptor* d = av_pix_fmt_desc_get( target );
			int cw = AV_CEIL_RSHIFT( w, d->log2_chroma_w ), ch = AV_CEIL_RSHIFT( h, d->log2_chroma_h );
			layout.Add( { PlaneFormat::R8, 0, w, h, (size_t)w * h, 0 } );
			if( target == AV_PIX_FMT_NV12 )
			{
				layout.Add( { PlaneFormat::RG8, 0, cw, ch, (size_t)cw * ch * 2, 0 } );
				layout.shading = Shading::Nv12;
			}
			else
			{
				layout.Add( { PlaneFormat::R8, 0, cw, ch, (size_t)cw * ch, 0 } );
				layout.Add( { PlaneFormat::R8, 0, cw, ch, (size_t)cw * ch, 0 } );
				layout.shading = Shading::Yuv;
			}
			layout.fullRange = frame->color_range == AVCOL_RANGE_JPEG || IsFullRangeFormat( src );
			layout.bt709     = frame->colorspace == AVCOL_SPC_BT709 || ( frame->colorspace == AVCOL_SPC_UNSPECIFIED && h >= 720 );
		}
		else
		{
			target = AV_PIX_FMT_RGBA;
			layout.Add( { PlaneFormat::RGBA8, 0, w, h, (size_t)w * h * 4, 0 } );
			layout.shading = Shading::Rgba;
		}
	}

	// Writes the current frame into the layout: plane copies when it already matches, swscale otherwise.
	bool Store( uint8_t* destination )
	{
		if( !Materialize() )
			return false;
		uint8_t* planes[ 4 ] = {};
		int strides[ 4 ]     = {};
		for( unsigned p = 0; p < layout.planeCount; ++p )
		{
			planes[ p ]  = destination + layout.planes[ p ].offset;
			PlaneFormat f = layout.planes[ p ].format;
			strides[ p ]  = layout.planes[ p ].width * ( f == PlaneFormat::RGBA8 ? 4 : f == PlaneFormat::RG8 ? 2 : 1 );
		}
		if( frame->format == target && frame->width == layout.width && frame->height == layout.height )
		{
			for( unsigned p = 0; p < layout.planeCount; ++p )
				av_image_copy_plane( planes[ p ], strides[ p ], frame->data[ p ], frame->linesize[ p ], strides[ p ], layout.planes[ p ].height );
			return true;
		}
		sws = sws_getCachedContext( sws, frame->width, frame->height, (AVPixelFormat)frame->format, layout.width, layout.height, target, SWS_BILINEAR,
		                            nullptr, nullptr, nullptr );
		if( !sws )
			return false;
		sws_scale( sws, frame->data, frame->linesize, 0, frame->height, planes, strides );
		return true;
	}

	std::mutex mutex;
	AVFormatContext* format = nullptr;
	AVCodecContext* codec   = nullptr;
	AVFrame* frame          = nullptr;
	AVFrame* transfer       = nullptr; // a hardware frame's copy in memory
	AVPacket* packet        = nullptr;
	SwsContext* sws         = nullptr;
	AVPixelFormat target    = AV_PIX_FMT_NONE;
	int stream              = -1;
	AVRational timeBase{ 1, 1 };
	int64_t startPts  = 0;
	double fps        = 25;
	double duration   = 0;
	size_t frameCount = 1;
	bool still        = false;
	bool draining     = false;
	bool ended        = false;
	int64_t lastIndex = kUnknown;
	std::vector< uint8_t > cache; // a still image, decoded once
	std::vector< int64_t > keyframes;   // frame numbers, ascending; empty when the file has no index
	int64_t keyShift = 0;               // add to `keyframes` for the frames' own numbering (NoteKeyframe)
	std::map< int64_t, Kept > kept;     // decoded frames kept in memory, by frame number
	uint64_t useCounter = 0;
	FrameLayout layout;
	std::string codecName;
	std::string hardwareName; // empty when decoding on the CPU
	AVPixelFormat hardwareFormat = AV_PIX_FMT_NONE;
};

std::unique_ptr< FrameSource > OpenFfmpegSource( const std::string& path, std::string& error )
{
	av_log_set_level( AV_LOG_ERROR );
	auto source = std::make_unique< FfmpegSource >();
	if( !source->Open( path, error ) )
		return nullptr;
	return source;
}
}
#endif
