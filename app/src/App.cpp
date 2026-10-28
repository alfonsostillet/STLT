#include "App.h"
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include "Settings.h"
#include "fx/BuiltinEffects.h"
#include "gl/Gl.h"
#include "ui/AdvancedOutputUi.h"
#include "ui/EffectsUi.h"
#include "ui/FileDialogs.h"
#include "ui/Panels.h"
#include "ui/Theme.h"

namespace fs = std::filesystem;

namespace stltvj
{
namespace
{
std::string iniPath; // ImGui keeps the pointer: must outlive the context

void OnDrop( GLFWwindow* window, int count, const char** paths )
{
	App* app = (App*)glfwGetWindowUserPointer( window );
	// The mouse may not have reported moves during the OS drag, so ask where the cursor is now.
	double x = 0, y = 0;
	int left = 0, top = 0;
	glfwGetCursorPos( window, &x, &y );
	glfwGetWindowPos( window, &left, &top );
	app->dropX = left + (float)x;
	app->dropY = top + (float)y;
	for( int i = 0; i < count; ++i )
		app->droppedPaths.push_back( paths[ i ] );
}

void OnGlfwError( int code, const char* message )
{
	std::fprintf( stderr, "GLFW error %d: %s\n", code, message );
}

// Bottom-up 24-bit BMP, which is how glReadPixels hands rows over.
bool WriteBmp( const std::string& path, int width, int height, const std::vector< uint8_t >& rgb )
{
	int rowBytes  = ( width * 3 + 3 ) & ~3;
	uint32_t size = 54 + (uint32_t)rowBytes * height;
	uint8_t header[ 54 ] = { 'B', 'M' };
	auto put32           = [ & ]( int at, uint32_t v ) {
        for( int i = 0; i < 4; ++i )
            header[ at + i ] = (uint8_t)( v >> ( 8 * i ) );
	};
	put32( 2, size );
	put32( 10, 54 );
	put32( 14, 40 );
	put32( 18, (uint32_t)width );
	put32( 22, (uint32_t)height );
	header[ 26 ] = 1;
	header[ 28 ] = 24;
	put32( 34, (uint32_t)rowBytes * height );
	FILE* f = std::fopen( path.c_str(), "wb" );
	if( !f )
		return false;
	std::fwrite( header, 1, sizeof( header ), f );
	std::vector< uint8_t > row( rowBytes, 0 );
	for( int y = 0; y < height; ++y )
	{
		for( int x = 0; x < width; ++x )
		{
			const uint8_t* p   = &rgb[ ( (size_t)y * width + x ) * 3 ];
			row[ x * 3 + 0 ] = p[ 2 ];
			row[ x * 3 + 1 ] = p[ 1 ];
			row[ x * 3 + 2 ] = p[ 0 ];
		}
		std::fwrite( row.data(), 1, row.size(), f );
	}
	std::fclose( f );
	return true;
}
}

bool App::Init( const LaunchOptions& launch )
{
	options = launch;
	glfwSetErrorCallback( OnGlfwError );
	if( !glfwInit() )
		return false;
	glfwWindowHint( GLFW_CONTEXT_VERSION_MAJOR, 4 );
	glfwWindowHint( GLFW_CONTEXT_VERSION_MINOR, 1 );
	glfwWindowHint( GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE );
	glfwWindowHint( GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE );
	glfwWindowHint( GLFW_MAXIMIZED, options.width > 0 ? GLFW_FALSE : GLFW_TRUE );
	window = glfwCreateWindow( options.width > 0 ? options.width : 1600, options.height > 0 ? options.height : 900, "STLT-VJ", nullptr, nullptr );
	glfwDefaultWindowHints();
	if( !window )
	{
		std::fprintf( stderr, "Could not create an OpenGL 4.1 window\n" );
		return false;
	}
	glfwMakeContextCurrent( window );
	glfwSwapInterval( 1 );
	glfwSetWindowUserPointer( window, this );
	glfwSetDropCallback( window, OnDrop );

	const char* missing = nullptr;
	if( !LoadGl( &missing ) )
	{
		std::fprintf( stderr, "OpenGL function %s is missing\n", missing );
		return false;
	}
	if( !Caps().s3tc )
		std::fprintf( stderr, "Warning: this GPU driver reports no S3TC (DXT) support; HAP needs it\n" );

	configDir = ConfigDirectory();
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO();
	io.ConfigFlags |= ImGuiConfigFlags_DockingEnable | ImGuiConfigFlags_ViewportsEnable | ImGuiConfigFlags_NavEnableKeyboard;
	io.ConfigWindowsMoveFromTitleBarOnly = true;
	io.ConfigDockingAlwaysTabBar         = true; // every panel has a tab to grab, docked or floating
	iniPath        = ( fs::u8path( configDir ) / "layout.ini" ).u8string();
	io.IniFilename = options.fresh ? nullptr : iniPath.c_str();
	workspaces.Install( &panels, ( fs::u8path( configDir ) / "workspaces" ).u8string() );

	float scaleX = 1, scaleY = 1;
	glfwGetWindowContentScale( window, &scaleX, &scaleY );
#ifdef __APPLE__
	scaleX = 1; // macOS scales the framebuffer itself
#endif
	ApplyTheme( std::max( 1.0f, scaleX ) );
	ImGui_ImplGlfw_InitForOpenGL( window, true );
	ImGui_ImplOpenGL3_Init( "#version 410" );

	std::string error;
	if( !compositor.Init( error ) )
	{
		std::fprintf( stderr, "%s\n", error.c_str() );
		return false;
	}
	preview.Ensure( 480, 270 );
	if( !outputs.Init( error ) )
	{
		std::fprintf( stderr, "%s\n", error.c_str() );
		return false;
	}

	std::string folders;
	if( ReadTextFile( ( fs::u8path( configDir ) / "plugin-folders.txt" ).u8string(), folders ) )
	{
		size_t at = 0;
		while( at < folders.size() )
		{
			size_t end       = folders.find( '\n', at );
			std::string line = folders.substr( at, end == std::string::npos ? std::string::npos : end - at );
			while( !line.empty() && ( line.back() == '\r' || line.back() == ' ' ) )
				line.pop_back();
			if( !line.empty() )
				extraPluginFolders.push_back( line );
			at = end == std::string::npos ? folders.size() : end + 1;
		}
	}
	registry = std::make_unique< EffectRegistry >();
	RescanPlugins();
	if( options.listEffects )
	{
		for( const EffectRegistry::Entry& e : registry->Entries() )
			std::printf( "%-8s %s\n", e.source ? "source" : "effect", e.id.c_str() );
		for( const EffectRegistry::Failure& f : registry->Failures() )
			std::printf( "skipped  %s: %s\n", f.path.c_str(), f.reason.c_str() );
	}

	browserDirectory = HomeDirectory();
	composition      = Composition::MakeDefault();
	std::string last = ( fs::u8path( configDir ) / "last-session.stltvj" ).u8string();
	if( !options.fresh && fs::exists( fs::u8path( last ) ) )
		OpenComposition( last );
	compositionPath.clear(); // the session file is not the user's own composition
	std::string outputText, outputError;
	if( options.fresh || !ReadTextFile( ( fs::u8path( configDir ) / "advanced-output.json" ).u8string(), outputText )
	    || !LoadAdvancedOutput( outputText, advancedOutput, outputError ) || advancedOutput.screens.empty() )
		advancedOutput = AdvancedOutputSetup::MakeDefault( composition.width, composition.height );
	for( const LaunchOptions::ClipArg& clip : options.clips )
	{
		LoadClip( clip.layer - 1, clip.column - 1, clip.path );
		Launch( clip.layer - 1, clip.column - 1 );
	}
	for( const LaunchOptions::ClipArg& source : options.sources )
	{
		LoadGenerator( source.layer - 1, source.column - 1, source.path );
		Launch( source.layer - 1, source.column - 1 );
	}
	if( !options.outputSetup.empty() )
	{
		std::string text, setupError;
		if( fs::u8path( options.outputSetup ).extension() == ".xml" )
			ImportResolumeOutput( options.outputSetup );
		else if( !ReadTextFile( options.outputSetup, text ) || !LoadAdvancedOutput( text, advancedOutput, setupError ) )
			std::fprintf( stderr, "Cannot load %s %s\n", options.outputSetup.c_str(), setupError.c_str() );
	}
	if( options.advancedOutputTab >= 0 )
	{
		panels.advancedOutput = true;
		ShowAdvancedOutputTab( options.advancedOutputTab, 0, 0 );
	}
	for( auto [ layer, column ] : options.launches )
		Launch( layer - 1, column - 1 );
	for( const LaunchOptions::EffectArg& fx : options.effects )
	{
		int layer = 0, column = 0;
		if( fx.scope == "comp" )
			AddEffect( composition.effects, fx.id );
		else if( std::sscanf( fx.scope.c_str(), "layer:%d", &layer ) == 1 && layer >= 1 && layer <= (int)composition.layers.size() )
			AddEffect( composition.layers[ layer - 1 ].effects, fx.id );
		else if( std::sscanf( fx.scope.c_str(), "clip:%d,%d", &layer, &column ) == 2 && layer >= 1 && layer <= (int)composition.layers.size() && column >= 1
		         && column <= composition.ColumnCount() )
			AddEffect( composition.layers[ layer - 1 ].clips[ column - 1 ].effects, fx.id );
		else
			std::fprintf( stderr, "Ignoring --fx %s=%s\n", fx.scope.c_str(), fx.id.c_str() );
	}
	lastTime = startTime = glfwGetTime();
	return true;
}

void App::Run()
{
	while( !glfwWindowShouldClose( window ) && !quitRequested )
	{
		glfwPollEvents();
		Frame();
	}
}

void App::Shutdown()
{
	if( !window )
		return;
	if( !options.fresh )
		WriteTextFile( ( fs::u8path( configDir ) / "last-session.stltvj" ).u8string(), SaveComposition( composition ) );
	WriteTextFile( ( fs::u8path( configDir ) / "advanced-output.json" ).u8string(), SaveAdvancedOutput( advancedOutput ) );
	glfwMakeContextCurrent( window );
	outputs.Shutdown();
	composition = Composition{}; // frees the players' GPU memory and plugin instances while the context is alive
	preview.Release();
	compositor.Shutdown();
	ReleaseBuiltinPrograms();
	registry.reset(); // unloads the plugins
	ImGui_ImplOpenGL3_Shutdown();
	ImGui_ImplGlfw_Shutdown();
	ImGui::DestroyContext();
	glfwDestroyWindow( window );
	window = nullptr;
	glfwTerminate();
}

ClipPlayer* App::PlayerAt( int layer, int column )
{
	if( layer < 0 || layer >= (int)composition.layers.size() || column < 0 || column >= composition.ColumnCount() )
		return nullptr;
	return composition.layers[ layer ].clips[ column ].player.get();
}

void App::SetStatus( const std::string& message, bool error )
{
	status        = message;
	statusIsError = error;
	statusTime    = glfwGetTime();
	if( error )
		std::fprintf( stderr, "%s\n", message.c_str() );
}

void App::LoadClip( int layer, int column, const std::string& path )
{
	if( layer < 0 || column < 0 )
		return;
	while( (int)composition.layers.size() <= layer )
		composition.AddLayer();
	while( composition.ColumnCount() <= column )
		composition.AddColumn();
	if( composition.layers[ layer ].active == column )
		ClearLayer( layer );

	Clip clip;
	clip.path   = path;
	clip.name   = fs::u8path( path ).stem().u8string();
	clip.player = std::make_unique< ClipPlayer >();
	if( clip.player->Open( path ) )
	{
		compositor.MakeThumbnail( *clip.player );
		const FrameSource& source = clip.player->Source();
		if( source.IsHap() || source.IsStill() )
			SetStatus( "Loaded " + clip.name );
		else
			SetStatus( "Loaded " + clip.name + " (" + source.CodecName() + ", decoded on the CPU: convert it to HAP for the best performance)" );
	}
	else
	{
		SetStatus( clip.name + ": " + clip.player->Error(), true );
	}
	composition.layers[ layer ].clips[ column ] = std::move( clip );
	Select( layer, column );
}

void App::RemoveClip( int layer, int column )
{
	if( !PlayerAt( layer, column ) && ( layer < 0 || layer >= (int)composition.layers.size() ) )
		return;
	if( composition.layers[ layer ].active == column )
		ClearLayer( layer );
	composition.layers[ layer ].clips[ column ] = Clip{};
}

void App::MoveClip( int fromLayer, int fromColumn, int toLayer, int toColumn )
{
	int layers = (int)composition.layers.size(), columns = composition.ColumnCount();
	if( fromLayer < 0 || fromLayer >= layers || toLayer < 0 || toLayer >= layers || fromColumn < 0 || fromColumn >= columns || toColumn < 0
	    || toColumn >= columns || ( fromLayer == toLayer && fromColumn == toColumn ) )
		return;
	Layer& a = composition.layers[ fromLayer ];
	Layer& b = composition.layers[ toLayer ];
	if( fromLayer == toLayer )
	{
		std::swap( a.clips[ fromColumn ], a.clips[ toColumn ] );
		if( a.active == fromColumn )
			a.active = toColumn;
		else if( a.active == toColumn )
			a.active = fromColumn;
	}
	else
	{
		// A playing clip that changes layer stops: the target layer decides what plays.
		if( a.active == fromColumn )
			ClearLayer( fromLayer );
		if( b.active == toColumn )
			ClearLayer( toLayer );
		std::swap( a.clips[ fromColumn ], b.clips[ toColumn ] );
	}
	Select( toLayer, toColumn );
}

void App::Launch( int layer, int column )
{
	if( layer < 0 || layer >= (int)composition.layers.size() || column < 0 || column >= composition.ColumnCount() )
		return;
	Layer& l   = composition.layers[ layer ];
	Clip& clip = l.clips[ column ];
	Select( layer, column );
	if( !clip.Playable() )
	{
		ClearLayer( layer );
		return;
	}
	if( l.active >= 0 && l.active != column )
		ClearLayer( layer );
	l.active = column;
	if( clip.generator )
		return;
	clip.player->Activate();
	clip.Restart();
	// Decode the first frame now so the trigger shows on this very frame.
	clip.player->ShowNow( clip.CurrentFrame() );
}

void App::LaunchColumn( int column )
{
	for( int layer = 0; layer < (int)composition.layers.size(); ++layer )
	{
		Layer& l = composition.layers[ layer ];
		if( column < (int)l.clips.size() && l.clips[ column ].Playable() )
			Launch( layer, column );
		else
			ClearLayer( layer );
	}
	composition.activeColumn = column;
}

void App::ClearLayer( int layer )
{
	if( layer < 0 || layer >= (int)composition.layers.size() )
		return;
	Layer& l = composition.layers[ layer ];
	if( l.active >= 0 && l.active < (int)l.clips.size() && l.clips[ l.active ].player )
		l.clips[ l.active ].player->Deactivate();
	l.active = -1;
}

void App::Select( int layer, int column )
{
	composition.selectedLayer  = layer;
	composition.selectedColumn = column;
}

void App::NewComposition()
{
	composition     = Composition::MakeDefault();
	compositionPath.clear();
	SetStatus( "New composition" );
}

bool App::SaveCompositionTo( const std::string& path )
{
	if( !WriteTextFile( path, SaveComposition( composition ) ) )
	{
		SetStatus( "Could not save " + path, true );
		return false;
	}
	compositionPath = path;
	SetStatus( "Saved " + path );
	return true;
}

bool App::OpenComposition( const std::string& path )
{
	std::string text, error;
	Composition loaded;
	if( !ReadTextFile( path, text ) || !LoadComposition( text, loaded, error ) )
	{
		SetStatus( "Could not open " + path + ( error.empty() ? "" : ": " + error ), true );
		return false;
	}
	composition = std::move( loaded );
	RestoreEffects();
	int failures = 0;
	for( Layer& layer : composition.layers )
		for( Clip& clip : layer.clips )
		{
			if( clip.path.empty() )
				continue;
			clip.player = std::make_unique< ClipPlayer >();
			if( clip.player->Open( clip.path ) )
				compositor.MakeThumbnail( *clip.player );
			else
				failures++;
		}
	compositionPath = path;
	int missing = 0;
	auto countMissing = [ & ]( const EffectChain& chain ) {
		for( const EffectSlot& slot : chain )
			missing += slot.effect ? 0 : 1;
	};
	countMissing( composition.effects );
	for( const Layer& layer : composition.layers )
	{
		countMissing( layer.effects );
		for( const Clip& clip : layer.clips )
		{
			countMissing( clip.effects );
			missing += clip.IsGenerator() && !clip.generator ? 1 : 0;
		}
	}
	if( missing )
		failures += missing;
	SetStatus( failures ? "Opened " + path + " (" + std::to_string( failures ) + " clips missing or unsupported)" : "Opened " + path, failures > 0 );
	return true;
}

void App::RestoreEffects()
{
	registry->Restore( composition.effects );
	for( Layer& layer : composition.layers )
	{
		registry->Restore( layer.effects );
		for( Clip& clip : layer.clips )
		{
			registry->Restore( clip.effects );
			if( clip.IsGenerator() && !clip.generator )
			{
				clip.generator = registry->Create( clip.generatorId );
				if( clip.generator )
				{
					LoadEffectState( *clip.generator, clip.generatorState );
					compositor.MakeGeneratorThumbnail( clip, frame );
				}
			}
		}
	}
}

void App::AddEffect( EffectChain& chain, const std::string& id, int position )
{
	std::unique_ptr< Effect > effect = registry->Create( id );
	if( !effect )
	{
		SetStatus( "Effect " + id + " is not available", true );
		return;
	}
	if( effect->IsSource() )
	{
		SetStatus( effect->Name() + " is a source: drop it on an empty clip cell", true );
		return;
	}
	EffectSlot slot;
	slot.effect = std::move( effect );
	if( position < 0 || position > (int)chain.size() )
		chain.push_back( std::move( slot ) );
	else
		chain.insert( chain.begin() + position, std::move( slot ) );
}

void App::LoadGenerator( int layer, int column, const std::string& id )
{
	if( layer < 0 || column < 0 )
		return;
	std::unique_ptr< Effect > generator = registry->Create( id );
	if( !generator || !generator->IsSource() )
	{
		SetStatus( "Only sources can go in a clip cell; drop effects on a clip, a layer or the composition", true );
		return;
	}
	while( (int)composition.layers.size() <= layer )
		composition.AddLayer();
	while( composition.ColumnCount() <= column )
		composition.AddColumn();
	if( composition.layers[ layer ].active == column )
		ClearLayer( layer );
	Clip clip;
	clip.name        = generator->Name();
	clip.generatorId = id;
	clip.generator   = std::move( generator );
	compositor.MakeGeneratorThumbnail( clip, frame );
	composition.layers[ layer ].clips[ column ] = std::move( clip );
	Select( layer, column );
}

std::vector< std::string > App::PluginFolders() const
{
	std::vector< std::string > folders = EffectRegistry::DefaultFolders( configDir );
	for( const std::string& f : extraPluginFolders )
		if( std::find( folders.begin(), folders.end(), f ) == folders.end() )
			folders.push_back( f );
	return folders;
}

void App::RescanPlugins()
{
	size_t before = registry->Entries().size();
	registry->Scan( PluginFolders() );
	size_t added = registry->Entries().size() - before;
	if( added || !registry->Failures().empty() )
		SetStatus( std::to_string( added ) + " plugin(s) loaded" + ( registry->Failures().empty() ? "" : ", " + std::to_string( registry->Failures().size() ) + " skipped (see Effects panel)" ) );
	// Effects that were missing may be installed now.
	RestoreEffects();
}

void App::SavePluginFolders() const
{
	std::string text;
	for( const std::string& f : extraPluginFolders )
		text += f + "\n";
	WriteTextFile( ( fs::u8path( configDir ) / "plugin-folders.txt" ).u8string(), text );
}

void App::AddPluginFolder( const std::string& folder )
{
	if( folder.empty() || std::find( extraPluginFolders.begin(), extraPluginFolders.end(), folder ) != extraPluginFolders.end() )
		return;
	extraPluginFolders.push_back( folder );
	SavePluginFolders();
	RescanPlugins();
}

void App::RemovePluginFolder( const std::string& folder )
{
	extraPluginFolders.erase( std::remove( extraPluginFolders.begin(), extraPluginFolders.end(), folder ), extraPluginFolders.end() );
	SavePluginFolders();
}

void App::ShowScreenOn( size_t index, OutputDevice device, const std::string& display )
{
	if( advancedOutput.screens.empty() )
		advancedOutput = AdvancedOutputSetup::MakeDefault( composition.width, composition.height );
	if( index >= advancedOutput.screens.size() )
		return;
	Screen& screen = advancedOutput.screens[ index ];
	screen.device  = device;
	screen.display = display;
	screen.enabled = true;
	if( device == OutputDevice::Display )
		for( const OutputWindow::Display& d : OutputWindow::Displays() )
			if( d.monitorName == display && screen.slices.size() <= 1 && ( screen.width != d.width || screen.height != d.height ) )
			{
				// A simple one-slice screen follows the display's resolution.
				screen.width  = d.width;
				screen.height = d.height;
				if( screen.slices.size() == 1 && !screen.slices[ 0 ].polygon && !screen.slices[ 0 ].mask && screen.slices[ 0 ].warp == WarpMode::Perspective )
					screen.slices[ 0 ].FitOutput( (float)d.width, (float)d.height );
			}
}

void App::StopOutputs()
{
	for( Screen& screen : advancedOutput.screens )
		screen.device = OutputDevice::Virtual;
}

std::string App::OutputPresetDirectory() const
{
	return ( fs::u8path( configDir ) / "output-presets" ).u8string();
}

std::vector< std::string > App::OutputPresets() const
{
	std::vector< std::string > names;
	std::error_code error;
	for( const fs::directory_entry& entry : fs::directory_iterator( fs::u8path( OutputPresetDirectory() ), error ) )
		if( entry.path().extension() == ".json" )
			names.push_back( entry.path().stem().u8string() );
	std::sort( names.begin(), names.end() );
	return names;
}

bool App::SaveOutputPreset( const std::string& name )
{
	if( name.empty() || name.find_first_of( "/\\:*?\"<>|" ) != std::string::npos )
		return false;
	std::error_code ignored;
	fs::create_directories( fs::u8path( OutputPresetDirectory() ), ignored );
	return WriteTextFile( ( fs::u8path( OutputPresetDirectory() ) / fs::u8path( name + ".json" ) ).u8string(), SaveAdvancedOutput( advancedOutput ) );
}

bool App::LoadOutputPreset( const std::string& name )
{
	std::string text, error;
	AdvancedOutputSetup loaded;
	if( !ReadTextFile( ( fs::u8path( OutputPresetDirectory() ) / fs::u8path( name + ".json" ) ).u8string(), text ) || !LoadAdvancedOutput( text, loaded, error ) )
	{
		SetStatus( "Could not load output preset " + name + ( error.empty() ? "" : ": " + error ), true );
		return false;
	}
	advancedOutput = std::move( loaded );
	SetStatus( "Output preset " + name + " loaded" );
	return true;
}

bool App::ImportResolumeOutput( const std::string& path )
{
	std::string xml, error;
	AdvancedOutputSetup imported;
	int compWidth = 0, compHeight = 0;
	if( !ReadTextFile( path, xml ) || !ImportResolumeXml( xml, imported, error, &compWidth, &compHeight ) )
	{
		SetStatus( "Could not import " + path + ( error.empty() ? "" : ": " + error ), true );
		return false;
	}
	size_t slices = 0;
	for( const Screen& s : imported.screens )
		slices += s.slices.size();
	advancedOutput = std::move( imported );
	std::string note;
	if( compWidth != composition.width || compHeight != composition.height )
		note = " (made for a " + std::to_string( compWidth ) + "x" + std::to_string( compHeight ) + " composition; this one is " + std::to_string( composition.width )
		       + "x" + std::to_string( composition.height ) + ")";
	SetStatus( "Imported " + std::to_string( advancedOutput.screens.size() ) + " screen(s) and " + std::to_string( slices ) + " slice(s) from Resolume" + note,
	           !note.empty() );
	return true;
}

void App::TapTempo()
{
	double now = glfwGetTime();
	if( !taps.empty() && now - taps.back() > 2.0 )
		taps.clear();
	taps.push_back( now );
	if( taps.size() > 5 )
		taps.erase( taps.begin() );
	if( taps.size() >= 2 )
	{
		double interval = ( taps.back() - taps.front() ) / ( taps.size() - 1 );
		if( interval > 0 )
			composition.bpm = std::clamp( 60.0 / interval, 20.0, 500.0 );
	}
}

void App::UpdatePlayback( double dt )
{
	for( Layer& layer : composition.layers )
	{
		if( layer.active < 0 )
			continue;
		Clip& clip = layer.clips[ layer.active ];
		if( !clip.Loaded() )
			continue; // generators run on composition time
		if( !composition.paused )
			clip.Advance( dt * composition.speed );
		clip.player->Update( clip.CurrentFrame(), composition.paused ? 0 : clip.PlayDirection() );
	}
}

void App::Frame()
{
	workspaces.ApplyPending();
	double now = glfwGetTime();
	double dt  = std::min( now - lastTime, 0.1 );
	lastTime   = now;
	frameTimes.push_back( (float)( dt * 1000.0 ) );
	if( frameTimes.size() > 240 )
		frameTimes.pop_front();

	UpdatePlayback( dt );
	if( !composition.paused )
		composition.beat += dt * composition.speed * composition.bpm / 60.0;
	frame.time  = now - startTime;
	frame.delta = dt;
	frame.bpm   = composition.bpm;
	frame.beat  = composition.beat;
	compositor.Render( composition, frame );
	outputs.Render( advancedOutput, compositor.CompositionTexture(), composition.width, composition.height );
	if( Clip* selected = composition.SelectedClip() )
		if( selected->generator || ( selected->Loaded() && selected->player->Active() ) )
			compositor.RenderClipAlone( *selected, preview, composition.width, composition.height, frame );
	glFlush(); // makes the new composition visible to the output window's context

	std::string dialogError = PollFileDialogs();
	if( !dialogError.empty() )
		SetStatus( "File dialog: " + dialogError + " (Composition > Type a Path works without it)", true );

	ImGui_ImplOpenGL3_NewFrame();
	ImGui_ImplGlfw_NewFrame();
	ImGui::NewFrame();
	DrawUi();
	ImGui::Render();

	int width = 0, height = 0;
	glfwGetFramebufferSize( window, &width, &height );
	glBindFramebuffer( GL_FRAMEBUFFER, 0 );
	glViewport( 0, 0, width, height );
	glClearColor( 0.08f, 0.08f, 0.08f, 1 );
	glClear( GL_COLOR_BUFFER_BIT );
	ImGui_ImplOpenGL3_RenderDrawData( ImGui::GetDrawData() );
	frameCount++;
	if( !options.capturePath.empty() && frameCount == options.captureFrames )
		Capture();

	if( ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable )
	{
		ImGui::UpdatePlatformWindows();
		ImGui::RenderPlatformWindowsDefault();
		glfwMakeContextCurrent( window );
	}

	// Only one window may wait for vsync, or the frame rate halves: the output when it is open.
	bool wantOutputVsync = outputs.AnyWindowOpen();
	if( wantOutputVsync != outputVsync )
	{
		glfwSwapInterval( wantOutputVsync ? 0 : 1 );
		outputVsync = wantOutputVsync;
	}
	glfwSwapBuffers( window );
	outputs.SyncWindows( advancedOutput, window );
	outputs.Present( compositor, window );
}

void App::Capture()
{
	int width = 0, height = 0;
	glfwGetFramebufferSize( window, &width, &height );
	std::vector< uint8_t > pixels( (size_t)width * height * 3 );
	glPixelStorei( GL_PACK_ALIGNMENT, 1 );
	glReadPixels( 0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, pixels.data() );
	if( WriteBmp( options.capturePath, width, height, pixels ) )
		std::printf( "Captured %dx%d to %s\n", width, height, options.capturePath.c_str() );
	quitRequested = true;
}

void App::DrawUi()
{
	ImGuiViewport* viewport = ImGui::GetMainViewport();
	ImGui::SetNextWindowPos( viewport->WorkPos );
	ImGui::SetNextWindowSize( viewport->WorkSize );
	ImGui::SetNextWindowViewport( viewport->ID );
	ImGuiWindowFlags hostFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove
	                             | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus
	                             | ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoScrollbar;
	ImGui::PushStyleVar( ImGuiStyleVar_WindowRounding, 0 );
	ImGui::PushStyleVar( ImGuiStyleVar_WindowBorderSize, 0 );
	ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding, ImVec2( 0, 0 ) );
	ImGui::Begin( "##Host", nullptr, hostFlags );
	ImGui::PopStyleVar( 3 );

	DrawMainMenu( *this );
	DrawToolbar( *this );

	float statusHeight = ImGui::GetFrameHeight();
	ImVec2 dockSize    = ImGui::GetContentRegionAvail();
	dockSize.y -= statusHeight;
	ImGuiID dockspace = ImGui::GetID( "MainDock" );
	workspaces.BuildDefaultIfNeeded( dockspace, dockSize );
	ImGui::DockSpace( dockspace, dockSize, ImGuiDockNodeFlags_None );
	DrawStatusBar( *this );
	ImGui::End();

	if( panels.clips )
		DrawClipGrid( *this );
	if( panels.compositionMonitor )
		DrawCompositionMonitor( *this );
	if( panels.previewMonitor )
		DrawPreviewMonitor( *this );
	if( panels.composition )
		DrawCompositionPanel( *this );
	if( panels.layer )
		DrawLayerPanel( *this );
	if( panels.clip )
		DrawClipPanel( *this );
	if( panels.files )
		DrawFilesPanel( *this );
	if( panels.output )
		DrawOutputPanel( *this );
	if( panels.performance )
		DrawPerformancePanel( *this );
	if( panels.effects )
		DrawEffectsPanel( *this );
	if( panels.advancedOutput )
		DrawAdvancedOutputPanel( *this );
	droppedPaths.clear();
}
}
