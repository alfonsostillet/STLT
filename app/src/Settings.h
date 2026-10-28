#pragma once
#include <string>

namespace stltvj
{
// Where the app keeps its layout, workspaces and preferences:
// %APPDATA%\STLT-VJ, ~/Library/Application Support/STLT-VJ or ~/.config/stlt-vj.
std::string ConfigDirectory();
std::string HomeDirectory();
// The user's Documents folder (follows a redirected or OneDrive-synced folder on Windows).
std::string DocumentsDirectory();

bool ReadTextFile( const std::string& path, std::string& text );
bool WriteTextFile( const std::string& path, const std::string& text );
}
