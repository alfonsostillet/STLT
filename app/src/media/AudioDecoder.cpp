#include "media/AudioDecoder.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>
#ifdef STLTVJ_WITH_FFMPEG
extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
}
#endif

namespace stltvj
{
#ifdef STLTVJ_WITH_FFMPEG
struct AudioDecoder::Impl
{
	AVFormatContext* format = nullptr;
	AVCodecContext* codec   = nullptr;
	SwrContext* swr         = nullptr;
	AVPacket* packet        = nullptr;
	AVFrame* frame          = nullptr;
	int stream              = -1;
	int rate                = 48000;
	AVRational timeBase{ 1, 1 };
	double duration = 0;
	double position = 0;        // of the first frame in `pending`
	std::vector< float > pending; // converted, not yet read (stereo)
	size_t pendingAt  = 0;      // frames of `pending` already read
	double seekTarget = -1;     // after a seek: drop what comes before it
	bool ended        = false;

	~Impl()
	{
		swr_free( &swr );
		av_frame_free( &frame );
		av_packet_free( &packet );
		avcodec_free_context( &codec );
		avformat_close_input( &format );
	}

	bool MakeResampler()
	{
		swr_free( &swr );
		AVChannelLayout stereo = AV_CHANNEL_LAYOUT_STEREO;
		// Zeroed: av_channel_layout_copy uninitialises its destination first, and stack garbage that reads as a
		// custom order would have it free a garbage pointer (an intermittent crash on Windows).
		AVChannelLayout source{};
		if( codec->ch_layout.nb_channels > 0 && codec->ch_layout.order != AV_CHANNEL_ORDER_UNSPEC )
			av_channel_layout_copy( &source, &codec->ch_layout );
		else
			av_channel_layout_default( &source, std::max( codec->ch_layout.nb_channels, 1 ) );
		int result = swr_alloc_set_opts2( &swr, &stereo, AV_SAMPLE_FMT_FLT, rate, &source, codec->sample_fmt, codec->sample_rate, 0, nullptr );
		av_channel_layout_uninit( &source );
		return result >= 0 && swr_init( swr ) >= 0;
	}

	// Decodes the next block into `pending`; false at the end.
	bool DecodeMore()
	{
		for( ;; )
		{
			int result = avcodec_receive_frame( codec, frame );
			if( result == 0 )
			{
				double start = frame->best_effort_timestamp != AV_NOPTS_VALUE ? frame->best_effort_timestamp * av_q2d( timeBase ) : position;
				int capacity = swr_get_out_samples( swr, frame->nb_samples ) + 16;
				std::vector< float > block( (size_t)capacity * 2 );
				uint8_t* out    = (uint8_t*)block.data();
				int converted   = swr_convert( swr, &out, capacity, (const uint8_t**)frame->extended_data, frame->nb_samples );
				av_frame_unref( frame );
				if( converted <= 0 )
					continue;
				block.resize( (size_t)converted * 2 );
				size_t skip = 0;
				if( seekTarget >= 0 )
				{
					// Up to the sample asked for: drop the beginning of the block that holds it, or whole blocks before.
					double end = start + converted / (double)rate;
					if( end <= seekTarget )
						continue;
					skip       = (size_t)std::clamp( std::llround( ( seekTarget - start ) * rate ), 0LL, (long long)converted );
					start      = start + skip / (double)rate;
					seekTarget = -1;
				}
				pending.erase( pending.begin(), pending.begin() + pendingAt * 2 );
				pendingAt = 0;
				if( pending.empty() )
					position = start;
				pending.insert( pending.end(), block.begin() + skip * 2, block.end() );
				return true;
			}
			if( result == AVERROR_EOF )
				return false;
			if( result != AVERROR( EAGAIN ) )
				return false;
			// Feed the decoder.
			for( ;; )
			{
				if( ended )
				{
					avcodec_send_packet( codec, nullptr );
					break;
				}
				if( av_read_frame( format, packet ) < 0 )
				{
					ended = true;
					continue;
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
};

AudioDecoder::AudioDecoder() = default;
AudioDecoder::~AudioDecoder() = default;

bool AudioDecoder::Open( const std::string& path, int sampleRate )
{
	auto d  = std::make_unique< Impl >();
	d->rate = sampleRate > 0 ? sampleRate : 48000;
	if( avformat_open_input( &d->format, path.c_str(), nullptr, nullptr ) < 0 || avformat_find_stream_info( d->format, nullptr ) < 0 )
		return false;
	const AVCodec* decoder = nullptr;
	d->stream              = av_find_best_stream( d->format, AVMEDIA_TYPE_AUDIO, -1, -1, &decoder, 0 );
	if( d->stream < 0 || !decoder )
		return false;
	AVStream* st = d->format->streams[ d->stream ];
	d->codec     = avcodec_alloc_context3( decoder );
	avcodec_parameters_to_context( d->codec, st->codecpar );
	if( avcodec_open2( d->codec, decoder, nullptr ) < 0 || !d->MakeResampler() )
		return false;
	// Only the sound is read here: the picture has its own decoder.
	for( unsigned i = 0; i < d->format->nb_streams; ++i )
		if( (int)i != d->stream )
			d->format->streams[ i ]->discard = AVDISCARD_ALL;
	d->timeBase = st->time_base;
	if( st->duration != AV_NOPTS_VALUE && st->duration > 0 )
		d->duration = st->duration * av_q2d( st->time_base );
	else if( d->format->duration > 0 )
		d->duration = d->format->duration / (double)AV_TIME_BASE;
	d->packet = av_packet_alloc();
	d->frame  = av_frame_alloc();
	impl      = std::move( d );
	return true;
}

int AudioDecoder::Read( float* out, int frames )
{
	if( !impl )
		return 0;
	int done = 0;
	while( done < frames )
	{
		size_t available = impl->pending.size() / 2 - impl->pendingAt;
		if( available == 0 )
		{
			if( !impl->DecodeMore() )
				break;
			continue;
		}
		size_t n = std::min( available, (size_t)( frames - done ) );
		std::memcpy( out + done * 2, impl->pending.data() + impl->pendingAt * 2, n * 2 * sizeof( float ) );
		impl->pendingAt += n;
		impl->position += n / (double)impl->rate;
		done += (int)n;
	}
	return done;
}

void AudioDecoder::Seek( double seconds )
{
	if( !impl )
		return;
	seconds  = std::max( seconds, 0.0 );
	int64_t ts = (int64_t)( seconds / av_q2d( impl->timeBase ) );
	if( impl->format->streams[ impl->stream ]->start_time != AV_NOPTS_VALUE )
		ts += impl->format->streams[ impl->stream ]->start_time;
	av_seek_frame( impl->format, impl->stream, ts, AVSEEK_FLAG_BACKWARD );
	avcodec_flush_buffers( impl->codec );
	impl->MakeResampler(); // nothing left over from before the jump
	impl->pending.clear();
	impl->pendingAt  = 0;
	impl->ended      = false;
	impl->seekTarget = seconds;
	impl->position   = seconds;
}

double AudioDecoder::Position() const
{
	return impl ? impl->position : 0;
}

double AudioDecoder::Duration() const
{
	return impl ? impl->duration : 0;
}

int AudioDecoder::SampleRate() const
{
	return impl ? impl->rate : 0;
}
#else
struct AudioDecoder::Impl
{
};
AudioDecoder::AudioDecoder() = default;
AudioDecoder::~AudioDecoder() = default;
bool AudioDecoder::Open( const std::string&, int )
{
	return false;
}
int AudioDecoder::Read( float*, int )
{
	return 0;
}
void AudioDecoder::Seek( double ) {}
double AudioDecoder::Position() const
{
	return 0;
}
double AudioDecoder::Duration() const
{
	return 0;
}
int AudioDecoder::SampleRate() const
{
	return 0;
}
#endif
}
