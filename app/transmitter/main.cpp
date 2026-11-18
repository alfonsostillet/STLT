// NIMS PRO LINK transmitter: runs on the DJ's computer next to Traktor. It takes the decks Traktor's script posts (see
// resources/nimslink/qml) and sends them to the VJ programs on the network. A small window with no settings: lights
// for Traktor, the network and the VJs listening, and the four decks.
//
//   NIMSLink [--install | --uninstall]   put the script into Traktor / take it out, and quit (exit code 0: done)
//          [--demo]                    send four made-up decks instead of Traktor's (to try the network first)
//          [--headless]                no window (prints the state every few seconds)
//          [--name NAME]               the name the VJs see (the computer's otherwise)
//          [--capture FILE.bmp --frames N]  draws N frames, saves the window and quits (for the docs and tests)
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include "TraktorInstall.h"
#include "nimslink/Link.h"
#include "nimslink/Net.h"
#include "nimslink/TraktorBridge.h"
#ifndef NIMSLINK_HEADLESS_ONLY

#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#endif

namespace stltvj
{
extern const unsigned char kFont_Inter_Regular[];
extern const size_t kFont_Inter_Regular_size;
extern const unsigned char kFont_Inter_SemiBold[];
extern const size_t kFont_Inter_SemiBold_size;
}

#ifdef __APPLE__
void KeepAwake(); // KeepAwake.mm: no App Nap, which would hold the network back while the window is hidden
#else
static void KeepAwake() {}
#endif

using namespace nimslink;

namespace
{
const char* kTransmitterVersion = "1.0";

struct Options
{
	bool install = false, uninstall = false, demo = false, headless = false;
	std::string name, capture, traktorFolder;
	int frames = 0;
};

// The decks made up by --demo: A plays a track, B is cued, C loaded, D empty.
struct Demo
{
	double start = Now();
	std::vector< TrackInfo > Tracks()
	{
		std::vector< TrackInfo > list;
		const char* titles[]  = { "Kerala", "Kong", "Cirrus" };
		const double lengths[] = { 241.5, 232.0, 348.0 };
		const float bpms[]     = { 62.5f, 125.0f, 124.0f };
		for( int d = 0; d < 3; ++d )
		{
			TrackInfo t;
			t.title  = titles[ d ];
			t.artist = "Bonobo";
			t.length = lengths[ d ];
			t.bpm    = bpms[ d ];
			t.stems  = d < 2;
			t.hotcues[ 0 ] = { true, -1, 10.0, 0, "Intro" };
			t.hotcues[ 3 ] = { true, 5, 51.2, 7.68, "Loop" };
			list.push_back( t );
		}
		return list;
	}
	void Decks( DeckState* d, double now ) const
	{
		double t = now - start;
		for( int i = 0; i < 3; ++i )
		{
			d[ i ].loaded = true;
			d[ i ].rate   = 1;
		}
		d[ 0 ].playing  = true;
		d[ 0 ].position = std::fmod( 30 + t, 240.0 );
		d[ 0 ].bpm      = 62.5f;
		d[ 0 ].beat     = d[ 0 ].position * 62.5 / 60;
		d[ 0 ].beatPhase = (float)( d[ 0 ].beat - std::floor( d[ 0 ].beat ) );
		d[ 0 ].volume   = 1;
		float pulse     = (float)std::pow( 1 - d[ 0 ].beatPhase, 3 );
		d[ 0 ].stems[ 0 ] = 0.2f + 0.7f * pulse;
		d[ 0 ].stems[ 1 ] = 0.45f + 0.1f * (float)std::sin( t * 3 );
		d[ 0 ].stems[ 2 ] = 0.6f;
		d[ 0 ].stems[ 3 ] = std::fmod( t, 16.0 ) < 8 ? 0.05f : 0.55f;
		d[ 0 ].level      = 0.7f;
		d[ 1 ].position = 4.1;
		d[ 1 ].bpm      = 125;
		d[ 2 ].bpm      = 124;
	}
};

// Everything the window shows, gathered once a frame.
struct View
{
	bool traktor = false;  // posts arriving
	float rate   = 0;
	std::vector< TraktorFolder > folders;
	std::string network;   // "Ethernet · 192.168.1.14"
	bool networked = false;
	std::vector< Sender::Subscriber > vjs;
	DeckState decks[ kDecks ];
	TrackInfo tracks[ kDecks ];
	std::string message; // the outcome of an install
};

class Transmitter
{
public:
	explicit Transmitter( const Options& o ) : options( o ) {}

	bool Start( std::string& error )
	{
		std::string name = options.name.empty() ? HostName() : options.name;
		if( !sender.Start( name, error ) )
			return false;
		if( options.demo )
		{
			std::vector< TrackInfo > tracks = demo.Tracks();
			for( int d = 0; d < (int)tracks.size(); ++d )
			{
				sender.SetTrack( d, tracks[ d ] );
				Keep( nullptr, &tracks[ d ], d );
			}
			sender.SetProgram( "Traktor (demo)" );
			demoThread = std::thread( [ this ] {
				while( running )
				{
					DeckState d[ kDecks ];
					double now = Now();
					demo.Decks( d, now );
					Keep( d, nullptr, -1 );
					sender.Publish( d, now );
					std::this_thread::sleep_for( std::chrono::milliseconds( 30 ) );
				}
			} );
			return true;
		}
		bool ok = bridge.Start(
		    [ this ]( const DeckState* d, double time ) {
			    Keep( d, nullptr, -1 );
			    sender.Publish( d, time );
		    },
		    [ this ]( int deck, const TrackInfo& t ) {
			    Keep( nullptr, &t, deck );
			    sender.SetTrack( deck, t );
		    },
		    error );
		if( !ok )
			sender.Stop();
		return ok;
	}

	void Stop()
	{
		running = false;
		if( demoThread.joinable() )
			demoThread.join();
		bridge.Stop();
		sender.Stop();
	}

	// Once a frame (or every few seconds, headless).
	View Look()
	{
		View v;
		v.traktor = options.demo || bridge.Silence() < 2.0;
		v.rate    = options.demo ? 33 : bridge.Rate();
		if( !options.demo )
			sender.SetProgram( v.traktor ? "Traktor" : "" );
		double now = Now();
		if( now - lastScan > 3 )
		{
			lastScan   = now;
			folders    = FindTraktor( options.traktorFolder );
			interfaces = Interfaces();
		}
		v.folders = folders;
		for( const Interface& i : interfaces )
			if( !i.loopback && i.ip )
			{
				if( v.networked )
				{
					v.network += "  +";
					break;
				}
				v.networked = true;
				Address a{ i.ip, 0 };
				v.network   = i.name + "  " + a.IpText();
			}
		v.vjs = sender.Subscribers();
		{
			std::lock_guard< std::mutex > lock( mutex );
			std::copy( decks, decks + kDecks, v.decks );
			std::copy( tracks, tracks + kDecks, v.tracks );
		}
		v.message = message;
		return v;
	}

	// Install (or remove) the script, asking for an administrator when needed.
	void Change( bool install )
	{
		std::vector< TraktorFolder > list = FindTraktor( options.traktorFolder );
		if( list.empty() )
		{
			message = "Traktor was not found.";
			return;
		}
		bool needAdmin = false;
		std::string error;
		for( const TraktorFolder& t : list )
		{
			bool denied = false;
			bool ok     = install ? InstallInto( t, error, denied ) : RemoveFrom( t, error, denied );
			needAdmin |= !ok && denied;
		}
		if( needAdmin && !RunAsAdministrator( install ? "--install" : "--uninstall" ) )
		{
			message = "Not changed: the administrator's permission was not given.";
			lastScan = 0;
			return;
		}
		message  = install ? "Done. Restart Traktor." : "Removed. Restart Traktor.";
		lastScan = 0;
	}

	const Options& options;

private:
	void Keep( const DeckState* d, const TrackInfo* t, int deck )
	{
		std::lock_guard< std::mutex > lock( mutex );
		if( d )
			std::copy( d, d + kDecks, decks );
		if( t && deck >= 0 && deck < kDecks )
			tracks[ deck ] = *t;
	}

	Sender sender;
	TraktorBridge bridge;
	Demo demo;
	std::thread demoThread;
	std::atomic< bool > running{ true };
	std::mutex mutex;
	DeckState decks[ kDecks ];
	TrackInfo tracks[ kDecks ];
	std::vector< TraktorFolder > folders;
	std::vector< Interface > interfaces;
	double lastScan = -10;
	std::string message;
};

int InstallCommand( const Options& o, bool install )
{
	std::vector< TraktorFolder > list = FindTraktor( o.traktorFolder );
	if( list.empty() )
	{
		std::fprintf( stderr, "Traktor was not found.\n" );
		return 2;
	}
	int code = 0;
	for( const TraktorFolder& t : list )
	{
		std::string error;
		bool denied = false;
		if( install ? InstallInto( t, error, denied ) : RemoveFrom( t, error, denied ) )
			std::printf( "%s: %s (%s)\n", t.name.c_str(), install ? "installed" : "removed", t.folder.c_str() );
		else
		{
			std::fprintf( stderr, "%s: %s\n", t.name.c_str(), error.c_str() );
			code = denied ? 3 : 1;
		}
	}
	return code;
}

#ifndef NIMSLINK_HEADLESS_ONLY
// ---- The window

namespace colour
{
const ImU32 Background = IM_COL32( 0x0b, 0x0b, 0x0b, 255 );
const ImU32 Panel      = IM_COL32( 0x1b, 0x1b, 0x1b, 255 );
const ImU32 Panel2     = IM_COL32( 0x26, 0x26, 0x26, 255 );
const ImU32 Text       = IM_COL32( 0xd8, 0xd8, 0xd8, 255 );
const ImU32 Dim        = IM_COL32( 0x8c, 0x8c, 0x8c, 255 );
const ImU32 Muted      = IM_COL32( 0x5f, 0x5f, 0x5f, 255 );
const ImU32 Lime       = IM_COL32( 0xa9, 0xd2, 0x3a, 255 );
const ImU32 Off        = IM_COL32( 0x3a, 0x3a, 0x3a, 255 );
const ImU32 Orange     = IM_COL32( 0xff, 0x6a, 0x1f, 255 );
const ImU32 Blue       = IM_COL32( 0x2e, 0x8c, 0xf2, 255 );
const ImU32 Deck[ 4 ]  = { IM_COL32( 0xff, 0x3b, 0x30, 255 ), IM_COL32( 0x2e, 0x8c, 0xf2, 255 ), IM_COL32( 0x5f, 0xd3, 0x5a, 255 ),
	                       IM_COL32( 0xff, 0xd0, 0x2e, 255 ) };
}

// Fonts are made at the framebuffer's resolution (twice the window's on a Retina screen) and drawn at the window's.
float gPixel = 1;
float Px( const ImFont* font )
{
	return font->FontSize / gPixel;
}

struct Fonts
{
	ImFont *body = nullptr, *bold = nullptr, *small = nullptr, *title = nullptr;
};

std::string Fit( ImFont* font, const std::string& text, float width )
{
	if( font->CalcTextSizeA( Px( font ), FLT_MAX, 0, text.c_str() ).x <= width )
		return text;
	std::string t = text;
	while( !t.empty() )
	{
		t.pop_back();
		while( !t.empty() && ( (unsigned char)t.back() & 0xC0 ) == 0x80 )
			t.pop_back();
		std::string e = t + "...";
		if( font->CalcTextSizeA( Px( font ), FLT_MAX, 0, e.c_str() ).x <= width )
			return e;
	}
	return "";
}

void Led( ImDrawList* dl, ImVec2 c, float r, bool on )
{
	if( on )
		dl->AddCircleFilled( c, r * 1.6f, IM_COL32( 0xa9, 0xd2, 0x3a, 36 ) );
	dl->AddCircleFilled( c, r, on ? colour::Lime : colour::Off );
}

// One of the three status rows: light, label, value, and a note on the right.
void Row( ImDrawList* dl, const Fonts& f, float s, ImVec2 at, float width, bool on, const char* label, const std::string& value,
          const std::string& note )
{
	float h = 50 * s;
	dl->AddRectFilled( at, ImVec2( at.x + width, at.y + h ), colour::Panel );
	Led( dl, ImVec2( at.x + 20 * s, at.y + h / 2 ), 6 * s, on );
	dl->AddText( f.small, Px( f.small ), ImVec2( at.x + 38 * s, at.y + 8 * s ), colour::Muted, label );
	float noteWidth = note.empty() ? 0 : f.body->CalcTextSizeA( Px( f.body ), FLT_MAX, 0, note.c_str() ).x;
	std::string v   = Fit( f.body, value, width - 38 * s - noteWidth - 28 * s );
	dl->AddText( f.body, Px( f.body ), ImVec2( at.x + 38 * s, at.y + 24 * s ), colour::Text, v.c_str() );
	if( !note.empty() )
		dl->AddText( f.body, Px( f.body ), ImVec2( at.x + width - 12 * s - noteWidth, at.y + 17 * s ), colour::Dim, note.c_str() );
}

// A text button drawn on the list; true when clicked.
bool Button( ImDrawList* dl, const Fonts& f, float s, ImVec2 at, const char* text, bool primary, const char* id )
{
	ImVec2 size = f.bold->CalcTextSizeA( Px( f.bold ), FLT_MAX, 0, text );
	ImVec2 box( size.x + 24 * s, 30 * s );
	ImGui::SetCursorScreenPos( at );
	bool clicked = ImGui::InvisibleButton( id, box );
	bool hover   = ImGui::IsItemHovered();
	ImU32 fill   = primary ? ( hover ? IM_COL32( 0x4a, 0x9e, 0xf5, 255 ) : colour::Blue ) : ( hover ? IM_COL32( 0x33, 0x33, 0x33, 255 ) : colour::Panel2 );
	dl->AddRectFilled( at, ImVec2( at.x + box.x, at.y + box.y ), fill );
	dl->AddText( f.bold, Px( f.bold ), ImVec2( at.x + 12 * s, at.y + ( box.y - size.y ) / 2 ), primary ? IM_COL32_WHITE : colour::Text, text );
	return clicked;
}

void Draw( Transmitter& tx, const View& v, const Fonts& f, float s, ImVec2 size )
{
	ImDrawList* dl = ImGui::GetWindowDrawList();
	ImVec2 o       = ImGui::GetWindowPos();
	float pad      = 16 * s;
	float width    = size.x - 2 * pad;
	float y        = o.y + pad;
	dl->AddRectFilled( o, ImVec2( o.x + size.x, o.y + size.y ), colour::Background );
	// Title.
	dl->AddText( f.title, Px( f.title ), ImVec2( o.x + pad, y ), colour::Text, "NIMS PRO LINK" );
	float tw = f.title->CalcTextSizeA( Px( f.title ), FLT_MAX, 0, "NIMS PRO LINK" ).x;
	dl->AddText( f.small, Px( f.small ), ImVec2( o.x + pad + tw + 12 * s, y + 5 * s ), colour::Muted, "TRANSMITTER" );
	y += 32 * s;

	// Traktor.
	bool installed = false, outdated = false;
	for( const TraktorFolder& t : v.folders )
	{
		installed |= t.state == TraktorFolder::State::Installed;
		outdated |= t.state == TraktorFolder::State::Outdated;
	}
	std::string traktorValue = tx.options.demo ? "Demo decks (not Traktor)"
	                           : v.traktor     ? "Traktor connected"
	                           : v.folders.empty() ? "Traktor not found"
	                           : !installed && !outdated ? "Not set up in Traktor yet"
	                                                     : "Waiting for Traktor";
	char rate[ 32 ];
	std::snprintf( rate, sizeof( rate ), "%d updates/s", (int)std::lround( v.rate ) );
	Row( dl, f, s, ImVec2( o.x + pad, y ), width, v.traktor, "TRAKTOR", traktorValue, v.traktor ? rate : "" );
	y += 56 * s;
	// The network.
	Row( dl, f, s, ImVec2( o.x + pad, y ), width, v.networked, "NETWORK", v.networked ? v.network : "No network: this computer only",
	     v.networked ? "on the network" : "" );
	y += 56 * s;
	// The VJs listening.
	std::string names;
	float worst = 0;
	for( const Sender::Subscriber& vj : v.vjs )
	{
		names += ( names.empty() ? "" : ", " ) + vj.name;
		worst = std::max( worst, vj.roundTrip );
	}
	char trip[ 32 ] = "";
	if( !v.vjs.empty() && worst > 0 )
		std::snprintf( trip, sizeof( trip ), worst < 0.001f ? "< 1 ms" : "%.0f ms", worst * 1000 / 2 );
	Row( dl, f, s, ImVec2( o.x + pad, y ), width, !v.vjs.empty(), "VJs CONNECTED", v.vjs.empty() ? "None yet" : names, trip );
	y += 60 * s;

	// The four decks.
	float gap = 4 * s;
	float dw  = ( width - 3 * gap ) / 4;
	float dh  = 64 * s;
	for( int d = 0; d < kDecks; ++d )
	{
		ImVec2 a( o.x + pad + d * ( dw + gap ), y );
		dl->AddRectFilled( a, ImVec2( a.x + dw, a.y + dh ), colour::Panel );
		dl->AddRectFilled( a, ImVec2( a.x + dw, a.y + 3 * s ), colour::Deck[ d ] );
		const DeckState& st = v.decks[ d ];
		char letter[ 2 ]    = { (char)( 'A' + d ), 0 };
		float lx            = a.x + 8 * s, ly = a.y + 10 * s;
		ImU32 lc            = st.loaded && st.playing ? colour::Lime : colour::Dim;
		if( st.loaded && st.playing )
			dl->AddTriangleFilled( ImVec2( lx, ly + 2 * s ), ImVec2( lx, ly + 11 * s ), ImVec2( lx + 8 * s, ly + 6.5f * s ), lc );
		else if( st.loaded )
			dl->AddRectFilled( ImVec2( lx, ly + 2.5f * s ), ImVec2( lx + 8 * s, ly + 10.5f * s ), lc );
		dl->AddText( f.small, Px( f.small ), ImVec2( lx + ( st.loaded ? 12 * s : 0 ), ly ), lc, letter );
		std::string title = st.loaded ? v.tracks[ d ].title : "";
		dl->AddText( f.bold, Px( f.bold ), ImVec2( lx, a.y + 26 * s ), st.loaded ? colour::Text : colour::Muted,
		             Fit( f.bold, st.loaded ? ( title.empty() ? "Untitled" : title ) : "-", dw - 16 * s ).c_str() );
		// What the VJs have for it.
		Visual best = Visual::None;
		for( const Sender::Subscriber& vj : v.vjs )
			best = std::max( best, vj.visuals[ d ] );
		if( st.loaded && !v.vjs.empty() )
		{
			const char* text = best == Visual::OnAir ? "visual on air" : best == Visual::Ready ? "visual ready" : "no visual";
			dl->AddText( f.small, Px( f.small ), ImVec2( lx, a.y + 45 * s ), best == Visual::OnAir ? colour::Orange : colour::Dim,
			             Fit( f.small, text, dw - 16 * s ).c_str() );
		}
	}
	y += dh + 12 * s;

	// What to do, when something is missing; otherwise the line from the mock-up.
	auto line = [ & ]( const std::string& text, ImU32 c ) {
		dl->AddText( f.body, Px( f.body ), ImVec2( o.x + pad, y ), c, Fit( f.body, text, width ).c_str() );
		y += 18 * s;
	};
	if( tx.options.demo )
		line( "Made-up decks, to try the network without Traktor.", colour::Muted );
	else if( !v.traktor && v.folders.empty() )
		line( "Install Traktor Pro 4, then open this again.", colour::Muted );
	else if( !v.traktor && ( !installed || outdated ) )
	{
		line( outdated ? "A newer NIMS PRO LINK script is ready for Traktor." : "NIMS PRO LINK needs a small script in Traktor.", colour::Dim );
		if( Button( dl, f, s, ImVec2( o.x + pad, y + 2 * s ), outdated ? "Update Traktor" : "Set up Traktor", true, "install" ) )
			tx.Change( true );
		y += 40 * s;
	}
	else if( !v.traktor )
	{
		line( "Start Traktor. In Preferences > Controller Manager, add a", colour::Dim );
		line( "Kontrol D2 (no hardware needed): NIMS PRO LINK reads its screens.", colour::Dim );
	}
	else
		line( "No settings: it finds the VJs on the same network.", colour::Muted );
	if( !v.message.empty() )
		line( v.message, colour::Dim );
	if( ( installed || outdated ) && !tx.options.demo )
	{
		// Out of the way, bottom right.
		const char* text = "Remove from Traktor";
		ImVec2 ts        = f.small->CalcTextSizeA( Px( f.small ), FLT_MAX, 0, text );
		ImVec2 at( o.x + size.x - pad - ts.x, o.y + size.y - pad - ts.y );
		ImGui::SetCursorScreenPos( at );
		if( ImGui::InvisibleButton( "remove", ts ) )
			tx.Change( false );
		dl->AddText( f.small, Px( f.small ), at, ImGui::IsItemHovered() ? colour::Text : colour::Muted, text );
	}
}

void SaveBmp( const std::string& path, int w, int h )
{
	using GetPixels = void ( * )( int, int, int, int, unsigned, unsigned, void* );
	auto readPixels = (GetPixels)glfwGetProcAddress( "glReadPixels" );
	std::vector< unsigned char > rgba( (size_t)w * h * 4 );
	readPixels( 0, 0, w, h, 0x1908 /* GL_RGBA */, 0x1401 /* GL_UNSIGNED_BYTE */, rgba.data() );
	FILE* out = std::fopen( path.c_str(), "wb" );
	if( !out )
		return;
	int row = w * 3 + ( 4 - ( w * 3 ) % 4 ) % 4;
	unsigned size = 54 + (unsigned)row * h;
	unsigned char head[ 54 ] = { 'B', 'M' };
	auto put = [ & ]( int at, unsigned v ) {
		for( int i = 0; i < 4; ++i )
			head[ at + i ] = (unsigned char)( v >> ( 8 * i ) );
	};
	put( 2, size );
	put( 10, 54 );
	put( 14, 40 );
	put( 18, (unsigned)w );
	put( 22, (unsigned)h );
	head[ 26 ] = 1;
	head[ 28 ] = 24;
	std::fwrite( head, 1, 54, out );
	std::vector< unsigned char > line( (size_t)row );
	for( int y = 0; y < h; ++y )
	{
		for( int x = 0; x < w; ++x )
		{
			const unsigned char* p = &rgba[ ( (size_t)y * w + x ) * 4 ];
			line[ x * 3 ]          = p[ 2 ];
			line[ x * 3 + 1 ]      = p[ 1 ];
			line[ x * 3 + 2 ]      = p[ 0 ];
		}
		std::fwrite( line.data(), 1, line.size(), out );
	}
	std::fclose( out );
}

int RunWindow( Transmitter& tx )
{
	if( !glfwInit() )
	{
		std::fprintf( stderr, "No window system: use --headless.\n" );
		return 1;
	}
	glfwWindowHint( GLFW_CONTEXT_VERSION_MAJOR, 3 );
	glfwWindowHint( GLFW_CONTEXT_VERSION_MINOR, 2 );
	glfwWindowHint( GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE );
	glfwWindowHint( GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE );
	glfwWindowHint( GLFW_RESIZABLE, GLFW_FALSE );
	glfwWindowHint( GLFW_SCALE_TO_MONITOR, GLFW_TRUE );
	GLFWwindow* window = glfwCreateWindow( 440, 390, "NIMS PRO LINK Transmitter", nullptr, nullptr );
	if( !window )
	{
		glfwTerminate();
		return 1;
	}
	glfwMakeContextCurrent( window );
	glfwSwapInterval( 1 );
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io    = ImGui::GetIO();
	io.IniFilename = nullptr;
	float sx = 1, sy = 1;
	glfwGetWindowContentScale( window, &sx, &sy );
	int fbw, fbh, ww, wh;
	glfwGetFramebufferSize( window, &fbw, &fbh );
	glfwGetWindowSize( window, &ww, &wh );
	float pixel = ww > 0 ? (float)fbw / ww : 1; // retina: the framebuffer is bigger than the window
	gPixel      = pixel;
	float s     = std::max( 1.0f, sx / pixel );
	ImFontConfig config;
	config.FontDataOwnedByAtlas = false;
	auto font = [ & ]( const unsigned char* data, size_t size, float px ) {
		return io.Fonts->AddFontFromMemoryTTF( (void*)data, (int)size, px * s * pixel, &config );
	};
	Fonts f;
	f.body  = font( stltvj::kFont_Inter_Regular, stltvj::kFont_Inter_Regular_size, 14 );
	f.bold  = font( stltvj::kFont_Inter_SemiBold, stltvj::kFont_Inter_SemiBold_size, 13 );
	f.small = font( stltvj::kFont_Inter_SemiBold, stltvj::kFont_Inter_SemiBold_size, 10.5f );
	f.title = font( stltvj::kFont_Inter_SemiBold, stltvj::kFont_Inter_SemiBold_size, 16 );
	io.FontGlobalScale = 1.0f / pixel;
	ImGui_ImplGlfw_InitForOpenGL( window, true );
	ImGui_ImplOpenGL3_Init( "#version 150" );
	using Clear      = void ( * )( unsigned );
	using ClearColor = void ( * )( float, float, float, float );
	auto glClear_      = (Clear)glfwGetProcAddress( "glClear" );
	auto glClearColor_ = (ClearColor)glfwGetProcAddress( "glClearColor" );
	int frame = 0;
	while( !glfwWindowShouldClose( window ) )
	{
		glfwWaitEventsTimeout( 1.0 / 30 );
		ImGui_ImplOpenGL3_NewFrame();
		ImGui_ImplGlfw_NewFrame();
		ImGui::NewFrame();
		View v = tx.Look();
		ImGui::SetNextWindowPos( ImVec2( 0, 0 ) );
		ImGui::SetNextWindowSize( io.DisplaySize );
		ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding, ImVec2( 0, 0 ) );
		ImGui::Begin( "transmitter", nullptr,
		              ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoSavedSettings |
		                  ImGuiWindowFlags_NoBringToFrontOnFocus );
		Draw( tx, v, f, s, io.DisplaySize );
		ImGui::End();
		ImGui::PopStyleVar();
		ImGui::Render();
		glfwGetFramebufferSize( window, &fbw, &fbh );
		glClearColor_( 0.043f, 0.043f, 0.043f, 1 );
		glClear_( 0x4000 );
		ImGui_ImplOpenGL3_RenderDrawData( ImGui::GetDrawData() );
		if( !tx.options.capture.empty() && ++frame >= std::max( 1, tx.options.frames ) )
		{
			SaveBmp( tx.options.capture, fbw, fbh );
			break;
		}
		glfwSwapBuffers( window );
	}
	ImGui_ImplOpenGL3_Shutdown();
	ImGui_ImplGlfw_Shutdown();
	ImGui::DestroyContext();
	glfwDestroyWindow( window );
	glfwTerminate();
	return 0;
}
#endif

int RunHeadless( Transmitter& tx )
{
	double printed = -10;
	while( true )
	{
		double now = Now();
		if( now - printed >= 5 )
		{
			printed = now;
			View v  = tx.Look();
			std::printf( "Traktor: %s (%.0f/s)  VJs: %d", v.traktor ? "sending" : "silent", v.rate, (int)v.vjs.size() );
			for( const Sender::Subscriber& vj : v.vjs )
				std::printf( "  %s %s %.2f ms", vj.name.c_str(), vj.address.IpText().c_str(), vj.roundTrip * 1000 );
			std::printf( "\n" );
			std::fflush( stdout );
		}
		std::this_thread::sleep_for( std::chrono::milliseconds( 200 ) );
	}
}
}

int main( int argc, char** argv )
{
	Options o;
	for( int i = 1; i < argc; ++i )
	{
		std::string a = argv[ i ];
		auto next     = [ & ]() { return i + 1 < argc ? std::string( argv[ ++i ] ) : std::string(); };
		if( a == "--install" )
			o.install = true;
		else if( a == "--uninstall" )
			o.uninstall = true;
		else if( a == "--demo" )
			o.demo = true;
		else if( a == "--headless" )
			o.headless = true;
		else if( a == "--name" )
			o.name = next();
		else if( a == "--capture" )
			o.capture = next();
		else if( a == "--frames" )
			o.frames = std::atoi( next().c_str() );
		else if( a == "--traktor" )
			o.traktorFolder = next();
		else if( a == "--version" )
		{
			std::printf( "NIMS PRO LINK transmitter %s\n", kTransmitterVersion );
			return 0;
		}
	}
	if( o.install || o.uninstall )
		return InstallCommand( o, o.install );
	KeepAwake();
	Transmitter tx( o );
	std::string error;
	if( !tx.Start( error ) )
	{
		std::fprintf( stderr, "NIMS PRO LINK transmitter: %s\n", error.c_str() );
#ifndef NIMSLINK_HEADLESS_ONLY
		if( !o.headless && glfwInit() )
		{
			// Said in a window too: double-clicked, there is no console to read.
			GLFWwindow* w = glfwCreateWindow( 520, 80, ( "NIMS PRO LINK: " + error ).c_str(), nullptr, nullptr );
			while( w && !glfwWindowShouldClose( w ) )
				glfwWaitEvents();
			glfwTerminate();
		}
#endif
		return 1;
	}
	int code = 0;
#ifndef NIMSLINK_HEADLESS_ONLY
	if( !o.headless )
		code = RunWindow( tx );
	else
#endif
		code = RunHeadless( tx );
	tx.Stop();
	return code;
}
