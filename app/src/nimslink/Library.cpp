#include "nimslink/Library.h"
#include <algorithm>
#include <cmath>
#include <json.hpp>

namespace nimslink
{
bool TrackLibrary::Remember( const TrackInfo& info, double now )
{
	TrackId id = IdOf( info );
	if( id.Empty() )
		return false;
	for( LibraryTrack& t : tracks )
		if( SameTrack( t.id, id ) )
		{
			t.seen       = now;
			bool changed = false;
			// What the stored entry lacked, or the DJ changed (a new grid, a key analysed).
			if( t.id.length <= 0 && id.length > 0 )
				t.id.length = id.length, changed = true;
			if( info.bpm > 0 && std::fabs( info.bpm - t.bpm ) > 0.005 )
				t.bpm = info.bpm, changed = true;
			if( !info.key.empty() && info.key != t.key )
				t.key = info.key, changed = true;
			return changed;
		}
	LibraryTrack t;
	t.id   = id;
	t.bpm  = info.bpm;
	t.key  = info.key;
	t.seen = now;
	tracks.push_back( t );
	return true;
}

void TrackLibrary::Forget( const TrackId& id )
{
	tracks.erase( std::remove_if( tracks.begin(), tracks.end(), [ & ]( const LibraryTrack& t ) { return SameTrack( t.id, id ); } ), tracks.end() );
}

const LibraryTrack* TrackLibrary::Find( const TrackId& id ) const
{
	for( const LibraryTrack& t : tracks )
		if( SameTrack( t.id, id ) )
			return &t;
	return nullptr;
}

std::string TrackLibrary::Save() const
{
	nlohmann::json root;
	root[ "format" ]  = "stlt-traktor-tracks";
	root[ "version" ] = 1;
	nlohmann::json list = nlohmann::json::array();
	for( const LibraryTrack& t : tracks )
		list.push_back( { { "title", t.id.title }, { "artist", t.id.artist }, { "length", t.id.length }, { "bpm", t.bpm }, { "key", t.key }, { "seen", t.seen } } );
	root[ "tracks" ] = list;
	// A tag that is not valid UTF-8 is written with replacement characters rather than stopping the save.
	return root.dump( 1, '\t', false, nlohmann::json::error_handler_t::replace );
}

bool TrackLibrary::Load( const std::string& text )
{
	tracks.clear();
	nlohmann::json root = nlohmann::json::parse( text, nullptr, false );
	if( !root.is_object() || root.value( "format", std::string() ) != "stlt-traktor-tracks" || !root.contains( "tracks" ) || !root[ "tracks" ].is_array() )
		return false;
	auto text_ = []( const nlohmann::json& j, const char* key ) { return j.contains( key ) && j[ key ].is_string() ? j[ key ].get< std::string >() : std::string(); };
	auto number = []( const nlohmann::json& j, const char* key ) { return j.contains( key ) && j[ key ].is_number() ? j[ key ].get< double >() : 0.0; };
	for( const nlohmann::json& j : root[ "tracks" ] )
	{
		if( !j.is_object() )
			continue;
		LibraryTrack t;
		t.id.title  = text_( j, "title" );
		t.id.artist = text_( j, "artist" );
		t.id.length = number( j, "length" );
		t.bpm       = number( j, "bpm" );
		t.key       = text_( j, "key" );
		t.seen      = number( j, "seen" );
		if( !t.id.Empty() && !Find( t.id ) )
			tracks.push_back( t );
	}
	return true;
}
}
