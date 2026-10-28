#pragma once
#include <cstdint>
#include <string>

namespace stltvj
{
// A read-only file with positional reads, safe to use from several threads at once.
// Opened with sequential-scan hints so the OS read-ahead keeps the disk busy while we decode.
class FileReader
{
public:
	FileReader() = default;
	~FileReader();
	FileReader( const FileReader& ) = delete;
	FileReader& operator=( const FileReader& ) = delete;

	bool Open( const std::string& path );
	void Close();
	bool IsOpen() const;
	uint64_t Size() const { return size; }

	// Reads exactly `bytes` at `offset`; false on a short read or error.
	bool ReadAt( uint64_t offset, void* destination, size_t bytes ) const;

private:
#ifdef _WIN32
	void* handle = nullptr;
#else
	int fd = -1;
#endif
	uint64_t size = 0;
};
}
