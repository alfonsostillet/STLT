// VJ LINK in STLT: Traktor's decks, from the DJ's computer, drive the clips linked to their tracks, the tempo and
// the stem animations. The protocol is in vjlink/; docs/VJLINK.md says how it all fits.
#include <algorithm>
#include <cmath>
#include "App.h"
#include "vjlink/Net.h"

namespace stltvj
{
namespace
{
std::vector< std::string > SplitAddresses( const std::string& text )
{
	std::vector< std::string > list;
	std::string item;
	for( char c : text + "," )
	{
		if( c == ',' || c == ';' || c == ' ' || c == '\n' )
		{
			if( !item.empty() )
				list.push_back( item );
			item.clear();
		}
		else
			item += c;
	}
	return list;
}

// Whether `clip` belongs to `track`: linked to it, or (not linked to anything) named after it.
bool Belongs( const Clip& clip, const vjlink::TrackInfo& track )
{
	if( !clip.Playable() )
		return false;
	if( !clip.track.Empty() )
		return vjlink::SameTrack( clip.track, vjlink::IdOf( track ) );
	return vjlink::NameMatches( clip.name.empty() ? clip.path : clip.name, track );
}

// Where in the movie a track position falls. A movie at least half as long as the track follows it (and holds its
// last frame past its end); a shorter one is a loop that goes round with the track (or bounces, or plays once, as
// its play mode says).
double MovieTime( const Clip& clip, double position, double trackLength )
{
	double start = clip.RangeStart(), end = clip.RangeEnd(), length = end - start;
	if( length <= 0 )
		return start;
	double t    = std::max( 0.0, position );
	bool follow = trackLength <= 0 || length >= trackLength * 0.5;
	if( follow || clip.playMode == PlayMode::Once || clip.playMode == PlayMode::OnceClear )
		return std::min( start + t, end - 1e-3 );
	if( clip.playMode == PlayMode::Bounce )
	{
		double f = std::fmod( t, 2 * length );
		return start + ( f < length ? f : 2 * length - f );
	}
	return start + std::fmod( t, length );
}
}

std::string App::VjDeckName( int source, int deck ) const
{
	std::string letter( 1, (char)( 'A' + deck ) );
	int online = 0;
	for( const vjlink::Source& s : vjSources )
		online += s.online ? 1 : 0;
	if( online <= 1 || source < 0 || source >= (int)vjSources.size() )
		return letter;
	return vjSources[ source ].name + " " + letter;
}

bool App::VjDeckFor( const Clip& clip, int& source, int& deck ) const
{
	// The loudest deck that has the track loaded (a track can be on two decks).
	float best = -1;
	source = deck = -1;
	for( int s = 0; s < (int)vjSources.size(); ++s )
	{
		const vjlink::Source& src = vjSources[ s ];
		if( !src.online )
			continue;
		for( int d = 0; d < vjlink::kDecks; ++d )
		{
			const vjlink::DeckNow& now = src.decks[ d ];
			if( !now.present || !now.state.loaded || !src.hasTrack[ d ] || !Belongs( clip, src.tracks[ d ] ) )
				continue;
			float loud = now.state.volume + ( now.state.playing ? 2.0f : 0.0f );
			if( loud > best )
			{
				best   = loud;
				source = s;
				deck   = d;
			}
		}
	}
	return source >= 0;
}

void App::UpdateVjLink( double dt )
{
	// Nothing is driven unless found so again below.
	for( Layer& layer : composition.layers )
		for( Clip& clip : layer.clips )
			clip.driven = false;
	if( !prefs.vjlinkEnabled )
	{
		if( vjlink.Running() )
			vjlink.Stop();
		vjSources.clear();
		vjLeadDeck = -1;
		std::fill( vjStems, vjStems + 5, 0.0f );
		tempoSync.SetTraktor( false, 0, 0, "" );
		return;
	}
	double now = vjlink::Now();
	if( !vjlink.Running() && now - vjlinkTried > 10 )
	{
		vjlinkTried = now;
		std::string error;
		if( !vjlink.Start( vjlink::HostName(), error ) && vjlinkTried > 0 )
			SetStatus( "VJ LINK: " + error, true );
	}
	vjlink.SetAddresses( SplitAddresses( prefs.vjlinkAddresses ) );
	// The decks as they are when this frame shows (the picture comes out later: Preferences > Sync > Latency).
	vjSources = vjlink.Sources( now + prefs.syncLatencyMs / 1000.0 );

	// The deck that is on: playing, the highest fader, across every transmitter (b2b).
	std::vector< vjlink::DeckNow > all;
	std::vector< std::pair< int, int > > where;
	int current = -1;
	for( int s = 0; s < (int)vjSources.size(); ++s )
		for( int d = 0; d < vjlink::kDecks; ++d )
		{
			if( vjSources[ s ].id == vjLeadSource && d == vjLeadDeck )
				current = (int)all.size();
			all.push_back( vjSources[ s ].decks[ d ] );
			where.emplace_back( s, d );
		}
	int lead = vjlink::LeadingDeck( all.data(), (int)all.size(), current );
	if( lead >= 0 )
	{
		const vjlink::Source& src = vjSources[ where[ lead ].first ];
		const vjlink::DeckNow& d  = all[ lead ];
		vjLeadSource              = src.id;
		vjLeadDeck                = where[ lead ].second;
		for( int k = 0; k < 4; ++k )
			vjStems[ k ] = d.state.stems[ k ];
		vjStems[ 4 ] = d.state.level;
		tempoSync.SetTraktor( prefs.vjlinkTempo && d.bpm > 0, d.bpm, d.beat, "Traktor deck " + VjDeckName( where[ lead ].first, vjLeadDeck ) );
	}
	else
	{
		vjLeadDeck = -1;
		// The stems fall back to silence rather than freezing where they were.
		for( float& s : vjStems )
			s = std::max( 0.0f, s - (float)dt * 4 );
		tempoSync.SetTraktor( false, 0, 0, "" );
	}

	// Each layer plays the linked clip whose track is on: playing with its fader up, the loudest if several are
	// (the DJ's mix becomes the layer's transition); a clip holds where it is when its deck stops.
	std::vector< std::vector< vjlink::Visual > > visuals( vjSources.size(), std::vector< vjlink::Visual >( vjlink::kDecks, vjlink::Visual::None ) );
	for( int l = 0; l < (int)composition.layers.size(); ++l )
	{
		Layer& layer = composition.layers[ l ];
		int best       = -1;
		float bestLoud = 0.05f; // a fader all the way down: the DJ is only cueing in the headphones
		for( int c = 0; c < (int)layer.clips.size(); ++c )
		{
			Clip& clip = layer.clips[ c ];
			int s, d;
			if( clip.Empty() || !VjDeckFor( clip, s, d ) )
				continue;
			visuals[ s ][ d ] = std::max( visuals[ s ][ d ], vjlink::Visual::Ready );
			const vjlink::DeckNow& deck = vjSources[ s ].decks[ d ];
			if( !deck.state.playing )
				continue;
			float loud = deck.state.volume;
			if( layer.Plays( composition.deck, l, c ) )
				loud += 0.1f; // the one on stays until another is clearly louder
			if( loud > bestLoud )
			{
				bestLoud = loud;
				best     = c;
			}
		}
		if( best >= 0 && !layer.Plays( composition.deck, l, best ) )
		{
			// Launched without taking the selection away from what the VJ is editing.
			int row = composition.selectedRow, column = composition.selectedColumn, selected = composition.selectedLayer;
			Launch( l, best, true, l );
			composition.selectedRow    = row;
			composition.selectedColumn = column;
			composition.selectedLayer  = selected;
			vjFollow.erase( l );
		}
		// The clip on the layer (and one fading out) follows its deck.
		for( Clip* clip : { composition.ActiveClip( l ), composition.OutgoingClip( l ) } )
		{
			int s, d;
			if( !clip || !clip->Loaded() || !VjDeckFor( *clip, s, d ) )
				continue;
			const vjlink::Source& src  = vjSources[ s ];
			const vjlink::DeckNow& deck = src.decks[ d ];
			if( clip == composition.ActiveClip( l ) )
				visuals[ s ][ d ] = vjlink::Visual::OnAir;
			// Smoothly towards the deck's position: small differences are taken up over a few frames, jumps at once.
			VjFollow& f    = vjFollow[ clip == composition.ActiveClip( l ) ? l : -1 - l ];
			bool playing   = deck.state.playing;
			double rate    = deck.state.rate > 0 ? deck.state.rate : 1.0;
			double target  = deck.position;
			bool fresh     = f.source != src.id || f.deck != d || f.jumps != deck.jumps;
			double shown   = f.shown + ( playing ? rate * dt : 0.0 );
			double error   = target - shown;
			f.shown        = fresh || !playing || std::fabs( error ) > vjlink::DeckTracker::kJumpThreshold ? target : shown + error * 0.25;
			f.source       = src.id;
			f.deck         = d;
			f.jumps        = deck.jumps;
			clip->time          = MovieTime( *clip, f.shown, src.tracks[ d ].length );
			clip->driven        = true;
			clip->drivenPlaying = playing && !composition.paused;
			clip->finished      = false;
		}
	}
	for( int s = 0; s < (int)vjSources.size(); ++s )
		if( vjSources[ s ].online )
			vjlink.SetVisuals( vjSources[ s ].id, visuals[ s ].data() );
}
}
