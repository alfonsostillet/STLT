#include "App.h"
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <algorithm>
#include <set>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <json.hpp>
#include "Settings.h"
#include "audio/AudioEngine.h"
#include "fx/BuiltinEffects.h"
#include "fx/IsfEffect.h"
#include "gl/Gl.h"
#include "ui/AdvancedOutputUi.h"
#include "ui/ControlUi.h"
#include "ui/EffectsUi.h"
#include "ui/FileDialogs.h"
#include "chaser/ChaserLink.h"
#include "ui/Panels.h"
#include "ui/PreferencesUi.h"
#include "ui/TimelineUi.h"
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
	// The MIDI clock goes straight from the inputs' threads to the tempo sync, stamped as it arrives.
	remote.onClock = [ this ]( const std::string& device, const unsigned char* bytes, size_t size, double time ) {
		tempoSync.OnMidi( device, bytes, size, time );
		timecode.OnMidi( device, bytes, size, time );
	};
	// STLT-CHASER's sequences, for its editor and its effects (set before the effects load).
	chaserStage = ( fs::u8path( chaser::UseDataDirectory( configDir ) ) / "stlt-vj-stage.json" ).u8string();
	{
		std::string text;
		if( !options.fresh && ReadTextFile( ( fs::u8path( configDir ) / "preferences.json" ).u8string(), text ) )
			prefs.Load( text );
		ApplyPreferences( false );
	}
	// Sound: the output chosen in the Preferences, else the system's default (STLTVJ_NO_AUDIO=1 keeps it off).
	if( !std::getenv( "STLTVJ_NO_AUDIO" ) && !audio.Start( prefs.audioDevice, prefs.audioBuffer ) )
		std::fprintf( stderr, "Audio: %s\n", audio.Status().c_str() );
	{
		std::string text;
		if( ReadTextFile( ( fs::u8path( configDir ) / "control.json" ).u8string(), text ) )
			control.Load( text );
		// The preset the maps came from, to tell whether they changed since.
		std::string preset;
		ControlSettings saved;
		if( !control.preset.empty() && ReadTextFile( ( fs::u8path( ShortcutPresetDirectory() ) / fs::u8path( control.preset + ".json" ) ).u8string(), preset )
		    && saved.LoadShortcuts( preset ) )
			shortcutPresetText = saved.SaveShortcuts();
		else
			control.preset.clear();
		if( control.preset.empty() )
			shortcutPresetText = ControlSettings{}.SaveShortcuts();
		if( options.oscPort > 0 )
		{
			control.oscEnabled = true;
			control.oscPort    = options.oscPort;
		}
	}
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO();
	// No keyboard navigation: the keys belong to the user's shortcuts (text fields still take typing).
	io.ConfigFlags |= ImGuiConfigFlags_DockingEnable | ImGuiConfigFlags_ViewportsEnable;
	io.ConfigWindowsMoveFromTitleBarOnly = true;
#ifdef NDEBUG
	// Dear ImGui's warning about two controls sharing an ID is for us, not for someone playing a show.
	io.ConfigDebugHighlightIdConflicts = false;
#endif
	io.ConfigDockingAlwaysTabBar         = true; // every panel has a tab to grab, docked or floating
	iniPath        = ( fs::u8path( configDir ) / "layout.ini" ).u8string();
	io.IniFilename = options.fresh ? nullptr : iniPath.c_str();
	workspaces.Install( &panels, ( fs::u8path( configDir ) / "workspaces" ).u8string() );
	// The view in use when the app last closed: its layout is the one saved.
	std::string viewText;
	if( !options.fresh && ReadTextFile( ( fs::u8path( configDir ) / "view.txt" ).u8string(), viewText ) && viewText.rfind( "arrangement", 0 ) == 0 )
	{
		view = View::Arrangement;
		workspaces.SetView( 1 );
	}
	// A start view other than the last one: switched to once the layout is in (after the first frame).
	if( !options.fresh && prefs.startView > 0 && ( prefs.startView == 2 ) != ( view == View::Arrangement ) )
		startViewPending = prefs.startView;

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
	if( !outputs.Init( error ) || !quickOutputs.Init( error ) )
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
	if( !options.isfFolder.empty() )
		registry->Scan( { options.isfFolder } );
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
	if( !options.fresh && prefs.reopenLastSession && fs::exists( fs::u8path( last ) ) )
		OpenComposition( last );
	compositionPath.clear(); // the session file is not the user's own composition
	std::string outputText, outputError;
	if( options.fresh || !ReadTextFile( ( fs::u8path( configDir ) / "advanced-output.json" ).u8string(), outputText )
	    || !LoadAdvancedOutput( outputText, advancedOutput, outputError ) || advancedOutput.screens.empty() )
		advancedOutput = AdvancedOutputSetup::MakeDefault( composition.width, composition.height );
	// The output mode of the last session; before modes existed the setup's screens were always live.
	std::string modeText;
	nlohmann::json mode;
	if( !options.fresh && ReadTextFile( ( fs::u8path( configDir ) / "output-mode.json" ).u8string(), modeText ) )
		mode = nlohmann::json::parse( modeText, nullptr, false );
	if( mode.is_object() )
	{
		std::string name = mode.value( "mode", "off" );
		outputMode       = name == "window" ? OutputMode::Window : name == "fullscreen" ? OutputMode::Fullscreen : name == "advanced" ? OutputMode::Advanced : OutputMode::Off;
		outputDisplay    = mode.value( "display", "" );
		std::string before = mode.value( "beforeAdvanced", "off" );
		beforeAdvanced     = before == "window" ? OutputMode::Window : before == "fullscreen" ? OutputMode::Fullscreen : OutputMode::Off;
	}
	else if( !options.fresh )
		outputMode = std::any_of( advancedOutput.screens.begin(), advancedOutput.screens.end(), []( const Screen& s ) { return s.device != OutputDevice::Virtual; } )
		                 ? OutputMode::Advanced
		                 : OutputMode::Off;
	for( const LaunchOptions::ClipArg& clip : options.clips )
	{
		LoadClip( clip.layer - 1, clip.column - 1, clip.path );
		Launch( clip.layer - 1, clip.column - 1, true );
	}
	for( const LaunchOptions::ClipArg& source : options.sources )
	{
		LoadGenerator( source.layer - 1, source.column - 1, source.path );
		Launch( source.layer - 1, source.column - 1, true );
	}
	if( !options.outputSetup.empty() )
	{
		std::string text, setupError;
		if( fs::u8path( options.outputSetup ).extension() == ".xml" )
			ImportResolumeOutput( options.outputSetup );
		else if( !ReadTextFile( options.outputSetup, text ) || !LoadAdvancedOutput( text, advancedOutput, setupError ) )
			std::fprintf( stderr, "Cannot load %s %s\n", options.outputSetup.c_str(), setupError.c_str() );
		outputMode = OutputMode::Advanced;
	}
	if( options.advancedOutputTab >= 0 )
	{
		panels.advancedOutput = true;
		ShowAdvancedOutputTab( options.advancedOutputTab, 0, 0 );
	}
	for( auto [ layer, column ] : options.launches )
		Launch( layer - 1, column - 1, true );
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
	savedOutput          = SaveAdvancedOutput( advancedOutput );
	history.Reset( Snapshot() );
	return true;
}

void App::Run()
{
	while( true )
	{
		if( glfwWindowShouldClose( window ) || quitRequested )
		{
			// Unsaved edits to the Advanced Output: ask first (the editor opens with the question).
			if( outputSavePrompt == 2 || !OutputUnsaved() )
				break;
			glfwSetWindowShouldClose( window, GLFW_FALSE );
			quitRequested         = false;
			panels.advancedOutput = true;
			outputSavePrompt      = 2;
		}
		glfwPollEvents();
		Frame();
	}
}

void App::Shutdown()
{
	if( !window )
		return;
	UpdateLights( true ); // the controllers' lights off
	if( !options.fresh )
		WriteTextFile( ( fs::u8path( configDir ) / "last-session.stltvj" ).u8string(), SaveComposition( composition ) );
	WriteTextFile( ( fs::u8path( configDir ) / "advanced-output.json" ).u8string(), savedOutput );
	if( !options.fresh )
		SaveOutputMode();
	glfwMakeContextCurrent( window );
	audio.Stop();
	outputs.Shutdown();
	quickOutputs.Shutdown();
	composition = Composition{}; // frees the players' GPU memory and plugin instances while the context is alive
	preview.Release();
	compositor.Shutdown();
	ReleaseBuiltinPrograms();
	ReleaseIsfPrograms();
	registry.reset(); // unloads the plugins
	ImGui_ImplOpenGL3_Shutdown();
	ImGui_ImplGlfw_Shutdown();
	ImGui::DestroyContext();
	glfwDestroyWindow( window );
	window = nullptr;
	glfwTerminate();
}

int App::CheckIsf()
{
	glfwMakeContextCurrent( window );
	int checked = 0, failed = 0;
	for( const EffectRegistry::Entry& e : registry->Entries() )
	{
		if( !e.isf )
			continue;
		++checked;
		std::string error;
		if( CheckIsfShader( e.isf, error ) )
			std::printf( "ok      %s\n", e.id.c_str() );
		else
		{
			++failed;
			std::string first = error.substr( 0, error.find( '\n', error.find( '\n' ) + 1 ) );
			std::printf( "FAILED  %s: %s\n", e.id.c_str(), first.c_str() );
		}
	}
	for( const EffectRegistry::Failure& f : registry->Failures() )
		std::printf( "skipped %s: %s\n", f.path.c_str(), f.reason.c_str() );
	std::printf( "%d ISF shaders checked, %d failed\n", checked, failed );
	return failed;
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
	StopCell( layer, column );

	Clip clip;
	clip.path   = path;
	clip.name   = fs::u8path( path ).stem().u8string();
	clip.player = std::make_unique< ClipPlayer >();
	if( clip.player->Open( path ) )
	{
		clip.audio = AudioVoice::Open( path, audio.SampleRate() );
		compositor.MakeThumbnail( *clip.player );
		const FrameSource& source = clip.player->Source();
		if( source.IsGpuTexture() || source.IsStill() )
			SetStatus( "Loaded " + clip.name );
		else if( source.IsHardwareDecoded() )
			SetStatus( "Loaded " + clip.name + " (" + source.CodecName() + ", " + source.Details() + "; HAP is still faster to jump around and layer)" );
		else
			SetStatus( "Loaded " + clip.name + " (" + source.CodecName() + ", decoded on the CPU: convert it to HAP for the best performance)" );
	}
	else
	{
		SetStatus( clip.name + ": " + clip.player->Error(), true );
	}
	// The Preferences' clip defaults (Fill, Fit...; loop; trigger; BPM sync for movies).
	if( !( clip.Loaded() && clip.player->Source().IsStill() ) )
		prefs.ApplyToClip( clip );
	else
		clip.fit = prefs.clipFit;
	composition.layers[ layer ].clips[ column ] = std::move( clip );
	Select( layer, column );
}

void App::RemoveClip( int layer, int column )
{
	if( !PlayerAt( layer, column ) && ( layer < 0 || layer >= (int)composition.layers.size() ) )
		return;
	StopCell( layer, column );
	composition.layers[ layer ].clips[ column ] = Clip{};
}

void App::MoveClip( int fromLayer, int fromColumn, int toLayer, int toColumn )
{
	int layers = (int)composition.layers.size(), columns = composition.ColumnCount();
	if( fromLayer < 0 || fromLayer >= layers || toLayer < 0 || toLayer >= layers || fromColumn < 0 || fromColumn >= columns || toColumn < 0
	    || toColumn >= columns || ( fromLayer == toLayer && fromColumn == toColumn ) )
		return;
	// Playing clips follow their cells within a row; one that changes row stops.
	for( int l = 0; l < layers; ++l )
		FinishTransition( l );
	Layer& a = composition.layers[ fromLayer ];
	Layer& b = composition.layers[ toLayer ];
	if( fromLayer == toLayer )
	{
		std::swap( a.clips[ fromColumn ], a.clips[ toColumn ] );
		for( Layer& layer : composition.layers )
			if( layer.activeDeck == composition.deck && layer.activeRow == fromLayer && ( layer.active == fromColumn || layer.active == toColumn ) )
				layer.active = layer.active == fromColumn ? toColumn : fromColumn;
	}
	else
	{
		StopCell( fromLayer, fromColumn );
		StopCell( toLayer, toColumn );
		std::swap( a.clips[ fromColumn ], b.clips[ toColumn ] );
	}
	Select( toLayer, toColumn );
}

void App::ShowDeck( int deck )
{
	if( deck < 0 || deck >= composition.DeckCount() || deck == composition.deck )
		return;
	// Waiting launches, picks and the selection name cells of the deck being left.
	queuedLaunches.clear();
	movePicked.clear();
	composition.ShowDeck( deck );
	SetStatus( "Deck: " + composition.decks[ deck ].name );
}

void App::AddDeck()
{
	int number = composition.DeckCount() + 1;
	auto taken = [ & ]( const std::string& name ) {
		return std::any_of( composition.decks.begin(), composition.decks.end(), [ & ]( const Deck& d ) { return d.name == name; } );
	};
	while( taken( "Deck " + std::to_string( number ) ) )
		++number;
	composition.AddDeck( "Deck " + std::to_string( number ) );
	ShowDeck( composition.DeckCount() - 1 );
}

void App::RemoveDeck( int deck )
{
	if( deck < 0 || deck >= composition.DeckCount() || composition.DeckCount() <= 1 )
		return;
	// What plays from it stops first.
	for( int l = 0; l < (int)composition.layers.size(); ++l )
	{
		const Layer& layer = composition.layers[ l ];
		if( layer.previous >= 0 && layer.previousDeck == deck )
			FinishTransition( l );
		if( layer.active >= 0 && layer.activeDeck == deck )
			ClearLayer( l );
	}
	queuedLaunches.clear();
	movePicked.clear();
	std::string name = composition.decks[ deck ].name;
	composition.RemoveDeck( deck );
	SetStatus( "Removed " + name );
}

void App::DeleteLayer( int layer )
{
	if( layer < 0 || layer >= (int)composition.layers.size() || composition.layers.size() <= 1 )
		return;
	// The layer stops, and so does whatever plays its row's clips on other layers.
	for( int l = 0; l < (int)composition.layers.size(); ++l )
	{
		FinishTransition( l );
		if( l == layer || ( composition.layers[ l ].active >= 0 && composition.layers[ l ].activeRow == layer ) )
			ClearLayer( l );
	}
	queuedLaunches.clear();
	movePicked.clear();
	composition.RemoveLayer( layer );
}

void App::SetMoveMode( bool on )
{
	moveMode = on;
	if( !on )
		movePicked.clear();
	SetStatus( on ? "Move: pick clips, then drag them (or use the arrows) to move them together" : "Move off" );
}

bool App::IsPicked( int row, int column ) const
{
	return std::find( movePicked.begin(), movePicked.end(), std::make_pair( row, column ) ) != movePicked.end();
}

void App::TogglePicked( int row, int column )
{
	auto it = std::find( movePicked.begin(), movePicked.end(), std::make_pair( row, column ) );
	if( it != movePicked.end() )
		movePicked.erase( it );
	else if( row >= 0 && row < (int)composition.layers.size() && column >= 0 && column < composition.ColumnCount()
	         && !composition.layers[ row ].clips[ column ].Empty() )
		movePicked.emplace_back( row, column );
}

void App::PickAllClips()
{
	movePicked.clear();
	for( int r = 0; r < (int)composition.layers.size(); ++r )
		for( int c = 0; c < composition.ColumnCount(); ++c )
			if( !composition.layers[ r ].clips[ c ].Empty() )
				movePicked.emplace_back( r, c );
}

bool App::MovePicked( int rows, int columns )
{
	int layers = (int)composition.layers.size();
	movePicked.erase( std::remove_if( movePicked.begin(), movePicked.end(),
	                                  [ & ]( const std::pair< int, int >& p ) {
		                                  return p.first < 0 || p.first >= layers || p.second < 0 || p.second >= composition.ColumnCount()
		                                         || composition.layers[ p.first ].clips[ p.second ].Empty();
	                                  } ),
	                  movePicked.end() );
	if( movePicked.empty() || ( rows == 0 && columns == 0 ) )
		return false;
	int lastColumn = 0;
	for( auto [ r, c ] : movePicked )
	{
		if( r + rows < 0 || r + rows >= layers || c + columns < 0 )
		{
			SetStatus( "Cannot move there: the clips would leave the deck" );
			return false;
		}
		lastColumn = std::max( lastColumn, c + columns );
	}
	for( auto [ r, c ] : movePicked )
	{
		int tr = r + rows, tc = c + columns;
		if( tc < composition.ColumnCount() && !composition.layers[ tr ].clips[ tc ].Empty() && !IsPicked( tr, tc ) )
		{
			SetStatus( "Cannot move there: " + composition.layers[ tr ].name + ", column " + std::to_string( tc + 1 ) + " holds another clip" );
			return false;
		}
	}
	while( composition.ColumnCount() <= lastColumn )
		composition.AddColumn();
	for( int l = 0; l < layers; ++l )
		FinishTransition( l );
	queuedLaunches.clear();
	// Take the clips out, then put them in their new cells; what plays and what is selected follow them.
	std::vector< Clip > moving;
	moving.reserve( movePicked.size() );
	for( auto [ r, c ] : movePicked )
	{
		moving.push_back( std::move( composition.layers[ r ].clips[ c ] ) );
		composition.layers[ r ].clips[ c ] = Clip{};
	}
	auto moved = [ & ]( int& row, int& column ) {
		if( IsPicked( row, column ) )
		{
			row += rows;
			column += columns;
		}
	};
	for( Layer& layer : composition.layers )
		if( layer.active >= 0 && layer.activeDeck == composition.deck )
			moved( layer.activeRow, layer.active );
	moved( composition.selectedRow, composition.selectedColumn );
	for( size_t i = 0; i < movePicked.size(); ++i )
	{
		auto& [ r, c ] = movePicked[ i ];
		r += rows;
		c += columns;
		composition.layers[ r ].clips[ c ] = std::move( moving[ i ] );
	}
	SetStatus( "Moved " + std::to_string( movePicked.size() ) + ( movePicked.size() == 1 ? " clip" : " clips" ) );
	return true;
}

void App::Launch( int row, int column, bool now, int onLayer )
{
	if( row < 0 || row >= (int)composition.layers.size() || column < 0 || column >= composition.ColumnCount() )
		return;
	// The layer it plays on: its own row's, or the selected one ("layer based" deck).
	int layer = onLayer >= 0 ? onLayer : composition.target == ClipTarget::SelectedLayer ? composition.selectedLayer : row;
	if( layer < 0 || layer >= (int)composition.layers.size() )
		return;
	Layer& l   = composition.layers[ layer ];
	Clip& clip = composition.layers[ row ].clips[ column ];
	Select( row, column );
	if( !now && composition.quantize != LaunchQuantize::Now && clip.Playable() )
	{
		// On the next beat or bar; a later launch on the same layer replaces it.
		double beats = composition.quantize == LaunchQuantize::Bar ? 4.0 : 1.0;
		double at    = ( std::floor( composition.beat / beats ) + 1 ) * beats;
		queuedLaunches.erase( std::remove_if( queuedLaunches.begin(), queuedLaunches.end(), [ & ]( const QueuedLaunch& q ) { return q.layer == layer; } ),
		                      queuedLaunches.end() );
		queuedLaunches.push_back( { layer, row, column, at } );
		return;
	}
	if( !clip.Playable() )
	{
		ClearLayer( layer );
		return;
	}
	// A clip plays on one layer at a time: elsewhere it stops.
	int showing = composition.LayerShowing( row, column );
	if( showing >= 0 && showing != layer )
		StopCell( row, column );
	int deck  = composition.deck;
	bool same = l.Plays( deck, row, column );
	if( l.active >= 0 && !same )
	{
		// Another clip plays: it hands over through the layer's transition, or stops.
		FinishTransition( layer );
		Clip* outgoing = composition.ActiveClip( layer );
		bool showing   = outgoing && ( outgoing->generator || ( outgoing->Loaded() && outgoing->player->HasFrame() ) );
		if( l.TransitionSeconds( composition.bpm ) > 0 || showing )
		{
			// A transition; or a cut, where the outgoing clip stays until the new one's first frame is decoded.
			l.previous           = l.active;
			l.previousRow        = l.activeRow;
			l.previousDeck       = l.activeDeck;
			l.transitionProgress = 0;
			l.cutPending         = l.TransitionSeconds( composition.bpm ) <= 0;
		}
		else
			ClearLayer( layer );
	}
	else if( l.FadesOut( deck, row, column ) )
	{
		// Back to the clip that was fading out: it simply goes on.
		l.previous = l.previousRow = -1;
		l.transitionProgress       = 1;
	}
	l.active     = column;
	l.activeRow  = row;
	l.activeDeck = deck;
	if( clip.generator )
		return;
	// The player's decoders start on the first frame right away; it shows as soon as it is ready (a frame or two),
	// without holding up the main thread.
	clip.player->Activate();
	clip.Restart();
	clip.player->Update( clip.CurrentFrame(), clip.PlayDirection() );
}

void App::TriggerClip( int row, int column, bool down )
{
	Clip* clip = composition.ClipAt( row, column );
	if( !clip )
		return;
	TriggerStyle style = clip->Playable() ? clip->trigger : TriggerStyle::Normal;
	bool on            = false; // playing, or waiting for its beat
	for( const QueuedLaunch& q : queuedLaunches )
		on |= q.layer >= 0 && q.row == row && q.column == column;
	for( const Layer& layer : composition.layers )
		on |= layer.Plays( composition.deck, row, column );
	auto stop = [ & ]() {
		queuedLaunches.erase( std::remove_if( queuedLaunches.begin(), queuedLaunches.end(),
		                                      [ & ]( const QueuedLaunch& q ) { return q.layer >= 0 && q.row == row && q.column == column; } ),
		                      queuedLaunches.end() );
		StopCell( row, column );
	};
	switch( style )
	{
	case TriggerStyle::Normal:
		if( down )
			Launch( row, column );
		break;
	case TriggerStyle::Toggle:
		if( down && on )
			stop();
		else if( down )
			Launch( row, column );
		break;
	case TriggerStyle::Piano:
		if( down )
			Launch( row, column );
		else
			stop();
		break;
	default: break;
	}
}

void App::LaunchColumn( int column, bool now )
{
	if( !now && composition.quantize != LaunchQuantize::Now )
	{
		double beats = composition.quantize == LaunchQuantize::Bar ? 4.0 : 1.0;
		queuedLaunches.clear(); // the column decides every layer
		queuedLaunches.push_back( { -1, -1, column, ( std::floor( composition.beat / beats ) + 1 ) * beats } );
		return;
	}
	// A column plays each row on its own layer.
	for( int layer = 0; layer < (int)composition.layers.size(); ++layer )
	{
		Layer& l = composition.layers[ layer ];
		if( column < (int)l.clips.size() && l.clips[ column ].ignoreColumn )
			continue; // left alone, and so is its layer
		if( column < (int)l.clips.size() && l.clips[ column ].Playable() )
			Launch( layer, column, true, layer );
		else
			ClearLayer( layer );
	}
	composition.activeColumn = column;
}

bool App::IsQueued( int row, int column ) const
{
	for( const QueuedLaunch& q : queuedLaunches )
		if( q.column == column && ( q.row == row || q.layer < 0 ) )
			return true;
	return false;
}

void App::RunQueuedLaunches( bool all )
{
	std::vector< QueuedLaunch > due;
	for( auto q = queuedLaunches.begin(); q != queuedLaunches.end(); )
	{
		if( all || composition.beat >= q->beat )
		{
			due.push_back( *q );
			q = queuedLaunches.erase( q );
		}
		else
			++q;
	}
	for( const QueuedLaunch& q : due )
	{
		if( q.layer < 0 )
			LaunchColumn( q.column, true );
		else
			Launch( q.row, q.column, true, q.layer );
	}
}

void App::ResyncBeat()
{
	tempoSync.Resync( TempoSync::Now(), composition.beat );
	RunQueuedLaunches( true ); // what waited for a beat goes on the new first beat
}

void App::JumpToCue( int row, int column, int cue )
{
	Clip* found = composition.ClipAt( row, column );
	if( !found || cue < 0 || cue >= kCuePoints || found->cues[ cue ] < 0 || !found->Loaded() )
		return;
	Clip& clip = *found;
	int layer  = composition.LayerShowing( row, column );
	if( layer < 0 || !composition.layers[ layer ].Plays( composition.deck, row, column ) )
		Launch( row, column, true );
	if( composition.LayerShowing( row, column ) < 0 )
		return;
	clip.JumpTo( clip.cues[ cue ] );
	clip.player->Update( clip.CurrentFrame(), clip.PlayDirection() ); // the decoders start on it now
}

void App::LaunchNextClip( int layer, int step )
{
	if( layer < 0 || layer >= (int)composition.layers.size() || composition.ColumnCount() == 0 )
		return;
	Layer& l    = composition.layers[ layer ];
	int columns = composition.ColumnCount();
	// Along the row of the clip playing (or waiting to), from it; else along the layer's own row, from the clip
	// selected there or from its edge.
	bool here = l.active >= 0 && l.activeDeck == composition.deck; // its clip may be in a deck not shown
	int row = here ? l.activeRow : layer, from = here ? l.active : -1;
	for( const QueuedLaunch& q : queuedLaunches )
		if( q.layer == layer )
			row = q.row, from = q.column;
	if( from < 0 )
		from = composition.selectedRow == row && composition.selectedColumn >= 0 ? composition.selectedColumn : step > 0 ? -1 : columns;
	for( int i = 1; i <= columns; ++i )
	{
		int c = ( ( from + step * i ) % columns + columns ) % columns;
		if( composition.layers[ row ].clips[ c ].Playable() )
		{
			Launch( row, c, false, layer );
			return;
		}
	}
}

void App::LaunchNextColumn( int step )
{
	int columns = composition.ColumnCount();
	if( columns == 0 )
		return;
	int from = composition.activeColumn >= 0 ? composition.activeColumn : step > 0 ? -1 : columns;
	LaunchColumn( ( ( from + step ) % columns + columns ) % columns );
}

void App::SetAllDirections( int direction )
{
	if( direction == 0 )
	{
		composition.paused = true;
		return;
	}
	composition.paused = false;
	for( int l = 0; l < (int)composition.layers.size(); ++l )
		if( Clip* clip = composition.ActiveClip( l ) )
		{
			clip->direction = direction;
			clip->finished  = false;
		}
}

void App::FinishTransition( int layer )
{
	if( layer < 0 || layer >= (int)composition.layers.size() )
		return;
	Layer& l       = composition.layers[ layer ];
	Clip* outgoing = composition.OutgoingClip( layer );
	if( outgoing && outgoing != composition.ActiveClip( layer ) && outgoing->player )
		outgoing->player->Deactivate();
	l.previous = l.previousRow = -1;
	l.transitionProgress       = 1;
	l.cutPending               = false;
}

void App::ClearLayer( int layer )
{
	if( layer < 0 || layer >= (int)composition.layers.size() )
		return;
	FinishTransition( layer );
	Layer& l = composition.layers[ layer ];
	if( Clip* clip = composition.ActiveClip( layer ); clip && clip->player )
		clip->player->Deactivate();
	l.active = l.activeRow = -1;
	queuedLaunches.erase( std::remove_if( queuedLaunches.begin(), queuedLaunches.end(), [ & ]( const QueuedLaunch& q ) { return q.layer == layer; } ),
	                      queuedLaunches.end() );
}

void App::Select( int row, int column )
{
	composition.selectedRow    = row;
	composition.selectedColumn = column;
	// With clips playing on their own row's layer, picking a clip picks its layer too; layer based, the layer
	// stays the one the clips go to.
	if( composition.target == ClipTarget::OwnLayer )
		composition.selectedLayer = row;
}

void App::StopCell( int row, int column )
{
	for( int l = 0; l < (int)composition.layers.size(); ++l )
	{
		Layer& layer = composition.layers[ l ];
		if( layer.FadesOut( composition.deck, row, column ) )
			FinishTransition( l );
		if( layer.Plays( composition.deck, row, column ) )
			ClearLayer( l );
	}
}

void App::NewComposition()
{
	composition     = Composition::MakeDefault();
	compositionPath.clear();
	movePicked.clear();
	history.Reset( Snapshot() );
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
	movePicked.clear();
	RestoreEffects();
	history.Reset( Snapshot() );
	int failures = 0;
	composition.ForEachClip( [ & ]( Clip& clip, int, int, int ) {
		if( clip.path.empty() )
			return;
		clip.player = std::make_unique< ClipPlayer >();
		if( clip.player->Open( clip.path ) )
		{
			compositor.MakeThumbnail( *clip.player );
			clip.audio = AudioVoice::Open( clip.path, audio.SampleRate() );
		}
		else
			failures++;
	} );
	// The clips that played when it was saved play again (from their start).
	for( int i = 0; i < (int)composition.layers.size(); ++i )
	{
		Layer& l = composition.layers[ i ];
		if( l.active < 0 )
			continue;
		Clip* clip = composition.ActiveClip( i );
		if( !clip || !clip->Playable() )
		{
			l.active = l.activeRow = -1;
			continue;
		}
		if( clip->generator )
			continue;
		clip->player->Activate();
		clip->Restart();
		clip->player->Update( clip->CurrentFrame(), clip->PlayDirection() );
	}
	compositionPath = path;
	int missing = 0;
	auto countMissing = [ & ]( const EffectChain& chain ) {
		for( const EffectSlot& slot : chain )
			missing += slot.effect ? 0 : 1;
	};
	countMissing( composition.effects );
	for( const Layer& layer : composition.layers )
		countMissing( layer.effects );
	composition.ForEachClip( [ & ]( const Clip& clip, int, int, int ) {
		countMissing( clip.effects );
		missing += clip.IsGenerator() && !clip.generator ? 1 : 0;
	} );
	if( missing )
		failures += missing;
	SetStatus( failures ? "Opened " + path + " (" + std::to_string( failures ) + " clips missing or unsupported)" : "Opened " + path, failures > 0 );
	return true;
}

History::State App::Snapshot() const
{
	// Which deck is shown is not an undo step.
	bool external = tempoSync.Following() != TempoSync::Source::Internal;
	return { SaveComposition( composition, false, external ? undoBpm : -1 ), SaveAdvancedOutput( advancedOutput ) };
}

namespace
{
// Every animation off: an effect taking an earlier state gets exactly the animations that state had.
void ClearAnimations( Effect& effect )
{
	for( size_t i = 0; i < effect.Params().size(); ++i )
		effect.Anim( (int)i ) = Animation{};
}

// Hands the live effects of `old` to the matching slots of `fresh` (same effect, in order), with fresh's settings.
void ReuseEffects( EffectChain& fresh, EffectChain* old )
{
	if( !old )
		return;
	std::vector< bool > used( old->size(), false );
	for( EffectSlot& slot : fresh )
		for( size_t i = 0; i < old->size(); ++i )
			if( !used[ i ] && ( *old )[ i ].effect && ( *old )[ i ].effect->Id() == slot.missingId )
			{
				used[ i ]   = true;
				slot.effect = std::move( ( *old )[ i ].effect );
				ClearAnimations( *slot.effect );
				LoadEffectState( *slot.effect, slot.savedState );
				slot.missingId.clear();
				slot.savedState.clear();
				break;
			}
}
}

void App::RestoreComposition( const std::string& text )
{
	movePicked.clear(); // the picks were cells of the composition being replaced
	Composition fresh;
	std::string error;
	if( !LoadComposition( text, fresh, error ) )
		return;
	// Transitions end and waiting launches are dropped: the clips they name may move.
	for( int l = 0; l < (int)composition.layers.size(); ++l )
		FinishTransition( l );
	queuedLaunches.clear();
	// The clips still there keep their players and generators: matched where they were, else anywhere.
	std::vector< Clip* > taken;
	auto same = [ & ]( const Clip& a, const Clip& b ) {
		return !b.Empty() && a.path == b.path && a.generatorId == b.generatorId && std::find( taken.begin(), taken.end(), &b ) == taken.end();
	};
	auto find = [ & ]( const Clip& want, int d, int l, int c ) -> Clip* {
		if( Clip* there = composition.ClipIn( d, l, c ); there && same( want, *there ) )
			return there;
		Clip* found = nullptr;
		composition.ForEachClip( [ & ]( Clip& clip, int, int, int ) {
			if( !found && same( want, clip ) )
				found = &clip;
		} );
		return found;
	};
	// The deck shown stays shown (showing a deck is not an undo step).
	fresh.ShowDeck( std::min( composition.deck, fresh.DeckCount() - 1 ) );
	// The clip each layer plays, to follow it if the restored state has it in another cell.
	struct Cell
	{
		int deck = -1, row = -1, column = -1;
	};
	std::vector< const Clip* > playing( composition.layers.size(), nullptr );
	std::vector< Cell > follow( composition.layers.size() );
	for( int l = 0; l < (int)composition.layers.size(); ++l )
		playing[ l ] = composition.ActiveClip( l );
	for( int l = 0; l < (int)fresh.layers.size(); ++l )
		ReuseEffects( fresh.layers[ l ].effects, l < (int)composition.layers.size() ? &composition.layers[ l ].effects : nullptr );
	fresh.ForEachClip( [ & ]( Clip& clip, int d, int l, int c ) {
		if( clip.Empty() )
			return;
		Clip* old = find( clip, d, l, c );
		if( !old )
			return;
		taken.push_back( old );
		for( int p = 0; p < (int)playing.size(); ++p )
			if( old == playing[ p ] )
				follow[ p ] = { d, l, c };
		clip.player             = std::move( old->player );
		clip.audio              = std::move( old->audio );
		clip.generatorThumbnail = std::move( old->generatorThumbnail );
		clip.time               = old->time;
		clip.bounceSign         = old->bounceSign;
		clip.finished           = old->finished;
		if( old->generator )
		{
			clip.generator = std::move( old->generator );
			ClearAnimations( *clip.generator );
			LoadEffectState( *clip.generator, clip.generatorState );
		}
		ReuseEffects( clip.effects, &old->effects );
	} );
	ReuseEffects( fresh.effects, &composition.effects );

	// What plays and what is selected stays, where it still exists.
	for( int l = 0; l < (int)fresh.layers.size() && l < (int)composition.layers.size(); ++l )
	{
		Layer& layer = fresh.layers[ l ];
		const Layer& was = composition.layers[ l ];
		Cell cell        = follow[ l ].row >= 0 ? follow[ l ] : Cell{ was.activeDeck, was.activeRow, was.active };
		Clip* clip       = fresh.ClipIn( cell.deck, cell.row, cell.column );
		bool kept        = clip && !clip->Empty();
		layer.active     = kept ? cell.column : -1;
		layer.activeRow  = kept ? cell.row : -1;
		layer.activeDeck = kept ? cell.deck : 0;
		layer.solo      = composition.layers[ l ].solo;
	}
	// The timeline: its playhead goes on, and the clips still there keep their players, sounds and effects.
	{
		Timeline& was = composition.timeline;
		Timeline& now = fresh.timeline;
		now.position  = was.position;
		now.playing   = was.playing;
		now.black     = was.black;
		std::vector< const Clip* > used;
		auto free     = [ & ]( const Clip& c ) { return std::find( used.begin(), used.end(), &c ) == used.end(); };
		for( int t = 0; t < (int)now.tracks.size(); ++t )
		{
			if( t < (int)was.tracks.size() )
				ReuseEffects( now.tracks[ t ].effects, &was.tracks[ t ].effects );
			for( int i = 0; i < (int)now.tracks[ t ].clips.size(); ++i )
			{
				Clip& clip = now.tracks[ t ].clips[ i ].clip;
				Clip* old  = nullptr;
				auto fits  = [ & ]( Clip& c ) { return c.path == clip.path && c.generatorId == clip.generatorId && free( c ); };
				if( t < (int)was.tracks.size() && i < (int)was.tracks[ t ].clips.size() && fits( was.tracks[ t ].clips[ i ].clip ) )
					old = &was.tracks[ t ].clips[ i ].clip;
				for( TimelineTrack& track : was.tracks )
					for( TimelineClip& c : track.clips )
						if( !old && fits( c.clip ) )
							old = &c.clip;
				if( !old )
					continue;
				used.push_back( old );
				clip.player             = std::move( old->player );
				clip.audio              = std::move( old->audio );
				clip.generatorThumbnail = std::move( old->generatorThumbnail );
				if( old->generator )
				{
					clip.generator = std::move( old->generator );
					ClearAnimations( *clip.generator );
					LoadEffectState( *clip.generator, clip.generatorState );
				}
				ReuseEffects( clip.effects, &old->effects );
			}
		}
	}
	fresh.beat           = composition.beat;
	fresh.paused         = composition.paused;
	if( tempoSync.Following() != TempoSync::Source::Internal )
		fresh.bpm = composition.bpm; // the tempo is Link's or the clock's, not an undo step
	fresh.selectedLayer  = std::clamp( composition.selectedLayer, 0, std::max( (int)fresh.layers.size() - 1, 0 ) );
	fresh.selectedRow    = std::clamp( composition.selectedRow, 0, std::max( (int)fresh.layers.size() - 1, 0 ) );
	fresh.selectedColumn = composition.selectedColumn < fresh.ColumnCount() ? composition.selectedColumn : -1;
	fresh.activeColumn   = composition.activeColumn < fresh.ColumnCount() ? composition.activeColumn : -1;
	composition          = std::move( fresh );

	RestoreEffects(); // creates the effects and generators that were not there
	composition.ForEachClip( [ & ]( Clip& clip, int, int, int ) {
		if( !clip.path.empty() && !clip.player )
		{
			clip.player = std::make_unique< ClipPlayer >();
			if( clip.player->Open( clip.path ) )
			{
				compositor.MakeThumbnail( *clip.player );
				clip.audio = AudioVoice::Open( clip.path, audio.SampleRate() );
			}
		}
	} );
}

bool App::Undo()
{
	if( ImGui::GetDragDropPayload() )
		return false; // a drag holds pointers into the composition
	History::State before;
	if( !history.Undo( Snapshot(), before ) )
		return false;
	std::string label  = history.RedoLabel(); // the step just undone
	History::State now = Snapshot();
	if( before.composition != now.composition )
		RestoreComposition( before.composition );
	if( before.output != now.output )
	{
		std::string error;
		LoadAdvancedOutput( before.output, advancedOutput, error );
	}
	history.Settle( Snapshot() );
	SetStatus( "Undo " + label );
	return true;
}

bool App::Redo()
{
	if( ImGui::GetDragDropPayload() )
		return false; // a drag holds pointers into the composition
	History::State after;
	if( !history.Redo( Snapshot(), after ) )
		return false;
	History::State now = Snapshot();
	if( after.composition != now.composition )
		RestoreComposition( after.composition );
	if( after.output != now.output )
	{
		std::string error;
		LoadAdvancedOutput( after.output, advancedOutput, error );
	}
	history.Settle( Snapshot() );
	SetStatus( "Redo " + history.UndoLabel() );
	return true;
}

void App::SaveControlSettings() const
{
	WriteTextFile( ( fs::u8path( configDir ) / "control.json" ).u8string(), control.Save() );
}

std::string App::ShortcutPresetDirectory() const
{
	return ( fs::u8path( configDir ) / "shortcut-presets" ).u8string();
}

std::vector< std::string > App::ShortcutPresets() const
{
	std::vector< std::string > names;
	std::error_code error;
	for( const fs::directory_entry& entry : fs::directory_iterator( fs::u8path( ShortcutPresetDirectory() ), error ) )
		if( entry.path().extension() == ".json" )
			names.push_back( entry.path().stem().u8string() );
	std::sort( names.begin(), names.end() );
	return names;
}

bool App::SaveShortcutPreset( const std::string& name )
{
	if( name.empty() || name.find_first_of( "/\\:*?\"<>|" ) != std::string::npos )
	{
		SetStatus( "Give the shortcut preset a name without / \\ : * ? \" < > |", true );
		return false;
	}
	std::error_code ignored;
	fs::create_directories( fs::u8path( ShortcutPresetDirectory() ), ignored );
	std::string text = control.SaveShortcuts();
	if( !WriteTextFile( ( fs::u8path( ShortcutPresetDirectory() ) / fs::u8path( name + ".json" ) ).u8string(), text ) )
	{
		SetStatus( "Could not save the shortcut preset " + name, true );
		return false;
	}
	control.preset     = name;
	shortcutPresetText = text;
	SaveControlSettings();
	SetStatus( "Shortcuts saved as \"" + name + "\"" );
	return true;
}

bool App::LoadShortcutPreset( const std::string& name )
{
	std::string text;
	if( !ReadTextFile( ( fs::u8path( ShortcutPresetDirectory() ) / fs::u8path( name + ".json" ) ).u8string(), text ) || !control.LoadShortcuts( text ) )
	{
		SetStatus( "Could not load the shortcut preset " + name, true );
		return false;
	}
	control.preset     = name;
	shortcutPresetText = control.SaveShortcuts();
	learnTarget.clear();
	SaveControlSettings();
	SetStatus( "Shortcuts \"" + name + "\" loaded: " + std::to_string( control.keys.size() ) + " keys, " + std::to_string( control.midi.size() ) + " MIDI" );
	return true;
}

bool App::DeleteShortcutPreset( const std::string& name )
{
	std::error_code error;
	if( !fs::remove( fs::u8path( ShortcutPresetDirectory() ) / fs::u8path( name + ".json" ), error ) )
		return false;
	if( control.preset == name )
	{
		// The maps stay; they just no longer come from a preset.
		control.preset     = "";
		shortcutPresetText = ControlSettings{}.SaveShortcuts();
		SaveControlSettings();
	}
	SetStatus( "Deleted the shortcut preset " + name );
	return true;
}

bool App::ExportShortcuts( const std::string& path )
{
	if( !WriteTextFile( path, control.SaveShortcuts() ) )
	{
		SetStatus( "Could not write " + path, true );
		return false;
	}
	SetStatus( "Shortcuts exported to " + path );
	return true;
}

bool App::ImportShortcuts( const std::string& path )
{
	std::string text;
	if( !ReadTextFile( path, text ) || !control.LoadShortcuts( text ) )
	{
		SetStatus( "Not an STLT-VJ shortcut file: " + path, true );
		return false;
	}
	// It becomes a preset of its own, named after the file.
	std::string name = fs::u8path( path ).stem().u8string();
	learnTarget.clear();
	SaveShortcutPreset( name );
	SetStatus( "Shortcuts imported from " + path + " (preset \"" + name + "\")" );
	return true;
}

void App::PollRemote()
{
	// Devices come and go: look again every couple of seconds.
	double now = glfwGetTime();
	if( now - lastRemoteConfigure > 2 )
	{
		remote.Configure( control );
		lastRemoteConfigure = now;
	}
	char value[ 32 ];
	for( RemoteInputs::Event& e : remote.Poll() )
	{
		if( !e.midi )
		{
			bool has = !e.values.empty();
			value[ 0 ] = 0;
			if( has )
				std::snprintf( value, sizeof( value ), " %.3g", e.values[ 0 ] );
			lastControl = "OSC " + e.address + value;
			// Every value above 0 presses, as before; a 0 after one releases.
			float v        = has ? e.values[ 0 ] : 1.0f;
			float previous = v <= 0 ? lastOscValues[ e.address ] : 0.0f;
			lastOscValues[ e.address ] = v;
			if( !ApplyControl( e.address, has, v, false, previous ) )
				lastControl += "  (not an STLT-VJ address)";
			continue;
		}
		int id         = (int)e.key.type * 4096 + e.key.channel * 128 + e.key.number;
		float previous = lastMidiValues[ id ];
		lastMidiValues[ id ] = e.value;
		std::snprintf( value, sizeof( value ), " %.2f", e.value );
		lastControl = "MIDI " + MidiKeyName( e.key ) + value + "  (" + e.device + ")";
		if( learning && !learnKeys && !learnTarget.empty() && e.value > 0 )
		{
			// One control per key: learning again moves it.
			control.midi.erase( std::remove_if( control.midi.begin(), control.midi.end(), [ & ]( const MidiMapping& m ) { return m.key == e.key; } ),
			                    control.midi.end() );
			control.midi.push_back( { e.key, learnTarget, e.device } );
			SetStatus( "Mapped " + MidiKeyName( e.key ) + " to " + learnTarget );
			learnTarget.clear();
			SaveControlSettings();
			continue;
		}
		for( const MidiMapping& m : control.midi )
			if( m.key == e.key )
				ApplyControl( m.address, true, e.value, e.key.type == MidiKey::Type::Note, previous );
	}
}

void App::UpdateAudioInput( double now, double dt )
{
	bool wanted = ( AudioAnimationWanted( 10 ) || audioInputViewed > 0 ) && !std::getenv( "STLTVJ_NO_AUDIO" );
	if( audioInputViewed > 0 )
		--audioInputViewed;
	if( wanted && !audioInput.Running() && now - audioInputTried > 10 )
	{
		audioInputTried = now; // one try every ten seconds while it cannot open
		if( !audioInput.Start( prefs.audioInput ) )
			SetStatus( "Audio input: " + audioInput.Status(), true );
	}
	else if( !wanted && audioInput.Running() )
		audioInput.Stop();
	audioInput.gain = prefs.audioInputGain;
	AnimationClock clock;
	clock.seconds = now - startTime;
	if( audioInput.Running() )
	{
		audioInput.Analyse( dt );
		for( int b = 0; b < AudioInput::BandCount; ++b )
			clock.audio[ b ] = audioInput.Value( (AudioInput::Band)b );
	}
	SetAnimationClock( clock );
}

Light App::LightFor( const std::string& address )
{
	using Kind = ControlTarget::Kind;
	ControlTarget t;
	if( !ParseControlAddress( address, t ) )
		return Light::Off;
	auto queued = [ & ]( int row, int column ) {
		for( const QueuedLaunch& q : queuedLaunches )
			if( q.column == column && ( row < 0 ? q.layer < 0 : q.row == row ) )
				return true;
		return false;
	};
	switch( t.kind )
	{
	case Kind::ClipConnect:
	{
		Clip* clip = composition.ClipAt( t.layer, t.column );
		if( !clip || !clip->Playable() )
			return Light::Off;
		if( queued( t.layer, t.column ) )
			return Light::Waiting;
		return composition.LayerShowing( t.layer, t.column ) >= 0 ? Light::On : Light::Ready;
	}
	case Kind::ColumnConnect:
	{
		if( queued( -1, t.column ) )
			return Light::Waiting;
		if( composition.activeColumn == t.column )
			return Light::On;
		for( int row = 0; row < (int)composition.layers.size(); ++row )
			if( Clip* clip = composition.ClipAt( row, t.column ); clip && clip->Playable() )
				return Light::Ready;
		return Light::Off;
	}
	case Kind::LayerClear:
		return t.layer >= 0 && t.layer < (int)composition.layers.size() && composition.layers[ t.layer ].active >= 0 ? Light::Ready : Light::Off;
	case Kind::LayerBypass:
		return t.layer >= 0 && t.layer < (int)composition.layers.size() && composition.layers[ t.layer ].bypass ? Light::On : Light::Off;
	case Kind::LayerSolo:
		return t.layer >= 0 && t.layer < (int)composition.layers.size() && composition.layers[ t.layer ].solo ? Light::On : Light::Off;
	case Kind::CompositionBypass: return composition.bypass ? Light::On : Light::Off;
	case Kind::Pause: return composition.paused ? Light::On : Light::Off;
	case Kind::TimelinePlay: return composition.timeline.playing ? Light::On : Light::Off;
	case Kind::DeckSelect:
		return t.slot == composition.deck ? Light::On : t.slot >= 0 && t.slot < (int)composition.decks.size() ? Light::Ready : Light::Off;
	case Kind::TimelineCue:
		if( t.slot < 0 || t.slot >= (int)composition.timeline.blocks.size() )
			return Light::Off;
		return composition.timeline.BlockAt( composition.timeline.position ) == t.slot ? Light::On : Light::Ready;
	case Kind::ClipCue:
	{
		Clip* clip = composition.ClipAt( t.layer, t.column );
		return clip && t.slot >= 0 && t.slot < kCuePoints && clip->cues[ t.slot ] >= 0 ? Light::Ready : Light::Off;
	}
	case Kind::TempoTap: // blinks with the beat
		return composition.beat - std::floor( composition.beat ) < 0.15 ? Light::On : Light::Off;
	default:
	{
		// Other buttons are there to press: lit dimly; faders have no light.
		return t.IsButton() || t.IsSwitch() ? Light::Ready : Light::Off;
	}
	}
}

void App::UpdateLights( bool off )
{
	if( remote.OutputsVersion() != lightsVersion )
	{
		lightsSent.clear(); // an output (re)opened: everything again
		lightsVersion = remote.OutputsVersion();
	}
	std::vector< std::string > inputs = remote.OpenInputs();
	std::set< std::string > mapped;
	for( const MidiMapping& m : control.midi )
	{
		Light light = off ? Light::Off : LightFor( m.address );
		for( const std::string& device : inputs )
		{
			if( !m.device.empty() && m.device != device )
				continue;
			std::vector< unsigned char > message = LightMessage( device, m.key, light );
			if( message.empty() )
				continue;
			std::string at = device + "|" + std::to_string( (int)m.key.type ) + ":" + std::to_string( m.key.channel ) + ":" + std::to_string( m.key.number );
			mapped.insert( at );
			auto sent = lightsSent.find( at );
			if( sent != lightsSent.end() && sent->second.message == message )
				continue;
			if( remote.SendMidi( device, message ) )
				lightsSent[ at ] = { device, m.key, message };
		}
	}
	// Keys no longer mapped (or on an input that went) go dark.
	for( auto it = lightsSent.begin(); it != lightsSent.end(); )
	{
		if( mapped.count( it->first ) )
		{
			++it;
			continue;
		}
		remote.SendMidi( it->second.device, LightMessage( it->second.device, it->second.key, Light::Off ) );
		it = lightsSent.erase( it );
	}
}

namespace
{
// Keys that can be mapped: not the modifiers (they go with a key), Escape (it leaves mapping), Space alone
// (pause), the app's own Ctrl shortcuts, the mouse or game pads.
bool MappableKey( ImGuiKey key, bool ctrl, bool shift, bool alt )
{
	if( key < ImGuiKey_Tab || key >= ImGuiKey_GamepadStart || key == ImGuiKey_Escape || key == ImGuiKey_Menu
	    || ( key >= ImGuiKey_LeftCtrl && key <= ImGuiKey_RightSuper ) || ( key == ImGuiKey_Space && !ctrl && !shift && !alt ) )
		return false;
	if( ctrl )
		for( ImGuiKey own : { ImGuiKey_Z, ImGuiKey_Y, ImGuiKey_S, ImGuiKey_O, ImGuiKey_I, ImGuiKey_M, ImGuiKey_K, ImGuiKey_C, ImGuiKey_X, ImGuiKey_V, ImGuiKey_D, ImGuiKey_E, ImGuiKey_Comma } )
			if( key == own )
				return false;
	return !( ctrl && shift && key == ImGuiKey_A );
}
}

void App::ToggleMapping( bool keys )
{
	learning    = !( learning && learnKeys == keys );
	learnKeys   = keys;
	learnTarget.clear();
}

void App::PollKeyboard()
{
	// A mapped key acts like a MIDI note: 1 while it is held, 0 when it is let go (buttons act on the press,
	// switches flip). Nothing while a text field has the keyboard.
	ImGuiIO& io = ImGui::GetIO();
	for( auto held = keysHeld.begin(); held != keysHeld.end(); )
	{
		if( ImGui::IsKeyDown( (ImGuiKey)held->first ) )
		{
			++held;
			continue;
		}
		ApplyControl( held->second, true, 0.0f, true, 1.0f );
		held = keysHeld.erase( held );
	}
	if( io.WantTextInput || ImGui::IsPopupOpen( "", ImGuiPopupFlags_AnyPopupId ) )
		return;
	for( int k = ImGuiKey_Tab; k < ImGuiKey_GamepadStart; ++k )
	{
		ImGuiKey key = (ImGuiKey)k;
		if( !ImGui::IsKeyPressed( key, false ) || !MappableKey( key, io.KeyCtrl, io.KeyShift, io.KeyAlt ) )
			continue;
		KeyChord chord{ ImGui::GetKeyName( key ), io.KeyCtrl, io.KeyShift, io.KeyAlt };
		if( learning )
		{
			if( !learnKeys || learnTarget.empty() )
				continue;
			// One control per key: learning again moves it.
			control.keys.erase( std::remove_if( control.keys.begin(), control.keys.end(), [ & ]( const KeyMapping& m ) { return m.chord == chord; } ),
			                    control.keys.end() );
			control.keys.push_back( { chord, learnTarget } );
			SetStatus( "Mapped the key " + KeyChordName( chord ) + " to " + learnTarget );
			learnTarget.clear();
			SaveControlSettings();
			continue;
		}
		for( const KeyMapping& m : control.keys )
			if( m.chord == chord )
			{
				lastControl = "Key " + KeyChordName( chord );
				ApplyControl( m.address, true, 1.0f, true, 0.0f );
				keysHeld.push_back( { k, m.address } );
			}
	}
}

bool App::ApplyControl( const std::string& address, bool hasValue, float value, bool momentary, float previous )
{
	using Kind = ControlTarget::Kind;
	ControlTarget t;
	if( !ParseControlAddress( address, t ) )
		return false;
	bool press   = !hasValue || ( value > 0 && previous <= 0 );
	float unit   = std::clamp( value, 0.0f, 1.0f );
	bool layerOk = t.layer >= 0 && t.layer < (int)composition.layers.size();
	bool columnOk = t.column >= 0 && t.column < composition.ColumnCount();
	// A switch flips on a press (a key, or a message without value) and otherwise follows the value.
	auto flip = [ & ]( bool& flag ) {
		if( !hasValue || momentary )
		{
			if( press )
				flag = !flag;
		}
		else
			flag = value >= 0.5f;
	};
	switch( t.kind )
	{
	case Kind::ClipConnect:
		// A release (key up, note off, value back to 0) matters to Piano clips.
		if( press && layerOk && columnOk )
			TriggerClip( t.layer, t.column, true );
		else if( hasValue && value <= 0 && previous > 0 && layerOk && columnOk )
			TriggerClip( t.layer, t.column, false );
		return layerOk && columnOk;
	case Kind::ColumnConnect:
		if( press && columnOk )
			LaunchColumn( t.column );
		return columnOk;
	case Kind::TimelineCue:
		if( press )
			TimelineCue( t.slot );
		return t.slot < (int)composition.timeline.blocks.size();
	case Kind::TimelineNextCue:
	case Kind::TimelinePrevCue:
		if( press )
			TimelineNextCue( t.kind == Kind::TimelineNextCue ? 1 : -1 );
		return true;
	case Kind::TimelinePlay:
	{
		bool playing = composition.timeline.playing;
		flip( playing );
		TimelinePlay( playing );
		return true;
	}
	case Kind::TimelineStop:
		if( press )
			TimelineStop();
		return true;
	case Kind::DeckSelect:
		if( press && t.slot < composition.DeckCount() )
			ShowDeck( t.slot );
		return t.slot < composition.DeckCount();
	case Kind::NextDeck:
	case Kind::PrevDeck:
		if( press )
		{
			int count = composition.DeckCount();
			ShowDeck( ( composition.deck + ( t.kind == Kind::NextDeck ? 1 : -1 ) + count ) % count );
		}
		return true;
	case Kind::LayerClear:
		if( press && layerOk )
			ClearLayer( t.layer );
		return layerOk;
	case Kind::LayerBypass:
		if( layerOk )
			flip( composition.layers[ t.layer ].bypass );
		return layerOk;
	case Kind::LayerSolo:
		if( layerOk )
			flip( composition.layers[ t.layer ].solo );
		return layerOk;
	case Kind::LayerOpacity:
		if( layerOk )
			composition.layers[ t.layer ].opacity = unit;
		return layerOk;
	case Kind::LayerVolume:
		if( layerOk )
			composition.layers[ t.layer ].volume = unit;
		return layerOk;
	case Kind::ClipVolume:
		if( layerOk && columnOk )
			composition.layers[ t.layer ].clips[ t.column ].volume = unit;
		return layerOk && columnOk;
	case Kind::ClipCue:
		if( press && layerOk && columnOk )
			JumpToCue( t.layer, t.column, t.slot );
		return layerOk && columnOk;
	case Kind::LayerCue:
		if( press && layerOk && composition.layers[ t.layer ].active >= 0 )
		{
			const Layer& l = composition.layers[ t.layer ];
			Clip* clip     = composition.ActiveClip( t.layer );
			if( l.activeDeck == composition.deck )
				JumpToCue( l.activeRow, l.active, t.slot );
			else if( clip && clip->Loaded() && t.slot >= 0 && t.slot < kCuePoints && clip->cues[ t.slot ] >= 0 )
			{
				// Its clip is in a deck not shown: it jumps where it plays.
				clip->JumpTo( clip->cues[ t.slot ] );
				clip->player->Update( clip->CurrentFrame(), clip->PlayDirection() );
			}
		}
		return layerOk;
	case Kind::LayerTransition:
		if( layerOk )
		{
			Layer& layer             = composition.layers[ t.layer ];
			layer.transitionDuration = unit * ( layer.transitionInBeats ? 16.0f : 10.0f );
		}
		return layerOk;
	case Kind::LayerMaster:
		if( layerOk )
			composition.layers[ t.layer ].master = unit;
		return layerOk;
	case Kind::LayerSpeed:
		if( layerOk )
			composition.layers[ t.layer ].speed = unit * 4;
		return layerOk;
	case Kind::LayerNextClip:
	case Kind::LayerPrevClip:
		if( press && layerOk )
			LaunchNextClip( t.layer, t.kind == Kind::LayerNextClip ? 1 : -1 );
		return layerOk;
	case Kind::SelectedNextClip:
	case Kind::SelectedPrevClip:
		if( press )
			LaunchNextClip( composition.selectedLayer, t.kind == Kind::SelectedNextClip ? 1 : -1 );
		return true;
	case Kind::NextColumn:
	case Kind::PrevColumn:
		if( press )
			LaunchNextColumn( t.kind == Kind::NextColumn ? 1 : -1 );
		return true;
	case Kind::Crossfader: composition.crossfader = unit; return true;
	case Kind::CompositionBypass: flip( composition.bypass ); return true;
	case Kind::AudioVolume: composition.audioVolume = unit; return true;
	case Kind::Master: composition.master = unit; return true;
	case Kind::Speed: composition.speed = unit * 4; return true;
	case Kind::Tempo: composition.bpm = value <= 1 ? 20.0 + unit * 480.0 : std::clamp( (double)value, 20.0, 500.0 ); return true;
	case Kind::TempoTap:
		if( press )
			TapTempo();
		return true;
	case Kind::Resync:
		if( press )
			ResyncBeat();
		return true;
	case Kind::Pause: flip( composition.paused ); return true;
	case Kind::Stop:
		if( press )
		{
			for( int l = 0; l < (int)composition.layers.size(); ++l )
				ClearLayer( l );
			composition.activeColumn = -1;
		}
		return true;
	case Kind::EffectParam:
	{
		EffectChain* chain = t.layer < 0 ? &composition.effects
		                     : !layerOk   ? nullptr
		                     : t.column < 0 ? &composition.layers[ t.layer ].effects
		                     : columnOk     ? &composition.layers[ t.layer ].clips[ t.column ].effects
		                                    : nullptr;
		if( !chain || t.slot >= (int)chain->size() || !( *chain )[ t.slot ].effect )
			return false;
		Effect& effect = *( *chain )[ t.slot ].effect;
		for( int i = 0; i < (int)effect.Params().size(); ++i )
		{
			const ParamInfo& p = effect.Params()[ i ];
			if( AddressName( p.name ) != t.param )
				continue;
			switch( p.kind )
			{
			case ParamKind::Bool:
			{
				bool on = effect.Value( i ) >= 0.5f;
				flip( on );
				effect.SetValue( i, on ? 1.0f : 0.0f );
				break;
			}
			case ParamKind::Event:
				if( press )
					effect.Trigger( i );
				break;
			case ParamKind::Option:
				if( !p.optionValues.empty() )
					effect.SetValue( i, p.optionValues[ std::min( (size_t)std::lround( unit * ( p.optionValues.size() - 1 ) ), p.optionValues.size() - 1 ) ] );
				break;
			case ParamKind::Text:
			case ParamKind::File:
			case ParamKind::Buffer: return false;
			default:
			{
				float v = p.min + unit * ( p.max - p.min );
				effect.SetValue( i, p.kind == ParamKind::Integer ? std::round( v ) : v );
			}
			}
			return true;
		}
		return false;
	}
	case Kind::None: break;
	}
	return false;
}

void App::RestoreEffects()
{
	registry->Restore( composition.effects );
	for( Layer& layer : composition.layers )
		registry->Restore( layer.effects );
	composition.ForEachClip( [ & ]( Clip& clip, int, int, int ) {
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
	} );
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
	StopCell( layer, column );
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

namespace
{
// Shows `screen` on `device`; a simple one-slice screen also takes the display's resolution.
void PlaceScreen( Screen& screen, OutputDevice device, const std::string& display )
{
	screen.device  = device;
	screen.display = display;
	screen.enabled = true;
	GLFWmonitor* monitor = device == OutputDevice::Display ? OutputWindow::FindDisplay( display ) : nullptr;
	if( monitor )
		for( const OutputWindow::Display& d : OutputWindow::Displays() )
			if( d.monitor == monitor && screen.slices.size() <= 1 && ( screen.width != d.width || screen.height != d.height ) )
			{
				// A simple one-slice screen follows the display's resolution.
				screen.width  = d.width;
				screen.height = d.height;
				if( screen.slices.size() == 1 && !screen.slices[ 0 ].polygon && !screen.slices[ 0 ].mask && screen.slices[ 0 ].warp == WarpMode::Perspective )
					screen.slices[ 0 ].FitOutput( (float)d.width, (float)d.height );
			}
}
}

void App::ShowScreenOn( size_t index, OutputDevice device, const std::string& display )
{
	if( advancedOutput.screens.empty() )
		advancedOutput = AdvancedOutputSetup::MakeDefault( composition.width, composition.height );
	if( index >= advancedOutput.screens.size() )
		return;
	// From the Output panel this is a choice of its own: saved at once, unless the editor holds unsaved edits.
	bool clean = !OutputUnsaved();
	PlaceScreen( advancedOutput.screens[ index ], device, display );
	if( clean )
		SaveOutputSetup();
	if( device != OutputDevice::Virtual )
		SetOutputMode( OutputMode::Advanced );
}

bool App::OutputUnsaved() const
{
	return SaveAdvancedOutput( advancedOutput ) != savedOutput;
}

void App::SaveOutputSetup()
{
	savedOutput = SaveAdvancedOutput( advancedOutput );
	if( WriteTextFile( ( fs::u8path( configDir ) / "advanced-output.json" ).u8string(), savedOutput ) )
		SetStatus( "Advanced Output saved" );
	else
		SetStatus( "Cannot save the Advanced Output in " + configDir );
}

void App::RevertOutputSetup()
{
	std::string error;
	AdvancedOutputSetup saved;
	if( !LoadAdvancedOutput( savedOutput, saved, error ) || saved.screens.empty() )
		return;
	advancedOutput = std::move( saved );
	SetStatus( "Advanced Output: back to the saved setup" );
}

void App::ToggleAdvancedOutput()
{
	if( outputMode == OutputMode::Advanced )
	{
		SetOutputMode( beforeAdvanced, outputDisplay );
		panels.advancedOutput = false;
		return;
	}
	SetOutputMode( OutputMode::Advanced );
	panels.advancedOutput = true;
}

void App::SetOutputMode( OutputMode mode, const std::string& display )
{
	if( mode == OutputMode::Advanced && outputMode != OutputMode::Advanced )
		beforeAdvanced = outputMode;
	outputMode = mode;
	if( mode == OutputMode::Fullscreen )
		outputDisplay = display;
	UpdateQuickOutput();
	SaveOutputMode();
	SetStatus( "Output: " + OutputModeLabel() );
}

std::string App::OutputModeLabel() const
{
	switch( outputMode )
	{
	case OutputMode::Off: return "Off";
	case OutputMode::Window: return "Window";
	case OutputMode::Fullscreen:
		return "Fullscreen on " + OutputWindow::DisplayLabel( outputDisplay );
	case OutputMode::Advanced: return "Advanced";
	}
	return "";
}

void App::UpdateQuickOutput()
{
	// The composition alone on one screen, made again when the mode, the display or the composition's size change.
	bool quick      = outputMode == OutputMode::Window || outputMode == OutputMode::Fullscreen;
	std::string key = std::to_string( (int)outputMode ) + "|" + outputDisplay + "|" + std::to_string( composition.width ) + "x" + std::to_string( composition.height );
	if( !quick || key == quickOutputKey )
		return;
	quickOutputKey = key;
	quickOutput    = AdvancedOutputSetup::MakeDefault( composition.width, composition.height );
	quickOutput.screens[ 0 ].name = "Output";
	PlaceScreen( quickOutput.screens[ 0 ], outputMode == OutputMode::Window ? OutputDevice::Window : OutputDevice::Display, outputDisplay );
}

void App::SaveOutputMode() const
{
	const char* names[] = { "off", "window", "fullscreen", "advanced" };
	nlohmann::json mode = { { "mode", names[ (int)outputMode ] }, { "display", outputDisplay }, { "beforeAdvanced", names[ (int)beforeAdvanced ] } };
	WriteTextFile( ( fs::u8path( configDir ) / "output-mode.json" ).u8string(), mode.dump( 1 ) );
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

void App::UpdateAudio()
{
	if( view == View::Arrangement )
	{
		// The timeline's clips under the playhead (video tracks' sound and audio tracks) while it plays.
		Timeline& timeline = composition.timeline;
		bool anySolo       = std::any_of( timeline.tracks.begin(), timeline.tracks.end(), []( const TimelineTrack& t ) { return t.solo; } );
		std::vector< std::shared_ptr< AudioVoice > > voices;
		for( TimelineTrack& track : timeline.tracks )
		{
			int index = track.ClipAt( timeline.position );
			if( index < 0 )
				continue;
			Clip& clip = track.clips[ index ].clip;
			if( !clip.audio )
				continue;
			bool audible = timeline.playing && !timeline.black && !composition.bypass && !track.mute && ( !anySolo || track.solo );
			clip.audio->Follow( track.clips[ index ].MediaTime( timeline.position ), std::max( 0.0f, clip.speed ), audible ? clip.volume * track.volume : 0.0f );
			voices.push_back( clip.audio );
		}
		audio.SetVoices( std::move( voices ), composition.audioVolume );
		return;
	}
	// Each playing clip's sound follows its playhead; bypassed layers, layers others solo over and a paused
	// composition are silent.
	bool anySolo = std::any_of( composition.layers.begin(), composition.layers.end(), []( const Layer& l ) { return l.solo; } );
	std::vector< std::shared_ptr< AudioVoice > > voices;
	for( int index = 0; index < (int)composition.layers.size(); ++index )
	{
		Layer& layer = composition.layers[ index ];
		if( !composition.ActiveClip( index ) )
			continue;
		bool audible = !composition.paused && !composition.bypass && !layer.bypass && ( !anySolo || layer.solo );
		// During a transition the outgoing clip's sound fades out as the new one fades in.
		bool fading  = composition.OutgoingClip( index ) && layer.transitionProgress < 1;
		for( int which = 0; which < ( fading ? 2 : 1 ); ++which )
		{
			Clip& clip = *( which == 0 ? composition.ActiveClip( index ) : composition.OutgoingClip( index ) );
			if( !clip.audio || !clip.Loaded() )
				continue;
			float fade  = !fading ? 1.0f : which == 0 ? layer.transitionProgress : 1.0f - layer.transitionProgress;
			fade *= layer.master * layer.CrossfadeGain( composition.crossfader );
			double rate = clip.PlayDirection() * std::fabs( clip.Rate( composition.bpm ) ) * composition.speed * layer.speed;
			clip.audio->Follow( clip.time, rate, audible ? clip.volume * layer.volume * fade : 0.0f );
			voices.push_back( clip.audio );
		}
	}
	audio.SetVoices( std::move( voices ), composition.audioVolume );
}

void App::CopyClip( bool cut )
{
	Clip* selected = composition.SelectedClip();
	if( !selected )
	{
		SetStatus( "Select a clip first", true );
		return;
	}
	clipboard = std::make_unique< Clip >( stltvj::CopyClip( *selected ) );
	SetStatus( std::string( cut ? "Cut " : "Copied " ) + selected->name );
	if( cut )
		RemoveClip( composition.selectedRow, composition.selectedColumn );
}

void App::PasteClip()
{
	if( !clipboard )
		return;
	int row = composition.selectedRow, column = composition.selectedColumn;
	if( row < 0 || row >= (int)composition.layers.size() || column < 0 || column >= composition.ColumnCount() )
	{
		SetStatus( "Select a cell to paste into", true );
		return;
	}
	StopCell( row, column );
	Clip clip = stltvj::CopyClip( *clipboard );
	PrepareClip( clip );
	std::string name                          = clip.name;
	composition.layers[ row ].clips[ column ] = std::move( clip );
	Select( row, column );
	SetStatus( "Pasted " + name );
}

void App::CopyEffects()
{
	Clip* selected = composition.SelectedClip();
	if( !selected )
		return;
	effectsClipboard = std::make_unique< Clip >( stltvj::CopyClip( *selected ) );
	SetStatus( "Copied the effects of " + selected->name + " (" + std::to_string( selected->effects.size() ) + ")" );
}

void App::PasteEffects()
{
	Clip* selected = composition.SelectedClip();
	if( !selected || !effectsClipboard )
		return;
	Clip copy = stltvj::CopyClip( *effectsClipboard );
	for( EffectSlot& slot : copy.effects )
		selected->effects.push_back( std::move( slot ) );
	registry->Restore( selected->effects );
	SetStatus( "Pasted effects onto " + selected->name );
}

void App::UpdateChaserStage()
{
	advancedOutput.EnsureIds();
	std::string text = SaveChaserStage( advancedOutput, composition.width, composition.height );
	if( text != chaserStageText && WriteTextFile( chaserStage, text ) )
		chaserStageText = text;
}

void App::OpenChaser()
{
	UpdateChaserStage();
	std::string error;
	if( chaser::OpenEditor( chaserStage, error ) )
		SetStatus( "STLT-CHASER: draw the sequences there, then add the STLT-CHASER effect from the Effects panel" );
	else
		SetStatus( error, true );
}

int App::ResizeAllClips( FitMode fit )
{
	int count = 0;
	composition.ForEachClip( [ & ]( Clip& clip, int, int, int ) {
		if( !clip.Empty() )
		{
			clip.fit = fit;
			++count;
		}
	} );
	for( TimelineTrack& track : composition.timeline.tracks )
		for( TimelineClip& clip : track.clips )
		{
			clip.clip.fit = fit;
			++count;
		}
	return count;
}

void App::SavePreferences()
{
	WriteTextFile( ( fs::u8path( configDir ) / "preferences.json" ).u8string(), prefs.Save() );
}

void App::ApplySync()
{
	tempoSync.SetClockInput( prefs.midiClockInput );
	tempoSync.latency = prefs.syncLatencyMs / 1000.0;
	tempoSync.EnableLink( prefs.linkEnabled, composition.bpm, composition.beat );
	bool off = std::getenv( "STLTVJ_NO_AUDIO" ) && prefs.timecodeSource == (int)TimecodeInput::Source::Ltc;
	timecode.Configure( off ? TimecodeInput::Source::Off : (TimecodeInput::Source)prefs.timecodeSource, prefs.timecodeLtcDevice,
	                    prefs.timecodeLtcChannel, prefs.timecodeMtcInput );
}

void App::ApplyPreferences( bool restartAudio )
{
	ApplySync();
	SetHardwareDecoding( prefs.hardwareDecoding );
	SetFrameCacheMegabytes( prefs.frameCacheMegabytes );
	Composition::newLayerBlend          = prefs.layerBlend;
	Composition::newLayerTransition     = prefs.layerTransition;
	Composition::newLayerTransitionTime = prefs.layerTransitionTime;
	if( restartAudio && !std::getenv( "STLTVJ_NO_AUDIO" ) )
	{
		audio.Start( prefs.audioDevice, prefs.audioBuffer );
		SetStatus( "Audio: " + audio.Status() );
	}
}

void App::SetView( View v )
{
	if( view == v )
		return;
	view = v;
	workspaces.SwitchView( v == View::Arrangement ? 1 : 0 );
	// A first visit: two video tracks and an audio track to start with.
	Timeline& t = composition.timeline;
	if( v == View::Arrangement && t.tracks.empty() )
		for( auto [ name, audioTrack ] : { std::pair{ "Video 1", false }, std::pair{ "Video 2", false }, std::pair{ "Audio 1", true } } )
		{
			TimelineTrack track;
			track.name  = name;
			track.audio = audioTrack;
			t.tracks.push_back( std::move( track ) );
		}
	if( !options.fresh )
		WriteTextFile( ( fs::u8path( configDir ) / "view.txt" ).u8string(), v == View::Arrangement ? "arrangement" : "session" );
	SetStatus( v == View::Arrangement ? "Arrangement: the timeline is on the output" : "Session: the deck is on the output" );
}

void App::TimelinePlay( bool play )
{
	Timeline& t = composition.timeline;
	if( play && t.position >= t.Length() )
		t.Locate( 0 );
	t.playing = play;
	if( play )
		t.black = false;
}

void App::TimelineStop()
{
	// As in Ableton: the first Stop stops where it is, the second goes back to the start.
	Timeline& t = composition.timeline;
	if( t.playing )
		t.playing = false;
	else
		t.Locate( 0 );
}

void App::TimelineCue( int block )
{
	Timeline& t = composition.timeline;
	if( block < 0 || block >= (int)t.blocks.size() )
		return;
	t.Cue( block );
	SetStatus( "Cue " + std::to_string( block + 1 ) + ( t.blocks[ block ].name.empty() ? "" : ": " + t.blocks[ block ].name ) );
}

void App::TimelineNextCue( int step )
{
	Timeline& t = composition.timeline;
	if( t.blocks.empty() )
		return;
	int at = t.BlockAt( t.position ), target = -1;
	if( at >= 0 )
		target = at + step;
	else if( step > 0 )
	{
		for( int i = 0; i < (int)t.blocks.size() && target < 0; ++i )
			if( t.blocks[ i ].start > t.position )
				target = i;
	}
	else
		for( int i = (int)t.blocks.size() - 1; i >= 0 && target < 0; --i )
			if( t.blocks[ i ].start < t.position )
				target = i;
	if( target >= 0 && target < (int)t.blocks.size() )
		TimelineCue( target );
}

void App::PrepareTimelineClip( TimelineClip& tc )
{
	if( tc.prepared )
		return;
	tc.prepared = true;
	PrepareClip( tc.clip );
}

void App::PrepareClip( Clip& clip )
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
	if( !clip.path.empty() && !clip.player )
	{
		// A movie or still; a sound alone has no picture but plays on audio tracks.
		clip.player = std::make_unique< ClipPlayer >();
		if( clip.player->Open( clip.path ) )
			compositor.MakeThumbnail( *clip.player );
		if( !clip.audio )
			clip.audio = AudioVoice::Open( clip.path, audio.SampleRate() );
	}
}

int App::AddTimelineClip( int track, double start, Clip clip )
{
	Timeline& t = composition.timeline;
	if( track < 0 || track >= (int)t.tracks.size() || clip.Empty() )
		return -1;
	TimelineClip placed;
	placed.clip  = std::move( clip );
	placed.start = std::max( 0.0, start );
	// On the timeline keyframes move things, not the deck's beat animations.
	for( Animation& animation : placed.clip.animations )
		animation.mode = AnimMode::Off;
	PrepareTimelineClip( placed );
	// As long as its media (between in and out, at its speed); stills and sources: four bars.
	double duration = placed.MediaDuration();
	double out      = placed.clip.outPoint >= 0 ? placed.clip.outPoint : duration;
	double length   = duration > 0 && out > placed.clip.inPoint ? ( out - placed.clip.inPoint ) / std::max( 0.01f, placed.clip.speed ) : 0;
	if( length <= 0 || ( placed.clip.Loaded() && placed.clip.player->Source().IsStill() ) )
		length = t.SecondsPerBar() * 4;
	placed.length = length;
	std::string name = placed.clip.name;
	int index        = t.Place( track, std::move( placed ) );
	SetStatus( "Placed " + name + " on " + t.tracks[ track ].name + " at " + t.BarsBeats( start ) );
	return index;
}

int App::AddTimelineFile( int track, double start, const std::string& path )
{
	// A sound file belongs on an audio track: the first one (made if there is none).
	Timeline& t = composition.timeline;
	if( IsAudioExtension( fs::u8path( path ).extension().u8string() ) && track >= 0 && track < (int)t.tracks.size() && !t.tracks[ track ].audio )
	{
		track = -1;
		for( int k = 0; k < (int)t.tracks.size() && track < 0; ++k )
			if( t.tracks[ k ].audio )
				track = k;
		if( track < 0 )
		{
			TimelineTrack audioTrack;
			audioTrack.name  = "Audio 1";
			audioTrack.audio = true;
			t.tracks.push_back( std::move( audioTrack ) );
			track = (int)t.tracks.size() - 1;
		}
	}
	Clip clip;
	clip.path = path;
	clip.name = fs::u8path( path ).stem().u8string();
	clip.fit  = prefs.clipFit; // on the timeline only the size matters of the clip defaults
	return AddTimelineClip( track, start, std::move( clip ) );
}

// The playhead where the timecode coming in says, when the timeline follows it: running with it (small
// differences made up smoothly, large ones a jump), stopped where it stops, and stopped when it is gone for longer
// than the freewheel. False when the timeline is on its own.
bool App::FollowTimecode( double dt )
{
	Timeline& t    = composition.timeline;
	timecodeLocked = false;
	if( !t.followTimecode || timecode.Current() == TimecodeInput::Source::Off )
		return false;
	TimecodeReceiver::State in = timecode.At( TempoSync::Now() + prefs.syncLatencyMs / 1000.0, prefs.timecodeFreewheelMs / 1000.0 );
	if( !in.present )
	{
		t.playing = false;
		return true;
	}
	timecodeLocked = true;
	Timecode start;
	start.rate    = in.last.rate;
	double origin = ParseTimecode( t.timecodeStart, start ) ? start.Seconds() : 0.0;
	double target = in.seconds - origin;
	if( target < 0 )
	{
		// Before the show's start: waiting at the top.
		t.playing = false;
		if( t.position != 0 )
			t.Locate( 0 );
		return true;
	}
	if( in.running )
	{
		t.playing        = true;
		double predicted = t.position + dt;
		double error     = target - predicted;
		if( std::fabs( error ) > 0.25 )
			t.Locate( target );
		else
			t.position = predicted + error * std::min( 1.0, dt * 8 );
		t.black = false;
	}
	else
	{
		t.playing = false;
		if( std::fabs( target - t.position ) > 0.5 / TimecodeRateFps( in.last.rate ) )
			t.Locate( target );
	}
	return true;
}

void App::UpdateTimeline( double dt )
{
	Timeline& t = composition.timeline;
	if( !FollowTimecode( dt ) )
		t.Advance( dt );
	// The clips under the playhead play; those about to start get their first frame ready; the others rest.
	const double ahead = 1.5;
	bool shown         = view == View::Arrangement || t.playing;
	for( TimelineTrack& track : t.tracks )
		for( TimelineClip& tc : track.clips )
		{
			PrepareTimelineClip( tc );
			tc.Apply( t.position ); // keyframed opacity, position, scale, rotation, volume
			Clip& clip = tc.clip;
			if( !clip.Loaded() || track.audio )
				continue;
			bool now  = shown && !t.black && t.position >= tc.start && t.position < tc.End();
			bool soon = t.playing && tc.start > t.position && tc.start - t.position < ahead;
			if( now || soon )
			{
				if( !clip.player->Active() && !clip.player->Activate() )
					continue;
				clip.time = tc.MediaTime( now ? t.position : tc.start );
				clip.player->Update( clip.CurrentFrame(), now && t.playing ? 1 : 0 );
			}
			else if( clip.player->Active() )
				clip.player->Deactivate();
		}
}

void App::UpdatePlayback( double dt )
{
	for( int index = 0; index < (int)composition.layers.size(); ++index )
	{
		Layer& layer = composition.layers[ index ];
		if( layer.active < 0 )
			continue;
		double step = composition.paused ? 0.0 : dt * composition.speed * layer.speed;
		auto play   = [ & ]( Clip& clip ) {
			if( !clip.Loaded() )
				return; // generators run on composition time
			clip.Advance( step, composition.bpm );
			clip.player->Update( clip.FrameFor( step, composition.bpm ), composition.paused ? 0 : clip.PlayDirection() );
		};
		if( layer.cutPending )
		{
			// A cut: the outgoing clip stays on until the new one has its first frame (decoded off the main thread).
			Clip* incoming = composition.ActiveClip( index );
			Clip* outgoing = composition.OutgoingClip( index );
			bool ready     = !incoming || incoming->generator || !incoming->Loaded() || incoming->player->HasFrame();
			if( ready || !outgoing )
				FinishTransition( index );
			else
				play( *outgoing );
		}
		else if( layer.previous >= 0 )
		{
			// A transition: the outgoing clip plays on until it is over (time in seconds, or in beats).
			double seconds = layer.TransitionSeconds( composition.bpm );
			if( !composition.paused )
				layer.transitionProgress += seconds > 0 ? (float)( dt * ( layer.transitionInBeats ? composition.speed : 1.0 ) / seconds ) : 1.0f;
			if( layer.transitionProgress >= 1 || !composition.OutgoingClip( index ) )
				FinishTransition( index );
			else
				play( *composition.OutgoingClip( index ) );
		}
		Clip* playing = composition.ActiveClip( index );
		if( !playing )
			continue;
		Clip& clip = *playing;
		play( clip );
		if( clip.finished && clip.playMode == PlayMode::OnceClear )
			ClearLayer( index ); // played once: the layer empties
	}
}

double App::FrameStep( double measured )
{
	if( !frameClock )
	{
		GLFWmonitor* monitor    = glfwGetPrimaryMonitor();
		const GLFWvidmode* mode = monitor ? glfwGetVideoMode( monitor ) : nullptr;
		frameClock              = std::make_unique< FrameClock >( mode && mode->refreshRate > 0 ? 1.0 / mode->refreshRate : 1.0 / 60 );
	}
	return frameClock->Step( measured );
}

void App::UpdatePreview( double dt )
{
	Clip* selected = composition.SelectedClip();
	bool live      = selected && composition.LayerShowing( composition.selectedRow, composition.selectedColumn ) >= 0;
	ClipPlayer* wanted = selected && !live && selected->Loaded() && selected->Playable() ? selected->player.get() : nullptr;
	if( previewPlayer && previewPlayer != wanted )
	{
		// Stop the clip previewed until now, unless it has since been launched (or removed: then it is gone).
		composition.ForEachClip( [ & ]( Clip& clip, int deck, int row, int column ) {
			if( clip.player.get() == previewPlayer && composition.LayerShowingIn( deck, row, column ) < 0 )
				previewPlayer->Deactivate();
		} );
		previewPlayer = nullptr;
	}
	if( !wanted )
		return;
	if( !previewPlayer )
	{
		if( !wanted->Activate() )
			return;
		selected->Restart();
		previewPlayer = wanted;
	}
	double step = composition.paused ? 0.0 : dt * composition.speed;
	selected->Advance( step, composition.bpm );
	wanted->Update( selected->FrameFor( step, composition.bpm ), composition.paused ? 0 : selected->PlayDirection() );
}

void App::Frame()
{
	if( startViewPending > 0 && ImGui::GetFrameCount() > 2 )
	{
		SetView( startViewPending == 2 ? View::Arrangement : View::Session );
		startViewPending = -1;
	}
	workspaces.ApplyPending();
	double now = glfwGetTime();
	double measured = std::min( now - lastTime, 0.1 );
	double dt       = FrameStep( measured );
	lastTime        = now;
	frameTimes.push_back( (float)( measured * 1000.0 ) );
	if( frameTimes.size() > 240 )
		frameTimes.pop_front();

	PollRemote();
	if( control.midiEnabled )
		UpdateLights( !control.midiLights );
	UpdateAudioInput( now, dt );
	UpdatePlayback( dt );
	UpdateTimeline( dt );
	UpdateAudio();
	if( !composition.paused )
		composition.beat += dt * composition.speed * composition.bpm / 60.0;
	if( tempoSync.Update( TempoSync::Now(), composition.bpm, composition.beat ) )
		RunQueuedLaunches( true ); // the beat went back (a Start, a new Link timeline): what waited goes now
	if( tempoSync.Following() == TempoSync::Source::Internal )
		undoBpm = composition.bpm;
	RunQueuedLaunches( false );
	frame.time  = now - startTime;
	frame.delta = dt;
	frame.bpm   = composition.bpm;
	frame.beat  = composition.beat;
	compositor.Render( composition, frame, view == View::Arrangement );
	UpdateQuickOutput();
	bool advancedLive = outputMode == OutputMode::Advanced;
	bool quickLive    = outputMode == OutputMode::Window || outputMode == OutputMode::Fullscreen;
	// The Advanced Output setup is drawn when it is shown and for the editor's views.
	if( now - chaserStageChecked > 0.5 )
	{
		UpdateChaserStage();
		chaser::Reap();
		chaserStageChecked = now;
	}
	if( advancedLive || panels.advancedOutput )
		outputs.Render( advancedOutput, compositor.CompositionTexture(), composition.width, composition.height, advancedLive );
	if( quickLive )
		quickOutputs.Render( quickOutput, compositor.CompositionTexture(), composition.width, composition.height );
	UpdatePreview( dt );
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
	// An edit has ended when nothing is held or being typed into: record it. Keyboard edits (nudges, shortcuts)
	// end when their key comes up; anything else (a drop from the desktop, a file dialog) is caught every couple of
	// seconds. Most frames serialise nothing.
	bool editing     = ImGui::IsAnyItemActive() || ImGui::IsMouseDown( ImGuiMouseButton_Left ) || ImGui::IsMouseDown( ImGuiMouseButton_Right );
	bool keyReleased = false;
	for( int k = ImGuiKey_NamedKey_BEGIN; k < ImGuiKey_GamepadStart && !keyReleased; ++k )
		keyReleased = ImGui::IsKeyReleased( (ImGuiKey)k );
	if( !editing && ( wasEditing || keyReleased || now - lastRecord > 2.0 ) )
	{
		history.Record( Snapshot() );
		lastRecord = now;
	}
	wasEditing = editing;
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
	bool wantOutputVsync = outputs.AnyWindowOpen() || quickOutputs.AnyWindowOpen();
	if( wantOutputVsync != outputVsync )
	{
		glfwSwapInterval( wantOutputVsync ? 0 : 1 );
		outputVsync = wantOutputVsync;
	}
	glfwSwapBuffers( window );
	outputs.SyncWindows( advancedOutput, window, advancedLive );
	quickOutputs.SyncWindows( quickOutput, window, quickLive );
	outputs.TakeWindowClosed();
	if( quickOutputs.TakeWindowClosed() )
		SetOutputMode( OutputMode::Off ); // the output window closed by hand
	outputs.Present( compositor, window );
	quickOutputs.Present( compositor, window );
}

void App::Capture()
{
	int width = 0, height = 0;
	if( options.captureComposition )
	{
		width  = composition.width;
		height = composition.height;
		glBindFramebuffer( GL_FRAMEBUFFER, compositor.CompositionFramebuffer() );
	}
	else
		glfwGetFramebufferSize( window, &width, &height );
	std::vector< uint8_t > pixels( (size_t)width * height * 3 );
	glPixelStorei( GL_PACK_ALIGNMENT, 1 );
	glReadPixels( 0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, pixels.data() );
	glBindFramebuffer( GL_FRAMEBUFFER, 0 );
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
	BeginMappable( *this );
	DrawToolbar( *this );
	EndMappable( *this );

	float statusHeight = ImGui::GetFrameHeight();
	ImVec2 dockSize    = ImGui::GetContentRegionAvail();
	dockSize.y -= statusHeight;
	ImGuiID dockspace = ImGui::GetID( "MainDock" );
	workspaces.BuildDefaultIfNeeded( dockspace, dockSize );
	ImGui::DockSpace( dockspace, dockSize, ImGuiDockNodeFlags_None );
	DrawStatusBar( *this );
	ImGui::End();

	// Mapping MIDI (Ctrl+M) or keys (Ctrl+K), like Resolume's mapping modes; Escape leaves them.
	ImGuiIO& io = ImGui::GetIO();
	if( io.KeyCtrl && !io.WantTextInput && ImGui::IsKeyPressed( ImGuiKey_M, false ) )
		ToggleMapping( false );
	else if( io.KeyCtrl && !io.WantTextInput && ImGui::IsKeyPressed( ImGuiKey_K, false ) )
		ToggleMapping( true );
	else if( learning && ImGui::IsKeyPressed( ImGuiKey_Escape, false ) )
		learning = false;
	else
		PollKeyboard();

	BeginMappable( *this );
	if( panels.timeline )
		DrawTimelinePanel( *this ); // before the deck: a file dropped on it is not for the deck
	if( panels.layers )
		DrawLayersPanel( *this );
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
	EndMappable( *this );
	if( panels.control )
		DrawControlPanel( *this );
	if( panels.files )
		DrawFilesPanel( *this );
	if( panels.output )
		DrawOutputPanel( *this );
	if( panels.performance )
		DrawPerformancePanel( *this );
	if( panels.effects )
		DrawEffectsPanel( *this );
	DrawPreferences( *this );
	DrawAdvancedOutputSavePrompt( *this );
	if( panels.advancedOutput )
		DrawAdvancedOutputPanel( *this );
	droppedPaths.clear();
}
}
