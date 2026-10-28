#include "output/Sharing.h"
#include <SpoutSender.h>

namespace stltvj
{
namespace
{
// A Spout sender: other programs on this computer receive the texture through DirectX, without a copy to memory.
class SpoutTextureSender : public TextureSender
{
public:
	explicit SpoutTextureSender( const std::string& name ) { sender.SetSenderName( name.c_str() ); }
	~SpoutTextureSender() override { sender.ReleaseSender(); }

	void Send( unsigned int texture, int width, int height ) override
	{
		// OpenGL textures start at the bottom, DirectX ones at the top: Spout turns it over.
		sent = sender.SendTexture( texture, GL_TEXTURE_2D, (unsigned int)width, (unsigned int)height, true, 0 );
	}

	std::string Status() const override { return sent ? "Spout: sending" : "Spout: cannot send (graphics driver?)"; }

private:
	SpoutSender sender;
	bool sent = true;
};
}

const char* LocalSharingName()
{
	return "Spout";
}

std::unique_ptr< TextureSender > CreateLocalSender( const std::string& name )
{
	return std::make_unique< SpoutTextureSender >( name );
}
}
