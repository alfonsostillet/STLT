#include "ClipPlayer.h"
#include <hap.h>
#include <algorithm>
#include <chrono>
#include <climits>
#include <cstdlib>
#include <vector>

namespace stltvj
{
namespace
{
using Clock = std::chrono::steady_clock;

double Seconds()
{
	return std::chrono::duration< double >( Clock::now().time_since_epoch() ).count();
}

// Jumps this close together are a scratch (one alone, a cue point or a launch, is not); it lasts this long after
// the last of them.
constexpr double kScratchSeconds = 0.25;

float Smooth( float average, float sample )
{
	return average == 0 ? sample : average * 0.9f + sample * 0.1f;
}

GLenum GlFormat( unsigned hapFormat )
{
	return hapFormat == HapTextureFormat_YCoCg_DXT5 ? GL_COMPRESSED_RGBA_S3TC_DXT5_EXT : (GLenum)hapFormat;
}
}

ClipPlayer::~ClipPlayer()
{
	Deactivate();
}

bool ClipPlayer::Open( const std::string& path )
{
	Deactivate();
	error.clear();
	source = OpenFrameSource( path, error );
	if( !source )
		return false;
	layout = source->Layout();
	for( unsigned i = 0; i < layout.planeCount; ++i )
	{
		unsigned f = layout.planes[ i ].compressedFormat;
		bool bptc  = f == HapTextureFormat_RGBA_BPTC_UNORM || f == HapTextureFormat_RGB_BPTC_UNSIGNED_FLOAT || f == HapTextureFormat_RGB_BPTC_SIGNED_FLOAT;
		if( layout.planes[ i ].format == PlaneFormat::Compressed && bptc && !Caps().bptc )
		{
			error = source->CodecName() + " needs BPTC texture support, which this GPU driver lacks";
			source.reset();
			return false;
		}
	}
	return true;
}

bool ClipPlayer::Activate()
{
	if( Active() )
		return true;
	if( !source || !layout.planeCount )
		return false;

	glPixelStorei( GL_UNPACK_ALIGNMENT, 1 );
	// Compressed textures are created from zeros: one buffer for every player, grown when needed, so a launch
	// allocates nothing.
	static std::vector< uint8_t > zeros;
	for( unsigned i = 0; i < layout.planeCount; ++i )
		if( zeros.size() < layout.planes[ i ].bytes )
			zeros.assign( layout.planes[ i ].bytes, 0 );
	for( unsigned i = 0; i < layout.planeCount; ++i )
	{
		const Plane& plane = layout.planes[ i ];
		glGenTextures( 1, &textures[ i ] );
		glBindTexture( GL_TEXTURE_2D, textures[ i ] );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
		switch( plane.format )
		{
		case PlaneFormat::Compressed:
			internalFormats[ i ] = GlFormat( plane.compressedFormat );
			glCompressedTexImage2D( GL_TEXTURE_2D, 0, internalFormats[ i ], plane.width, plane.height, 0, (GLsizei)plane.bytes, zeros.data() );
			break;
		case PlaneFormat::R8:
			internalFormats[ i ] = GL_R8;
			glTexImage2D( GL_TEXTURE_2D, 0, GL_R8, plane.width, plane.height, 0, GL_RED, GL_UNSIGNED_BYTE, nullptr );
			break;
		case PlaneFormat::RG8:
			internalFormats[ i ] = GL_RG8;
			glTexImage2D( GL_TEXTURE_2D, 0, GL_RG8, plane.width, plane.height, 0, GL_RG, GL_UNSIGNED_BYTE, nullptr );
			break;
		case PlaneFormat::RGBA8:
			internalFormats[ i ] = GL_RGBA8;
			glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA8, plane.width, plane.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr );
			break;
		}
	}
	glBindTexture( GL_TEXTURE_2D, 0 );
	glPixelStorei( GL_UNPACK_ALIGNMENT, 4 );

	glBindBuffer( GL_PIXEL_UNPACK_BUFFER, 0 );
	for( Slot& slot : slots )
	{
		glGenBuffers( 1, &slot.buffer );
		slot.state = SlotState::Free;
		slot.frame = -1;
		Remap( slot );
	}
	shownFrame = -1;
	shownExact = true;
	lastWanted = -1;
	stopping   = false;
	lastJump   = -1e9;
	jumpBefore = -1e9;
	int decoders = source->IsGpuTexture() ? 2 : 1; // other formats decode one frame at a time
	for( int i = 0; i < decoders; ++i )
		threads.emplace_back( [ this ] { DecodeLoop(); } );
	return true;
}

void ClipPlayer::Deactivate()
{
	if( !Active() )
		return;
	{
		std::lock_guard< std::mutex > lock( mutex );
		stopping = true;
	}
	wake.notify_all();
	for( std::thread& t : threads )
		t.join();
	threads.clear();
	source->ReleaseMemory();

	for( Slot& slot : slots )
	{
		if( slot.mapped )
		{
			glBindBuffer( GL_PIXEL_UNPACK_BUFFER, slot.buffer );
			glUnmapBuffer( GL_PIXEL_UNPACK_BUFFER );
		}
		glDeleteBuffers( 1, &slot.buffer );
		slot = Slot{};
	}
	glBindBuffer( GL_PIXEL_UNPACK_BUFFER, 0 );
	glDeleteTextures( 3, textures );
	textures[ 0 ] = textures[ 1 ] = textures[ 2 ] = 0;
	shownFrame                    = -1;
}

// Gives the slot a fresh mapped buffer. Orphaning (glBufferData with no data) means the driver never
// waits for an upload still reading the previous storage.
void ClipPlayer::Remap( Slot& slot )
{
	GLsizeiptr size = (GLsizeiptr)layout.totalBytes;
	glBindBuffer( GL_PIXEL_UNPACK_BUFFER, slot.buffer );
	glBufferData( GL_PIXEL_UNPACK_BUFFER, size, nullptr, GL_STREAM_DRAW );
	slot.mapped = (uint8_t*)glMapBufferRange( GL_PIXEL_UNPACK_BUFFER, 0, size, GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_BUFFER_BIT );
	glBindBuffer( GL_PIXEL_UNPACK_BUFFER, 0 );
}

void ClipPlayer::Upload( Slot& slot )
{
	Clock::time_point start = Clock::now();
	glBindBuffer( GL_PIXEL_UNPACK_BUFFER, slot.buffer );
	glUnmapBuffer( GL_PIXEL_UNPACK_BUFFER );
	slot.mapped = nullptr;
	glPixelStorei( GL_UNPACK_ALIGNMENT, 1 );
	for( unsigned i = 0; i < layout.planeCount; ++i )
	{
		const Plane& plane = layout.planes[ i ];
		const void* offset = (const void*)plane.offset;
		glBindTexture( GL_TEXTURE_2D, textures[ i ] );
		if( plane.format == PlaneFormat::Compressed )
			glCompressedTexSubImage2D( GL_TEXTURE_2D, 0, 0, 0, plane.width, plane.height, internalFormats[ i ], (GLsizei)plane.bytes, offset );
		else
			glTexSubImage2D( GL_TEXTURE_2D, 0, 0, 0, plane.width, plane.height,
			                 plane.format == PlaneFormat::R8 ? GL_RED : plane.format == PlaneFormat::RG8 ? GL_RG : GL_RGBA, GL_UNSIGNED_BYTE, offset );
	}
	glBindTexture( GL_TEXTURE_2D, 0 );
	glPixelStorei( GL_UNPACK_ALIGNMENT, 4 );
	glBindBuffer( GL_PIXEL_UNPACK_BUFFER, 0 );
	Remap( slot );
	uploadMs = Smooth( uploadMs, std::chrono::duration< float, std::milli >( Clock::now() - start ).count() );
}

int64_t ClipPlayer::Wrap( int64_t frame ) const
{
	int64_t count = (int64_t)FrameCount();
	if( count <= 0 )
		return 0;
	frame %= count;
	return frame < 0 ? frame + count : frame;
}

bool ClipPlayer::StillWanted( int64_t frame ) const
{
	int64_t w = wantedNow.load();
	int d     = directionNow.load();
	int span  = d == 0 ? 1 : kSlots;
	for( int k = 0; k < span; ++k )
		if( Wrap( w + k * d ) == frame )
			return true;
	return false;
}

bool ClipPlayer::Scratching() const
{
	double last = lastJump.load();
	return Seconds() - last < kScratchSeconds && last - jumpBefore.load() < kScratchSeconds;
}

bool ClipPlayer::InWindow( int64_t frame ) const
{
	int span = wantedDirection == 0 ? 1 : kSlots;
	for( int k = 0; k < span; ++k )
		if( Wrap( wanted + k * wantedDirection ) == frame )
			return true;
	return false;
}

// The nearest frame ahead of the playhead that nobody has decoded yet, and a free slot for it.
bool ClipPlayer::PickWork( int& slotIndex, int64_t& frame ) const
{
	slotIndex = -1;
	for( int i = 0; i < kSlots; ++i )
		if( slots[ i ].state == SlotState::Free && slots[ i ].mapped )
		{
			slotIndex = i;
			break;
		}
	if( slotIndex < 0 )
		return false;
	int span = wantedDirection == 0 ? 1 : kSlots;
	for( int k = 0; k < span; ++k )
	{
		int64_t candidate = Wrap( wanted + k * wantedDirection );
		if( candidate == shownFrame && shownExact )
			continue;
		bool taken = false;
		for( const Slot& s : slots )
			if( s.state != SlotState::Free && s.frame == candidate )
				taken = true;
		if( !taken )
		{
			frame = candidate;
			return true;
		}
	}
	return false;
}

bool ClipPlayer::DecodeInto( int64_t frame, uint8_t* destination )
{
	return source->Decode( (size_t)frame, destination );
}

void ClipPlayer::DecodeLoop()
{
	std::unique_lock< std::mutex > lock( mutex );
	for( ;; )
	{
		int slotIndex = -1;
		int64_t frame = 0;
		wake.wait( lock, [ & ] { return stopping || PickWork( slotIndex, frame ); } );
		if( stopping )
			return;
		Slot& slot           = slots[ slotIndex ];
		slot.state           = SlotState::Decoding;
		slot.frame           = frame;
		uint8_t* destination = slot.mapped;
		DecodeRequest request;
		request.abandon     = [ this, frame ] { return !StillWanted( frame ); };
		request.approximate = [ this ] { return Scratching(); };
		request.shown       = shownFrame;
		lock.unlock();

		Clock::time_point start = Clock::now();
		int64_t content         = frame;
		DecodeResult result     = source->DecodeFrame( (size_t)frame, destination, request, content );
		if( result != DecodeResult::Abandoned )
		{
			decodeMs.store( Smooth( decodeMs.load(), std::chrono::duration< float, std::milli >( Clock::now() - start ).count() ) );
			decodedCount++;
		}

		lock.lock();
		switch( result )
		{
		case DecodeResult::Exact:
		case DecodeResult::Approximate:
			slot.state   = SlotState::Ready;
			slot.content = content;
			slot.exact   = result == DecodeResult::Exact;
			break;
		case DecodeResult::Abandoned:
			slot.state = SlotState::Free;
			slot.frame = -1;
			break;
		case DecodeResult::Failed: slot.state = SlotState::Failed; break;
		}
	}
}

void ClipPlayer::Update( int64_t frame, int direction )
{
	if( !Active() )
		return;
	frame         = Wrap( frame );
	int dir       = direction > 0 ? 1 : direction < 0 ? -1 : 0;
	// A jump: anything but staying put or moving on in the play direction by up to a quarter of a second (playing
	// fast, or with few frames drawn, moves on by several frames at a time).
	if( lastWanted >= 0 && frame != lastWanted )
	{
		int64_t count  = (int64_t)FrameCount();
		int64_t step   = dir == 0 || count <= 0 ? 0 : ( ( frame - lastWanted ) * dir % count + count ) % count;
		int64_t onward = std::max< int64_t >( 3, (int64_t)( source->FrameRate() / 4 ) );
		if( !( step >= 1 && step <= onward ) )
		{
			jumpBefore = lastJump.load();
			lastJump   = Seconds();
		}
	}
	bool scratching = Scratching();
	Slot* show      = nullptr;
	{
		std::lock_guard< std::mutex > lock( mutex );
		wanted          = frame;
		wantedDirection = dir;
		wantedNow       = frame;
		directionNow    = dir;
		// The exact frame; else, while scratching, the ready frame nearest the playhead if it is nearer than
		// what is on show.
		for( Slot& s : slots )
			if( s.state == SlotState::Ready && s.frame == frame && s.exact && s.content == frame )
				show = &s;
		if( !show && scratching )
		{
			int64_t best = shownFrame < 0 ? INT64_MAX : std::llabs( shownFrame - frame );
			for( Slot& s : slots )
				if( s.state == SlotState::Ready && std::llabs( s.content - frame ) < best )
				{
					best = std::llabs( s.content - frame );
					show = &s;
				}
		}
		// Done frames nobody will show: outside the window, already on show, or approximate ones overtaken.
		for( Slot& s : slots )
		{
			if( &s == show )
				continue;
			bool done = s.state == SlotState::Ready || s.state == SlotState::Failed;
			if( done && ( !InWindow( s.frame ) || ( s.content == shownFrame && s.exact == shownExact ) || !s.exact ) )
			{
				s.state = SlotState::Free;
				s.frame = -1;
			}
		}
	}
	if( show && !( show->content == shownFrame && show->exact == shownExact ) )
	{
		// The decoder never touches a Ready slot, so the upload runs without the lock.
		Upload( *show );
		std::lock_guard< std::mutex > lock( mutex );
		shownFrame  = show->content;
		shownExact  = show->exact && show->content == frame;
		show->state = SlotState::Free;
		show->frame = -1;
	}
	else
	{
		if( show )
		{
			std::lock_guard< std::mutex > lock( mutex );
			show->state = SlotState::Free;
			show->frame = -1;
		}
		if( frame != shownFrame && frame != lastWanted && shownFrame >= 0 )
			lateCount++;
	}
	lastWanted = frame;
	wake.notify_all();
}

bool ClipPlayer::ShowNow( int64_t frame )
{
	if( !Active() )
		return false;
	frame      = Wrap( frame );
	Slot* slot = nullptr;
	{
		std::lock_guard< std::mutex > lock( mutex );
		for( Slot& s : slots )
			if( s.state == SlotState::Free && s.mapped )
			{
				slot        = &s;
				s.state     = SlotState::Decoding;
				s.frame     = frame;
				break;
			}
	}
	if( !slot )
		return false;
	bool ok = DecodeInto( frame, slot->mapped );
	if( ok )
		Upload( *slot );
	{
		std::lock_guard< std::mutex > lock( mutex );
		if( ok )
		{
			shownFrame = frame;
			shownExact = true;
		}
		slot->state = SlotState::Free;
		slot->frame = -1;
	}
	wake.notify_one();
	return ok;
}

ClipPlayer::Stats ClipPlayer::GetStats() const
{
	Stats stats;
	stats.decodeMs = decodeMs.load();
	stats.uploadMs = uploadMs;
	stats.decoded  = decodedCount.load();
	stats.late     = lateCount;
	return stats;
}
}
