#pragma once
#include <functional>
#include <string>
#include <vector>

namespace stltvj
{
// The system's own open, save and folder dialogs.
// On Windows they run on their own thread, so playback and the output never stop while one is open;
// macOS and Linux require dialogs on the main thread, so there they pause the interface until closed.
// Results arrive on the main thread through PollFileDialogs().
enum class FileDialog
{
	OpenMedia,       // movies and images, several at once
	OpenComposition,
	SaveComposition,
	PickFolder,
	OpenAnyFile,
	OpenShortcuts,
	SaveShortcuts,
};

using FileDialogDone = std::function< void( const std::vector< std::string >& paths ) >;

// Starts a dialog; ignored while another one is open. `onDone` gets the chosen paths (none if cancelled).
void ShowFileDialog( FileDialog kind, const std::string& defaultPath, FileDialogDone onDone, const std::string& defaultName = {} );
// Main thread, once per frame: delivers finished dialogs. Returns an error text if a dialog could not open.
std::string PollFileDialogs();
bool FileDialogOpen();
}
