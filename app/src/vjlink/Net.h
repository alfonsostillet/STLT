#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// The few socket calls VJ LINK needs, the same on Windows, macOS and Linux.
namespace vjlink
{
// Seconds on the steady clock (never jumps), as every time in VJ LINK is kept.
double Now();
// This computer's name, as the other side shows it.
std::string HostName();

struct Address
{
	uint32_t ip    = 0; // host order
	uint16_t port  = 0;
	bool operator==( const Address& o ) const { return ip == o.ip && port == o.port; }
	bool operator!=( const Address& o ) const { return !( *this == o ); }
	std::string Text() const;      // "192.168.1.20:52131"
	std::string IpText() const;    // "192.168.1.20"
	static bool Parse( const std::string& text, uint16_t defaultPort, Address& address ); // "host[:port]", names too
};

// A network this computer is on.
struct Interface
{
	std::string name;   // as the system calls it ("en0", "Ethernet")
	uint32_t ip        = 0;
	uint32_t broadcast = 0;
	bool loopback      = false;
};
std::vector< Interface > Interfaces();

class UdpSocket
{
public:
	UdpSocket() = default;
	~UdpSocket();
	UdpSocket( const UdpSocket& )            = delete;
	UdpSocket& operator=( const UdpSocket& ) = delete;

	// `shared`: other programs may listen on the same port (broadcasts reach all of them). Port 0: any free one.
	bool Open( uint16_t port, bool shared, std::string& error );
	void Close();
	bool IsOpen() const { return handle != -1; }
	uint16_t Port() const { return port; }
	bool Send( const Address& to, const void* data, size_t size );
	// Waits up to `timeout` seconds for a packet on any of `sockets`; the index of one that has one, or -1.
	static int Wait( UdpSocket* const* sockets, int count, double timeout );
	// A packet waiting on this socket (-1: none).
	int Receive( void* data, size_t size, Address& from );

private:
	intptr_t handle = -1;
	uint16_t port   = 0;
};
}
