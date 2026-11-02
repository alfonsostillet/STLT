// STLT: live HAP playback with dockable panels.
//   STLT [--fresh] [--clip LAYER,COLUMN,PATH]... [--source LAYER,COLUMN,EFFECT-ID]...
//           [--fx comp|layer:L|clip:L,C=EFFECT-ID]... [--launch LAYER,COLUMN]... [--list-effects] [--check-isf [FOLDER]]
//           [--output-setup FILE.json|AdvancedOutput.xml] [--advanced-output input|output] [--osc PORT]
//           [--size WxH] [--capture FILE.bmp [--frames N]]
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include "App.h"

int main( int argc, char** argv )
{
	stltvj::LaunchOptions options;
	for( int i = 1; i < argc; ++i )
	{
		std::string arg = argv[ i ];
		bool hasValue   = i + 1 < argc;
		if( arg == "--fresh" )
			options.fresh = true;
		else if( arg == "--list-effects" )
			options.listEffects = true;
		else if( arg == "--check-isf" )
		{
			options.checkIsf = true;
			if( hasValue && argv[ i + 1 ][ 0 ] != '-' )
				options.isfFolder = argv[ ++i ];
		}
		else if( arg == "--output-setup" && hasValue )
			options.outputSetup = argv[ ++i ];
		else if( arg == "--advanced-output" && hasValue )
			options.advancedOutputTab = std::strcmp( argv[ ++i ], "output" ) == 0 ? 1 : 0;
		else if( arg == "--capture" && hasValue )
			options.capturePath = argv[ ++i ];
		else if( arg == "--capture-composition" )
			options.captureComposition = true;
		else if( arg == "--capture-screen" && hasValue )
			options.captureScreen = std::max( 1, std::atoi( argv[ ++i ] ) );
		else if( arg == "--frames" && hasValue )
			options.captureFrames = std::max( 1, std::atoi( argv[ ++i ] ) );
		else if( arg == "--osc" && hasValue )
			options.oscPort = std::clamp( std::atoi( argv[ ++i ] ), 0, 65535 );
		else if( arg == "--size" && hasValue )
			std::sscanf( argv[ ++i ], "%dx%d", &options.width, &options.height );
		else if( arg == "--clip" && hasValue )
		{
			std::string value = argv[ ++i ];
			int layer = 0, column = 0, consumed = 0;
			if( std::sscanf( value.c_str(), "%d,%d,%n", &layer, &column, &consumed ) == 2 && consumed > 0 && layer > 0 && column > 0 )
				options.clips.push_back( { layer, column, value.substr( (size_t)consumed ) } );
			else
				std::fprintf( stderr, "Ignoring --clip %s (expected LAYER,COLUMN,PATH)\n", value.c_str() );
		}
		else if( arg == "--source" && hasValue )
		{
			std::string value = argv[ ++i ];
			int layer = 0, column = 0, consumed = 0;
			if( std::sscanf( value.c_str(), "%d,%d,%n", &layer, &column, &consumed ) == 2 && consumed > 0 && layer > 0 && column > 0 )
				options.sources.push_back( { layer, column, value.substr( (size_t)consumed ) } );
			else
				std::fprintf( stderr, "Ignoring --source %s (expected LAYER,COLUMN,EFFECT-ID)\n", value.c_str() );
		}
		else if( arg == "--launch" && hasValue )
		{
			int layer = 0, column = 0;
			if( std::sscanf( argv[ ++i ], "%d,%d", &layer, &column ) == 2 )
				options.launches.push_back( { layer, column } );
		}
		else if( arg == "--fx" && hasValue )
		{
			std::string value = argv[ ++i ];
			size_t equals     = value.find( '=' );
			if( equals == std::string::npos )
				std::fprintf( stderr, "Ignoring --fx %s (expected SCOPE=EFFECT-ID)\n", value.c_str() );
			else
				options.effects.push_back( { value.substr( 0, equals ), value.substr( equals + 1 ) } );
		}
		else
			std::fprintf( stderr, "Unknown argument %s\n", arg.c_str() );
	}

	stltvj::App app;
	if( !app.Init( options ) )
	{
		app.Shutdown();
		return 1;
	}
	if( options.checkIsf )
	{
		int failed = app.CheckIsf();
		app.Shutdown();
		return failed ? 1 : 0;
	}
	app.Run();
	app.Shutdown();
	return 0;
}
