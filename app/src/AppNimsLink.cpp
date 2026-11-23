// NIMS PRO LINK in STLT: Traktor's decks, from the DJ's computer, drive the clips linked to their tracks, the tempo and
// the stem animations. The protocol is in nimslink/; docs/NIMSLINK.md says how it all fits.
#include <algorithm>
#include <cmath>
#include <ctime>
#include <filesystem>
#include "App.h"
#include "Settings.h"
#include "nimslink/Net.h"

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

// Whether `clip` is linked to `track`.
bool Belongs( const Clip& clip, const nimslink::TrackInfo& track )
{
	return clip.Playable() && !clip.track.Empty() && nimslink::SameTrack( clip.track, nimslink::IdOf( track ) );
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

std::string App::LinkDeckName( int source, int deck ) const
{
	std::string letter( 1, (char)( 'A' + deck ) );
	int online = 0;
	for( const nimslink::Source& s : linkSources )
		online += s.online ? 1 : 0;
	if( online <= 1 || source < 0 || source >= (int)linkSources.size() )
		return letter;
	return linkSources[ source ].name + " " + letter;
}

bool App::LinkDeckFor( const Clip& clip, int& source, int& deck ) const
{
	// The loudest deck that has the track loaded (a track can be on two decks).
	float best = -1;
	source = deck = -1;
	for( int s = 0; s < (int)linkSources.size(); ++s )
	{
		const nimslink::Source& src = linkSources[ s ];
		if( !src.online )
			continue;
		for( int d = 0; d < nimslink::kDecks; ++d )
		{
			const nimslink::DeckNow& now = src.decks[ d ];
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

void App::UpdateNimsLink( double dt )
{
	// Nothing is driven unless found so again below; a clip linked to no track shows at its own opacity.
	// Every deck's clips: one playing from a deck not on show is driven too. With NIMS off no clip follows a fader.
	bool drive = prefs.nimslinkEnabled && nimsActive;
	composition.ForEachClip( [ & ]( Clip& clip, int, int, int ) {
		clip.driven = false;
		if( clip.track.Empty() || !drive )
			clip.trackGain = 1;
	} );
	if( !drive )
		for( Layer& layer : composition.layers )
			layer.hand = nimslink::LayerHand(); // NIMS has no layer while it is off
	if( !prefs.nimslinkEnabled )
	{
		if( nimslink.Running() )
			nimslink.Stop();
		linkSources.clear();
		linkLeadDeck = -1;
		std::fill( linkStems, linkStems + 5, 0.0f );
		tempoSync.SetTraktor( false, 0, 0, "" );
		return;
	}
	double now = nimslink::Now();
	if( !nimslink.Running() && now - nimslinkTried > 10 )
	{
		nimslinkTried = now;
		std::string error;
		if( !nimslink.Start( nimslink::HostName(), error ) && nimslinkTried > 0 )
			SetStatus( "NIMS PRO LINK: " + error, true );
	}
	nimslink.SetAddresses( SplitAddresses( prefs.nimslinkAddresses ) );
	// The decks as they are when this frame shows (the picture comes out later: Preferences > Sync > Latency).
	linkSources = nimslink.Sources( now + prefs.syncLatencyMs / 1000.0 );

	// Every track a deck has is remembered, for linking clips to it later without the DJ playing it (Track Links).
	if( !trackLibraryLoaded )
	{
		trackLibraryLoaded = true;
		std::string text;
		if( ReadTextFile( ( std::filesystem::u8path( configDir ) / "traktor-tracks.json" ).u8string(), text ) )
			trackLibrary.Load( text );
	}
	bool remembered = false;
	for( const nimslink::Source& src : linkSources )
		for( int d = 0; src.online && d < nimslink::kDecks; ++d )
		{
			if( !src.hasTrack[ d ] || !src.decks[ d ].present || !src.decks[ d ].state.loaded )
				continue;
			// Once per track a deck loads (and again when its BPM or key change), not every frame.
			const nimslink::TrackInfo& t = src.tracks[ d ];
			std::string seen             = t.title + '\n' + t.artist + '\n' + t.file + '\n' + std::to_string( t.length ) + '\n' + std::to_string( t.bpm ) + '\n' + t.key;
			std::string& last            = linkRemembered[ { src.id, d } ];
			if( seen == last )
				continue;
			last = seen;
			remembered |= trackLibrary.Remember( t, (double)std::time( nullptr ) );
		}
	if( remembered )
		SaveTrackLibrary();

	// The deck that is on: playing, the highest fader, across every transmitter (b2b).
	std::vector< nimslink::DeckNow > all;
	std::vector< std::pair< int, int > > where;
	int current = -1;
	for( int s = 0; s < (int)linkSources.size(); ++s )
		for( int d = 0; d < nimslink::kDecks; ++d )
		{
			if( linkSources[ s ].id == linkLeadSource && d == linkLeadDeck )
				current = (int)all.size();
			all.push_back( linkSources[ s ].decks[ d ] );
			where.emplace_back( s, d );
		}
	int lead = nimslink::LeadingDeck( all.data(), (int)all.size(), current );
	if( lead >= 0 )
	{
		const nimslink::Source& src = linkSources[ where[ lead ].first ];
		const nimslink::DeckNow& d  = all[ lead ];
		linkLeadSource              = src.id;
		linkLeadDeck                = where[ lead ].second;
		for( int k = 0; k < 4; ++k )
			linkStems[ k ] = d.state.stems[ k ];
		linkStems[ 4 ] = d.state.level;
		tempoSync.SetTraktor( prefs.nimslinkTempo && d.bpm > 0, d.bpm, d.beat, "Traktor deck " + LinkDeckName( where[ lead ].first, linkLeadDeck ) );
	}
	else
	{
		linkLeadDeck = -1;
		// The stems fall back to silence rather than freezing where they were.
		for( float& s : linkStems )
			s = std::max( 0.0f, s - (float)dt * 4 );
		tempoSync.SetTraktor( false, 0, 0, "" );
	}

	// Each layer plays the linked clip whose track is on: playing with its fader up, the loudest if several are, from
	// whichever deck of clips it is in. A clip holds where it is when its deck stops, and its opacity follows the
	// channel's fader: the DJ's mix is the visuals' mix. All this while NIMS is on: off, the clips are left alone (the
	// DJ's side is still told which tracks have a clip).
	std::vector< std::vector< nimslink::Visual > > visuals( linkSources.size(), std::vector< nimslink::Visual >( nimslink::kDecks, nimslink::Visual::None ) );
	for( int l = 0; l < (int)composition.layers.size(); ++l )
	{
		Layer& layer   = composition.layers[ l ];
		int best       = -1, bestDeck = -1;
		float bestLoud = 0.05f; // a fader all the way down: the DJ is only cueing in the headphones
		for( int k = 0; k < composition.DeckCount(); ++k )
		{
			std::vector< Clip >* row = k == composition.deck ? &layer.clips : l < (int)composition.decks[ k ].rows.size() ? &composition.decks[ k ].rows[ l ] : nullptr;
			for( int c = 0; row && c < (int)row->size(); ++c )
			{
				Clip& clip = ( *row )[ c ];
				int s, d;
				if( clip.Empty() || !LinkDeckFor( clip, s, d ) )
					continue;
				visuals[ s ][ d ] = std::max( visuals[ s ][ d ], nimslink::Visual::Ready );
				const nimslink::DeckNow& deck = linkSources[ s ].decks[ d ];
				if( !deck.state.playing )
					continue;
				float loud = deck.state.volume;
				if( layer.Plays( k, l, c ) )
					loud += 0.1f; // the one on stays until another is clearly louder
				if( loud > bestLoud )
				{
					bestLoud = loud;
					best     = c;
					bestDeck = k;
				}
			}
		}
		if( !drive )
			continue;
		Clip* bestClip = best >= 0 ? composition.ClipIn( bestDeck, l, best ) : nullptr;
		bool playsBest = bestClip && layer.Plays( bestDeck, l, best );
		if( layer.hand.Step( bestClip ? &bestClip->track : nullptr, playsBest, layer.active >= 0 ) )
		{
			// Launched without taking the selection away from what the VJ is editing, and without counting as the VJ's.
			int row = composition.selectedRow, column = composition.selectedColumn, selected = composition.selectedLayer;
			{
				QuietLink quiet( linkQuiet );
				Launch( l, best, true, l, bestDeck );
			}
			composition.selectedRow    = row;
			composition.selectedColumn = column;
			composition.selectedLayer  = selected;
			linkFollow.erase( l );
		}
		// The clip on the layer (and one fading out) follows its deck.
		for( Clip* clip : { composition.ActiveClip( l ), composition.OutgoingClip( l ) } )
		{
			int s, d;
			if( !clip || !clip->Loaded() || !LinkDeckFor( *clip, s, d ) )
				continue;
			const nimslink::Source& src  = linkSources[ s ];
			const nimslink::DeckNow& deck = src.decks[ d ];
			if( clip == composition.ActiveClip( l ) )
				visuals[ s ][ d ] = nimslink::Visual::OnAir;
			// Smoothly towards the deck's position: small differences are taken up over a few frames, jumps at once.
			LinkFollow& f    = linkFollow[ clip == composition.ActiveClip( l ) ? l : -1 - l ];
			bool playing   = deck.state.playing;
			double rate    = deck.state.rate > 0 ? deck.state.rate : 1.0;
			double target  = deck.position;
			bool fresh     = f.source != src.id || f.deck != d || f.jumps != deck.jumps;
			double shown   = f.shown + ( playing ? rate * dt : 0.0 );
			double error   = target - shown;
			f.shown        = fresh || !playing || std::fabs( error ) > nimslink::DeckTracker::kJumpThreshold ? target : shown + error * 0.25;
			f.source       = src.id;
			f.deck         = d;
			f.jumps        = deck.jumps;
			clip->time          = MovieTime( *clip, f.shown + clip->trackOffset, src.tracks[ d ].length );
			clip->trackGain     = deck.state.volume; // the DJ's fader is the visual's
			clip->driven        = true;
			clip->drivenPlaying = playing && !composition.paused;
			clip->finished      = false;
		}
	}
	for( int s = 0; s < (int)linkSources.size(); ++s )
		if( linkSources[ s ].online )
			nimslink.SetVisuals( linkSources[ s ].id, visuals[ s ].data() );
}

void App::SetNimsActive( bool on )
{
	if( on == nimsActive )
		return;
	nimsActive = on;
	linkFollow.clear(); // on again: every clip goes straight to its track's place
	if( on )
		SetStatus( "NIMS on: the linked clips follow Traktor's tracks" );
	else
		SetStatus( "NIMS off: the layers are yours. Press NIMS to give them back to Traktor" );
}

void App::LinkClipToTrack( Clip& clip, nimslink::TrackId track )
{
	if( clip.Empty() || track.Empty() )
		return;
	// A track has one clip: the one linked before lets it go.
	composition.ForEachClip( [ & ]( Clip& other, int, int, int ) {
		if( &other != &clip && nimslink::SameTrack( other.track, track ) )
		{
			other.track       = nimslink::TrackId();
			other.trackOffset = 0;
		}
	} );
	if( !nimslink::SameTrack( clip.track, track ) )
		clip.trackOffset = 0;
	clip.track = track;
	SetStatus( "Linked " + clip.name + " to " + track.title + ( track.artist.empty() ? "" : " (" + track.artist + ")" ) );
}

void App::UnlinkTrack( nimslink::TrackId track )
{
	int unlinked = 0;
	composition.ForEachClip( [ & ]( Clip& clip, int, int, int ) {
		if( nimslink::SameTrack( clip.track, track ) )
		{
			clip.track       = nimslink::TrackId();
			clip.trackOffset = 0;
			++unlinked;
		}
	} );
	if( unlinked )
		SetStatus( "Unlinked " + track.title + ": it has no clip now" );
}

void App::SaveTrackLibrary()
{
	if( !WriteTextFile( ( std::filesystem::u8path( configDir ) / "traktor-tracks.json" ).u8string(), trackLibrary.Save() ) )
		SetStatus( "Could not save the list of Traktor tracks", true );
}
}
