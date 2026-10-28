#include "output/Sharing.h"
#import <Cocoa/Cocoa.h>
#import <OpenGL/OpenGL.h>
#import <OpenGL/gl3.h>
#import <Syphon/SyphonOpenGLServer.h>

namespace stltvj
{
namespace
{
// A Syphon server: other programs on this computer receive the texture through an IOSurface, without a copy.
class SyphonTextureSender : public TextureSender
{
public:
	explicit SyphonTextureSender( const std::string& name ) : name( name ) {}
	~SyphonTextureSender() override { [server stop]; }

	void Send( unsigned int texture, int width, int height ) override
	{
		CGLContextObj current = CGLGetCurrentContext();
		if( !server || current != context )
		{
			[server stop];
			context = current;
			server  = [[SyphonOpenGLServer alloc] initWithName:[NSString stringWithUTF8String:name.c_str()] context:current options:nil];
		}
		[server publishFrameTexture:texture
		              textureTarget:GL_TEXTURE_2D
		                imageRegion:NSMakeRect( 0, 0, width, height )
		          textureDimensions:NSMakeSize( width, height )
		                    flipped:NO];
	}

	std::string Status() const override
	{
		if( !server )
			return "Syphon: starting";
		return server.hasClients ? "Syphon: sending to a client" : "Syphon: waiting for clients";
	}

private:
	std::string name;
	CGLContextObj context      = nullptr;
	SyphonOpenGLServer* server = nil;
};
}

const char* LocalSharingName()
{
	return "Syphon";
}

std::unique_ptr< TextureSender > CreateLocalSender( const std::string& name )
{
	return std::make_unique< SyphonTextureSender >( name );
}
}
