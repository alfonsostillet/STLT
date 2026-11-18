#include "TraktorInstall.h"
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#elif defined( __APPLE__ )
#include <mach-o/dyld.h>
#include <spawn.h>
#include <sys/wait.h>
extern char** environ;
#endif

namespace nilink
{
// The scripts, compiled in from resources/nilink/qml.
extern const unsigned char kText_TraktorNILink[];
extern const size_t kText_TraktorNILink_size;
extern const unsigned char kText_NILinkDeck[];
extern const size_t kText_NILinkDeck_size;

namespace
{
namespace fs = std::filesystem;

const char* kLine      = "  TraktorNILink { name: \"nilink\" } // NI LINK (added by the NI LINK transmitter)";
const char* kBackup    = "D2.qml.nilink-backup";
// What the prototypes before NI LINK left (the probe, then "VJ LINK"): their files go, their copy of Traktor's own
// D2.qml is the original.
const char* kOldFiles[]   = { "TraktorVJProbe.qml", "VJProbeDeck.qml", "VJProbeGlobal.qml", "TraktorVJLink.qml", "VJLinkDeck.qml" };
const char* kOldBackups[] = { "D2.qml.vjprobe-backup", "D2.qml.vjlink-backup" };

struct Script
{
	const char* name;
	std::string text;
};
std::vector< Script > Scripts()
{
	return { { "TraktorNILink.qml", std::string( (const char*)kText_TraktorNILink, kText_TraktorNILink_size ) },
	         { "NILinkDeck.qml", std::string( (const char*)kText_NILinkDeck, kText_NILinkDeck_size ) } };
}

bool Read( const fs::path& path, std::string& text )
{
	std::ifstream in( path, std::ios::binary );
	if( !in )
		return false;
	std::stringstream s;
	s << in.rdbuf();
	text = s.str();
	return true;
}

bool Write( const fs::path& path, const std::string& text )
{
	std::ofstream out( path, std::ios::binary | std::ios::trunc );
	out << text;
	return (bool)out;
}

// D2.qml without the lines of ours (this script's and the prototype probe's).
std::string WithoutOurLines( const std::string& text )
{
	std::string out;
	std::istringstream in( text );
	std::string line;
	bool first = true;
	while( std::getline( in, line ) )
	{
		if( line.find( "TraktorNILink" ) != std::string::npos || line.find( "TraktorVJProbe" ) != std::string::npos ||
		    line.find( "TraktorVJLink" ) != std::string::npos )
			continue;
		if( !first )
			out += '\n';
		out += line;
		first = false;
	}
	if( !text.empty() && text.back() == '\n' )
		out += '\n';
	return out;
}

bool CanWrite( const fs::path& folder )
{
	fs::path probe = folder / ".nilink_write_test";
	{
		std::ofstream out( probe, std::ios::binary );
		if( !out )
			return false;
	}
	std::error_code ec;
	fs::remove( probe, ec );
	return true;
}

void Check( TraktorFolder& t )
{
	fs::path folder = fs::u8path( t.folder );
	std::string d2;
	Read( folder / "D2.qml", d2 );
	bool line = d2.find( "TraktorNILink" ) != std::string::npos;
	bool same = true, any = false;
	for( const Script& s : Scripts() )
	{
		std::string have;
		if( Read( folder / s.name, have ) )
			any = true;
		same = same && have == s.text;
	}
	t.state    = line && any ? ( same ? TraktorFolder::State::Installed : TraktorFolder::State::Outdated ) : TraktorFolder::State::NotInstalled;
	t.writable = CanWrite( folder );
}

void Consider( std::vector< TraktorFolder >& list, const fs::path& d2, const std::string& name )
{
	std::error_code ec;
	if( !fs::exists( d2 / "D2.qml", ec ) )
		return;
	for( const TraktorFolder& t : list )
		if( fs::u8path( t.folder ) == d2 )
			return;
	TraktorFolder t;
	t.name   = name;
	t.folder = d2.u8string();
	Check( t );
	list.push_back( t );
}

// Inside an install (a Traktor folder on Windows, a Traktor .app on macOS), where the D2 mapping is.
void ConsiderInstall( std::vector< TraktorFolder >& list, const fs::path& install )
{
	std::string name = install.filename().u8string();
	if( install.extension() == ".app" )
		name = install.stem().u8string();
	Consider( list, install / "Resources64" / "qml" / "CSI" / "D2", name );
	Consider( list, install / "Contents" / "Resources" / "qml" / "CSI" / "D2", name );
	Consider( list, install, name ); // a D2 folder itself
}

void Scan( std::vector< TraktorFolder >& list, const fs::path& parent, int depth )
{
	std::error_code ec;
	if( depth < 0 || !fs::is_directory( parent, ec ) )
		return;
	for( const fs::directory_entry& e : fs::directory_iterator( parent, fs::directory_options::skip_permission_denied, ec ) )
	{
		std::string n = e.path().filename().u8string();
		if( n.find( "Traktor" ) == std::string::npos && n.find( "TRAKTOR" ) == std::string::npos && n != "Native Instruments" )
			continue;
		if( n.find( "Traktor" ) != std::string::npos )
			ConsiderInstall( list, e.path() );
		if( e.path().extension() != ".app" )
			Scan( list, e.path(), depth - 1 );
	}
}
}

std::vector< TraktorFolder > FindTraktor( const std::string& extra )
{
	std::vector< TraktorFolder > list;
	if( !extra.empty() )
		ConsiderInstall( list, fs::u8path( extra ) );
#ifdef _WIN32
	for( const char* variable : { "ProgramW6432", "ProgramFiles", "ProgramFiles(x86)" } )
		if( const char* p = std::getenv( variable ) )
			Scan( list, fs::u8path( p ) / "Native Instruments", 1 );
#elif defined( __APPLE__ )
	Scan( list, "/Applications", 2 );
	if( const char* home = std::getenv( "HOME" ) )
		Scan( list, fs::path( home ) / "Applications", 2 );
#endif
	return list;
}

bool InstallInto( const TraktorFolder& traktor, std::string& error, bool& denied )
{
	denied          = false;
	fs::path folder = fs::u8path( traktor.folder );
	if( !CanWrite( folder ) )
	{
		denied = true;
		error  = "no permission to change " + traktor.folder;
		return false;
	}
	std::string d2;
	if( !Read( folder / "D2.qml", d2 ) )
	{
		error = "cannot read D2.qml in " + traktor.folder;
		return false;
	}
	std::error_code ec;
	fs::path backup = folder / kBackup;
	// Traktor's own D2.qml, kept once: the prototype probe's copy when there is one, else this one without our lines.
	if( !fs::exists( backup, ec ) )
	{
		std::string original;
		bool found = false;
		for( const char* old : kOldBackups )
			found = found || Read( folder / old, original );
		if( !found )
			original = WithoutOurLines( d2 );
		if( !Write( backup, original ) )
		{
			error = "cannot keep a copy of D2.qml";
			return false;
		}
	}
	for( const char* p : kOldFiles )
		fs::remove( folder / p, ec );
	for( const char* p : kOldBackups )
		fs::remove( folder / p, ec );
	std::string original;
	Read( backup, original );
	size_t close = original.rfind( "} //Mapping" );
	if( close == std::string::npos )
		close = original.rfind( '}' );
	if( close == std::string::npos )
	{
		error = "D2.qml is not as expected: left as it is";
		return false;
	}
	for( const Script& s : Scripts() )
		if( !Write( folder / s.name, s.text ) )
		{
			error = std::string( "cannot write " ) + s.name;
			return false;
		}
	if( !Write( folder / "D2.qml", original.substr( 0, close ) + kLine + "\n" + original.substr( close ) ) )
	{
		error = "cannot write D2.qml";
		return false;
	}
	return true;
}

bool RemoveFrom( const TraktorFolder& traktor, std::string& error, bool& denied )
{
	denied          = false;
	fs::path folder = fs::u8path( traktor.folder );
	if( !CanWrite( folder ) )
	{
		denied = true;
		error  = "no permission to change " + traktor.folder;
		return false;
	}
	std::error_code ec;
	std::string original;
	bool found = Read( folder / kBackup, original );
	for( const char* old : kOldBackups )
		found = found || Read( folder / old, original );
	if( found )
	{
		if( !Write( folder / "D2.qml", original ) )
		{
			error = "cannot write D2.qml";
			return false;
		}
		fs::remove( folder / kBackup, ec );
		for( const char* old : kOldBackups )
			fs::remove( folder / old, ec );
	}
	else
	{
		std::string d2;
		if( Read( folder / "D2.qml", d2 ) && !Write( folder / "D2.qml", WithoutOurLines( d2 ) ) )
		{
			error = "cannot write D2.qml";
			return false;
		}
	}
	for( const Script& s : Scripts() )
		fs::remove( folder / s.name, ec );
	for( const char* old : kOldFiles )
		fs::remove( folder / old, ec );
	return true;
}

bool RunAsAdministrator( const std::string& argument )
{
#ifdef _WIN32
	wchar_t exe[ MAX_PATH ];
	if( !GetModuleFileNameW( nullptr, exe, MAX_PATH ) )
		return false;
	std::wstring arg( argument.begin(), argument.end() );
	SHELLEXECUTEINFOW info{};
	info.cbSize       = sizeof( info );
	info.fMask        = SEE_MASK_NOCLOSEPROCESS;
	info.lpVerb       = L"runas";
	info.lpFile       = exe;
	info.lpParameters = arg.c_str();
	info.nShow        = SW_HIDE;
	if( !ShellExecuteExW( &info ) || !info.hProcess )
		return false;
	WaitForSingleObject( info.hProcess, 60000 );
	DWORD code = 1;
	GetExitCodeProcess( info.hProcess, &code );
	CloseHandle( info.hProcess );
	return code == 0;
#elif defined( __APPLE__ )
	char path[ 4096 ];
	uint32_t size = sizeof( path );
	if( _NSGetExecutablePath( path, &size ) != 0 )
		return false;
	// macOS asks for an administrator's password, then runs this program with the argument (as root).
	std::string shell = "'";
	for( char c : std::string( path ) )
		shell += c == '\'' ? std::string( "'\\''" ) : std::string( 1, c );
	shell += "' " + argument;
	std::string script = "do shell script \"";
	for( char c : shell )
	{
		if( c == '"' || c == '\\' )
			script += '\\';
		script += c;
	}
	script += "\" with administrator privileges";
	const char* args[] = { "/usr/bin/osascript", "-e", script.c_str(), nullptr };
	pid_t pid;
	if( posix_spawn( &pid, args[ 0 ], nullptr, nullptr, (char* const*)args, environ ) != 0 )
		return false;
	int status = 0;
	waitpid( pid, &status, 0 );
	return WIFEXITED( status ) && WEXITSTATUS( status ) == 0;
#else
	(void)argument;
	return false;
#endif
}
}
