#include "output/Sharing.h"
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>
#include "gl/Gl.h"
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace stltvj
{
namespace
{
// The few parts of NDI's C interface used here, as the NDI SDK documents them. The library itself is the NDI
// runtime installed on the computer (with NDI Tools), loaded when the first NDI output starts.
struct NdiSendCreate
{
	const char* name;
	const char* groups;
	bool clockVideo;
	bool clockAudio;
};

struct NdiVideoFrame
{
	int xres, yres;
	int fourCC;
	int frameRateN, frameRateD;
	float pictureAspectRatio;
	int frameFormat; // 1: progressive
	int64_t timecode;
	uint8_t* data;
	int lineStride; // bytes
	const char* metadata;
	int64_t timestamp;
};

constexpr int kFourCCRgbx         = 'R' | ( 'G' << 8 ) | ( 'B' << 16 ) | ( 'X' << 24 );
constexpr int64_t kSynthesizeTime = INT64_MAX;

struct NdiRuntime
{
	bool ( *initialize )()                                        = nullptr;
	void* ( *sendCreate )( const NdiSendCreate* )                 = nullptr;
	void ( *sendDestroy )( void* )                                = nullptr;
	void ( *sendVideo )( void*, const NdiVideoFrame* )            = nullptr;
	int ( *connections )( void*, uint32_t )                       = nullptr;
	std::string error;

	// Loads the runtime once; the reason it is missing stays in `error`.
	static NdiRuntime& Get()
	{
		static NdiRuntime runtime = Load();
		return runtime;
	}

private:
	static NdiRuntime Load()
	{
		NdiRuntime r;
		std::vector< std::string > candidates;
		if( const char* path = std::getenv( "STLTVJ_NDI_LIBRARY" ) )
			candidates.push_back( path );
#if defined( _WIN32 )
		const char* file = "Processing.NDI.Lib.x64.dll";
		for( const char* variable : { "NDI_RUNTIME_DIR_V6", "NDI_RUNTIME_DIR_V5" } )
			if( const char* dir = std::getenv( variable ) )
				candidates.push_back( std::string( dir ) + "\\" + file );
		candidates.push_back( file );
#elif defined( __APPLE__ )
		for( const char* variable : { "NDI_RUNTIME_DIR_V6", "NDI_RUNTIME_DIR_V5" } )
			if( const char* dir = std::getenv( variable ) )
				candidates.push_back( std::string( dir ) + "/libndi.dylib" );
		candidates.insert( candidates.end(), { "/usr/local/lib/libndi.dylib", "/Library/NDI SDK for Apple/lib/macOS/libndi.dylib", "libndi.dylib" } );
#else
		for( const char* variable : { "NDI_RUNTIME_DIR_V6", "NDI_RUNTIME_DIR_V5" } )
			if( const char* dir = std::getenv( variable ) )
				candidates.push_back( std::string( dir ) + "/libndi.so.6" );
		candidates.insert( candidates.end(), { "libndi.so.6", "libndi.so.5", "libndi.so" } );
#endif
		for( const std::string& path : candidates )
		{
#ifdef _WIN32
			HMODULE library = LoadLibraryA( path.c_str() );
			auto symbol     = [ & ]( const char* name ) { return library ? (void*)GetProcAddress( library, name ) : nullptr; };
#else
			void* library = dlopen( path.c_str(), RTLD_NOW | RTLD_LOCAL );
			auto symbol   = [ & ]( const char* name ) { return library ? dlsym( library, name ) : nullptr; };
#endif
			if( !library )
				continue;
			r.initialize  = (bool ( * )())symbol( "NDIlib_initialize" );
			r.sendCreate  = (void* ( * )( const NdiSendCreate* ))symbol( "NDIlib_send_create" );
			r.sendDestroy = (void ( * )( void* ))symbol( "NDIlib_send_destroy" );
			r.sendVideo   = (void ( * )( void*, const NdiVideoFrame* ))symbol( "NDIlib_send_send_video_v2" );
			r.connections = (int ( * )( void*, uint32_t ))symbol( "NDIlib_send_get_no_connections" );
			if( !r.initialize || !r.sendCreate || !r.sendDestroy || !r.sendVideo )
			{
				r.error = path + " is not a usable NDI runtime";
				return r;
			}
			if( !r.initialize() )
				r.error = "NDI does not run on this processor";
			return r;
		}
		r.error = "NDI runtime not found: install NDI Tools (ndi.video/tools)";
		return r;
	}
};

// Reads the screen back without waiting for the GPU (three buffers in turn: the one read two frames ago is
// ready), and sends from its own thread, so a slow network drops frames instead of slowing the show.
class NdiTextureSender : public TextureSender
{
public:
	NdiTextureSender( void* instance, NdiRuntime& runtime ) : instance( instance ), runtime( runtime )
	{
		glGenBuffers( 3, pbo );
		glGenFramebuffers( 1, &readFramebuffer );
		network = std::thread( [ this ] { Run(); } );
	}

	~NdiTextureSender() override
	{
		{
			std::lock_guard< std::mutex > lock( mutex );
			quit = true;
		}
		wake.notify_all();
		network.join();
		runtime.sendDestroy( instance );
		glDeleteBuffers( 3, pbo );
		glDeleteFramebuffers( 1, &readFramebuffer );
	}

	void Send( unsigned int texture, int w, int h ) override
	{
		if( w != width || h != height )
		{
			width  = w;
			height = h;
			for( int i = 0; i < 3; ++i )
			{
				glBindBuffer( GL_PIXEL_PACK_BUFFER, pbo[ i ] );
				glBufferData( GL_PIXEL_PACK_BUFFER, (GLsizeiptr)w * h * 4, nullptr, GL_STREAM_READ );
				filled[ i ] = false;
			}
		}
		// This frame into one buffer...
		glBindFramebuffer( GL_READ_FRAMEBUFFER, readFramebuffer );
		glFramebufferTexture2D( GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0 );
		glBindBuffer( GL_PIXEL_PACK_BUFFER, pbo[ next ] );
		glPixelStorei( GL_PACK_ALIGNMENT, 4 );
		glReadPixels( 0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, nullptr );
		filled[ next ] = true;
		// ...and the one from two frames ago to the network, if it is free.
		int ready = ( next + 1 ) % 3;
		if( filled[ ready ] )
		{
			std::unique_lock< std::mutex > lock( mutex, std::try_to_lock );
			if( lock.owns_lock() && !pending )
			{
				glBindBuffer( GL_PIXEL_PACK_BUFFER, pbo[ ready ] );
				const uint8_t* pixels = (const uint8_t*)glMapBufferRange( GL_PIXEL_PACK_BUFFER, 0, (GLsizeiptr)w * h * 4, GL_MAP_READ_BIT );
				if( pixels )
				{
					// OpenGL rows start at the bottom, NDI's at the top.
					size_t stride = (size_t)w * 4;
					frame.resize( stride * h );
					for( int y = 0; y < h; ++y )
						std::memcpy( frame.data() + (size_t)y * stride, pixels + (size_t)( h - 1 - y ) * stride, stride );
					frameWidth  = w;
					frameHeight = h;
					pending     = true;
					glUnmapBuffer( GL_PIXEL_PACK_BUFFER );
				}
				lock.unlock();
				wake.notify_one();
			}
		}
		next = ( next + 1 ) % 3;
		glBindBuffer( GL_PIXEL_PACK_BUFFER, 0 );
		glBindFramebuffer( GL_READ_FRAMEBUFFER, 0 );
	}

	std::string Status() const override
	{
		int n = receivers.load();
		return n > 0 ? "NDI: " + std::to_string( n ) + ( n == 1 ? " receiver" : " receivers" ) : "NDI: waiting for receivers";
	}

private:
	void Run()
	{
		std::vector< uint8_t > sending;
		for( ;; )
		{
			int w = 0, h = 0;
			{
				std::unique_lock< std::mutex > lock( mutex );
				wake.wait( lock, [ this ] { return quit || pending; } );
				if( quit )
					return;
				sending.swap( frame );
				w       = frameWidth;
				h       = frameHeight;
				pending = false;
			}
			NdiVideoFrame video{};
			video.xres               = w;
			video.yres               = h;
			video.fourCC             = kFourCCRgbx;
			video.frameRateN         = 60;
			video.frameRateD         = 1;
			video.pictureAspectRatio = h > 0 ? (float)w / h : 0;
			video.frameFormat        = 1;
			video.timecode           = kSynthesizeTime;
			video.data               = sending.data();
			video.lineStride         = w * 4;
			runtime.sendVideo( instance, &video ); // waits while NDI compresses the frame
			if( runtime.connections )
				receivers = runtime.connections( instance, 0 );
		}
	}

	void* instance;
	NdiRuntime& runtime;
	GLuint pbo[ 3 ]         = {};
	bool filled[ 3 ]        = {};
	int next                = 0;
	GLuint readFramebuffer  = 0;
	int width = 0, height = 0;

	std::thread network;
	std::mutex mutex;
	std::condition_variable wake;
	std::vector< uint8_t > frame; // the next frame for the network, top row first
	int frameWidth = 0, frameHeight = 0;
	bool pending = false, quit = false;
	std::atomic< int > receivers{ 0 };
};
}

std::unique_ptr< TextureSender > CreateNdiSender( const std::string& name, std::string& error )
{
	NdiRuntime& runtime = NdiRuntime::Get();
	if( !runtime.error.empty() )
	{
		error = runtime.error;
		return nullptr;
	}
	NdiSendCreate settings{ name.c_str(), nullptr, false, false };
	void* instance = runtime.sendCreate( &settings );
	if( !instance )
	{
		error = "NDI could not create the source \"" + name + "\"";
		return nullptr;
	}
	return std::make_unique< NdiTextureSender >( instance, runtime );
}
}
