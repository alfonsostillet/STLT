// Tests for a clip's range (in and out points), cue points, BPM sync, beat animations, layer transitions and how
// they are saved. Clips are opened without a GPU: ClipPlayer::Open only reads the file.
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>
#include "Composition.h"
#include "render/FrameClock.h"

using namespace stltvj;

static int failures = 0;

#define CHECK( condition )                                                                           \
	do                                                                                               \
	{                                                                                                \
		if( !( condition ) )                                                                         \
		{                                                                                            \
			std::fprintf( stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #condition ); \
			++failures;                                                                              \
		}                                                                                            \
	} while( 0 )

static bool Near( double a, double b, double tolerance = 1e-6 )
{
	return std::fabs( a - b ) <= tolerance;
}

// chunked_hapq.mov: 24 frames at 24 fps, one second.
static bool OpenClip( Clip& clip )
{
	clip.path   = std::string( FIXTURES_DIR ) + "/chunked_hapq.mov";
	clip.player = std::make_unique< ClipPlayer >();
	return clip.player->Open( clip.path ) && clip.Loaded() && Near( clip.Duration(), 1.0, 1e-3 );
}

static void TestRange()
{
	Clip clip;
	CHECK( OpenClip( clip ) );
	CHECK( Near( clip.RangeStart(), 0 ) && Near( clip.RangeEnd(), clip.Duration() ) );

	clip.inPoint  = 0.25;
	clip.outPoint = 0.75;
	CHECK( Near( clip.RangeStart(), 0.25 ) && Near( clip.RangeEnd(), 0.75 ) );
	clip.Restart();
	CHECK( Near( clip.time, 0.25 ) );

	// Loop: wraps inside the range.
	clip.Advance( 0.6, 120 );
	CHECK( Near( clip.time, 0.35 ) );
	// A playhead left outside the range (the in point moved past it) comes back in.
	clip.time = 0.05;
	clip.Advance( 0.0, 120 );
	CHECK( clip.time >= 0.25 && clip.time <= 0.75 );

	// Bounce: turns at the out point.
	clip.playMode = PlayMode::Bounce;
	clip.Restart();
	clip.Advance( 0.6, 120 );
	CHECK( Near( clip.time, 0.65 ) && clip.PlayDirection() == -1 );

	// Once: stops at the out point.
	clip.playMode = PlayMode::Once;
	clip.Restart();
	clip.Advance( 1.0, 120 );
	CHECK( Near( clip.time, 0.75 ) && clip.finished && clip.PlayDirection() == 0 );

	// Backwards starts from the out point.
	clip.playMode  = PlayMode::Loop;
	clip.direction = -1;
	clip.Restart();
	CHECK( Near( clip.time, 0.75 ) );
	clip.direction = 1;

	// An out point before the in point still leaves a playable range.
	clip.outPoint = 0.1;
	CHECK( clip.RangeEnd() > clip.RangeStart() );
}

static void TestCuesAndSync()
{
	Clip clip;
	CHECK( OpenClip( clip ) );
	clip.inPoint  = 0.25;
	clip.outPoint = 0.75;
	clip.JumpTo( 0.6 );
	CHECK( Near( clip.time, 0.6 ) && !clip.finished );
	clip.JumpTo( 0.1 ); // before the range: its start
	CHECK( Near( clip.time, 0.25 ) );

	// BPM sync: half a second of range over 2 beats at 120 BPM (one second) plays at half speed.
	clip.speed     = 3.0f; // ignored while synced
	clip.syncBeats = 2;
	CHECK( Near( clip.Rate( 120 ), 0.5 ) );
	CHECK( Near( clip.Rate( 240 ), 1.0 ) );
	clip.Restart();
	clip.Advance( 0.5, 120 );
	CHECK( Near( clip.time, 0.5 ) );
	// Synced, it plays even at speed 0.
	clip.speed = 0;
	CHECK( clip.PlayDirection() == 1 );
	clip.syncBeats = 0;
	CHECK( clip.PlayDirection() == 0 );
}

static void TestAnimationsAndTransitions()
{
	Clip clip;
	clip.opacity  = 0.8f;
	clip.rotation = 10;
	CHECK( Near( clip.Shown( ClipProperty::Opacity, 1.0, 1920, 1080 ), 0.8 ) );
	Animation& a = clip.animations[ (int)ClipProperty::Opacity ];
	a.mode       = AnimMode::Saw;
	a.beats      = 4;
	CHECK( Near( clip.Shown( ClipProperty::Opacity, 1.0, 1920, 1080 ), 0.25, 1e-5 ) );
	// Rotation over -180..180: from 0.5 to 1 of the range is 0 to 180 degrees.
	Animation& r = clip.animations[ (int)ClipProperty::Rotation ];
	r.mode       = AnimMode::Saw;
	r.beats      = 2;
	r.from       = 0.5f;
	r.to         = 1.0f;
	CHECK( Near( clip.Shown( ClipProperty::Rotation, 1.0, 1920, 1080 ), 90, 1e-3 ) );
	float lo, hi;
	Clip::PropertyRange( ClipProperty::PositionX, 1920, 1080, lo, hi );
	CHECK( lo == -960 && hi == 960 );

	Layer layer;
	layer.opacity = 0.5f;
	CHECK( Near( layer.ShownOpacity( 3 ), 0.5 ) );
	layer.opacityAnimation.mode  = AnimMode::Square;
	layer.opacityAnimation.beats = 1;
	CHECK( Near( layer.ShownOpacity( 0.25 ), 0 ) && Near( layer.ShownOpacity( 0.75 ), 1 ) );
	CHECK( layer.TransitionSeconds( 120 ) == 0 );
	layer.transitionDuration = 2;
	CHECK( Near( layer.TransitionSeconds( 120 ), 2 ) );
	layer.transitionInBeats = true;
	CHECK( Near( layer.TransitionSeconds( 120 ), 1 ) );
}

static void TestSaving()
{
	Composition c = Composition::MakeDefault();
	c.quantize    = LaunchQuantize::Bar;
	Layer& layer  = c.layers[ 1 ];
	layer.transition         = TransitionMode::WipeUp;
	layer.transitionDuration = 4;
	layer.transitionInBeats  = true;
	layer.opacityAnimation.mode = AnimMode::Sine;
	Clip& clip    = layer.clips[ 2 ];
	clip.path     = "movie.mov";
	clip.inPoint  = 1.5;
	clip.outPoint = 3.25;
	clip.cues[ 0 ] = 2.0;
	clip.cues[ 7 ] = 0.5;
	clip.syncBeats = 8;
	clip.animations[ (int)ClipProperty::Scale ].mode  = AnimMode::PingPong;
	clip.animations[ (int)ClipProperty::Scale ].beats = 2;
	clip.animations[ (int)ClipProperty::Scale ].to    = 0.75f;

	Composition loaded;
	std::string error;
	CHECK( LoadComposition( SaveComposition( c ), loaded, error ) );
	CHECK( loaded.quantize == LaunchQuantize::Bar );
	const Layer& l = loaded.layers[ 1 ];
	CHECK( l.transition == TransitionMode::WipeUp && l.transitionDuration == 4 && l.transitionInBeats );
	CHECK( l.opacityAnimation.mode == AnimMode::Sine );
	const Clip& k = l.clips[ 2 ];
	CHECK( Near( k.inPoint, 1.5 ) && Near( k.outPoint, 3.25 ) );
	CHECK( Near( k.cues[ 0 ], 2.0 ) && Near( k.cues[ 7 ], 0.5 ) && k.cues[ 3 ] < 0 );
	CHECK( k.syncBeats == 8 );
	const Animation& s = k.animations[ (int)ClipProperty::Scale ];
	CHECK( s.mode == AnimMode::PingPong && s.beats == 2 && s.to == 0.75f );
	CHECK( k.animations[ (int)ClipProperty::Opacity ].mode == AnimMode::Off );

	// A clip saved before any of this keeps the defaults.
	const Clip& plain = loaded.layers[ 0 ].clips[ 0 ];
	CHECK( plain.inPoint == 0 && plain.outPoint < 0 && plain.cues[ 0 ] < 0 && plain.syncBeats == 0 );
	CHECK( loaded.layers[ 0 ].transitionDuration == 0 );
}

static void TestLayersAndRows()
{
	Composition c = Composition::MakeDefault();
	// A clip of row 0 playing on layer 2 (a layer based launch).
	c.layers[ 0 ].clips[ 3 ].path = "a.mov";
	c.layers[ 2 ].active    = 3;
	c.layers[ 2 ].activeRow = 0;
	CHECK( c.ActiveClip( 2 ) == &c.layers[ 0 ].clips[ 3 ] );
	CHECK( c.ActiveClip( 0 ) == nullptr && c.OutgoingClip( 2 ) == nullptr );
	CHECK( c.LayerShowing( 0, 3 ) == 2 && c.LayerShowing( 2, 3 ) == -1 );
	c.layers[ 1 ].previous    = 4;
	c.layers[ 1 ].previousRow = 1;
	CHECK( c.OutgoingClip( 1 ) == &c.layers[ 1 ].clips[ 4 ] && c.LayerShowing( 1, 4 ) == 1 );
	// A row that no longer exists stops it.
	c.layers[ 2 ].activeRow = 7;
	c.Normalise();
	CHECK( c.layers[ 2 ].active == -1 && c.ActiveClip( 2 ) == nullptr );

	// The crossfader: each group fades over the far half; both in full in the middle.
	Layer layer;
	CHECK( layer.CrossfadeGain( 0 ) == 1 && layer.CrossfadeGain( 1 ) == 1 );
	layer.crossfader = 1;
	CHECK( layer.CrossfadeGain( 0 ) == 1 && layer.CrossfadeGain( 0.5f ) == 1 && Near( layer.CrossfadeGain( 0.75f ), 0.5 ) && layer.CrossfadeGain( 1 ) == 0 );
	layer.crossfader = 2;
	CHECK( layer.CrossfadeGain( 0 ) == 0 && Near( layer.CrossfadeGain( 0.25f ), 0.5 ) && layer.CrossfadeGain( 0.5f ) == 1 );

	// Saved and loaded.
	c.layers[ 1 ].master     = 0.5f;
	c.layers[ 1 ].speed      = 2;
	c.layers[ 1 ].crossfader = 2;
	c.crossfader             = 0.25f;
	c.target                 = ClipTarget::SelectedLayer;
	c.layers[ 0 ].clips[ 3 ].playMode = PlayMode::OnceClear;
	Composition loaded;
	std::string error;
	CHECK( LoadComposition( SaveComposition( c ), loaded, error ) );
	CHECK( loaded.layers[ 1 ].master == 0.5f && loaded.layers[ 1 ].speed == 2 && loaded.layers[ 1 ].crossfader == 2 );
	CHECK( loaded.crossfader == 0.25f && loaded.target == ClipTarget::SelectedLayer );
	CHECK( loaded.layers[ 0 ].clips[ 3 ].playMode == PlayMode::OnceClear );
	// Older compositions said "Play Once".
	std::string old = SaveComposition( c );
	size_t at       = old.find( "Play Once & Clear" );
	CHECK( at != std::string::npos );
	old.replace( at, std::string( "Play Once & Clear" ).size(), "Play Once" );
	CHECK( LoadComposition( old, loaded, error ) && loaded.layers[ 0 ].clips[ 3 ].playMode == PlayMode::Once );
}

// Deterministic noise in [-1, 1].
static double Noise( unsigned& seed )
{
	seed = seed * 1664525u + 1013904223u;
	return ( seed >> 8 ) / double( 1u << 24 ) * 2 - 1;
}

static void TestFrameClock()
{
	// A 59.94 Hz display whose vsync times reach the program with +-1.5 ms of jitter, missing one vsync now and
	// then; the clock starts out believing 60 Hz.
	const double period = 1.0 / 59.94;
	FrameClock clock( 1.0 / 60 );
	unsigned seed = 7;
	double shown = 0, real = 0, previous = 0;
	int uneven = 0, frames = 3000;
	for( int n = 1; n <= frames; ++n )
	{
		real += period * ( n % 97 == 0 ? 2 : 1 );
		double stamp = real + 0.0015 * Noise( seed );
		double step  = clock.Step( stamp - previous );
		previous     = stamp;
		shown += step;
		// Once learnt, steps are whole periods (apart from the rare payback of what the clock drifted).
		double periods = step / clock.Period();
		if( n > 300 && std::fabs( periods - std::round( periods ) ) > 1e-3 )
			uneven++;
	}
	CHECK( clock.Locked() );
	CHECK( std::fabs( clock.Period() - period ) < period * 0.002 );
	CHECK( std::fabs( shown - real ) < period ); // never drifts
	CHECK( uneven < frames / 100 );

	// A clock that is not a display's: nothing to lock to, the measurements go through.
	FrameClock free( 1.0 / 60 );
	CHECK( free.Step( 0.0021 ) == 0.0021 && !free.Locked() );
}

static void TestEvenFrames()
{
	// A 24 fps clip on a 48 Hz display with jittery vsync times: every frame shows for exactly two refreshes.
	Clip clip;
	CHECK( OpenClip( clip ) );
	const double period = 1.0 / 48;
	FrameClock clock( period );
	unsigned seed = 11;
	double real = 0, previous = 0;
	std::vector< int64_t > frames;
	clip.Restart();
	for( int n = 1; n <= 200; ++n )
	{
		real += period;
		double stamp = real + 0.002 * Noise( seed );
		double step  = clock.Step( stamp - previous );
		previous     = stamp;
		clip.Advance( step, 120 );
		frames.push_back( clip.FrameFor( step, 120 ) );
	}
	int run = 0, wrong = 0, runs = 0;
	for( size_t i = 1; i < frames.size(); ++i )
	{
		run++;
		if( frames[ i ] != frames[ i - 1 ] )
		{
			if( runs++ > 0 && run != 2 ) // the first run starts mid-way
				wrong++;
			run = 0;
		}
	}
	CHECK( runs > 50 && wrong == 0 );
	if( wrong )
	{
		for( int64_t f : frames )
			std::fprintf( stderr, "%lld ", (long long)f );
		std::fprintf( stderr, "\n" );
	}
}

// Decks: pages of clips over the same layers; a clip plays on while another deck is shown; saving keeps them all,
// and an undo snapshot does not depend on the deck shown.
static void TestDecks()
{
	Composition c = Composition::MakeDefault();
	c.layers[ 0 ].clips[ 1 ].path = "a.mov";
	c.AddDeck( "Second" );
	CHECK( c.DeckCount() == 2 && c.deck == 0 );
	// Playing deck 1's clip at row 0, column 1.
	c.layers[ 0 ].active     = 1;
	c.layers[ 0 ].activeRow  = 0;
	c.layers[ 0 ].activeDeck = 0;
	Clip* playing            = c.ActiveClip( 0 );
	CHECK( playing && playing->path == "a.mov" );
	std::string before = SaveComposition( c, false );
	c.ShowDeck( 1 );
	CHECK( c.deck == 1 && c.ColumnCount() == 9 && c.layers[ 0 ].clips[ 1 ].Empty() );
	// Still playing, from where it was in memory; not shown in this deck's cells.
	CHECK( c.ActiveClip( 0 ) == playing && c.LayerShowing( 0, 1 ) < 0 && c.LayerShowingIn( 0, 0, 1 ) == 0 );
	CHECK( SaveComposition( c, false ) == before );
	c.layers[ 1 ].clips[ 2 ].path = "b.mov";
	c.AddLayer(); // a row in every deck
	CHECK( c.decks[ 0 ].rows.size() == c.layers.size() && c.decks[ 0 ].rows.back().size() == 9 );
	int count = 0;
	c.ForEachClip( [ & ]( Clip& clip, int, int, int ) { count += clip.Empty() ? 0 : 1; } );
	CHECK( count == 2 );

	// Saving and loading: every deck, the one shown included.
	std::string text = SaveComposition( c );
	Composition loaded;
	std::string error;
	CHECK( LoadComposition( text, loaded, error ) );
	CHECK( loaded.DeckCount() == 2 && loaded.deck == 1 && loaded.decks[ 1 ].name == "Second" );
	CHECK( loaded.layers[ 1 ].clips[ 2 ].path == "b.mov" && loaded.ClipIn( 0, 0, 1 ) && loaded.ClipIn( 0, 0, 1 )->path == "a.mov" );
	CHECK( loaded.layers.size() == 4 );

	// Removing the deck a clip plays from stops it; the other deck's numbers move down.
	c.layers[ 1 ].active     = 2;
	c.layers[ 1 ].activeRow  = 1;
	c.layers[ 1 ].activeDeck = 1;
	c.RemoveDeck( 0 );
	CHECK( c.DeckCount() == 1 && c.deck == 0 && c.layers[ 0 ].active < 0 );
	CHECK( c.layers[ 1 ].activeDeck == 0 && c.ActiveClip( 1 ) && c.ActiveClip( 1 )->path == "b.mov" );

	// Trigger style and ignore column are saved with the clip.
	Composition t = Composition::MakeDefault();
	t.layers[ 0 ].clips[ 0 ].path         = "p.mov";
	t.layers[ 0 ].clips[ 0 ].trigger      = TriggerStyle::Piano;
	t.layers[ 0 ].clips[ 0 ].ignoreColumn = true;
	t.layers[ 1 ].clips[ 0 ].path         = "t.mov";
	t.layers[ 1 ].clips[ 0 ].trigger      = TriggerStyle::Toggle;
	Composition tl;
	CHECK( LoadComposition( SaveComposition( t ), tl, error ) );
	CHECK( tl.layers[ 0 ].clips[ 0 ].trigger == TriggerStyle::Piano && tl.layers[ 0 ].clips[ 0 ].ignoreColumn );
	CHECK( tl.layers[ 1 ].clips[ 0 ].trigger == TriggerStyle::Toggle && !tl.layers[ 1 ].clips[ 0 ].ignoreColumn );

	// A composition from before decks: its clips become deck 1.
	const char* old = R"({"format":"stlt-vj-composition","columns":["A","B"],"layers":[{"name":"L","clips":[null,{"path":"x.mov"}]}]})";
	Composition legacy;
	CHECK( LoadComposition( old, legacy, error ) );
	CHECK( legacy.DeckCount() == 1 && legacy.ColumnCount() == 2 && legacy.layers[ 0 ].clips[ 1 ].path == "x.mov" );
}

// The timeline: blocks end as asked (on, loop, hold, black), cues start them, clips overwrite what they cover,
// split in two, map timeline time to media time, and everything is saved.
static void TestTimeline()
{
	Timeline t;
	t.bpm = 120; // 0.5 s a beat, 2 s a bar
	t.tracks.resize( 1 );
	TimelineClip a;
	a.clip.path = "a.mov";
	a.start     = 0;
	a.length    = 10;
	t.Place( 0, std::move( a ) );
	t.blocks.push_back( { "One", 0, 4, BlockEnd::Next, 0 } );
	t.blocks.push_back( { "Two", 4, 6, BlockEnd::Loop, 1 } );
	t.blocks.push_back( { "Three", 6, 8, BlockEnd::Hold, 2 } );
	t.blocks.push_back( { "Four", 8, 10, BlockEnd::Black, 3 } );
	CHECK( Near( t.Length(), 10 ) && t.BlockAt( 4.5 ) == 1 && t.BlockAt( 11 ) == -1 );

	// Next: plays on into block Two, which loops.
	t.Cue( 0 );
	CHECK( t.playing && Near( t.position, 0 ) );
	t.Advance( 4.5 );
	CHECK( t.playing && Near( t.position, 4.5 ) );
	t.Advance( 2.0 ); // 6.5 would leave Two: back to 4.5
	CHECK( t.playing && Near( t.position, 4.5 ) );
	// Three holds at its end; Four ends in black.
	t.Cue( 2 );
	t.Advance( 3 );
	CHECK( !t.playing && !t.black && t.position < 8 && t.position > 7.99 );
	t.Cue( 3 );
	CHECK( t.playing && !t.black && Near( t.position, 8 ) );
	t.Advance( 2.5 );
	CHECK( !t.playing && t.black );
	t.Locate( 1 );
	CHECK( !t.black && Near( t.position, 1 ) );

	// Overwrite: a clip in the middle of another cuts it in two; one over a head moves it on.
	TimelineClip b;
	b.clip.path = "b.mov";
	b.start     = 3;
	b.length    = 2;
	t.Place( 0, std::move( b ) );
	std::vector< TimelineClip >& clips = t.tracks[ 0 ].clips;
	CHECK( clips.size() == 3 && clips[ 0 ].clip.path == "a.mov" && Near( clips[ 0 ].End(), 3 ) );
	CHECK( clips[ 1 ].clip.path == "b.mov" && clips[ 2 ].clip.path == "a.mov" && Near( clips[ 2 ].start, 5 ) && Near( clips[ 2 ].clip.inPoint, 5 ) );
	CHECK( t.tracks[ 0 ].ClipAt( 4 ) == 1 && t.tracks[ 0 ].ClipAt( 5.5 ) == 2 && t.tracks[ 0 ].ClipAt( 12 ) == -1 );
	CHECK( t.Split( 0, 2, 7 ) && clips.size() == 4 && Near( clips[ 3 ].start, 7 ) && Near( clips[ 3 ].clip.inPoint, 7 ) );
	CHECK( !t.Split( 0, 0, 5 ) );

	// Media time: from the in point at the clip's speed (no player here: no wrapping).
	TimelineClip m;
	m.start         = 10;
	m.clip.inPoint  = 2;
	m.clip.speed    = 2;
	CHECK( Near( m.MediaTime( 11 ), 4 ) && Near( m.MediaTime( 9 ), 2 ) );

	CHECK( t.Timecode( 3661.5 ) == "01:01:01:12" && t.BarsBeats( 0 ) == "1.1" && t.BarsBeats( 2.6 ) == "2.2" );

	// Saved with the composition, blocks sorted.
	Composition c = Composition::MakeDefault();
	c.timeline    = std::move( t );
	c.timeline.tracks.push_back( TimelineTrack{} );
	c.timeline.tracks[ 1 ].audio = true;
	std::swap( c.timeline.blocks[ 0 ], c.timeline.blocks[ 3 ] );
	Composition loaded;
	std::string error;
	CHECK( LoadComposition( SaveComposition( c ), loaded, error ) );
	const Timeline& l = loaded.timeline;
	CHECK( l.tracks.size() == 2 && l.tracks[ 0 ].clips.size() == 4 && l.tracks[ 1 ].audio );
	CHECK( l.blocks.size() == 4 && l.blocks[ 0 ].name == "One" && l.blocks[ 3 ].endAction == BlockEnd::Black );
	CHECK( Near( l.tracks[ 0 ].clips[ 2 ].clip.inPoint, 5 ) && Near( l.tracks[ 0 ].clips[ 1 ].length, 2 ) );
	CHECK( !l.playing && Near( l.position, 0 ) );
}

// Keyframes: straight lines between them, held before the first and after the last; they stay put in time when
// a clip is split, trimmed by an overwrite or saved.
static void TestKeyframes()
{
	TimelineClip c;
	c.clip.path = "k.mov";
	c.start     = 10;
	c.length    = 10;
	c.clip.opacity = 0.7f;
	CHECK( Near( c.Evaluate( KeyProperty::Opacity, 3 ), 0.7 ) && !c.Animated( KeyProperty::Opacity ) );
	c.SetKey( KeyProperty::Opacity, 2, 0.0f );
	c.SetKey( KeyProperty::Opacity, 6, 1.0f );
	c.SetKey( KeyProperty::Opacity, 4, 0.2f ); // in the middle, kept in order
	CHECK( c.keys[ 0 ].size() == 3 && Near( c.keys[ 0 ][ 1 ].time, 4 ) );
	CHECK( Near( c.Evaluate( KeyProperty::Opacity, 0 ), 0 ) && Near( c.Evaluate( KeyProperty::Opacity, 3 ), 0.1, 1e-5 ) );
	CHECK( Near( c.Evaluate( KeyProperty::Opacity, 5 ), 0.6, 1e-5 ) && Near( c.Evaluate( KeyProperty::Opacity, 9 ), 1 ) );
	c.SetKey( KeyProperty::Opacity, 4.0005, 0.5f ); // replaces the one there
	CHECK( c.keys[ 0 ].size() == 3 && Near( c.keys[ 0 ][ 1 ].value, 0.5, 1e-6 ) );
	c.SetKey( KeyProperty::Scale, 0, 1.0f );
	c.SetKey( KeyProperty::Scale, 10, 2.0f );
	c.Apply( 15 ); // timeline time: 5 s into the clip
	CHECK( Near( c.clip.scale, 1.5, 1e-5 ) && Near( c.clip.opacity, 0.75, 1e-5 ) && Near( c.clip.rotation, 0 ) );
	CHECK( c.RemoveKey( KeyProperty::Opacity, 4 ) && !c.RemoveKey( KeyProperty::Opacity, 4 ) && c.keys[ 0 ].size() == 2 );

	// Split: the tail's keyframes are the same moments in time.
	Timeline t;
	t.tracks.resize( 1 );
	t.Place( 0, std::move( c ) );
	CHECK( t.Split( 0, 0, 14 ) );
	TimelineClip& head = t.tracks[ 0 ].clips[ 0 ];
	TimelineClip& tail = t.tracks[ 0 ].clips[ 1 ];
	CHECK( Near( tail.start, 14 ) && tail.keys[ (int)KeyProperty::Scale ].size() == 2 && Near( tail.keys[ (int)KeyProperty::Scale ][ 1 ].time, 6 ) );
	CHECK( Near( tail.Evaluate( KeyProperty::Scale, 1 ), head.Evaluate( KeyProperty::Scale, 5 ), 1e-5 ) );
	// An overwrite over the tail's start: what is left starts later and its keyframes stay put in time.
	TimelineClip over;
	over.clip.path = "o.mov";
	over.start     = 13;
	over.length    = 3;
	over.blend     = BlendMode::Add;
	t.Place( 0, std::move( over ) );
	TimelineClip& rest = t.tracks[ 0 ].clips.back();
	CHECK( Near( rest.start, 16 ) && Near( rest.Evaluate( KeyProperty::Scale, 0 ), 1.6, 1e-5 ) );

	// Saved.
	Composition comp = Composition::MakeDefault();
	comp.timeline    = std::move( t );
	Composition loaded;
	std::string error;
	CHECK( LoadComposition( SaveComposition( comp ), loaded, error ) );
	const std::vector< TimelineClip >& clips = loaded.timeline.tracks[ 0 ].clips;
	CHECK( clips.size() == 3 && clips[ 1 ].blend == BlendMode::Add && clips[ 0 ].keys[ 0 ].size() == 2 );
	CHECK( Near( clips[ 2 ].Evaluate( KeyProperty::Scale, 0 ), 1.6, 1e-5 ) );
}

// Keyframe curves, moving and removing keyframes at a moment, effect parameter keyframes; all saved.
static void TestKeyCurves()
{
	CHECK( Near( CurveAt( KeyCurve::Linear, 0.25f ), 0.25 ) && Near( CurveAt( KeyCurve::EaseIn, 0.5f ), 0.125 ) );
	CHECK( Near( CurveAt( KeyCurve::EaseOut, 0.5f ), 0.875 ) && Near( CurveAt( KeyCurve::EaseInOut, 0.5f ), 0.5 ) && Near( CurveAt( KeyCurve::Hold, 0.9f ), 0 ) );
	CHECK( CurveAt( KeyCurve::EaseInOut, 0.25f ) < 0.25f && CurveAt( KeyCurve::EaseInOut, 0.75f ) > 0.75f );
	TimelineClip c;
	c.clip.path = "c.mov";
	c.length    = 10;
	c.SetKey( KeyProperty::Opacity, 0, 0.0f );
	c.SetKey( KeyProperty::Opacity, 4, 1.0f );
	c.SetKey( KeyProperty::Scale, 4, 2.0f );
	c.SetCurveAt( 0, KeyCurve::Hold );
	CHECK( Near( c.Evaluate( KeyProperty::Opacity, 3.9 ), 0 ) && Near( c.Evaluate( KeyProperty::Opacity, 4 ), 1 ) );
	c.SetCurveAt( 0, KeyCurve::EaseIn );
	CHECK( Near( c.Evaluate( KeyProperty::Opacity, 2 ), 0.125, 1e-5 ) );
	ParamKeys* pk = c.FindParamKeys( 1, "amount", true );
	SetKeyIn( pk->keys, 4, 0.3f );
	SetKeyIn( pk->keys, 8, 0.9f );
	CHECK( c.FindParamKeys( 1, "amount" ) && !c.FindParamKeys( 0, "amount" ) && Near( EvaluateKeys( pk->keys, 6, 0 ), 0.6, 1e-5 ) );
	std::vector< double > times = c.KeyTimes();
	CHECK( times.size() == 3 && Near( times[ 0 ], 0 ) && Near( times[ 1 ], 4 ) && Near( times[ 2 ], 8 ) );
	// Every keyframe at 4 (opacity, scale, the parameter) moves to 5; then all at 5 go.
	c.MoveKeysAt( 4, 5 );
	CHECK( Near( c.keys[ (int)KeyProperty::Opacity ][ 1 ].time, 5 ) && Near( c.keys[ (int)KeyProperty::Scale ][ 0 ].time, 5 ) );
	CHECK( Near( c.paramKeys[ 0 ].keys[ 0 ].time, 5 ) && c.KeyTimes().size() == 3 );
	c.ShiftKeys( 1 );
	CHECK( Near( c.paramKeys[ 0 ].keys[ 1 ].time, 7 ) );
	Composition comp = Composition::MakeDefault();
	comp.timeline.tracks.resize( 1 );
	comp.timeline.Place( 0, std::move( c ) );
	Composition loaded;
	std::string error;
	CHECK( LoadComposition( SaveComposition( comp ), loaded, error ) );
	const TimelineClip& l = loaded.timeline.tracks[ 0 ].clips[ 0 ];
	CHECK( l.keys[ (int)KeyProperty::Opacity ][ 0 ].curve == KeyCurve::EaseIn && l.paramKeys.size() == 1 && l.paramKeys[ 0 ].slot == 1 );
	CHECK( l.paramKeys[ 0 ].param == "amount" && l.paramKeys[ 0 ].keys.size() == 2 && Near( l.paramKeys[ 0 ].keys[ 1 ].value, 0.9, 1e-6 ) );
	TimelineClip r;
	r.CopyLookFrom( l );
	r.RemoveKeysAt( 4 );
	CHECK( r.keys[ (int)KeyProperty::Scale ].empty() && r.paramKeys[ 0 ].keys.size() == 1 && r.keys[ (int)KeyProperty::Opacity ].size() == 1 );
}

int main()
{
	TestRange();
	TestCuesAndSync();
	TestAnimationsAndTransitions();
	TestSaving();
	TestLayersAndRows();
	TestDecks();
	TestTimeline();
	TestKeyframes();
	TestKeyCurves();
	TestFrameClock();
	TestEvenFrames();
	if( failures )
	{
		std::fprintf( stderr, "%d check(s) failed\n", failures );
		return 1;
	}
	std::printf( "clip_test: all checks passed\n" );
	return 0;
}
