#pragma once
#include <string>

namespace stltvj
{
// Where the app keeps its layout, workspaces and preferences:
// %APPDATA%\STLT, ~/Library/Application Support/STLT or ~/.config/stlt (what STLT-VJ kept is copied there once).
std::string ConfigDirectory();
std::string HomeDirectory();
// The user's Documents folder (follows a redirected or OneDrive-synced folder on Windows).
std::string DocumentsDirectory();
// The folder of the running program (inside the .app bundle's Contents/MacOS on macOS).
std::string ExecutableDirectory();

bool ReadTextFile( const std::string& path, std::string& text );
bool WriteTextFile( const std::string& path, const std::string& text );
}
