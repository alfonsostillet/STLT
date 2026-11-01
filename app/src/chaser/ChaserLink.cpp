#include "chaser/ChaserLink.h"
#include <cstdlib>
#include <filesystem>
#include <vector>
#include "Settings.h"
#ifdef _WIN32
#include <windows.h>
#else
#include <spawn.h>
#include <sys/wait.h>
extern char** environ;
#endif

namespace fs = std::filesystem;

namespace stltvj
{
namespace chaser
{
#ifndef _WIN32
static std::vector< pid_t > children;
#endif

std::string UseDataDirectory( const std::string& configDir )
{
	fs::path dir = fs::u8path( configDir ) / "chaser";
	std::error_code ignored;
	fs::create_directories( dir, ignored );
	std::string path = dir.u8string();
#ifdef _WIN32
	// The process environment itself, which the effects read, and the C runtime's copy.
	SetEnvironmentVariableW( L"STLT_DATA_DIR", dir.wstring().c_str() );
	_wputenv_s( L"STLT_DATA_DIR", dir.wstring().c_str() );
#else
	setenv( "STLT_DATA_DIR", path.c_str(), 1 );
#endif
	return path;
}

std::string EditorPath()
{
	if( const char* custom = std::getenv( "STLTVJ_CHASER_EDITOR" ); custom && *custom )
		return custom;
	fs::path here = fs::u8path( ExecutableDirectory() );
#if defined( _WIN32 )
	return ( here / "chaser" / "STLT-CHASER.exe" ).u8string();
#elif defined( __APPLE__ )
	return ( here.parent_path() / "Resources" / "chaser" / "STLT-CHASER.app" / "Contents" / "MacOS" / "STLT-CHASER" ).u8string();
#else
	return ( here / "chaser" / "stlt-chaser" ).u8string();
#endif
}

bool OpenEditor( const std::string& stagePath, std::string& error )
{
	std::string editor = EditorPath();
	std::error_code ignored;
	if( !fs::exists( fs::u8path( editor ), ignored ) )
	{
		error = "STLT-CHASER editor not found (" + editor + ")";
		return false;
	}
#ifdef _WIN32
	std::wstring exe   = fs::u8path( editor ).wstring();
	std::wstring stage = fs::u8path( stagePath ).wstring();
	std::wstring line  = L"\"" + exe + L"\" --stlt-vj \"" + stage + L"\"";
	STARTUPINFOW startup{};
	startup.cb = sizeof( startup );
	PROCESS_INFORMATION process{};
	std::wstring workDir = fs::u8path( editor ).parent_path().wstring();
	if( !CreateProcessW( exe.c_str(), line.data(), nullptr, nullptr, FALSE, 0, nullptr, workDir.c_str(), &startup, &process ) )
	{
		error = "could not start STLT-CHASER (error " + std::to_string( GetLastError() ) + ")";
		return false;
	}
	CloseHandle( process.hThread );
	CloseHandle( process.hProcess );
	return true;
#else
	Reap();
	std::vector< std::string > args = { editor, "--stlt-vj", stagePath };
#ifndef __APPLE__
	// Chromium's sandbox helper needs setuid root, which a folder unpacked from an archive does not keep.
	args.insert( args.begin() + 1, "--no-sandbox" );
#endif
	std::vector< char* > argv;
	for( std::string& a : args )
		argv.push_back( a.data() );
	argv.push_back( nullptr );
	pid_t pid = 0;
	int result = posix_spawn( &pid, editor.c_str(), nullptr, nullptr, argv.data(), environ );
	if( result != 0 )
	{
		error = "could not start STLT-CHASER (error " + std::to_string( result ) + ")";
		return false;
	}
	children.push_back( pid );
	return true;
#endif
}

void Reap()
{
#ifndef _WIN32
	for( size_t i = 0; i < children.size(); )
	{
		if( waitpid( children[ i ], nullptr, WNOHANG ) != 0 )
			children.erase( children.begin() + (long)i );
		else
			++i;
	}
#endif
}
}
}
