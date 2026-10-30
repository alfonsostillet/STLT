#include "FileDialogs.h"
#include <nfd.h>
#include <atomic>
#include <filesystem>
#include <mutex>
#include <thread>

namespace fs = std::filesystem;

namespace stltvj
{
namespace
{
struct Pending
{
	FileDialogDone onDone;
	std::vector< std::string > paths;
	std::string error;
	bool finished = false;
};

std::mutex mutex;
std::unique_ptr< Pending > pending;
std::atomic< bool > open{ false };

const nfdu8filteritem_t kMediaFilters[] = {
	{ "Movies and images", "mov,mp4,m4v,avi,mkv,webm,mpg,mpeg,ts,mxf,gif,png,jpg,jpeg,bmp,tif,tiff,webp" },
	{ "Movies", "mov,mp4,m4v,avi,mkv,webm,mpg,mpeg,ts,mxf" },
	{ "Images", "png,jpg,jpeg,bmp,gif,tif,tiff,webp" },
};
const nfdu8filteritem_t kCompositionFilters[] = { { "STLT-VJ composition", "stltvj" } };
const nfdu8filteritem_t kShortcutFilters[]    = { { "STLT-VJ shortcuts", "json" } };

// The folder part of a default path: dialogs want an existing folder to start in.
std::string StartFolder( const std::string& path )
{
	if( path.empty() )
		return {};
	std::error_code ignored;
	fs::path p = fs::u8path( path );
	if( fs::is_directory( p, ignored ) )
		return p.u8string();
	return p.parent_path().u8string();
}

void Run( FileDialog kind, const std::string& defaultPath, const std::string& defaultName, Pending& result )
{
	if( NFD_Init() != NFD_OKAY )
	{
		result.error = NFD_GetError() ? NFD_GetError() : "the file dialog could not start";
		return;
	}
	std::string folder  = StartFolder( defaultPath );
	const char* start   = folder.empty() ? nullptr : folder.c_str();
	nfdu8char_t* path   = nullptr;
	nfdresult_t outcome = NFD_CANCEL;
	switch( kind )
	{
	case FileDialog::OpenMedia:
	{
		const nfdpathset_t* set = nullptr;
		outcome                 = NFD_OpenDialogMultipleU8( &set, kMediaFilters, 3, start );
		if( outcome == NFD_OKAY )
		{
			nfdpathsetsize_t count = 0;
			NFD_PathSet_GetCount( set, &count );
			for( nfdpathsetsize_t i = 0; i < count; ++i )
			{
				nfdu8char_t* item = nullptr;
				if( NFD_PathSet_GetPathU8( set, i, &item ) == NFD_OKAY )
				{
					result.paths.push_back( item );
					NFD_PathSet_FreePathU8( item );
				}
			}
			NFD_PathSet_Free( set );
		}
		break;
	}
	case FileDialog::OpenComposition: outcome = NFD_OpenDialogU8( &path, kCompositionFilters, 1, start ); break;
	case FileDialog::SaveComposition:
		outcome = NFD_SaveDialogU8( &path, kCompositionFilters, 1, start, defaultName.empty() ? nullptr : defaultName.c_str() );
		break;
	case FileDialog::PickFolder: outcome = NFD_PickFolderU8( &path, start ); break;
	case FileDialog::OpenAnyFile: outcome = NFD_OpenDialogU8( &path, nullptr, 0, start ); break;
	case FileDialog::OpenShortcuts: outcome = NFD_OpenDialogU8( &path, kShortcutFilters, 1, start ); break;
	case FileDialog::SaveShortcuts:
		outcome = NFD_SaveDialogU8( &path, kShortcutFilters, 1, start, defaultName.empty() ? nullptr : defaultName.c_str() );
		break;
	}
	if( path )
	{
		result.paths.push_back( path );
		NFD_FreePathU8( path );
	}
	if( outcome == NFD_ERROR )
		result.error = NFD_GetError() ? NFD_GetError() : "the file dialog failed";
	// A save dialog may hand back a name without the extension.
	if( kind == FileDialog::SaveComposition && !result.paths.empty() && fs::u8path( result.paths[ 0 ] ).extension() != ".stltvj" )
		result.paths[ 0 ] += ".stltvj";
	if( kind == FileDialog::SaveShortcuts && !result.paths.empty() && fs::u8path( result.paths[ 0 ] ).extension() != ".json" )
		result.paths[ 0 ] += ".json";
	NFD_Quit();
}
}

void ShowFileDialog( FileDialog kind, const std::string& defaultPath, FileDialogDone onDone, const std::string& defaultName )
{
	if( open.exchange( true ) )
		return;
	{
		std::lock_guard< std::mutex > lock( mutex );
		pending         = std::make_unique< Pending >();
		pending->onDone = std::move( onDone );
	}
#ifdef _WIN32
	std::thread( [ kind, defaultPath, defaultName ] {
		Pending result;
		Run( kind, defaultPath, defaultName, result );
		std::lock_guard< std::mutex > lock( mutex );
		pending->paths    = std::move( result.paths );
		pending->error    = std::move( result.error );
		pending->finished = true;
	} ).detach();
#else
	Pending result;
	Run( kind, defaultPath, defaultName, result );
	std::lock_guard< std::mutex > lock( mutex );
	pending->paths    = std::move( result.paths );
	pending->error    = std::move( result.error );
	pending->finished = true;
#endif
}

std::string PollFileDialogs()
{
	std::unique_ptr< Pending > done;
	{
		std::lock_guard< std::mutex > lock( mutex );
		if( !pending || !pending->finished )
			return {};
		done = std::move( pending );
	}
	open = false;
	if( !done->paths.empty() && done->onDone )
		done->onDone( done->paths );
	return done->error;
}

bool FileDialogOpen()
{
	return open;
}
}
