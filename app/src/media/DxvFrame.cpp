// Resolume DXV 3: the outer compression of a frame, undone into GPU-ready textures.
//
// Ported from FFmpeg's DXV decoder (libavcodec/dxv.c and libavcodec/lzf.c):
//   Copyright (C) 2015 Vittorio Giovara, Copyright (C) 2018 Paul B Mahol, LZF Copyright (C) 2015 Luca Barbato.
//   FFmpeg is free software, licensed under the GNU Lesser General Public License 2.1 or later, which allows
//   this port to be distributed under STLT-VJ's GPL 3. The steps keep FFmpeg's names so the two can be compared;
//   the CPU expansion of the textures into pixels is left out, as the GPU reads the textures directly.
#include "media/DxvFrame.h"
#include <algorithm>
#include <cstring>

namespace stltvj
{
namespace
{
constexpr uint32_t Tag( char a, char b, char c, char d )
{
	return ( (uint32_t)(uint8_t)a << 24 ) | ( (uint32_t)(uint8_t)b << 16 ) | ( (uint32_t)(uint8_t)c << 8 ) | (uint32_t)(uint8_t)d;
}
constexpr uint32_t kDxt1 = Tag( 'D', 'X', 'T', '1' );
constexpr uint32_t kDxt5 = Tag( 'D', 'X', 'T', '5' );
constexpr uint32_t kYcg6 = Tag( 'Y', 'C', 'G', '6' );
constexpr uint32_t kYg10 = Tag( 'Y', 'G', '1', '0' );

uint32_t RL32( const uint8_t* p )
{
	return (uint32_t)p[ 0 ] | ( (uint32_t)p[ 1 ] << 8 ) | ( (uint32_t)p[ 2 ] << 16 ) | ( (uint32_t)p[ 3 ] << 24 );
}
uint16_t RL16( const uint8_t* p )
{
	return (uint16_t)( p[ 0 ] | ( p[ 1 ] << 8 ) );
}
void WL32( uint8_t* p, uint32_t v )
{
	p[ 0 ] = (uint8_t)v;
	p[ 1 ] = (uint8_t)( v >> 8 );
	p[ 2 ] = (uint8_t)( v >> 16 );
	p[ 3 ] = (uint8_t)( v >> 24 );
}
void WL16( uint8_t* p, uint16_t v )
{
	p[ 0 ] = (uint8_t)v;
	p[ 1 ] = (uint8_t)( v >> 8 );
}

// FFmpeg's GetByteContext: reads past the end give zeros and stop at the end.
struct Bytes
{
	const uint8_t* start = nullptr;
	const uint8_t* p     = nullptr;
	const uint8_t* end   = nullptr;

	Bytes( const uint8_t* data, size_t size ) : start( data ), p( data ), end( data + size ) {}
	int Left() const { return (int)( end - p ); }
	int Tell() const { return (int)( p - start ); }
	uint32_t Le32()
	{
		if( Left() < 4 )
		{
			p = end;
			return 0;
		}
		uint32_t v = RL32( p );
		p += 4;
		return v;
	}
	uint32_t Le16()
	{
		if( Left() < 2 )
		{
			p = end;
			return 0;
		}
		uint32_t v = RL16( p );
		p += 2;
		return v;
	}
	uint32_t Byte()
	{
		if( Left() < 1 )
			return 0;
		return *p++;
	}
	uint32_t PeekByte() const { return Left() < 1 ? 0 : *p; }
	void Skip( int n ) { p += std::clamp( n, 0, Left() ); }
	void SeekSet( int offset ) { p = start + std::clamp( offset, 0, (int)( end - start ) ); }
	void SeekCur( int offset ) { p = start + std::clamp( Tell() + offset, 0, (int)( end - start ) ); }
	int Read( uint8_t* out, int n )
	{
		n = std::min( n, Left() );
		std::memcpy( out, p, (size_t)n );
		p += n;
		return n;
	}
};

int Log2( unsigned v )
{
	int n = -1;
	while( v )
	{
		v >>= 1;
		++n;
	}
	return n;
}

// ---- DXT1 and DXT5: back references to earlier elements ----

// The 2-bit status: 0 new element, 1 the element x back, 2 and 3 further back by a byte or a word.
struct Checkpoint
{
	uint32_t value = 0;
	int state      = 0;
	bool Next( Bytes& gb, int x, int pos, uint32_t& op, int& idx )
	{
		if( state == 0 )
		{
			if( gb.Left() < 4 )
				return false;
			value = gb.Le32();
			state = 16;
		}
		op = value & 3;
		value >>= 2;
		state--;
		switch( op )
		{
		case 1: idx = x; break;
		case 2:
			idx = ( (int)gb.Byte() + 2 ) * x;
			if( idx > pos )
				return false;
			break;
		case 3:
			idx = ( (int)gb.Le16() + 0x102 ) * x;
			if( idx > pos )
				return false;
			break;
		}
		return true;
	}
};

bool DecompressDxt1( Bytes& gb, uint8_t* tex, size_t texSize )
{
	Checkpoint cp;
	uint32_t op = 0, prev;
	int idx = 0, pos = 2;
	WL32( tex, gb.Le32() );
	WL32( tex + 4, gb.Le32() );
	while( pos + 2 <= (int)( texSize / 4 ) )
	{
		if( !cp.Next( gb, 2, pos, op, idx ) )
			return false;
		if( op )
		{
			prev = RL32( tex + 4 * ( pos - idx ) );
			WL32( tex + 4 * pos, prev );
			pos++;
			prev = RL32( tex + 4 * ( pos - idx ) );
			WL32( tex + 4 * pos, prev );
			pos++;
		}
		else
		{
			if( !cp.Next( gb, 2, pos, op, idx ) )
				return false;
			prev = op ? RL32( tex + 4 * ( pos - idx ) ) : gb.Le32();
			WL32( tex + 4 * pos, prev );
			pos++;
			if( !cp.Next( gb, 2, pos, op, idx ) )
				return false;
			prev = op ? RL32( tex + 4 * ( pos - idx ) ) : gb.Le32();
			WL32( tex + 4 * pos, prev );
			pos++;
		}
	}
	return true;
}

bool DecompressDxt5( Bytes& gb, uint8_t* tex, size_t texSize )
{
	// The 2-bit statuses of the copies and of the checkpoints come from the same words (FFmpeg's shared
	// `value` and `state`).
	Checkpoint cp;
	uint32_t& value = cp.value;
	int& state      = cp.state;
	uint32_t op = 0, prev;
	int idx = 0, pos = 4, run = 0, probe, check;
	const int words = (int)( texSize / 4 );
	WL32( tex + 0, gb.Le32() );
	WL32( tex + 4, gb.Le32() );
	WL32( tex + 8, gb.Le32() );
	WL32( tex + 12, gb.Le32() );
	auto copyBack = [ & ]( int distance ) {
		prev = RL32( tex + 4 * ( pos - distance ) );
		WL32( tex + 4 * pos, prev );
		pos++;
	};
	while( pos + 2 <= words )
	{
		if( run )
		{
			run--;
			copyBack( 4 );
			copyBack( 4 );
		}
		else
		{
			if( gb.Left() < 1 )
				return false;
			if( state == 0 )
			{
				value = gb.Le32();
				state = 16;
			}
			op = value & 3;
			value >>= 2;
			state--;
			switch( op )
			{
			case 0: // long copy
				check = (int)gb.Byte() + 1;
				if( check == 256 )
				{
					do
					{
						probe = (int)gb.Le16();
						check += probe;
					} while( probe == 0xFFFF );
				}
				while( check && pos + 4 <= words )
				{
					copyBack( 4 );
					copyBack( 4 );
					copyBack( 4 );
					copyBack( 4 );
					check--;
				}
				continue;
			case 1: // a new run
				run = (int)gb.Byte();
				if( run == 255 )
				{
					do
					{
						probe = (int)gb.Le16();
						run += probe;
					} while( probe == 0xFFFF );
				}
				copyBack( 4 );
				copyBack( 4 );
				break;
			case 2: // two words from further back
				idx = 8 + 4 * (int)gb.Le16();
				if( idx > pos || (unsigned)( pos - idx ) + 2 > (unsigned)words )
					return false;
				copyBack( idx );
				copyBack( idx );
				break;
			case 3: // two words from the input
				WL32( tex + 4 * pos, gb.Le32() );
				pos++;
				WL32( tex + 4 * pos, gb.Le32() );
				pos++;
				break;
			}
		}

		if( !cp.Next( gb, 4, pos, op, idx ) )
			return false;
		if( pos + 2 > words )
			return false;
		if( op )
		{
			if( idx > pos || (unsigned)( pos - idx ) + 2 > (unsigned)words )
				return false;
			copyBack( idx );
			copyBack( idx );
		}
		else
		{
			if( !cp.Next( gb, 4, pos, op, idx ) )
				return false;
			if( op && ( idx > pos || (unsigned)( pos - idx ) + 2 > (unsigned)words ) )
				return false;
			prev = op ? RL32( tex + 4 * ( pos - idx ) ) : gb.Le32();
			WL32( tex + 4 * pos, prev );
			pos++;
			if( !cp.Next( gb, 4, pos, op, idx ) )
				return false;
			prev = op ? RL32( tex + 4 * ( pos - idx ) ) : gb.Le32();
			WL32( tex + 4 * pos, prev );
			pos++;
		}
	}
	return true;
}

// ---- YCG6 and YG10: opcodes (entropy coded) driving copies of 8-byte blocks ----

struct OpcodeTable
{
	int16_t next;
	uint8_t val1;
	uint8_t val2;
};

bool FillLTable( Bytes& gb, uint32_t* table, int& elements )
{
	unsigned half = 512, bits = 1023, left = 1024, input, mask;
	int value, counter = 0, rshift = 10, lshift = 30;
	mask = gb.Le32() >> 2;
	while( left )
	{
		if( counter >= 256 )
			return false;
		value = (int)( bits & mask );
		left -= bits & mask;
		mask >>= rshift;
		lshift -= rshift;
		table[ counter++ ] = (uint32_t)value;
		if( lshift < 16 )
		{
			if( gb.Left() <= 0 )
				return false;
			input = gb.Le16();
			mask += input << lshift;
			lshift += 16;
		}
		if( left < half )
		{
			half >>= 1;
			bits >>= 1;
			rshift--;
		}
	}
	for( ; !table[ counter - 1 ]; counter-- )
		if( counter <= 1 )
			return false;
	elements = counter;
	if( counter < 256 )
		std::memset( &table[ counter ], 0, 4 * ( 256 - (size_t)counter ) );
	if( lshift >= 16 )
		gb.SeekCur( -2 );
	return true;
}

bool FillOpTable( const uint32_t* table0, OpcodeTable* table1, int elements )
{
	unsigned table2[ 256 ] = { 0 };
	unsigned x = 0;
	int i, j = 2, k = 0;
	table2[ 0 ] = table0[ 0 ];
	for( i = 0; i < elements - 1; i++ )
		table2[ i + 1 ] = table0[ i + 1 ] + table2[ i ];
	if( !table2[ 0 ] )
	{
		do
		{
			k++;
		} while( k < 255 && !table2[ k ] );
	}
	j = 2;
	for( i = 1024; i > 0; i-- )
	{
		for( table1[ x ].val1 = (uint8_t)k; k < 256 && (unsigned)j > table2[ k ]; k++ )
			;
		x = ( x - 383 ) & 0x3FF;
		j++;
	}
	if( elements > 0 )
		std::memcpy( &table2[ 0 ], table0, 4 * (size_t)elements );
	for( i = 0; i < 1024; i++ )
	{
		int val0      = table1[ i ].val1;
		unsigned val1 = table2[ val0 ];
		table2[ val0 ]++;
		if( val1 == 0 )
			return false;
		int bitsUsed = Log2( val1 );
		if( bitsUsed > 10 )
			return false;
		table1[ i ].val2 = (uint8_t)( 10 - bitsUsed );
		table1[ i ].next = (int16_t)( ( val1 << table1[ i ].val2 ) - 1024 );
	}
	return true;
}

bool GetOpcodes( Bytes& gb, const uint32_t* table, uint8_t* dst, int opSize, int elements )
{
	OpcodeTable optable[ 1024 ];
	const uint8_t* src = gb.p;
	if( !FillOpTable( table, optable, elements ) )
		return false;
	int64_t sizeInBits = gb.Le32();
	unsigned endoffset = (unsigned)( ( ( sizeInBits + 7 ) >> 3 ) - 4 );
	if( (int)endoffset <= 0 || gb.Left() < (int)endoffset )
		return false;
	// The bits are read backwards from endoffset, four bytes at a time.
	if( (int64_t)endoffset + 4 > (int64_t)( gb.end - src ) )
		return false;
	unsigned offset = endoffset;
	unsigned next   = RL32( src + endoffset );
	int rshift      = ( ( ( (int)( sizeInBits & 0xFF ) ) - 1 ) & 7 ) + 15;
	int lshift      = 32 - rshift;
	int idx         = (int)( ( next >> rshift ) & 0x3FF );
	for( int i = 0; i < opSize; i++ )
	{
		if( idx < 0 || idx >= 1024 )
			return false;
		dst[ i ] = optable[ idx ].val1;
		int val  = optable[ idx ].val2;
		int sum  = val + lshift;
		int x    = (int)( ( next << lshift ) >> 1 >> ( 31 - val ) );
		unsigned newoffset = offset - (unsigned)( sum >> 3 );
		lshift = sum & 7;
		idx    = x + optable[ idx ].next;
		offset = newoffset;
		if( offset > endoffset )
			return false;
		next = RL32( src + offset );
	}
	gb.Skip( (int)( ( ( sizeInBits + 7 ) >> 3 ) - 4 ) );
	return true;
}

// The opcodes of one stream: stored, all the same, or entropy coded. Returns the bytes used, or -1.
int DecompressOpcodes( Bytes& gb, uint8_t* dst, int opSize )
{
	int pos  = gb.Tell();
	int flag = (int)gb.PeekByte();
	if( ( flag & 3 ) == 0 )
	{
		gb.Skip( 1 );
		if( gb.Read( dst, opSize ) != opSize )
			return -1;
	}
	else if( ( flag & 3 ) == 1 )
	{
		gb.Skip( 1 );
		std::memset( dst, (int)gb.Byte(), (size_t)opSize );
	}
	else
	{
		uint32_t table[ 256 ];
		int elements = 0;
		if( !FillLTable( gb, table, elements ) || !GetOpcodes( gb, table, dst, opSize, elements ) )
			return -1;
	}
	return gb.Tell() - pos;
}

uint8_t*& Hashed( uint8_t** table, uint32_t v )
{
	return table[ ( 0x9E3779B1u * v ) >> 24 ];
}

// One 8-byte block (an RGTC1 block, or half of an RGTC2 one), driven by the next opcode.
bool DecompressCgo( Bytes& gb, uint8_t* tex, int texSize, const uint8_t* opData, int& oi, int opSize, uint8_t*& dst, int& state, uint8_t** tab0,
                    uint8_t** tab1, int offset )
{
	uint8_t *tptr0, *tptr1, *tptr3;
	int opcode, v, vv;
	auto back       = [ & ]( int distance ) { return distance >= 0 && distance <= dst - tex; };
	auto hashFirst  = [ & ]() { Hashed( tab0, RL16( dst ) ) = dst; };
	auto hashSecond = [ & ]() { Hashed( tab1, RL32( dst + 2 ) & 0xFFFFFFu ) = dst + 2; };
	if( !back( 8 + offset ) )
		return false;
	if( state <= 0 )
	{
		if( oi >= opSize )
			return false;
		opcode = opData[ oi++ ];
		if( !opcode )
		{
			v = (int)gb.Byte();
			if( v == 255 )
			{
				do
				{
					if( gb.Left() <= 0 )
						return false;
					opcode = (int)gb.Le16();
					v += opcode;
				} while( opcode == 0xFFFF );
			}
			WL32( dst, RL32( dst - ( 8 + offset ) ) );
			WL32( dst + 4, RL32( dst - ( 4 + offset ) ) );
			state = v + 4;
			goto done;
		}
		switch( opcode )
		{
		case 1:
			WL32( dst, RL32( dst - ( 8 + offset ) ) );
			WL32( dst + 4, RL32( dst - ( 4 + offset ) ) );
			break;
		case 2:
			vv = ( 8 + offset ) * ( (int)gb.Le16() + 1 );
			if( !back( vv ) )
				return false;
			tptr0 = dst - vv;
			v     = (int)RL32( tptr0 );
			WL32( dst, RL32( tptr0 ) );
			WL32( dst + 4, RL32( tptr0 + 4 ) );
			Hashed( tab0, (uint16_t)v ) = dst;
			hashSecond();
			break;
		case 3:
			WL32( dst, gb.Le32() );
			WL32( dst + 4, gb.Le32() );
			hashFirst();
			hashSecond();
			break;
		case 4:
			tptr3 = tab1[ gb.Byte() ];
			if( !tptr3 )
				return false;
			WL16( dst, (uint16_t)gb.Le16() );
			WL16( dst + 2, RL16( tptr3 ) );
			dst[ 4 ] = tptr3[ 2 ];
			WL16( dst + 5, (uint16_t)gb.Le16() );
			dst[ 7 ] = (uint8_t)gb.Byte();
			hashFirst();
			break;
		case 5:
			tptr3 = tab1[ gb.Byte() ];
			if( !tptr3 )
				return false;
			WL16( dst, (uint16_t)gb.Le16() );
			WL16( dst + 2, (uint16_t)gb.Le16() );
			dst[ 4 ] = (uint8_t)gb.Byte();
			WL16( dst + 5, RL16( tptr3 ) );
			dst[ 7 ] = tptr3[ 2 ];
			hashFirst();
			hashSecond();
			break;
		case 6:
			tptr0 = tab1[ gb.Byte() ];
			if( !tptr0 )
				return false;
			tptr1 = tab1[ gb.Byte() ];
			if( !tptr1 )
				return false;
			WL16( dst, (uint16_t)gb.Le16() );
			WL16( dst + 2, RL16( tptr0 ) );
			dst[ 4 ] = tptr0[ 2 ];
			WL16( dst + 5, RL16( tptr1 ) );
			dst[ 7 ] = tptr1[ 2 ];
			hashFirst();
			break;
		case 7:
			v = ( 8 + offset ) * ( (int)gb.Le16() + 1 );
			if( !back( v ) )
				return false;
			tptr0 = dst - v;
			WL16( dst, (uint16_t)gb.Le16() );
			WL16( dst + 2, RL16( tptr0 + 2 ) );
			WL32( dst + 4, RL32( tptr0 + 4 ) );
			hashFirst();
			hashSecond();
			break;
		case 8:
			tptr1 = tab0[ gb.Byte() ];
			if( !tptr1 )
				return false;
			WL16( dst, RL16( tptr1 ) );
			WL16( dst + 2, (uint16_t)gb.Le16() );
			WL32( dst + 4, gb.Le32() );
			hashSecond();
			break;
		case 9:
			tptr1 = tab0[ gb.Byte() ];
			if( !tptr1 )
				return false;
			tptr3 = tab1[ gb.Byte() ];
			if( !tptr3 )
				return false;
			WL16( dst, RL16( tptr1 ) );
			WL16( dst + 2, RL16( tptr3 ) );
			dst[ 4 ] = tptr3[ 2 ];
			WL16( dst + 5, (uint16_t)gb.Le16() );
			dst[ 7 ] = (uint8_t)gb.Byte();
			hashSecond();
			break;
		case 10:
			tptr1 = tab0[ gb.Byte() ];
			if( !tptr1 )
				return false;
			tptr3 = tab1[ gb.Byte() ];
			if( !tptr3 )
				return false;
			WL16( dst, RL16( tptr1 ) );
			WL16( dst + 2, (uint16_t)gb.Le16() );
			dst[ 4 ] = (uint8_t)gb.Byte();
			WL16( dst + 5, RL16( tptr3 ) );
			dst[ 7 ] = tptr3[ 2 ];
			hashSecond();
			break;
		case 11:
			tptr0 = tab0[ gb.Byte() ];
			if( !tptr0 )
				return false;
			tptr3 = tab1[ gb.Byte() ];
			if( !tptr3 )
				return false;
			tptr1 = tab1[ gb.Byte() ];
			if( !tptr1 )
				return false;
			WL16( dst, RL16( tptr0 ) );
			WL16( dst + 2, RL16( tptr3 ) );
			dst[ 4 ] = tptr3[ 2 ];
			WL16( dst + 5, RL16( tptr1 ) );
			dst[ 7 ] = tptr1[ 2 ];
			break;
		case 12:
			tptr1 = tab0[ gb.Byte() ];
			if( !tptr1 )
				return false;
			v = ( 8 + offset ) * ( (int)gb.Le16() + 1 );
			if( !back( v ) )
				return false;
			tptr0 = dst - v;
			WL16( dst, RL16( tptr1 ) );
			WL16( dst + 2, RL16( tptr0 + 2 ) );
			WL32( dst + 4, RL32( tptr0 + 4 ) );
			hashSecond();
			break;
		case 13:
			WL16( dst, RL16( dst - ( 8 + offset ) ) );
			WL16( dst + 2, (uint16_t)gb.Le16() );
			WL32( dst + 4, gb.Le32() );
			hashSecond();
			break;
		case 14:
			tptr3 = tab1[ gb.Byte() ];
			if( !tptr3 )
				return false;
			WL16( dst, RL16( dst - ( 8 + offset ) ) );
			WL16( dst + 2, RL16( tptr3 ) );
			dst[ 4 ] = tptr3[ 2 ];
			WL16( dst + 5, (uint16_t)gb.Le16() );
			dst[ 7 ] = (uint8_t)gb.Byte();
			hashSecond();
			break;
		case 15:
			tptr3 = tab1[ gb.Byte() ];
			if( !tptr3 )
				return false;
			WL16( dst, RL16( dst - ( 8 + offset ) ) );
			WL16( dst + 2, (uint16_t)gb.Le16() );
			dst[ 4 ] = (uint8_t)gb.Byte();
			WL16( dst + 5, RL16( tptr3 ) );
			dst[ 7 ] = tptr3[ 2 ];
			hashSecond();
			break;
		case 16:
			tptr3 = tab1[ gb.Byte() ];
			if( !tptr3 )
				return false;
			tptr1 = tab1[ gb.Byte() ];
			if( !tptr1 )
				return false;
			WL16( dst, RL16( dst - ( 8 + offset ) ) );
			WL16( dst + 2, RL16( tptr3 ) );
			dst[ 4 ] = tptr3[ 2 ];
			WL16( dst + 5, RL16( tptr1 ) );
			dst[ 7 ] = tptr1[ 2 ];
			break;
		case 17:
			v = ( 8 + offset ) * ( (int)gb.Le16() + 1 );
			if( !back( v ) )
				return false;
			WL16( dst, RL16( dst - ( 8 + offset ) ) );
			WL16( dst + 2, RL16( &dst[ -v + 2 ] ) );
			WL32( dst + 4, RL32( &dst[ -v + 4 ] ) );
			hashSecond();
			break;
		default: break;
		}
	}
	else
	{
	done:
		WL32( dst, RL32( dst - ( 8 + offset ) ) );
		WL32( dst + 4, RL32( dst - ( 4 + offset ) ) );
		state--;
	}
	if( dst - tex + 8 > texSize )
		return false;
	dst += 8;
	return true;
}

// Two interleaved streams of 8-byte halves: an RGTC2 texture (Co and Cg, or Y and alpha).
bool DecompressCocg( Bytes& gb, uint8_t* tex, int texSize, std::vector< uint8_t >& op0, std::vector< uint8_t >& op1 )
{
	uint8_t *tab0[ 256 ] = {}, *tab1[ 256 ] = {}, *tab2[ 256 ] = {}, *tab3[ 256 ] = {};
	int opOffset      = (int)gb.Le32();
	unsigned opSize0  = gb.Le32();
	unsigned opSize1  = gb.Le32();
	int dataStart     = gb.Tell();
	int oi0 = 0, oi1 = 0, state0 = 0, state1 = 0;
	if( opOffset < 12 || opOffset - 12 > gb.Left() || opSize0 > op0.size() || opSize1 > op1.size() )
		return false;
	uint8_t* dst = tex;
	gb.Skip( opOffset - 12 );
	int skip0 = DecompressOpcodes( gb, op0.data(), (int)opSize0 );
	if( skip0 < 0 )
		return false;
	int skip1 = DecompressOpcodes( gb, op1.data(), (int)opSize1 );
	if( skip1 < 0 )
		return false;
	gb.SeekSet( dataStart );
	WL32( dst, gb.Le32() );
	WL32( dst + 4, gb.Le32() );
	WL32( dst + 8, gb.Le32() );
	WL32( dst + 12, gb.Le32() );
	Hashed( tab0, RL16( dst ) )                    = dst;
	Hashed( tab1, RL32( dst + 2 ) & 0xFFFFFF )     = dst + 2;
	Hashed( tab2, RL16( dst + 8 ) )                = dst + 8;
	Hashed( tab3, RL32( dst + 10 ) & 0xFFFFFF )    = dst + 10;
	dst += 16;
	while( dst + 10 < tex + texSize )
	{
		if( !DecompressCgo( gb, tex, texSize, op0.data(), oi0, (int)opSize0, dst, state0, tab0, tab1, 8 ) )
			return false;
		if( !DecompressCgo( gb, tex, texSize, op1.data(), oi1, (int)opSize1, dst, state1, tab2, tab3, 8 ) )
			return false;
	}
	gb.SeekSet( dataStart - 12 + opOffset + skip0 + skip1 );
	return true;
}

// One stream of 8-byte blocks: an RGTC1 texture (YCG6's Y).
bool DecompressYo( Bytes& gb, uint8_t* tex, int texSize, std::vector< uint8_t >& opData )
{
	uint8_t *table0[ 256 ] = {}, *table1[ 256 ] = {};
	int opOffset     = (int)gb.Le32();
	unsigned opSize  = gb.Le32();
	int dataStart    = gb.Tell();
	int state = 0, oi = 0;
	if( opOffset < 8 || opOffset - 8 > gb.Left() || opSize > opData.size() )
		return false;
	uint8_t* dst = tex;
	gb.Skip( opOffset - 8 );
	int skip = DecompressOpcodes( gb, opData.data(), (int)opSize );
	if( skip < 0 )
		return false;
	gb.SeekSet( dataStart );
	uint32_t v  = gb.Le32();
	WL32( dst, v );
	uint32_t vv = gb.Le32();
	Hashed( table0, (uint16_t)v ) = dst;
	WL32( dst + 4, vv );
	Hashed( table1, RL32( dst + 2 ) & 0xFFFFFF ) = dst + 2;
	dst += 8;
	while( dst < tex + texSize )
		if( !DecompressCgo( gb, tex, texSize, opData.data(), oi, (int)opSize, dst, state, table0, table1, 0 ) )
			return false;
	gb.SeekSet( dataStart + opOffset + skip - 8 );
	return true;
}

// The original DXV's LZF.
bool DecompressLzf( Bytes& gb, uint8_t* out, size_t outSize )
{
	size_t len = 0;
	while( gb.Left() > 2 )
	{
		uint8_t s = (uint8_t)gb.Byte();
		if( s < 32 )
		{
			size_t n = (size_t)s + 1;
			if( n > outSize - len || gb.Read( out + len, (int)n ) != (int)n )
				return false;
			len += n;
		}
		else
		{
			size_t l   = 2 + ( s >> 5 );
			size_t off = ( (size_t)( s & 0x1f ) << 8 ) + 1;
			if( l == 7 + 2 )
				l += gb.Byte();
			off += gb.Byte();
			if( off > len || l > outSize - len )
				return false;
			for( size_t i = 0; i < l; ++i ) // overlapping copies repeat the pattern
				out[ len + i ] = out[ len + i - off ];
			len += l;
		}
	}
	std::memset( out + len, 0, outSize - len ); // as FFmpeg: what the data does not reach stays zero
	return len > 0;
}

struct Header
{
	uint32_t tag    = 0; // kDxt1, kDxt5, kYcg6 or kYg10
	bool raw        = false;
	bool lzf        = false;
	int headerBytes = 0;
};

bool ReadHeader( const uint8_t* data, size_t size, Header& h )
{
	if( size < 12 )
		return false;
	Bytes gb( data, size );
	// FFmpeg reads the tag as a little-endian word and compares it with MKBETAG('D','X','T','1'): the bytes
	// in the file are "1TXD".
	uint32_t word = gb.Le32();
	uint32_t size24 = 0;
	if( word == kDxt1 || word == kDxt5 || word == kYcg6 || word == kYg10 )
	{
		h.tag = word;
		gb.Byte(); // major version
		gb.Byte(); // minor version
		h.raw = gb.Byte() != 0;
		gb.Skip( 1 );
		size24        = gb.Le32();
		h.headerBytes = 12;
	}
	else
	{
		// The first DXV: just a size and a type.
		size24        = word & 0x00FFFFFF;
		int oldType   = (int)( word >> 24 );
		int major     = ( oldType & 0x0F ) - 1;
		h.raw         = ( oldType & 0x80 ) != 0;
		h.lzf         = !h.raw;
		h.headerBytes = 4;
		if( oldType & 0x40 )
			h.tag = kDxt5;
		else if( ( oldType & 0x20 ) || major == 1 )
			h.tag = kDxt1;
		else
			return false;
	}
	return size24 == size - (size_t)h.headerBytes;
}
}

bool IsDxvCodec( uint32_t fourcc )
{
	return fourcc == Tag( 'D', 'X', 'D', '3' ) || fourcc == Tag( 'D', 'X', 'D', 'I' );
}

bool ProbeDxvFrame( const uint8_t* data, size_t size, int width, int height, DxvLayout& layout )
{
	Header h;
	if( width <= 0 || height <= 0 || !ReadHeader( data, size, h ) )
		return false;
	int w = ( width + 3 ) & ~3, hgt = ( height + 3 ) & ~3;
	if( hgt / 2 / 4 < 1 )
		return false;
	layout        = DxvLayout{};
	layout.width  = w;
	layout.height = hgt;
	size_t blocks = (size_t)( w / 4 ) * (size_t)( hgt / 4 );
	if( h.tag == kDxt1 || h.tag == kDxt5 )
	{
		layout.format = h.tag == kDxt1 ? DxvFormat::Dxt1 : DxvFormat::Dxt5;
		layout.bytes  = blocks * ( h.tag == kDxt1 ? 8 : 16 );
		return true;
	}
	if( h.lzf )
		return false; // the first DXV only knew DXT1 and DXT5
	layout.format       = h.tag == kYcg6 ? DxvFormat::Ycg6 : DxvFormat::Yg10;
	layout.bytes        = blocks * ( h.tag == kYcg6 ? 8 : 16 );
	layout.chromaWidth  = w / 2 / 4 * 4;
	layout.chromaHeight = hgt / 2 / 4 * 4;
	layout.chromaBytes  = (size_t)( w / 2 / 4 ) * (size_t)( hgt / 2 / 4 ) * 16;
	return layout.chromaWidth > 0 && layout.chromaHeight > 0;
}

bool DecodeDxvFrame( const uint8_t* data, size_t size, const DxvLayout& layout, uint8_t* texture, uint8_t* chroma )
{
	Header h;
	if( !ReadHeader( data, size, h ) )
		return false;
	Bytes gb( data + h.headerBytes, size - (size_t)h.headerBytes );
	if( h.raw )
	{
		if( gb.Left() < (int)layout.bytes )
			return false;
		gb.Read( texture, (int)layout.bytes );
		return layout.chromaBytes == 0; // raw frames are DXT1 or DXT5
	}
	if( h.lzf )
		return DecompressLzf( gb, texture, layout.bytes );
	switch( layout.format )
	{
	case DxvFormat::Dxt1: return DecompressDxt1( gb, texture, layout.bytes );
	case DxvFormat::Dxt5: return DecompressDxt5( gb, texture, layout.bytes );
	case DxvFormat::Ycg6:
	case DxvFormat::Yg10:
	{
		thread_local std::vector< uint8_t > ops[ 4 ];
		size_t pixels = (size_t)layout.width * layout.height;
		ops[ 0 ].resize( pixels / 16 );
		ops[ 1 ].resize( pixels / 32 );
		ops[ 2 ].resize( pixels / 32 );
		ops[ 3 ].resize( pixels / 16 );
		bool ok = layout.format == DxvFormat::Ycg6 ? DecompressYo( gb, texture, (int)layout.bytes, ops[ 0 ] )
		                                           : DecompressCocg( gb, texture, (int)layout.bytes, ops[ 0 ], ops[ 3 ] );
		return ok && DecompressCocg( gb, chroma, (int)layout.chromaBytes, ops[ 1 ], ops[ 2 ] );
	}
	}
	return false;
}
}
