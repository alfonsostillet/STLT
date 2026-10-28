// Tests for a clip's range (in and out points), cue points, BPM sync, beat animations, layer transitions and how
// they are saved. Clips are opened without a GPU: ClipPlayer::Open only reads the file.
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include "Composition.h"

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

int main()
{
	TestRange();
	TestCuesAndSync();
	TestAnimationsAndTransitions();
	TestSaving();
	if( failures )
	{
		std::fprintf( stderr, "%d check(s) failed\n", failures );
		return 1;
	}
	std::printf( "clip_test: all checks passed\n" );
	return 0;
}
