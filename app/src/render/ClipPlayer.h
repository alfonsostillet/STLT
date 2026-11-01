#pragma once
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include "gl/Gl.h"
#include "media/FrameSource.h"
#include "render/Fbo.h"

namespace stltvj
{
// Plays one movie or image into GPU textures.
//
// The pipeline keeps the GL thread free of decoding work:
//   decoder thread : decodes the frame straight into a mapped pixel buffer (no extra copy). For HAP that is a
//                    positional read plus Snappy decompression of the chunks on the shared thread pool; for other
//                    formats FFmpeg decodes and the Y, U and V planes are copied as they are
//   GL thread      : unmaps that buffer and lets glCompressedTexSubImage2D / glTexSubImage2D copy it to the
//                    textures by DMA, then orphans and remaps the buffer for the next frame
// HAP reaches the GPU as DXT/BC blocks and YUV as planes: neither is expanded to RGBA on the CPU.
// A few slots let the decoders run ahead of the playhead in the play direction. HAP gets two decoder threads
// working on different frames, so one slow read (a cold disk, a busy drive) does not stall the next frame.
// While the playhead jumps around (scratching) the decoder drops frames the playhead has left, and the nearest
// frame it has shows until the exact one is ready (see DecodeRequest).
class ClipPlayer
{
public:
	struct Stats
	{
		float decodeMs   = 0; // read + decompress, smoothed
		float uploadMs   = 0; // CPU time spent issuing the upload, smoothed
		uint64_t decoded = 0;
		uint64_t late    = 0; // frames that were not ready when their time came
	};

	ClipPlayer() = default;
	~ClipPlayer();
	ClipPlayer( const ClipPlayer& ) = delete;
	ClipPlayer& operator=( const ClipPlayer& ) = delete;

	// Opens the file and checks the first frame. Cheap: no GPU memory or thread until Activate().
	bool Open( const std::string& path );
	const std::string& Error() const { return error; }
	const FrameSource& Source() const { return *source; }
	const FrameLayout& Layout() const { return layout; }
	size_t FrameCount() const { return source ? source->FrameCount() : 0; }
	int Width() const { return layout.width; }
	int Height() const { return layout.height; }

	// GL thread. Allocates the textures and pixel buffers and starts the decoder; Deactivate frees them again.
	bool Activate();
	void Deactivate();
	bool Active() const { return !threads.empty(); }

	// GL thread, once per rendered frame: shows `frame` as soon as it is decoded and decodes ahead
	// in `direction` (+1 forward, -1 backward, 0 paused).
	void Update( int64_t frame, int direction );
	// GL thread: decodes and uploads `frame` right now (thumbnails, seeking while paused).
	bool ShowNow( int64_t frame );

	bool HasFrame() const { return shownFrame >= 0; }
	int64_t ShownFrame() const { return shownFrame; }
	GLuint Texture( int index ) const { return textures[ index ]; }
	Shading GetShading() const { return layout.shading; }
	// The part of the (block-padded) texture holding the picture.
	float UMax() const { return layout.planes[ 0 ].width ? (float)layout.width / layout.planes[ 0 ].width : 1.0f; }
	float VMax() const { return layout.planes[ 0 ].height ? (float)layout.height / layout.planes[ 0 ].height : 1.0f; }

	Stats GetStats() const;
	Fbo& Thumbnail() { return thumbnail; }

private:
	enum class SlotState
	{
		Free,     // mapped and waiting for the decoder
		Decoding, // the decoder is writing into it
		Ready,    // holds a decoded frame
		Failed,   // the frame could not be decoded
	};
	struct Slot
	{
		GLuint buffer   = 0;
		uint8_t* mapped = nullptr;
		SlotState state = SlotState::Free;
		int64_t frame   = -1; // the frame asked for
		int64_t content = -1; // the frame decoded into it (another, nearby one when approximate)
		bool exact      = true;
	};
	static constexpr int kSlots = 6;

	void DecodeLoop();
	bool PickWork( int& slot, int64_t& frame ) const;
	bool InWindow( int64_t frame ) const;
	bool StillWanted( int64_t frame ) const; // InWindow without the lock, for the decoder while it works
	bool Scratching() const;
	int64_t Wrap( int64_t frame ) const;
	bool DecodeInto( int64_t frame, uint8_t* destination );
	void Upload( Slot& slot );
	void Remap( Slot& slot );

	std::unique_ptr< FrameSource > source;
	FrameLayout layout;
	std::string error;

	GLuint textures[ 3 ] = {};
	GLenum internalFormats[ 3 ] = {};
	Slot slots[ kSlots ];
	int64_t shownFrame = -1;
	int64_t lastWanted = -1;
	Fbo thumbnail;

	mutable std::mutex mutex;
	std::condition_variable wake;
	std::vector< std::thread > threads;
	bool stopping      = false;
	int64_t wanted     = 0;
	int wantedDirection = 0;
	bool shownExact    = true;
	std::atomic< int64_t > wantedNow{ 0 };     // wanted and wantedDirection, for StillWanted
	std::atomic< int > directionNow{ 0 };
	std::atomic< double > lastJump{ -1e9 };    // seconds (steady clock) the playhead last jumped
	std::atomic< double > jumpBefore{ -1e9 };  // and the jump before that

	std::atomic< float > decodeMs{ 0 };
	float uploadMs = 0;
	std::atomic< uint64_t > decodedCount{ 0 };
	uint64_t lateCount = 0;
};
}
