#include "control/Control.h"
#include <algorithm>
#include <cctype>
#include <cstring>
#include <json.hpp>

namespace stltvj
{
bool ControlTarget::IsButton() const
{
	return kind == Kind::ClipConnect || kind == Kind::ColumnConnect || kind == Kind::LayerClear || kind == Kind::TempoTap || kind == Kind::Resync
	       || kind == Kind::Stop;
}

bool ControlTarget::IsSwitch() const
{
	return kind == Kind::LayerBypass || kind == Kind::LayerSolo || kind == Kind::Pause;
}

namespace
{
std::vector< std::string > Split( const std::string& address )
{
	std::vector< std::string > parts;
	size_t at = 0;
	while( at < address.size() )
	{
		size_t slash = address.find( '/', at );
		if( slash == std::string::npos )
			slash = address.size();
		if( slash > at )
			parts.push_back( address.substr( at, slash - at ) );
		at = slash + 1;
	}
	return parts;
}

// A 1-based number in an address, as 0-based; -1 if it is not one.
int Index( const std::string& text )
{
	if( text.empty() || text.size() > 6 )
		return -1;
	for( char c : text )
		if( !std::isdigit( (unsigned char)c ) )
			return -1;
	int n = std::stoi( text );
	return n >= 1 ? n - 1 : -1;
}
}

std::string AddressName( const std::string& name )
{
	std::string out;
	for( char c : name )
		out += std::isalnum( (unsigned char)c ) ? (char)std::tolower( (unsigned char)c ) : '_';
	return out;
}

std::string EffectParamAddress( const std::string& scope, int slot, const std::string& paramName )
{
	return scope + "/video/effects/" + std::to_string( slot + 1 ) + "/params/" + AddressName( paramName );
}

bool ParseControlAddress( const std::string& address, ControlTarget& t )
{
	using Kind               = ControlTarget::Kind;
	std::vector< std::string > p = Split( address );
	t                        = ControlTarget{};
	if( p.empty() || p[ 0 ] != "composition" )
		return false;
	size_t n = p.size();
	// Effect parameters, from any scope: .../video/effects/S/params/NAME
	if( n >= 6 && p[ n - 5 ] == "video" && p[ n - 4 ] == "effects" && p[ n - 2 ] == "params" )
	{
		t.slot  = Index( p[ n - 3 ] );
		t.param = p[ n - 1 ];
		if( t.slot < 0 )
			return false;
		size_t scope = n - 5; // parts before "video"
		if( scope == 3 && p[ 1 ] == "layers" )
			t.layer = Index( p[ 2 ] );
		else if( scope == 5 && p[ 1 ] == "layers" && p[ 3 ] == "clips" )
		{
			t.layer  = Index( p[ 2 ] );
			t.column = Index( p[ 4 ] );
			if( t.column < 0 )
				return false;
		}
		else if( scope != 1 )
			return false;
		if( scope > 1 && t.layer < 0 )
			return false;
		t.kind = Kind::EffectParam;
		return true;
	}
	if( n == 2 )
	{
		if( p[ 1 ] == "master" )
			t.kind = Kind::Master;
		else if( p[ 1 ] == "speed" )
			t.kind = Kind::Speed;
		else if( p[ 1 ] == "pause" )
			t.kind = Kind::Pause;
		else if( p[ 1 ] == "stop" )
			t.kind = Kind::Stop;
		return t.kind != Kind::None;
	}
	if( n == 3 && p[ 1 ] == "video" && p[ 2 ] == "opacity" )
		return t.kind = Kind::Master, true;
	if( n == 3 && p[ 1 ] == "tempocontroller" )
	{
		if( p[ 2 ] == "tempo" )
			t.kind = Kind::Tempo;
		else if( p[ 2 ] == "tempotap" )
			t.kind = Kind::TempoTap;
		else if( p[ 2 ] == "resync" )
			t.kind = Kind::Resync;
		return t.kind != Kind::None;
	}
	if( n == 4 && p[ 1 ] == "columns" && p[ 3 ] == "connect" )
	{
		t.column = Index( p[ 2 ] );
		t.kind   = Kind::ColumnConnect;
		return t.column >= 0;
	}
	if( n >= 4 && p[ 1 ] == "layers" )
	{
		t.layer = Index( p[ 2 ] );
		if( t.layer < 0 )
			return false;
		if( n == 4 && p[ 3 ] == "clear" )
			t.kind = Kind::LayerClear;
		else if( n == 4 && p[ 3 ] == "bypassed" )
			t.kind = Kind::LayerBypass;
		else if( n == 4 && p[ 3 ] == "solo" )
			t.kind = Kind::LayerSolo;
		else if( n == 5 && p[ 3 ] == "video" && p[ 4 ] == "opacity" )
			t.kind = Kind::LayerOpacity;
		else if( n == 6 && p[ 3 ] == "clips" && p[ 5 ] == "connect" )
		{
			t.column = Index( p[ 4 ] );
			if( t.column < 0 )
				return false;
			t.kind = Kind::ClipConnect;
		}
		return t.kind != Kind::None;
	}
	return false;
}

// ---- OSC

namespace
{
uint32_t ReadBig( const uint8_t* p )
{
	return (uint32_t)p[ 0 ] << 24 | (uint32_t)p[ 1 ] << 16 | (uint32_t)p[ 2 ] << 8 | p[ 3 ];
}

// An OSC string: zero-terminated, padded to four bytes. Moves `at` past it.
bool ReadString( const uint8_t* data, size_t size, size_t& at, std::string& out )
{
	size_t end = at;
	while( end < size && data[ end ] )
		end++;
	if( end >= size )
		return false;
	out.assign( (const char*)data + at, end - at );
	at = ( end + 4 ) & ~(size_t)3;
	return at <= size;
}

bool ParseOscPacket( const uint8_t* data, size_t size, std::vector< OscMessage >& out, int depth )
{
	if( size < 4 || depth > 8 || size % 4 )
		return false;
	if( size >= 16 && std::memcmp( data, "#bundle", 8 ) == 0 )
	{
		size_t at = 16; // "#bundle\0" and the time tag
		while( at + 4 <= size )
		{
			uint32_t length = ReadBig( data + at );
			at += 4;
			if( length > size - at || !ParseOscPacket( data + at, length, out, depth + 1 ) )
				return false;
			at += length;
		}
		return true;
	}
	OscMessage message;
	size_t at = 0;
	if( !ReadString( data, size, at, message.address ) || message.address.empty() || message.address[ 0 ] != '/' )
		return false;
	std::string types;
	if( at < size && !ReadString( data, size, at, types ) )
		return false;
	for( size_t i = 1; i < types.size(); ++i )
	{
		char type = types[ i ];
		if( type == 'f' || type == 'i' )
		{
			if( at + 4 > size )
				return false;
			uint32_t bits = ReadBig( data + at );
			at += 4;
			float value;
			if( type == 'f' )
				std::memcpy( &value, &bits, 4 );
			else
				value = (float)(int32_t)bits;
			message.values.push_back( value );
		}
		else if( type == 'd' || type == 'h' )
		{
			if( at + 8 > size )
				return false;
			uint64_t bits = (uint64_t)ReadBig( data + at ) << 32 | ReadBig( data + at + 4 );
			at += 8;
			double value;
			if( type == 'd' )
				std::memcpy( &value, &bits, 8 );
			else
				value = (double)(int64_t)bits;
			message.values.push_back( (float)value );
		}
		else if( type == 'T' )
			message.values.push_back( 1 );
		else if( type == 'F' )
			message.values.push_back( 0 );
		else if( type == 's' )
		{
			std::string ignored;
			if( !ReadString( data, size, at, ignored ) )
				return false;
		}
		else if( type != 'N' && type != 'I' )
			return false; // blobs and the rest: not for controlling
	}
	out.push_back( std::move( message ) );
	return true;
}
}

bool ParseOsc( const uint8_t* data, size_t size, std::vector< OscMessage >& out )
{
	return ParseOscPacket( data, size, out, 0 );
}

std::vector< uint8_t > MakeOscMessage( const std::string& address, const std::vector< float >& values )
{
	std::vector< uint8_t > out;
	auto string = [ & ]( const std::string& s ) {
		out.insert( out.end(), s.begin(), s.end() );
		do
			out.push_back( 0 );
		while( out.size() % 4 );
	};
	string( address );
	string( "," + std::string( values.size(), 'f' ) );
	for( float v : values )
	{
		uint32_t bits;
		std::memcpy( &bits, &v, 4 );
		for( int shift = 24; shift >= 0; shift -= 8 )
			out.push_back( (uint8_t)( bits >> shift ) );
	}
	return out;
}

// ---- MIDI

bool ReadMidi( const std::vector< unsigned char >& m, MidiKey& key, float& value )
{
	if( m.size() < 3 )
		return false;
	int status  = m[ 0 ] & 0xF0;
	key.channel = m[ 0 ] & 0x0F;
	key.number  = m[ 1 ] & 0x7F;
	if( status == 0x90 || status == 0x80 )
	{
		key.type = MidiKey::Type::Note;
		value    = status == 0x90 ? ( m[ 2 ] & 0x7F ) / 127.0f : 0.0f; // a note on at velocity 0 is a note off
		return true;
	}
	if( status == 0xB0 )
	{
		key.type = MidiKey::Type::Controller;
		value    = ( m[ 2 ] & 0x7F ) / 127.0f;
		return true;
	}
	return false;
}

std::string MidiKeyName( const MidiKey& key )
{
	static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
	std::string channel        = " ch " + std::to_string( key.channel + 1 );
	if( key.type == MidiKey::Type::Controller )
		return "CC " + std::to_string( key.number ) + channel;
	return std::string( names[ key.number % 12 ] ) + std::to_string( key.number / 12 - 2 ) + channel; // middle C (60) is C3
}

// ---- Settings

std::string ControlSettings::Save() const
{
	using nlohmann::json;
	json root;
	root[ "format" ]       = "stlt-vj-control";
	root[ "oscEnabled" ]   = oscEnabled;
	root[ "oscPort" ]      = oscPort;
	root[ "midiEnabled" ]  = midiEnabled;
	root[ "midiInputsOff" ] = midiInputsOff;
	json mappings          = json::array();
	for( const MidiMapping& m : midi )
		mappings.push_back( { { "type", m.key.type == MidiKey::Type::Note ? "note" : "cc" },
		                      { "channel", m.key.channel + 1 },
		                      { "number", m.key.number },
		                      { "address", m.address } } );
	root[ "midi" ] = mappings;
	return root.dump( 1 );
}

bool ControlSettings::Load( const std::string& text )
{
	using nlohmann::json;
	json root = json::parse( text, nullptr, false );
	if( root.is_discarded() || !root.is_object() || root.value( "format", "" ) != "stlt-vj-control" )
		return false;
	ControlSettings s;
	s.oscEnabled  = root.value( "oscEnabled", false );
	s.oscPort     = std::clamp( root.value( "oscPort", 7000 ), 1, 65535 );
	s.midiEnabled = root.value( "midiEnabled", true );
	if( root.contains( "midiInputsOff" ) && root[ "midiInputsOff" ].is_array() )
		for( const json& name : root[ "midiInputsOff" ] )
			if( name.is_string() )
				s.midiInputsOff.push_back( name.get< std::string >() );
	if( root.contains( "midi" ) && root[ "midi" ].is_array() )
		for( const json& m : root[ "midi" ] )
		{
			if( !m.is_object() || !m.contains( "address" ) )
				continue;
			MidiMapping mapping;
			mapping.key.type    = m.value( "type", "note" ) == "cc" ? MidiKey::Type::Controller : MidiKey::Type::Note;
			mapping.key.channel = std::clamp( m.value( "channel", 1 ), 1, 16 ) - 1;
			mapping.key.number  = std::clamp( m.value( "number", 0 ), 0, 127 );
			mapping.address     = m.value( "address", "" );
			s.midi.push_back( mapping );
		}
	*this = std::move( s );
	return true;
}
}
