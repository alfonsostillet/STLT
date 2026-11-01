#include "control/Feedback.h"
#include <algorithm>
#include <cctype>

namespace stltvj
{
namespace
{
std::string Lower( std::string s )
{
	std::transform( s.begin(), s.end(), s.begin(), []( unsigned char c ) { return (char)std::tolower( c ); } );
	return s;
}

// APC mini mk2 (Akai's protocol): a pad's note on sets its colour (the velocity, from the 128-colour palette) and
// its brightness or animation (the channel: 0..6 from 10 % to 100 %, 7..10 pulsing, 11..15 blinking).
constexpr unsigned char kApcGreen = 21, kApcBlue = 45;
constexpr int kApcHalf = 2, kApcFull = 6, kApcPulse = 9;
}

bool IsApcMiniMk2( const std::string& device )
{
	return Lower( device ).find( "apc mini mk2" ) != std::string::npos;
}

std::vector< unsigned char > LightMessage( const std::string& device, const MidiKey& key, Light light )
{
	if( key.type != MidiKey::Type::Note )
		return {};
	unsigned char note = (unsigned char)( key.number & 0x7F );
	if( IsApcMiniMk2( device ) )
	{
		if( key.number <= 63 ) // the pads
			switch( light )
			{
			case Light::Off: return { 0x90, note, 0 };
			case Light::Ready: return { (unsigned char)( 0x90 | kApcHalf ), note, kApcBlue };
			case Light::On: return { (unsigned char)( 0x90 | kApcFull ), note, kApcGreen };
			case Light::Waiting: return { (unsigned char)( 0x90 | kApcPulse ), note, kApcGreen };
			}
		// Track (100..107) and scene (112..119) buttons: one colour, off, on or blinking.
		if( ( key.number >= 100 && key.number <= 107 ) || ( key.number >= 112 && key.number <= 119 ) )
			return { 0x90, note, (unsigned char)( light == Light::On ? 1 : light == Light::Waiting ? 2 : 0 ) };
		return {};
	}
	unsigned char status = (unsigned char)( 0x90 | ( key.channel & 0x0F ) );
	return { status, note, (unsigned char)( light == Light::On || light == Light::Waiting ? 127 : 0 ) };
}

std::string MidiOutputNameFor( const std::string& inputName )
{
	if( inputName.rfind( "MIDIIN", 0 ) == 0 )
		return "MIDIOUT" + inputName.substr( 6 );
	return inputName;
}
}
