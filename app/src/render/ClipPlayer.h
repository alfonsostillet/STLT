#pragma once
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include "gl/Gl.h"
#include "media/HapFrame.h"
#include "media/MovFile.h"
#include "render/Fbo.h"

namespace stltvj
{
// Plays one HAP movie into GPU textures.
//
// The pipeline keeps the GL thread free of decoding work:
//   decoder thread : reads the compressed frame (positional read, OS read-ahead) and Snappy-decompresses its chunks
//                    on the shared thread pool straight into a mapped pixel buffer (no extra copy)
//   GL thread      : unmaps that buffer and lets glCompressedTexSubImage2D copy it to the texture by DMA,
//                    then orphans and remaps the buffer for the next frame
// The GPU receives the DXT/BC blocks as they are: the frame is never expanded to RGBA on the CPU.
// A few slots let the decoder run ahead of the playhead in the play direction.
class ClipPlayer
{
public:
	enum class Shading
	{
		Rgb,        // HAP (DXT1), HAP HDR
		Rgba,       // HAP Alpha (DXT5), HAP R (BC7)
		YCoCg,      // HAP Q
		YCoCgAlpha, // HAP Q Alpha: YCoCg colour + RGTC1 alpha
		AlphaOnly,  // HAP Alpha-Only (RGTC1)
	};

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

	// Parses the movie and checks the first frame. Cheap: no GPU memory or thread until Activate().
	bool Open( const std::string& path );
	const std::string& Error() const { return error; }
	const VideoTrack& Video() const { return movie.Video(); }
	const HapLayout& Layout() const { return layout; }
	size_t FrameCount() const { return movie.Video().samples.size(); }

	// GL thread. Allocates the textures and pixel buffers and starts the decoder; Deactivate frees them again.
	bool Activate();
	void Deactivate();
	bool Active() const { return thread.joinable(); }

	// GL thread, once per rendered frame: shows `frame` as soon as it is decoded and decodes ahead
	// in `direction` (+1 forward, -1 backward, 0 paused).
	void Update( int64_t frame, int direction );
	// GL thread: decodes and uploads `frame` right now (thumbnails, seeking while paused).
	bool ShowNow( int64_t frame );

	bool HasFrame() const { return shownFrame >= 0; }
	int64_t ShownFrame() const { return shownFrame; }
	GLuint Texture( int index ) const { return textures[ index ]; }
	Shading GetShading() const { return shading; }
	// The part of the padded texture holding the picture.
	float UMax() const { return layout.paddedWidth ? (float)Video().width / layout.paddedWidth : 1.0f; }
	float VMax() const { return layout.paddedHeight ? (float)Video().height / layout.paddedHeight : 1.0f; }

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
		int64_t frame   = -1;
	};
	static constexpr int kSlots = 4;

	void DecodeLoop();
	bool PickWork( int& slot, int64_t& frame ) const;
	bool InWindow( int64_t frame ) const;
	int64_t Wrap( int64_t frame ) const;
	bool DecodeInto( int64_t frame, uint8_t* destination, std::vector< uint8_t >& compressed );
	void Upload( Slot& slot );
	void Remap( Slot& slot );

	MovFile movie;
	HapLayout layout;
	Shading shading = Shading::Rgb;
	std::string error;

	GLuint textures[ 2 ] = {};
	GLenum internalFormats[ 2 ] = {};
	Slot slots[ kSlots ];
	int64_t shownFrame = -1;
	int64_t lastWanted = -1;
	Fbo thumbnail;

	mutable std::mutex mutex;
	std::condition_variable wake;
	std::thread thread;
	bool stopping      = false;
	int64_t wanted     = 0;
	int wantedDirection = 0;

	std::atomic< float > decodeMs{ 0 };
	float uploadMs = 0;
	std::atomic< uint64_t > decodedCount{ 0 };
	uint64_t lateCount = 0;
};
}
