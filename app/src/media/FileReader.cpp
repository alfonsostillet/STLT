#include "FileReader.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace stltvj
{
FileReader::~FileReader()
{
	Close();
}

#ifdef _WIN32
static std::wstring Widen( const std::string& utf8 )
{
	int length = MultiByteToWideChar( CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0 );
	std::wstring wide( length > 0 ? length - 1 : 0, L'\0' );
	if( length > 1 )
		MultiByteToWideChar( CP_UTF8, 0, utf8.c_str(), -1, wide.data(), length );
	return wide;
}

bool FileReader::Open( const std::string& path )
{
	Close();
	HANDLE h = CreateFileW( Widen( path ).c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
	                        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr );
	if( h == INVALID_HANDLE_VALUE )
		return false;
	LARGE_INTEGER fileSize;
	if( !GetFileSizeEx( h, &fileSize ) )
	{
		CloseHandle( h );
		return false;
	}
	handle = h;
	size   = (uint64_t)fileSize.QuadPart;
	return true;
}

void FileReader::Close()
{
	if( handle )
		CloseHandle( (HANDLE)handle );
	handle = nullptr;
	size   = 0;
}

bool FileReader::IsOpen() const
{
	return handle != nullptr;
}

bool FileReader::ReadAt( uint64_t offset, void* destination, size_t bytes ) const
{
	char* out = (char*)destination;
	while( bytes > 0 )
	{
		// An OVERLAPPED offset on a synchronous handle is a positional read that leaves the file pointer alone.
		OVERLAPPED overlapped{};
		overlapped.Offset     = (DWORD)( offset & 0xFFFFFFFFull );
		overlapped.OffsetHigh = (DWORD)( offset >> 32 );
		DWORD chunk = bytes > 0x40000000 ? 0x40000000 : (DWORD)bytes;
		DWORD read  = 0;
		if( !ReadFile( (HANDLE)handle, out, chunk, &read, &overlapped ) || read == 0 )
			return false;
		out += read;
		offset += read;
		bytes -= read;
	}
	return true;
}
#else
bool FileReader::Open( const std::string& path )
{
	Close();
	int f = ::open( path.c_str(), O_RDONLY );
	if( f < 0 )
		return false;
	struct stat info;
	if( fstat( f, &info ) != 0 )
	{
		::close( f );
		return false;
	}
#ifdef __APPLE__
	fcntl( f, F_RDAHEAD, 1 );
#else
	posix_fadvise( f, 0, 0, POSIX_FADV_SEQUENTIAL );
#endif
	fd   = f;
	size = (uint64_t)info.st_size;
	return true;
}

void FileReader::Close()
{
	if( fd >= 0 )
		::close( fd );
	fd   = -1;
	size = 0;
}

bool FileReader::IsOpen() const
{
	return fd >= 0;
}

bool FileReader::ReadAt( uint64_t offset, void* destination, size_t bytes ) const
{
	char* out = (char*)destination;
	while( bytes > 0 )
	{
		ssize_t read = ::pread( fd, out, bytes, (off_t)offset );
		if( read <= 0 )
			return false;
		out += read;
		offset += (uint64_t)read;
		bytes -= (size_t)read;
	}
	return true;
}
#endif
}
