#pragma once
#include "media/FrameSource.h"
#include "media/HapFrame.h"
#include "media/MovFile.h"

namespace stltvj
{
// A HAP movie: frames are read and decompressed straight into the GPU-ready layout, in any order,
// with the chunks of a frame decompressed in parallel on the shared thread pool.
class HapSource : public FrameSource
{
public:
	// Takes a movie already opened whose video track is HAP.
	bool Open( std::unique_ptr< MovFile > movie, std::string& error );

	const FrameLayout& Layout() const override { return layout; }
	size_t FrameCount() const override { return movie->Video().samples.size(); }
	double Duration() const override { return movie->Video().DurationSeconds(); }
	double FrameRate() const override { return movie->Video().FrameRate(); }
	size_t FrameAt( double seconds ) const override { return movie->Video().FrameAt( seconds ); }
	std::string CodecName() const override;
	std::string Details() const override;
	bool IsHap() const override { return true; }
	bool Decode( size_t frame, uint8_t* destination ) override;

private:
	std::unique_ptr< MovFile > movie;
	HapLayout hap;
	FrameLayout layout;
};
}
