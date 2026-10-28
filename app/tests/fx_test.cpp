// Tests for effects without a GPU: parameters, animations, saving and restoring chains in compositions.
#include <cmath>
#include <cstdio>
#include <json.hpp>
#include <string>
#include "Composition.h"
#include "fx/BuiltinEffects.h"
#include "fx/EffectRegistry.h"

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

static bool Near( float a, float b )
{
	return std::fabs( a - b ) < 1e-4f;
}

static void TestAnimation()
{
	Animation a;
	a.mode  = AnimMode::Saw;
	a.beats = 4;
	CHECK( Near( a.Evaluate( 0 ), 0 ) );
	CHECK( Near( a.Evaluate( 2 ), 0.5f ) );
	CHECK( Near( a.Evaluate( 5 ), 0.25f ) );
	a.mode = AnimMode::PingPong;
	CHECK( Near( a.Evaluate( 2 ), 1 ) );
	CHECK( Near( a.Evaluate( 3 ), 0.5f ) );
	a.mode = AnimMode::Square;
	CHECK( Near( a.Evaluate( 1 ), 0 ) );
	CHECK( Near( a.Evaluate( 3 ), 1 ) );
	a.mode    = AnimMode::Saw;
	a.from    = 0.2f;
	a.to      = 0.6f;
	a.reverse = true;
	CHECK( Near( a.Evaluate( 0 ), 0.6f ) );
	a.mode = AnimMode::Random;
	float r = a.Evaluate( 1 );
	CHECK( r >= 0.2f && r <= 0.6f );
	CHECK( Near( a.Evaluate( 1 ), a.Evaluate( 3.9 ) ) ); // one value per period
}

static void TestBuiltins()
{
	EffectRegistry registry;
	int effects = 0, sources = 0;
	for( const EffectRegistry::Entry& e : registry.Entries() )
	{
		std::unique_ptr< Effect > fx = registry.Create( e.id );
		CHECK( fx != nullptr );
		if( !fx )
			continue;
		CHECK( fx->Id() == e.id );
		CHECK( fx->IsSource() == e.source );
		CHECK( fx->Params().size() <= 12 ); // the GLSL template has p0..p11
		( e.source ? sources : effects )++;
	}
	CHECK( effects >= 10 );
	CHECK( sources >= 3 );
	CHECK( registry.Create( "builtin:Nope" ) == nullptr );

	std::unique_ptr< Effect > kaleido = CreateBuiltin( "Kaleidoscope" );
	int segments = kaleido->FindParam( "Segments" );
	CHECK( segments >= 0 );
	kaleido->SetValue( segments, 7.6f );
	CHECK( Near( kaleido->Value( segments ), 8 ) ); // integer
	kaleido->SetValue( segments, 100 );
	CHECK( Near( kaleido->Value( segments ), 24 ) ); // clamped to the range

	// Animation drives the effective value, the base value stays.
	kaleido->Anim( segments ).mode  = AnimMode::Saw;
	kaleido->Anim( segments ).beats = 4;
	FrameInfo frame;
	frame.beat = 2;
	kaleido->Update( frame );
	CHECK( Near( kaleido->EffectiveValue( segments ), 13 ) ); // 2 + (24 - 2) * 0.5
	CHECK( Near( kaleido->Value( segments ), 24 ) );

	// Events last one frame.
	std::unique_ptr< Effect > mirror = CreateBuiltin( "Mirror" );
	int mode                         = mirror->FindParam( "Mode" );
	CHECK( mirror->Params()[ mode ].kind == ParamKind::Option );
	CHECK( mirror->Params()[ mode ].options.size() == 5 );
}

static void TestEffectState()
{
	std::unique_ptr< Effect > strobe = CreateBuiltin( "Strobe" );
	int duty                         = strobe->FindParam( "Duty" );
	int red                          = strobe->FindParam( "Flash Red" );
	strobe->SetValue( duty, 0.25f );
	strobe->SetValue( red, 0.5f );
	strobe->Anim( duty ).mode  = AnimMode::Sine;
	strobe->Anim( duty ).beats = 2;
	std::string saved          = SaveEffectState( *strobe );

	std::unique_ptr< Effect > copy = CreateBuiltin( "Strobe" );
	LoadEffectState( *copy, saved );
	CHECK( Near( copy->Value( duty ), 0.25f ) );
	CHECK( Near( copy->Value( red ), 0.5f ) );
	CHECK( copy->Anim( duty ).mode == AnimMode::Sine );
	CHECK( Near( copy->Anim( duty ).beats, 2 ) );
	// Unknown parameters and garbage are ignored.
	LoadEffectState( *copy, R"({"params":{"Nope":3,"Duty":"text"}})" );
	LoadEffectState( *copy, "not json" );
	CHECK( Near( copy->Value( duty ), 0.25f ) );
}

static void TestCompositionRoundTrip()
{
	EffectRegistry registry;
	Composition c = Composition::MakeDefault();
	EffectSlot blur;
	blur.effect = registry.Create( "builtin:Blur" );
	blur.effect->SetValue( blur.effect->FindParam( "Radius" ), 20 );
	blur.mix = 0.5f;
	c.effects.push_back( std::move( blur ) );
	EffectSlot invert;
	invert.effect = registry.Create( "builtin:Invert" );
	invert.bypass = true;
	c.layers[ 1 ].effects.push_back( std::move( invert ) );
	// A plugin this machine does not have: its settings must survive a save.
	EffectSlot missing;
	missing.missingId  = "ffgl:ABCD:Not Here";
	missing.savedState = R"({"id":"ffgl:ABCD:Not Here","params":{"Amount":0.7}})";
	c.layers[ 0 ].effects.push_back( std::move( missing ) );
	Clip& generated       = c.layers[ 2 ].clips[ 3 ];
	generated.name        = "Lines";
	generated.generatorId = "builtin:Lines";
	generated.generator   = registry.Create( "builtin:Lines" );
	generated.generator->SetValue( generated.generator->FindParam( "Count" ), 12 );

	std::string text = SaveComposition( c );
	Composition loaded;
	std::string error;
	CHECK( LoadComposition( text, loaded, error ) );
	CHECK( loaded.effects.size() == 1 && loaded.layers[ 1 ].effects.size() == 1 && loaded.layers[ 0 ].effects.size() == 1 );
	registry.Restore( loaded.effects );
	registry.Restore( loaded.layers[ 0 ].effects );
	registry.Restore( loaded.layers[ 1 ].effects );
	CHECK( loaded.effects[ 0 ].effect && loaded.effects[ 0 ].effect->Name() == "Blur" );
	CHECK( Near( loaded.effects[ 0 ].mix, 0.5f ) );
	CHECK( Near( loaded.effects[ 0 ].effect->Value( loaded.effects[ 0 ].effect->FindParam( "Radius" ) ), 20 ) );
	CHECK( loaded.layers[ 1 ].effects[ 0 ].bypass );
	CHECK( !loaded.layers[ 0 ].effects[ 0 ].effect );
	CHECK( loaded.layers[ 0 ].effects[ 0 ].missingId == "ffgl:ABCD:Not Here" );
	const Clip& clip = loaded.layers[ 2 ].clips[ 3 ];
	CHECK( clip.generatorId == "builtin:Lines" );
	CHECK( clip.IsGenerator() && !clip.Empty() );

	// Saving again keeps the missing plugin's settings.
	std::string again = SaveComposition( loaded );
	CHECK( again.find( "Not Here" ) != std::string::npos );
	CHECK( again.find( "0.7" ) != std::string::npos );
	nlohmann::json json = nlohmann::json::parse( again );
	CHECK( json[ "decks" ][ 0 ][ "rows" ][ 2 ][ 3 ][ "generator" ][ "params" ][ "Count" ].get< float >() == 12 );
}

int main()
{
	TestAnimation();
	TestBuiltins();
	TestEffectState();
	TestCompositionRoundTrip();
	if( failures )
	{
		std::fprintf( stderr, "%d check(s) failed\n", failures );
		return 1;
	}
	std::printf( "effect tests passed\n" );
	return 0;
}
