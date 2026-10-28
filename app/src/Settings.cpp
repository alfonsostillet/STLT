#include "Settings.h"
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

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

std::string ConfigDirectory()
{
#if defined( _WIN32 )
	std::string base = Env( "APPDATA" );
	fs::path dir     = fs::u8path( base.empty() ? HomeDirectory() : base ) / "STLT-VJ";
#elif defined( __APPLE__ )
	fs::path dir = fs::u8path( HomeDirectory() ) / "Library" / "Application Support" / "STLT-VJ";
#else
	std::string base = Env( "XDG_CONFIG_HOME" );
	fs::path dir     = ( base.empty() ? fs::u8path( HomeDirectory() ) / ".config" : fs::u8path( base ) ) / "stlt-vj";
#endif
	std::error_code ignored;
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
}
