#pragma once
#include <imgui.h>
#include <string>
#include <vector>

namespace stltvj
{
// Which panels are open. Saved inside the layout, so a workspace brings back its own set of panels.
struct PanelVisibility
{
	bool layers             = true;
	bool timeline           = false; // the Arrangement view's main panel
	bool clips              = true;
	bool compositionMonitor = true;
	bool previewMonitor     = true;
	bool composition        = true;
	bool layer              = true;
	bool clip               = true;
	bool files              = true;
	bool output             = true;
	bool performance        = true;
	bool effects            = true;
	bool control            = true;
	bool advancedOutput     = false; // a big editor: floats on top when opened
};

// Panel window names: also the keys ImGui saves their place under.
namespace panel
{
constexpr const char* Layers             = "Layers"; // the layers' headers, beside the clips
constexpr const char* Timeline           = "Timeline";
constexpr const char* Clips              = "Clips";
constexpr const char* CompositionMonitor = "Composition Monitor";
constexpr const char* PreviewMonitor     = "Preview Monitor";
constexpr const char* Composition        = "Composition";
constexpr const char* Layer              = "Layer";
constexpr const char* Clip               = "Clip";
constexpr const char* Files              = "Files";
constexpr const char* Output             = "Output";
constexpr const char* Performance        = "Performance";
constexpr const char* Effects            = "Effects";
constexpr const char* Control            = "Shortcuts"; // keyboard, MIDI and OSC
constexpr const char* AdvancedOutput     = "Advanced Output";
}

// Named workspaces: snapshots of where every panel is docked, floating, sized and shown.
// Panels dock into each other (drag a tab onto another panel's drop targets), split, tab, or float as
// separate OS windows on any display; the layout in use is saved on exit and restored on start.
class Workspaces
{
public:
	void Install( PanelVisibility* panels, const std::string& directory );

	std::vector< std::string > List() const;
	bool Save( const std::string& name );
	bool Remove( const std::string& name );
	void RequestLoad( const std::string& name );
	void RequestDefault();
	const std::string& Current() const { return current; }

	// Call before ImGui::NewFrame: applies a requested workspace.
	void ApplyPending();
	// Session (0) and Arrangement (1) keep a layout each: switching keeps the one in use and brings back the
	// other's (its default the first time). SetView only says which one is in use (at start).
	void SwitchView( int view );
	void SetView( int v ) { view = v; }
	// Call inside the frame with the main dock space: lays out the default workspace when needed.
	void BuildDefaultIfNeeded( ImGuiID dockspace, ImVec2 size );
	// The interface was zoomed by `ratio`: floating panels grow or shrink with it, and the deck (Layers and Clips) fits its
	// headers and layers again; the other docked panels keep their share of the window.
	void Rescale( float ratio );
	int deckLayers = 4; // the default layout makes the deck as high as these many layers need
	// Call inside the frame, before the dock space: follows a zoom asked for with Rescale.
	void ApplyRescale();

private:
	void FitDeck();
	float DeckHeight() const;
	std::string PathFor( const std::string& name ) const;

	std::string directory;
	std::string current = "Default";
	std::string pending;
	std::string pendingText; // a view's layout to load
	bool pendingDefault = false;
	int view            = 0;
	bool buildDefault   = false;
	float rescale       = 1; // a zoom to follow, at the next BuildDefaultIfNeeded
};
}
