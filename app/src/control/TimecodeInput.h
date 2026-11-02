#pragma once
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include "control/Timecode.h"

namespace stltvj
{
// The timecode coming in from the source chosen in Preferences > Sync: LTC on a sound card's input, MTC on a MIDI
// input, or Art-Net on the network. Only that source is open.
class TimecodeInput
{
public:
	enum class Source
	{
		Off,
		Ltc,
		Mtc,
		ArtNet,
		Count
	};
	static const char* SourceName( Source source );

	TimecodeInput();
	~TimecodeInput();
	TimecodeInput( const TimecodeInput& ) = delete;
	TimecodeInput& operator=( const TimecodeInput& ) = delete;

	// Opens what `source` needs and closes the rest; nothing happens when nothing changed. `ltcDevice` "" is the
	// system's default input; `ltcChannel` counts from 0; `mtcInput` "*" is any MIDI input.
	void Configure( Source source, const std::string& ltcDevice, int ltcChannel, const std::string& mtcInput );
	// The source could not be opened (a port taken, a sound card gone): Configure tries it again.
	bool Failed() const { return !error.empty(); }
	Source Current() const { return source.load(); }
	// From the MIDI threads: quarter frames and SysEx, with the input they came from.
	void OnMidi( const std::string& device, const unsigned char* bytes, size_t size, double time );

	TimecodeReceiver::State At( double now, double freewheel ) const { return receiver.At( now, freewheel ); }
	std::string Status() const;

private:
	void Close();
	std::atomic< Source > source{ Source::Off }; // the MIDI threads read it
	std::string ltcDevice, mtcInput;
	int ltcChannel = 0;
	std::string error;

	TimecodeReceiver receiver;
	LtcDecoder ltc{ receiver };
	MtcDecoder mtc{ receiver };
	std::mutex mtcMutex; // the MIDI threads share the MTC decoder

	struct Audio;
	std::unique_ptr< Audio > audio; // LTC's sound card input
	std::string audioName;
	int channelUsed = 0, channels = 0;

	std::thread artThread; // Art-Net's socket
	std::atomic< bool > artRunning{ false };
	intptr_t artSocket = -1;
};
}
