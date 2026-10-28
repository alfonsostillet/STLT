#pragma once
#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include "control/Control.h"

class RtMidiIn;

namespace stltvj
{
// The live control inputs: an OSC server on a UDP port and every MIDI input, each delivering on its own thread.
// The main loop collects what arrived with Poll().
class RemoteInputs
{
public:
	struct Event
	{
		bool midi = false;
		// OSC
		std::string address;
		std::vector< float > values;
		// MIDI
		MidiKey key;
		float value = 0;
		std::string device;
	};

	RemoteInputs();
	~RemoteInputs();
	RemoteInputs( const RemoteInputs& ) = delete;
	RemoteInputs& operator=( const RemoteInputs& ) = delete;

	// Opens or closes the OSC port and the MIDI inputs to match `settings`; also picks up MIDI devices plugged in
	// or out since the last call (call it now and then). When the system's MIDI service could not be reached it
	// is only tried again on `retry` (the user changed something), as each failed try prints a message.
	void Configure( const ControlSettings& settings, bool retry = false );
	std::vector< Event > Poll();

	std::vector< std::string > MidiDevices() const { return devices; } // every input there is, on or off
	std::string OscStatus() const;
	std::string MidiStatus() const { return midiError; }

private:
	void StartOsc( int port );
	void StopOsc();
	void Push( Event event );
	static void OnMidi( double, std::vector< unsigned char >* message, void* user );

	std::mutex mutex;
	std::vector< Event > queue;

	std::thread oscThread;
	std::atomic< bool > oscRunning{ false };
	intptr_t oscSocket = -1;
	int oscPort        = 0;
	std::string oscError;

	struct MidiPort
	{
		RemoteInputs* owner;
		std::string name;
		std::unique_ptr< RtMidiIn > in;
		~MidiPort(); // where RtMidiIn is known
	};
	std::map< std::string, std::unique_ptr< MidiPort > > midiPorts; // open inputs by name
	std::vector< std::string > devices;
	std::string midiError;
	bool midiUnavailable = false;
};
}
