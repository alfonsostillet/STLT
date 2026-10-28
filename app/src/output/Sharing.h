#pragma once
#include <memory>
#include <string>

namespace stltvj
{
// Sends a screen's picture to other applications. Local sharing hands the GPU texture to programs on this
// computer (Spout on Windows, Syphon on macOS); NDI sends it over the network.
class TextureSender
{
public:
	virtual ~TextureSender() = default;
	// Main context current. `texture` is an RGBA texture of width x height, first row at the bottom.
	virtual void Send( unsigned int texture, int width, int height ) = 0;
	// For the panels: how many receivers, or what went wrong.
	virtual std::string Status() const = 0;
};

// "Spout" or "Syphon", or nullptr where this system has no local sharing.
const char* LocalSharingName();
// A local sender called `name`, or nullptr where there is none.
std::unique_ptr< TextureSender > CreateLocalSender( const std::string& name );
// An NDI source called `name`. NDI needs the NDI runtime (it comes with NDI Tools): without it, nullptr and the
// reason in `error`.
std::unique_ptr< TextureSender > CreateNdiSender( const std::string& name, std::string& error );
}
