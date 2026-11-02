// Tests for effects without a GPU: parameters, animations, saving and restoring chains in compositions.
#include <cmath>
#include <cstdio>
#include <json.hpp>
#include <string>
#include "Composition.h"
#include "fx/BuiltinEffects.h"
#include "fx/EffectRegistry.h"
#include "fx/Isf.h"
#include <filesystem>
#include <fstream>
#include <sstream>

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

	// Smooth Random glides from one random value to the next: at the period's start it is Random's value.
	Animation smooth = a;
	smooth.mode      = AnimMode::SmoothRandom;
	CHECK( Near( smooth.Evaluate( 4 ), a.Evaluate( 4 ) ) && Near( smooth.Evaluate( 8 ), a.Evaluate( 8 ) ) );
	float previous = smooth.Evaluate( 4 );
	for( double beat = 4.1; beat < 8; beat += 0.1 )
	{
		float v = smooth.Evaluate( beat );
		CHECK( v >= 0.2f && v <= 0.6f && std::fabs( v - previous ) < 0.05f ); // no jumps
		previous = v;
	}

	// A period in seconds follows the clock, whatever the beat.
	AnimationClock clock;
	clock.seconds = 1.5;
	SetAnimationClock( clock );
	Animation timed;
	timed.mode    = AnimMode::Saw;
	timed.seconds = true;
	timed.beats   = 2; // seconds here
	CHECK( Near( timed.Evaluate( 0 ), 0.75f ) && Near( timed.Evaluate( 123 ), 0.75f ) );

	// The sound coming in: the band's level mapped onto from..to, and the input asked for while it is used.
	clock.seconds    = 100;
	clock.audio[ 1 ] = 0.5f; // bass
	SetAnimationClock( clock );
	CHECK( !AudioAnimationWanted( 10 ) );
	Animation bass;
	bass.mode = AnimMode::AudioBass;
	bass.from = 0.2f;
	bass.to   = 1.0f;
	CHECK( Near( bass.Evaluate( 0 ), 0.6f ) );
	CHECK( AudioAnimationWanted( 10 ) );
	bass.reverse = true;
	CHECK( Near( bass.Evaluate( 0 ), 0.6f ) );
	bass.mode = AnimMode::AudioTreble;
	CHECK( Near( bass.Evaluate( 0 ), 1.0f ) ); // reversed silence
	clock.seconds = 111;
	SetAnimationClock( clock );
	CHECK( !AudioAnimationWanted( 10 ) ); // nobody listened for 11 seconds
	for( int m = 0; m < (int)AnimMode::Count; ++m )
		CHECK( AnimModeName( (AnimMode)m ) != nullptr && IsAudioMode( (AnimMode)m ) == ( m >= (int)AnimMode::AudioLevel ) );
	SetAnimationClock( AnimationClock() );
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

// ISF: the header read into inputs and passes, the shader rewritten for OpenGL 4.1 core, pass sizes, and every
// shader that comes with the app readable.
static void TestIsf()
{
	const char* fs = R"isf(/*{
	"DESCRIPTION": "test",
	"CREDIT": "by me",
	"CATEGORIES": [ "Blur", "Glitch" ],
	"INPUTS": [
		{ "NAME": "inputImage", "TYPE": "image" },
		{ "NAME": "blurAmount", "TYPE": "float", "MIN": 0, "MAX": 24, "DEFAULT": 12 },
		{ "NAME": "mode", "TYPE": "long", "VALUES": [ 0, 1, 2 ], "LABELS": [ "One", "Two", "Three" ], "DEFAULT": 1 },
		{ "NAME": "tint", "TYPE": "color", "DEFAULT": [ 1.0, 0.5, 0.25, 1.0 ] },
		{ "NAME": "center", "TYPE": "point2D", "DEFAULT": [ 0.5, 0.5 ], "MIN": [ 0, 0 ], "MAX": [ 1, 1 ] },
		{ "NAME": "flash", "TYPE": "event" },
		{ "NAME": "invert", "TYPE": "bool", "DEFAULT": true, },
	],
	"PASSES": [ { "TARGET": "half", "WIDTH": "floor($WIDTH/2.0)", "HEIGHT": "$HEIGHT/2", "PERSISTENT": true, "FLOAT": true }, {} ]
}*/
varying vec2 offset;
void main(void)
{
	vec4 c = IMG_THIS_PIXEL( inputImage ) + texture2D( half, vv_FragNormCoord ) * blurAmount;
	gl_FragColor = invert ? 1.0 - c : c;
}
)isf";
	IsfShader s;
	std::string error;
	CHECK( ParseIsf( "Test", fs, "", s, error ) );
	CHECK( s.name == "Test" && s.description == "test" && s.credit == "by me" && s.categories.size() == 2 && s.categories[ 0 ] == "Blur" );
	CHECK( !s.source && !s.usesAudio && !s.usesImports );
	CHECK( s.inputs.size() == 7 );
	if( s.inputs.size() == 7 )
	{
		CHECK( s.inputs[ 1 ].type == IsfInput::Type::Float && s.inputs[ 1 ].max == 24 && s.inputs[ 1 ].defaults[ 0 ] == 12 );
		CHECK( s.inputs[ 1 ].label == "Blur Amount" ); // no LABEL: the name, readable
		CHECK( s.inputs[ 2 ].type == IsfInput::Type::Long && s.inputs[ 2 ].values.size() == 3 && s.inputs[ 2 ].labels[ 2 ] == "Three" );
		CHECK( s.inputs[ 3 ].type == IsfInput::Type::Color && s.inputs[ 3 ].defaults[ 1 ] == 0.5f );
		CHECK( s.inputs[ 4 ].type == IsfInput::Type::Point2D && s.inputs[ 4 ].maxY == 1 );
		CHECK( s.inputs[ 5 ].type == IsfInput::Type::Event && s.inputs[ 6 ].defaults[ 0 ] == 1 );
	}
	CHECK( s.passes.size() == 2 && s.passes[ 0 ].target == "half" && s.passes[ 0 ].persistent && s.passes[ 0 ].floating && s.passes[ 1 ].target.empty() );
	// Rewritten: no GLSL 1.x left, ISF's names declared, the shader's main wrapped.
	CHECK( s.fragment.find( "gl_FragColor" ) == std::string::npos && s.fragment.find( "texture2D" ) == std::string::npos );
	CHECK( s.fragment.find( "varying" ) == std::string::npos && s.fragment.find( "in vec2 offset;" ) != std::string::npos );
	CHECK( s.fragment.find( "uniform sampler2D half;" ) != std::string::npos && s.fragment.find( "uniform vec4 tint;" ) != std::string::npos );
	CHECK( s.fragment.find( "void isf_main(void)" ) != std::string::npos && s.fragment.find( "isf_FragNormCoord )" ) != std::string::npos );
	CHECK( s.vertex.find( "isf_vertShaderInit(); }" ) != std::string::npos );

	// ISF 1: persistent buffers named apart; a generator; the header's mistakes.
	IsfShader old;
	CHECK( ParseIsf( "Old", R"isf(/*{ "PERSISTENT_BUFFERS": { "trail": { "WIDTH": "$WIDTH/4" } }, "PASSES": [ { "TARGET": "trail" }, { } ] }*/
void main() { gl_FragColor = vec4( 1.0 ); })isf",
	                 "void main() { vv_vertShaderInit(); }", old, error ) );
	CHECK( old.source && old.passes[ 0 ].persistent && old.passes[ 0 ].width == "$WIDTH/4" && !old.passes[ 1 ].persistent );
	CHECK( old.vertex.find( "isf_vertShaderInit(); }" ) != std::string::npos );
	CHECK( !ParseIsf( "Bad", "void main() {}", "", old, error ) && !error.empty() );
	CHECK( !ParseIsf( "Bad", "/*{ not json }*/ void main() {}", "", old, error ) );

	// Pass sizes.
	std::map< std::string, double > vars = { { "WIDTH", 1920 }, { "HEIGHT", 1080 }, { "blurAmount", 4 } };
	double v = 0;
	CHECK( EvalIsfExpression( "floor($WIDTH/2.0)", vars, v ) && v == 960 );
	CHECK( EvalIsfExpression( "$HEIGHT / ($blurAmount + 1) * 2", vars, v ) && std::fabs( v - 432 ) < 1e-9 );
	CHECK( EvalIsfExpression( "max(1.0, $WIDTH/pow(2.0, 12.0))", vars, v ) && std::fabs( v - 1 ) < 1e-9 );
	CHECK( EvalIsfExpression( "-$WIDTH + 2000", vars, v ) && v == 80 );
	CHECK( !EvalIsfExpression( "$NOPE", vars, v ) && !EvalIsfExpression( "1 +", vars, v ) && !EvalIsfExpression( "foo(2)", vars, v ) );

	// Every shader that comes with the app reads, and needs nothing this host does not feed.
	namespace fsys = std::filesystem;
	int count = 0;
	for( const auto& entry : fsys::directory_iterator( fsys::u8path( ISF_DIR ) ) )
	{
		if( entry.path().extension() != ".fs" )
			continue;
		auto read = []( const fsys::path& p ) {
			std::ifstream in( p, std::ios::binary );
			std::stringstream text;
			text << in.rdbuf();
			return text.str();
		};
		fsys::path vertex = entry.path();
		vertex.replace_extension( ".vs" );
		IsfShader shader;
		bool ok = ParseIsf( entry.path().stem().u8string(), read( entry.path() ), fsys::exists( vertex ) ? read( vertex ) : "", shader, error );
		if( !ok || shader.usesAudio || shader.usesImports )
			std::fprintf( stderr, "%s: %s\n", entry.path().u8string().c_str(), ok ? "needs audio or images" : error.c_str() );
		CHECK( ok && !shader.usesAudio && !shader.usesImports );
		++count;
	}
	CHECK( count > 150 );

	// From the registry: an ISF source with a colour and a point, its settings saved and restored by name.
	EffectRegistry registry;
	registry.Scan( { ISF_DIR } );
	const EffectRegistry::Entry* shape = registry.Find( "isf:Basic Shape" );
	CHECK( shape && shape->source && shape->isf && !shape->category.empty() );
	std::unique_ptr< Effect > a = registry.Create( "isf:Basic Shape" );
	CHECK( a && a->IsSource() && a->Id() == "isf:Basic Shape" );
	if( a )
	{
		int red = -1, x = -1;
		for( int i = 0; i < (int)a->Params().size(); ++i )
		{
			if( a->Params()[ i ].kind == ParamKind::Red && red < 0 )
				red = i;
			if( a->Params()[ i ].kind == ParamKind::XPos && x < 0 )
				x = i;
		}
		CHECK( red >= 0 && x >= 0 && a->Params()[ red + 1 ].kind == ParamKind::Green && a->Params()[ red + 3 ].kind == ParamKind::Alpha );
		CHECK( x >= 0 && a->Params()[ x + 1 ].kind == ParamKind::YPos );
		if( red >= 0 && x >= 0 )
		{
			a->SetValue( red, 0.25f );
			a->SetValue( x, 0.75f );
			std::unique_ptr< Effect > b = registry.Create( "isf:Basic Shape" );
			LoadEffectState( *b, SaveEffectState( *a ) );
			CHECK( Near( b->Value( red ), 0.25f ) && Near( b->Value( x ), 0.75f ) );
		}
	}
	CHECK( registry.Create( "isf:Not There" ) == nullptr );
}

int main()
{
	TestIsf();
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
