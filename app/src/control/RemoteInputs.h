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
class RtMidiOut;

namespace stltvj
{
#ifdef _WIN32
constexpr bool kRtMidiNumbersPorts = true; // RtMidi's Windows (WinMM) port names end in the port's number
#else
constexpr bool kRtMidiNumbersPorts = false;
#endif
// A MIDI input's name as the user knows it: the number RtMidi adds on Windows ("APC mini mk2 0") changes with
// the USB socket and makes one device listed twice look like two, so it goes.
std::string MidiDeviceName( const std::string& rtmidiName, bool numbered = kRtMidiNumbersPorts );

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
	// Sends to the output that belongs to an input in use (MidiOutputNameFor): the controllers' lights. False when
	// it has none.
	bool SendMidi( const std::string& inputDevice, const std::vector< unsigned char >& message );
	std::vector< std::string > OpenInputs() const; // the MIDI inputs in use
	// Changes whenever an output opens: lights sent before did not reach it.
	int OutputsVersion() const { return outputsVersion; }

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
	std::map< std::string, std::unique_ptr< RtMidiOut > > midiOuts; // their outputs, by input name
	int outputsVersion = 0;
	void ConfigureOutputs();
	std::vector< std::string > devices;
	std::string midiError;
	bool midiUnavailable = false;
};
}
