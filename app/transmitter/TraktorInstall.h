#pragma once
#include <string>
#include <vector>

// Puts NI LINK's QML script into Traktor, and takes it out again. Traktor draws the screens of its controllers with
// QML files in its own folder; the script is added to the Kontrol D2's mapping (Traktor loads it when a D2, real
// or not, is in its Controller Manager): two files of ours next to D2.qml, and one line in D2.qml. The D2.qml found
// first is kept as D2.qml.nilink-backup, and put back by Remove. Nothing else of Traktor is touched.
namespace nilink
{
struct TraktorFolder
{
	std::string name;   // "Traktor Pro 4"
	std::string folder; // its D2 mapping folder (…/qml/CSI/D2)
	enum class State
	{
		NotInstalled,
		Installed,
		Outdated, // an older version of the script: Install updates it
	} state = State::NotInstalled;
	bool writable = false; // this program can change it without asking for an administrator
};

// Every Traktor found in the usual places (an extra folder too, for tests: a D2 folder or a Traktor install).
std::vector< TraktorFolder > FindTraktor( const std::string& extra = "" );
// False with the reason when it could not (`denied`: for want of permission, so worth asking for an administrator).
bool InstallInto( const TraktorFolder& traktor, std::string& error, bool& denied );
bool RemoveFrom( const TraktorFolder& traktor, std::string& error, bool& denied );
// Runs this program again as administrator with `argument` ("--install"), waiting for it; false when refused.
bool RunAsAdministrator( const std::string& argument );
}
