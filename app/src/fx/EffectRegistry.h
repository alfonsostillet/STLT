#pragma once
#include <memory>
#include <string>
#include <vector>
#include "fx/Effect.h"
#include "fx/Isf.h"

namespace stltvj
{
class FfglLibrary;

// Every effect and source the app can create: the built-in ones and the FFGL plugins found in the plugin folders.
class EffectRegistry
{
public:
	struct Entry
	{
		std::string id;
		std::string name;
		std::string origin; // "Built-in" or the plugin file
		std::string description;
		bool source = false;
		std::shared_ptr< FfglLibrary > library; // FFGL only
		std::shared_ptr< const IsfShader > isf;  // ISF only
		std::string category;                    // ISF: its first category ("Blur", "Glitch"...)
	};
	struct Failure
	{
		std::string path;
		std::string reason;
	};

	EffectRegistry();
	~EffectRegistry();

	// Loads the FFGL plugins and ISF shaders (.fs) in these folders (and their subfolders). Already loaded files
	// are kept; plugins are never unloaded while the app runs. Needs the main OpenGL context current.
	void Scan( const std::vector< std::string >& folders );
	const std::vector< Entry >& Entries() const { return entries; }
	const std::vector< Failure >& Failures() const { return failures; }
	const Entry* Find( const std::string& id ) const;

	std::unique_ptr< Effect > Create( const std::string& id ) const;
	// Recreates an effect saved in a composition; a missing plugin leaves the slot empty but keeps its settings.
	void Restore( EffectSlot& slot ) const;
	void Restore( EffectChain& chain ) const;

	// The folders scanned by default: the app's own plugin and ISF folders, the system's ISF folder (shared with
	// VDMX and other ISF hosts) and Resolume's Extra Effects folders.
	static std::vector< std::string > DefaultFolders( const std::string& configDir );

private:
	void ScanFolder( const std::string& folder, int depth );
	void Add( const std::string& path );
	void AddIsf( const std::string& path );

	std::vector< Entry > entries;
	std::vector< Failure > failures;
	std::vector< std::string > loadedPaths;
};

// Saving and loading an effect's settings as JSON text (parameter values by name, animations, text).
std::string SaveEffectState( const Effect& effect );
void LoadEffectState( Effect& effect, const std::string& json );
}
