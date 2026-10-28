#include "output/Sharing.h"

namespace stltvj
{
// Linux has no common way to share textures between applications: NDI is the way out there.
const char* LocalSharingName()
{
	return nullptr;
}

std::unique_ptr< TextureSender > CreateLocalSender( const std::string& )
{
	return nullptr;
}
}
