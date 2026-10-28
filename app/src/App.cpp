#include "App.h"
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include "Settings.h"
#include "fx/BuiltinEffects.h"
#include "gl/Gl.h"
#include "ui/AdvancedOutputUi.h"
#include "ui/ControlUi.h"
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
	{
		std::string text;
		if( ReadTextFile( ( fs::u8path( configDir ) / "control.json" ).u8string(), text ) )
			control.Load( text );
		if( options.oscPort > 0 )
		{
			control.oscEnabled = true;
			control.oscPort    = options.oscPort;
		}
	}
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
	history.Reset( Snapshot() );
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
		else if( source.IsHardwareDecoded() )
			SetStatus( "Loaded " + clip.name + " (" + source.CodecName() + ", " + source.Details() + "; HAP is still faster to jump around and layer)" );
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
	RestoreEffects();
	history.Reset( Snapshot() );
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

History::State App::Snapshot() const
{
	return { SaveComposition( composition ), SaveAdvancedOutput( advancedOutput ) };
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
	Composition fresh;
	std::string error;
	if( !LoadComposition( text, fresh, error ) )
		return;
	// The clips still there keep their players and generators: matched where they were, else anywhere.
	std::vector< Clip* > taken;
	auto same = [ & ]( const Clip& a, const Clip& b ) {
		return !b.Empty() && a.path == b.path && a.generatorId == b.generatorId && std::find( taken.begin(), taken.end(), &b ) == taken.end();
	};
	auto find = [ & ]( const Clip& want, int l, int c ) -> Clip* {
		if( l < (int)composition.layers.size() && c < (int)composition.layers[ l ].clips.size() && same( want, composition.layers[ l ].clips[ c ] ) )
			return &composition.layers[ l ].clips[ c ];
		for( Layer& layer : composition.layers )
			for( Clip& clip : layer.clips )
				if( same( want, clip ) )
					return &clip;
		return nullptr;
	};
	// The clip each layer plays, to follow it if the restored state has it in another column.
	std::vector< const Clip* > playing( fresh.layers.size(), nullptr );
	std::vector< int > follow( fresh.layers.size(), -1 );
	for( int l = 0; l < (int)fresh.layers.size() && l < (int)composition.layers.size(); ++l )
	{
		const Layer& layer = composition.layers[ l ];
		if( layer.active >= 0 && layer.active < (int)layer.clips.size() )
			playing[ l ] = &layer.clips[ layer.active ];
	}
	for( int l = 0; l < (int)fresh.layers.size(); ++l )
	{
		Layer& layer = fresh.layers[ l ];
		ReuseEffects( layer.effects, l < (int)composition.layers.size() ? &composition.layers[ l ].effects : nullptr );
		for( int c = 0; c < (int)layer.clips.size(); ++c )
		{
			Clip& clip = layer.clips[ c ];
			if( clip.Empty() )
				continue;
			Clip* old = find( clip, l, c );
			if( !old )
				continue;
			taken.push_back( old );
			if( old == playing[ l ] )
				follow[ l ] = c;
			clip.player             = std::move( old->player );
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
		}
	}
	ReuseEffects( fresh.effects, &composition.effects );

	// What plays and what is selected stays, where it still exists.
	for( int l = 0; l < (int)fresh.layers.size() && l < (int)composition.layers.size(); ++l )
	{
		Layer& layer  = fresh.layers[ l ];
		int active    = follow[ l ] >= 0 ? follow[ l ] : composition.layers[ l ].active;
		layer.active  = active >= 0 && active < (int)layer.clips.size() && !layer.clips[ active ].Empty() ? active : -1;
		layer.solo    = composition.layers[ l ].solo;
	}
	fresh.beat           = composition.beat;
	fresh.paused         = composition.paused;
	fresh.selectedLayer  = std::clamp( composition.selectedLayer, 0, std::max( (int)fresh.layers.size() - 1, 0 ) );
	fresh.selectedColumn = composition.selectedColumn < fresh.ColumnCount() ? composition.selectedColumn : -1;
	fresh.activeColumn   = composition.activeColumn < fresh.ColumnCount() ? composition.activeColumn : -1;
	composition          = std::move( fresh );

	RestoreEffects(); // creates the effects and generators that were not there
	for( Layer& layer : composition.layers )
		for( Clip& clip : layer.clips )
			if( !clip.path.empty() && !clip.player )
			{
				clip.player = std::make_unique< ClipPlayer >();
				if( clip.player->Open( clip.path ) )
					compositor.MakeThumbnail( *clip.player );
			}
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
			if( !ApplyControl( e.address, has, has ? e.values[ 0 ] : 1.0f ) )
				lastControl += "  (not an STLT-VJ address)";
			continue;
		}
		int id         = (int)e.key.type * 4096 + e.key.channel * 128 + e.key.number;
		float previous = lastMidiValues[ id ];
		lastMidiValues[ id ] = e.value;
		std::snprintf( value, sizeof( value ), " %.2f", e.value );
		lastControl = "MIDI " + MidiKeyName( e.key ) + value + "  (" + e.device + ")";
		if( learning && !learnTarget.empty() && e.value > 0 )
		{
			// One control per key: learning again moves it.
			control.midi.erase( std::remove_if( control.midi.begin(), control.midi.end(), [ & ]( const MidiMapping& m ) { return m.key == e.key; } ),
			                    control.midi.end() );
			control.midi.push_back( { e.key, learnTarget } );
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
		if( press && layerOk && columnOk )
			Launch( t.layer, t.column );
		return layerOk && columnOk;
	case Kind::ColumnConnect:
		if( press && columnOk )
			LaunchColumn( t.column );
		return columnOk;
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

	PollRemote();
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
	// An edit has ended when nothing is held or being typed into: record it (and, every few frames, catch
	// keyboard edits such as nudges).
	bool editing = ImGui::IsAnyItemActive() || ImGui::IsMouseDown( ImGuiMouseButton_Left ) || ImGui::IsMouseDown( ImGuiMouseButton_Right );
	if( !editing && ( wasEditing || frameCount % 20 == 0 ) )
		history.Record( Snapshot() );
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

	// Learning MIDI: Ctrl+M, like Resolume's mapping mode; Escape leaves it.
	ImGuiIO& io = ImGui::GetIO();
	if( io.KeyCtrl && !io.WantTextInput && ImGui::IsKeyPressed( ImGuiKey_M, false ) )
	{
		learning = !learning;
		learnTarget.clear();
	}
	if( learning && ImGui::IsKeyPressed( ImGuiKey_Escape, false ) )
		learning = false;

	BeginMappable( *this );
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
	if( panels.advancedOutput )
		DrawAdvancedOutputPanel( *this );
	droppedPaths.clear();
}
}
