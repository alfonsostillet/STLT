#include "Settings.h"
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#ifdef _WIN32
#include <windows.h>
#include <shlobj.h>
#include <shellapi.h> // ShellExecuteW
#elif defined( __APPLE__ )
#include <mach-o/dyld.h>
#include <spawn.h>
extern char** environ;
#endif

namespace fs = std::filesystem;

namespace stltvj
{
static std::string Env( const char* name )
{
	const char* value = std::getenv( name );
	return value ? value : "";
}

std::string HomeDirectory()
{
#ifdef _WIN32
	std::string home = Env( "USERPROFILE" );
#else
	std::string home = Env( "HOME" );
#endif
	return home.empty() ? fs::current_path().u8string() : home;
}

std::string DocumentsDirectory()
{
#ifdef _WIN32
	PWSTR wide = nullptr;
	if( SUCCEEDED( SHGetKnownFolderPath( FOLDERID_Documents, 0, nullptr, &wide ) ) )
	{
		fs::path path( wide );
		CoTaskMemFree( wide );
		return path.u8string();
	}
#endif
	return ( fs::u8path( HomeDirectory() ) / "Documents" ).u8string();
}

std::string ExecutableDirectory()
{
	std::error_code ignored;
#if defined( _WIN32 )
	wchar_t path[ 32768 ];
	DWORD length = GetModuleFileNameW( nullptr, path, 32768 );
	if( length > 0 && length < 32768 )
		return fs::path( std::wstring( path, length ) ).parent_path().u8string();
#elif defined( __APPLE__ )
	char path[ 4096 ];
	uint32_t size = sizeof( path );
	if( _NSGetExecutablePath( path, &size ) == 0 )
		return fs::canonical( fs::u8path( path ), ignored ).parent_path().u8string();
#else
	fs::path self = fs::read_symlink( "/proc/self/exe", ignored );
	if( !ignored )
		return self.parent_path().u8string();
#endif
	return fs::current_path( ignored ).u8string();
}

std::string ConfigDirectory()
{
	// The program was called STLT-VJ: the settings it kept there are copied over the first time (and left where
	// they were, for an older copy of the program).
#if defined( _WIN32 )
	std::string base = Env( "APPDATA" );
	fs::path root    = fs::u8path( base.empty() ? HomeDirectory() : base );
	fs::path dir = root / "STLT", old = root / "STLT-VJ";
#elif defined( __APPLE__ )
	fs::path root = fs::u8path( HomeDirectory() ) / "Library" / "Application Support";
	fs::path dir = root / "STLT", old = root / "STLT-VJ";
#else
	std::string base = Env( "XDG_CONFIG_HOME" );
	fs::path root    = base.empty() ? fs::u8path( HomeDirectory() ) / ".config" : fs::u8path( base );
	fs::path dir = root / "stlt", old = root / "stlt-vj";
#endif
	std::error_code ignored;
	if( !fs::exists( dir, ignored ) && fs::is_directory( old, ignored ) )
		fs::copy( old, dir, fs::copy_options::recursive, ignored );
	fs::create_directories( dir, ignored );
	return dir.u8string();
}

bool ReadTextFile( const std::string& path, std::string& text )
{
	std::ifstream in( fs::u8path( path ), std::ios::binary );
	if( !in )
		return false;
	std::ostringstream buffer;
	buffer << in.rdbuf();
	text = buffer.str();
	return true;
}

bool WriteTextFile( const std::string& path, const std::string& text )
{
	// Write next to the target and swap, so a crash never leaves half a file.
	fs::path target = fs::u8path( path );
	fs::path temp   = target;
	temp += ".tmp";
	{
		std::ofstream out( temp, std::ios::binary | std::ios::trunc );
		if( !out )
			return false;
		out << text;
		if( !out )
			return false;
	}
	std::error_code error;
	fs::rename( temp, target, error );
	return !error;
}

bool OpenSystemDisplaySettings()
{
#ifdef _WIN32
	return (INT_PTR)ShellExecuteW( nullptr, L"open", L"ms-settings:display", nullptr, nullptr, SW_SHOWNORMAL ) > 32;
#elif defined( __APPLE__ )
	pid_t pid;
	char open[] = "/usr/bin/open", url[] = "x-apple.systempreferences:com.apple.preference.displays";
	char* argv[] = { open, url, nullptr };
	return posix_spawn( &pid, open, nullptr, nullptr, argv, environ ) == 0;
#else
	return false;
#endif
}
}
