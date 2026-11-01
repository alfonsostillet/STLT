#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace stltvj
{
// How a decoded frame goes to the GPU: up to three textures packed one after the other in one buffer.
enum class PlaneFormat
{
	Compressed, // DXT/RGTC/BPTC blocks (HAP): `compressedFormat` says which
	R8,         // one 8-bit channel (a Y, U or V plane)
	RG8,        // two 8-bit channels (the interleaved U and V of NV12)
	RGBA8,
};

struct Plane
{
	PlaneFormat format        = PlaneFormat::RGBA8;
	unsigned compressedFormat = 0; // HapTextureFormat value
	int width = 0, height = 0;     // texture size (whole 4x4 blocks for compressed planes)
	size_t bytes  = 0;
	size_t offset = 0;
};

// How the shader turns the planes into colour. Values match the clip shader's uShading.
enum class Shading
{
	Rgb,        // HAP (DXT1), HAP HDR
	Rgba,       // HAP Alpha (DXT5), HAP R (BC7), any RGBA frame
	YCoCg,      // HAP Q
	YCoCgAlpha, // HAP Q Alpha: YCoCg colour + RGTC1 alpha
	AlphaOnly,  // HAP Alpha-Only (RGTC1)
	Yuv,        // Y, U, V planes (H.264 and most other video): converted on the GPU
	Nv12,       // a Y plane and an interleaved UV plane (what hardware decoders give)
	RgbaPremultiplied, // DXV with alpha (DXT5, colour premultiplied)
	DxvYCoCg,          // DXV High Quality: Y (RGTC1) and half-size Co, Cg (RGTC2)
	DxvYCoCgAlpha,     // DXV High Quality with alpha: Y and alpha (RGTC2) and half-size Co, Cg (RGTC2)
};

struct FrameLayout
{
	unsigned planeCount = 0;
	Plane planes[ 3 ];
	size_t totalBytes = 0;
	Shading shading   = Shading::Rgba;
	int width = 0, height = 0; // the picture; textures may be larger (block padding)
	bool bt709     = true;     // YUV matrix: BT.709 (HD) or BT.601 (SD)
	bool fullRange = false;    // YUV levels: 0-255 (JPEG) or 16-235 (video)

	void Add( const Plane& plane );
};

// What a decoder may do while the playhead moves (scratching, cue jumps): give up on a frame nobody wants any
// more, or answer with a nearby frame for now. The exact frame follows once the playhead rests.
struct DecodeRequest
{
	std::function< bool() > abandon;     // true once the frame is no longer wanted
	std::function< bool() > approximate; // true while a nearby frame will do (the playhead is being scratched)
	int64_t shown = -1;                  // the frame on show: an approximate answer must be closer than this
};

enum class DecodeResult
{
	Exact,       // the frame asked for
	Approximate, // a nearby frame (`decoded` says which)
	Abandoned,   // nobody wanted it any more; nothing written
	Failed,
};

// Where a clip's frames come from. HAP files decode by themselves (fast, any frame in any order);
// everything else goes through FFmpeg, which decodes best front to back.
class FrameSource
{
public:
	virtual ~FrameSource() = default;

	virtual const FrameLayout& Layout() const = 0;
	virtual size_t FrameCount() const = 0;
	virtual double Duration() const = 0; // seconds; 0 for a still image
	virtual double FrameRate() const = 0;
	virtual size_t FrameAt( double seconds ) const = 0;
	virtual std::string CodecName() const = 0;
	virtual std::string Details() const { return {}; }
	virtual bool IsHap() const { return false; }
	// Frames go to the GPU as compressed textures and decode in any order (HAP, DXV).
	virtual bool IsGpuTexture() const { return IsHap(); }
	virtual bool IsHardwareDecoded() const { return false; } // by the graphics chip (VideoToolbox, D3D11, VA-API...)
	virtual bool IsStill() const { return false; }

	// Decodes `frame` into `destination` (Layout().totalBytes). Safe to call from several threads,
	// though only HAP decodes in parallel.
	virtual bool Decode( size_t frame, uint8_t* destination ) = 0;
	// As Decode, for the player: may give up or answer with a nearby frame (see DecodeRequest); `decoded` is the
	// frame written.
	virtual DecodeResult DecodeFrame( size_t frame, uint8_t* destination, const DecodeRequest& request, int64_t& decoded )
	{
		(void)request;
		decoded = (int64_t)frame;
		return Decode( frame, destination ) ? DecodeResult::Exact : DecodeResult::Failed;
	}
};

// Opens any supported file: HAP through the built-in decoder, the rest through FFmpeg when the app has it.
std::unique_ptr< FrameSource > OpenFrameSource( const std::string& path, std::string& error );
bool HasFfmpeg();
// Whether files opened from now on may use the computer's hardware video decoder (on by default; the
// STLTVJ_HWDEC=0 environment variable turns it off at start).
void SetHardwareDecoding( bool on );
bool HardwareDecoding();
// Memory each clip that is not HAP may keep decoded frames in (for scratching back over them), in megabytes;
// 0 keeps none. Applies to clips opened from then on and to the next frames of open ones.
void SetFrameCacheMegabytes( int megabytes );
int FrameCacheMegabytes();

// Extensions the browser and drops accept (lower case, with the dot).
bool IsMediaExtension( const std::string& extension );
// Sound files (no picture): for the timeline's audio tracks.
bool IsAudioExtension( const std::string& extension );
}
