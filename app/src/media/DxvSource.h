#pragma once
#include "media/DxvFrame.h"
#include "media/FrameSource.h"
#include "media/MovFile.h"

namespace stltvj
{
// A Resolume DXV 3 movie: like HAP, each frame stands alone and reaches the GPU as compressed textures, so it
// decodes in any order with little CPU (see DxvFrame.h).
class DxvSource : public FrameSource
{
public:
	// Takes a movie already opened whose video track is DXV; false (with `error`) for a kind this does not read.
	bool Open( std::unique_ptr< MovFile > movie, std::string& error );

	const FrameLayout& Layout() const override { return layout; }
	size_t FrameCount() const override { return movie->Video().samples.size(); }
	double Duration() const override { return movie->Video().DurationSeconds(); }
	double FrameRate() const override { return movie->Video().FrameRate(); }
	size_t FrameAt( double seconds ) const override { return movie->Video().FrameAt( seconds ); }
	std::string CodecName() const override;
	std::string Details() const override;
	bool IsGpuTexture() const override { return true; }
	bool Decode( size_t frame, uint8_t* destination ) override;

private:
	std::unique_ptr< MovFile > movie;
	DxvLayout dxv;
	FrameLayout layout;
};
}
