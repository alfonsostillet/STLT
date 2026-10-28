// STLT-VJ: live HAP playback with dockable panels.
//   stlt-vj [--fresh] [--clip LAYER,COLUMN,PATH]... [--size WxH] [--capture FILE.bmp [--frames N]]
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
		else if( arg == "--capture" && hasValue )
			options.capturePath = argv[ ++i ];
		else if( arg == "--frames" && hasValue )
			options.captureFrames = std::max( 1, std::atoi( argv[ ++i ] ) );
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
		else
			std::fprintf( stderr, "Unknown argument %s\n", arg.c_str() );
	}

	stltvj::App app;
	if( !app.Init( options ) )
	{
		app.Shutdown();
		return 1;
	}
	app.Run();
	app.Shutdown();
	return 0;
}
