#include "nimslink/Net.h"
#include <chrono>
#include <cstdlib>
#include <cstring>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <windows.h>
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace nimslink
{
namespace
{
#ifdef _WIN32
using Native = SOCKET;
bool Ready()
{
	static bool ready = [] {
		WSADATA data;
		return WSAStartup( MAKEWORD( 2, 2 ), &data ) == 0;
	}();
	return ready;
}
void CloseNative( Native s )
{
	closesocket( s );
}
#else
using Native = int;
bool Ready()
{
	return true;
}
void CloseNative( Native s )
{
	close( s );
}
#endif

sockaddr_in ToNative( const Address& a )
{
	sockaddr_in s{};
	s.sin_family      = AF_INET;
	s.sin_addr.s_addr = htonl( a.ip );
	s.sin_port        = htons( a.port );
	return s;
}
}

double Now()
{
	return std::chrono::duration< double >( std::chrono::steady_clock::now().time_since_epoch() ).count();
}

std::string HostName()
{
#ifdef _WIN32
	wchar_t name[ 256 ];
	DWORD size = 256;
	if( GetComputerNameExW( ComputerNamePhysicalDnsHostname, name, &size ) )
	{
		char utf8[ 512 ];
		int n = WideCharToMultiByte( CP_UTF8, 0, name, (int)size, utf8, (int)sizeof( utf8 ), nullptr, nullptr );
		if( n > 0 )
			return std::string( utf8, (size_t)n );
	}
	return "Windows";
#else
	char name[ 256 ] = {};
	if( gethostname( name, sizeof( name ) - 1 ) != 0 || !name[ 0 ] )
		return "computer";
	std::string s = name;
	// "studio.local" -> "studio"
	size_t dot = s.find( '.' );
	if( dot != std::string::npos && dot > 0 )
		s = s.substr( 0, dot );
	return s;
#endif
}

std::string Address::IpText() const
{
	return std::to_string( ip >> 24 ) + "." + std::to_string( ( ip >> 16 ) & 255 ) + "." + std::to_string( ( ip >> 8 ) & 255 ) + "." +
	       std::to_string( ip & 255 );
}

std::string Address::Text() const
{
	return IpText() + ":" + std::to_string( port );
}

bool Address::Parse( const std::string& text, uint16_t defaultPort, Address& address )
{
	if( !Ready() )
		return false;
	std::string host = text;
	uint16_t port    = defaultPort;
	size_t colon     = text.rfind( ':' );
	if( colon != std::string::npos )
	{
		host     = text.substr( 0, colon );
		int p    = std::atoi( text.c_str() + colon + 1 );
		if( p <= 0 || p > 65535 )
			return false;
		port = (uint16_t)p;
	}
	while( !host.empty() && host.back() == ' ' )
		host.pop_back();
	while( !host.empty() && host.front() == ' ' )
		host.erase( host.begin() );
	if( host.empty() )
		return false;
	addrinfo hints{};
	hints.ai_family   = AF_INET;
	hints.ai_socktype = SOCK_DGRAM;
	addrinfo* found   = nullptr;
	if( getaddrinfo( host.c_str(), nullptr, &hints, &found ) != 0 || !found )
		return false;
	address.ip   = ntohl( ( (sockaddr_in*)found->ai_addr )->sin_addr.s_addr );
	address.port = port;
	freeaddrinfo( found );
	return true;
}

std::vector< Interface > Interfaces()
{
	std::vector< Interface > list;
#ifdef _WIN32
	if( !Ready() )
		return list;
	ULONG size = 16 * 1024;
	std::vector< unsigned char > buffer( size );
	ULONG flags = GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER;
	ULONG r     = GetAdaptersAddresses( AF_INET, flags, nullptr, (IP_ADAPTER_ADDRESSES*)buffer.data(), &size );
	if( r == ERROR_BUFFER_OVERFLOW )
	{
		buffer.resize( size );
		r = GetAdaptersAddresses( AF_INET, flags, nullptr, (IP_ADAPTER_ADDRESSES*)buffer.data(), &size );
	}
	if( r != NO_ERROR )
		return list;
	for( IP_ADAPTER_ADDRESSES* a = (IP_ADAPTER_ADDRESSES*)buffer.data(); a; a = a->Next )
	{
		if( a->OperStatus != IfOperStatusUp )
			continue;
		for( IP_ADAPTER_UNICAST_ADDRESS* u = a->FirstUnicastAddress; u; u = u->Next )
		{
			if( u->Address.lpSockaddr->sa_family != AF_INET )
				continue;
			Interface i;
			char name[ 256 ] = {};
			WideCharToMultiByte( CP_UTF8, 0, a->FriendlyName, -1, name, (int)sizeof( name ) - 1, nullptr, nullptr );
			i.name     = name;
			i.ip       = ntohl( ( (sockaddr_in*)u->Address.lpSockaddr )->sin_addr.s_addr );
			int prefix = u->OnLinkPrefixLength;
			uint32_t mask = prefix <= 0 ? 0 : prefix >= 32 ? 0xFFFFFFFFu : ~( ( 1u << ( 32 - prefix ) ) - 1 );
			i.broadcast   = i.ip | ~mask;
			i.loopback    = a->IfType == IF_TYPE_SOFTWARE_LOOPBACK;
			list.push_back( i );
		}
	}
#else
	ifaddrs* all = nullptr;
	if( getifaddrs( &all ) != 0 )
		return list;
	for( ifaddrs* a = all; a; a = a->ifa_next )
	{
		if( !a->ifa_addr || a->ifa_addr->sa_family != AF_INET || !( a->ifa_flags & IFF_UP ) )
			continue;
		Interface i;
		i.name     = a->ifa_name ? a->ifa_name : "";
		i.ip       = ntohl( ( (sockaddr_in*)a->ifa_addr )->sin_addr.s_addr );
		i.loopback = ( a->ifa_flags & IFF_LOOPBACK ) != 0;
		if( ( a->ifa_flags & IFF_BROADCAST ) && a->ifa_broadaddr )
			i.broadcast = ntohl( ( (sockaddr_in*)a->ifa_broadaddr )->sin_addr.s_addr );
		else if( a->ifa_netmask )
			i.broadcast = i.ip | ~ntohl( ( (sockaddr_in*)a->ifa_netmask )->sin_addr.s_addr );
		list.push_back( i );
	}
	freeifaddrs( all );
#endif
	return list;
}

UdpSocket::~UdpSocket()
{
	Close();
}

void UdpSocket::Close()
{
	if( handle != -1 )
		CloseNative( (Native)handle );
	handle = -1;
	port   = 0;
}

bool UdpSocket::Open( uint16_t wanted, bool shared, std::string& error )
{
	Close();
	if( !Ready() )
	{
		error = "network not available";
		return false;
	}
	Native s = socket( AF_INET, SOCK_DGRAM, IPPROTO_UDP );
#ifdef _WIN32
	if( s == INVALID_SOCKET )
#else
	if( s < 0 )
#endif
	{
		error = "cannot open a network socket";
		return false;
	}
	int yes = 1;
	setsockopt( s, SOL_SOCKET, SO_BROADCAST, (const char*)&yes, sizeof( yes ) );
	if( shared )
	{
		setsockopt( s, SOL_SOCKET, SO_REUSEADDR, (const char*)&yes, sizeof( yes ) );
#ifdef SO_REUSEPORT
		setsockopt( s, SOL_SOCKET, SO_REUSEPORT, (const char*)&yes, sizeof( yes ) );
#endif
	}
#ifdef _WIN32
	// A packet sent to a port nobody listens on would otherwise make the next receive fail (WSAECONNRESET).
	BOOL off           = FALSE;
	DWORD returned     = 0;
	const DWORD kReset = 0x9800000C; // SIO_UDP_CONNRESET
	WSAIoctl( s, kReset, &off, sizeof( off ), nullptr, 0, &returned, nullptr, nullptr );
	u_long nonBlocking = 1;
	ioctlsocket( s, FIONBIO, &nonBlocking );
#else
	fcntl( s, F_SETFL, fcntl( s, F_GETFL, 0 ) | O_NONBLOCK );
#endif
	sockaddr_in a = ToNative( { 0, wanted } );
	if( bind( s, (sockaddr*)&a, sizeof( a ) ) != 0 )
	{
		CloseNative( s );
		error = "port " + std::to_string( wanted ) + " is taken by another program";
		return false;
	}
	socklen_t length = sizeof( a );
	getsockname( s, (sockaddr*)&a, &length );
	handle = (intptr_t)s;
	port   = ntohs( a.sin_port );
	return true;
}

bool UdpSocket::Send( const Address& to, const void* data, size_t size )
{
	if( handle == -1 )
		return false;
	sockaddr_in a = ToNative( to );
	return sendto( (Native)handle, (const char*)data, (int)size, 0, (sockaddr*)&a, sizeof( a ) ) == (int)size;
}

int UdpSocket::Wait( UdpSocket* const* sockets, int count, double timeout )
{
	fd_set set;
	FD_ZERO( &set );
	Native top = 0;
	for( int i = 0; i < count; ++i )
		if( sockets[ i ]->IsOpen() )
		{
			FD_SET( (Native)sockets[ i ]->handle, &set );
			if( (Native)sockets[ i ]->handle > top )
				top = (Native)sockets[ i ]->handle;
		}
	timeval tv;
	tv.tv_sec  = (long)timeout;
	tv.tv_usec = (long)( ( timeout - (double)tv.tv_sec ) * 1e6 );
	if( select( (int)top + 1, &set, nullptr, nullptr, &tv ) <= 0 )
		return -1;
	for( int i = 0; i < count; ++i )
		if( sockets[ i ]->IsOpen() && FD_ISSET( (Native)sockets[ i ]->handle, &set ) )
			return i;
	return -1;
}

int UdpSocket::Receive( void* data, size_t size, Address& from )
{
	if( handle == -1 )
		return -1;
	sockaddr_in a{};
	socklen_t length = sizeof( a );
	int n            = (int)recvfrom( (Native)handle, (char*)data, (int)size, 0, (sockaddr*)&a, &length );
	if( n < 0 )
		return -1;
	from.ip   = ntohl( a.sin_addr.s_addr );
	from.port = ntohs( a.sin_port );
	return n;
}
}
