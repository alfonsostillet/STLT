#include "HapFrame.h"
#include <hap.h>
#include "ThreadPool.h"

namespace stltvj
{
size_t HapBlockBytes( unsigned format )
{
	switch( format )
	{
	case HapTextureFormat_RGB_DXT1:
	case HapTextureFormat_A_RGTC1: return 8;
	case HapTextureFormat_RGBA_DXT5:
	case HapTextureFormat_YCoCg_DXT5:
	case HapTextureFormat_RGBA_BPTC_UNORM:
	case HapTextureFormat_RGB_BPTC_UNSIGNED_FLOAT:
	case HapTextureFormat_RGB_BPTC_SIGNED_FLOAT: return 16;
	default: return 0;
	}
}

bool ProbeHapFrame( const void* data, size_t size, int width, int height, HapLayout& layout )
{
	layout = HapLayout{};
	if( width <= 0 || height <= 0 )
		return false;
	unsigned count = 0;
	if( HapGetFrameTextureCount( data, (unsigned long)size, &count ) != HapResult_No_Error || count < 1 || count > 2 )
		return false;
	layout.textureCount = count;
	layout.paddedWidth  = ( width + 3 ) & ~3;
	layout.paddedHeight = ( height + 3 ) & ~3;
	size_t blocks       = (size_t)( layout.paddedWidth / 4 ) * (size_t)( layout.paddedHeight / 4 );
	for( unsigned i = 0; i < count; ++i )
	{
		if( HapGetFrameTextureFormat( data, (unsigned long)size, i, &layout.formats[ i ] ) != HapResult_No_Error )
			return false;
		size_t blockBytes = HapBlockBytes( layout.formats[ i ] );
		if( !blockBytes )
			return false;
		layout.bytes[ i ] = blocks * blockBytes;
		if( HapGetFrameTextureChunkCount( data, (unsigned long)size, i, &layout.chunks[ i ] ) != HapResult_No_Error )
			layout.chunks[ i ] = 1;
	}
	return true;
}

static void RunOnPool( HapDecodeWorkFunction function, void* p, unsigned int count, void* info )
{
	ThreadPool* pool = (ThreadPool*)info;
	if( !pool )
	{
		for( unsigned i = 0; i < count; ++i )
			function( p, i );
		return;
	}
	pool->Run( count, [ & ]( unsigned i ) { function( p, i ); } );
}

bool DecodeHapFrame( const void* data, size_t size, const HapLayout& layout, void* const outputs[ 2 ], ThreadPool* pool )
{
	for( unsigned i = 0; i < layout.textureCount; ++i )
	{
		unsigned long used = 0;
		unsigned format    = 0;
		unsigned result    = HapDecode( data, (unsigned long)size, i, RunOnPool, pool, outputs[ i ], (unsigned long)layout.bytes[ i ], &used, &format );
		if( result != HapResult_No_Error || format != layout.formats[ i ] )
			return false;
	}
	return true;
}
}
