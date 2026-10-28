#include "ui/EffectsUi.h"
#include <imgui.h>
#include <imgui_stdlib.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <map>
#include "App.h"
#include "ui/ControlUi.h"
#include "ui/FileDialogs.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"
#include "ui/Workspaces.h"

namespace fs = std::filesystem;

namespace stltvj
{
namespace
{
constexpr const char* kSlotPayload = "STLT_SLOT";

struct SlotRef
{
	EffectChain* chain;
	int index;
};

float Ui( float pixels )
{
	return pixels * ImGui::GetFontSize() / 15.0f;
}

std::string Lower( std::string s )
{
	std::transform( s.begin(), s.end(), s.begin(), []( unsigned char c ) { return (char)std::tolower( c ); } );
	return s;
}

// "Color Red" -> "Color", "Red" -> "Color": the label of a colour picker made of R, G, B parameters.
std::string ColourLabel( const std::string& first, const char* channel )
{
	std::string label = first;
	size_t n          = std::strlen( channel );
	if( label.size() >= n && Lower( label.substr( label.size() - n ) ) == Lower( channel ) )
		label = label.substr( 0, label.size() - n );
	else if( label.size() >= n && Lower( label.substr( 0, n ) ) == Lower( channel ) )
		label = label.substr( n );
	while( !label.empty() && ( label.back() == ' ' || label.back() == '_' ) )
		label.pop_back();
	while( !label.empty() && ( label.front() == ' ' || label.front() == '_' ) )
		label.erase( label.begin() );
	return label.empty() ? "Color" : label;
}

const float kBeatChoices[]       = { 0.25f, 0.5f, 1, 2, 4, 8, 16, 32, 64 };
const char* kBeatChoiceNames[]   = { "1/4 beat", "1/2 beat", "1 beat", "2 beats", "4 beats", "8 beats", "16 beats", "32 beats", "64 beats" };

// The menu a parameter's name opens: reset and beat-synced animation.
void ParamMenu( Effect& effect, int i )
{
	if( !ImGui::BeginPopup( "##paramMenu" ) )
		return;
	const ParamInfo& p = effect.Params()[ i ];
	ImGui::TextDisabled( "%s", p.displayName.c_str() );
	if( ImGui::MenuItem( "Reset to Default" ) )
	{
		if( p.IsText() )
			effect.SetText( i, p.defaultText );
		else
			effect.SetValue( i, p.defaultValue );
		effect.Anim( i ) = Animation{};
	}
	if( p.IsAnimatable() )
		AnimationControls( effect.Anim( i ), p.min, p.max );
	ImGui::EndPopup();
}
}

void AnimationControls( Animation& a, float min, float max )
{
	ImGui::SeparatorText( "Animate to the beat" );
	ImGui::SetNextItemWidth( Ui( 140 ) );
	if( ImGui::BeginCombo( "Mode", AnimModeName( a.mode ) ) )
	{
		for( int m = 0; m < (int)AnimMode::Count; ++m )
			if( ImGui::Selectable( AnimModeName( (AnimMode)m ), (int)a.mode == m ) )
				a.mode = (AnimMode)m;
		ImGui::EndCombo();
	}
	int beatIndex = 4;
	for( int b = 0; b < (int)std::size( kBeatChoices ); ++b )
		if( std::fabs( kBeatChoices[ b ] - a.beats ) < 1e-3f )
			beatIndex = b;
	ImGui::SetNextItemWidth( Ui( 140 ) );
	if( ImGui::Combo( "Period", &beatIndex, kBeatChoiceNames, (int)std::size( kBeatChoiceNames ) ) )
		a.beats = kBeatChoices[ beatIndex ];
	float lo = min + ( max - min ) * a.from, hi = min + ( max - min ) * a.to;
	ImGui::SetNextItemWidth( Ui( 140 ) );
	if( FilledSlider( "From", &lo, min, max ) && max != min )
		a.from = ( lo - min ) / ( max - min );
	ImGui::SetNextItemWidth( Ui( 140 ) );
	if( FilledSlider( "To", &hi, min, max ) && max != min )
		a.to = ( hi - min ) / ( max - min );
	ImGui::Checkbox( "Reverse", &a.reverse );
}

void AnimatedLabel( const char* label, Animation& anim, float min, float max )
{
	ImGui::TableNextRow();
	ImGui::TableSetColumnIndex( 0 );
	ImGui::AlignTextToFramePadding();
	ImGui::PushID( label );
	bool moving = anim.mode != AnimMode::Off;
	if( moving )
		ImGui::PushStyleColor( ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4( theme::Accent ) );
	ImVec2 size = ImGui::CalcTextSize( label, nullptr, true );
	if( ImGui::Selectable( label, false, ImGuiSelectableFlags_None, ImVec2( size.x, 0 ) ) )
		ImGui::OpenPopup( "##animate" );
	if( moving )
		ImGui::PopStyleColor();
	if( ImGui::IsItemHovered( ImGuiHoveredFlags_DelayNormal ) )
		ImGui::SetTooltip( moving ? "Moving with the beat: click to change or stop it" : "Click: animate to the beat" );
	if( ImGui::BeginPopup( "##animate" ) )
	{
		ImGui::TextDisabled( "%s", label );
		if( moving && ImGui::MenuItem( "Stop Animating" ) )
			anim.mode = AnimMode::Off;
		AnimationControls( anim, min, max );
		ImGui::EndPopup();
	}
	ImGui::PopID();
	ImGui::TableSetColumnIndex( 1 );
	ImGui::SetNextItemWidth( -FLT_MIN );
}

namespace
{

void AnimatedMark( bool animated )
{
	if( !animated )
		return;
	ImVec2 min = ImGui::GetItemRectMin();
	ImGui::GetWindowDrawList()->AddRectFilled( ImVec2( min.x - Ui( 4 ), min.y ), ImVec2( min.x - Ui( 1 ), ImGui::GetItemRectMax().y ), theme::Accent );
}

void ParamLabel( const char* label )
{
	ImGui::TableNextRow();
	ImGui::TableSetColumnIndex( 0 );
	ImGui::AlignTextToFramePadding();
	ImGui::TextUnformatted( label );
	ImGui::TableSetColumnIndex( 1 );
	ImGui::SetNextItemWidth( -FLT_MIN );
}

// A parameter's name: a click opens its menu (reset, animate).
void ParamName( const char* label, bool animatable )
{
	ImGui::TableNextRow();
	ImGui::TableSetColumnIndex( 0 );
	ImGui::AlignTextToFramePadding();
	ImVec2 size = ImGui::CalcTextSize( label, nullptr, true );
	if( ImGui::Selectable( label, false, ImGuiSelectableFlags_None, ImVec2( size.x, 0 ) ) )
		ImGui::OpenPopup( "##paramMenu" );
	if( ImGui::IsItemHovered( ImGuiHoveredFlags_DelayNormal ) )
		ImGui::SetTooltip( animatable ? "Click: animate to the beat, or reset\nRight-click the value: reset" : "Click: reset" );
	ImGui::TableSetColumnIndex( 1 );
	ImGui::SetNextItemWidth( -FLT_MIN );
}

// Right-click (or double-click) on the value just drawn: the parameter back to its default, not animated.
void ResetParam( Effect& effect, int i )
{
	float value = effect.Value( i );
	if( !ResetValue( &value, effect.Params()[ i ].defaultValue ) )
		return;
	effect.SetValue( i, value );
	effect.Anim( i ) = Animation{};
}

// The effect whose parameters are being drawn, for MIDI mapping: its chain's address and place (none: -1).
App* mapApp = nullptr;
std::string mapScope;
int mapSlot = -1;

// Draws parameter i (and the ones a colour picker groups with it); returns how many parameters it used.
int ParamWidget( Effect& effect, int i )
{
	const std::vector< ParamInfo >& params = effect.Params();
	const ParamInfo& p                     = params[ i ];
	bool animated                          = effect.Anim( i ).mode != AnimMode::Off;
	ImGui::PushID( i );
	int used = 1;

	auto kindAt = [ & ]( int k ) { return k < (int)params.size() ? params[ k ].kind : ParamKind::Float; };
	if( ( p.kind == ParamKind::Red && kindAt( i + 1 ) == ParamKind::Green && kindAt( i + 2 ) == ParamKind::Blue )
	    || ( p.kind == ParamKind::Hue && kindAt( i + 1 ) == ParamKind::Saturation && kindAt( i + 2 ) == ParamKind::Brightness ) )
	{
		bool hsv   = p.kind == ParamKind::Hue;
		bool alpha = kindAt( i + 3 ) == ParamKind::Alpha;
		used       = alpha ? 4 : 3;
		float c[ 4 ];
		for( int k = 0; k < used; ++k )
			c[ k ] = effect.EffectiveValue( i + k );
		std::string label = ColourLabel( p.displayName, hsv ? "Hue" : "Red" );
		ParamName( label.c_str(), p.IsAnimatable() );
		ImGuiColorEditFlags flags = ImGuiColorEditFlags_Float | ImGuiColorEditFlags_NoOptions | ( hsv ? ImGuiColorEditFlags_InputHSV | ImGuiColorEditFlags_DisplayHSV : 0 );
		bool changed = alpha ? ImGui::ColorEdit4( "##colour", c, flags | ImGuiColorEditFlags_AlphaBar ) : ImGui::ColorEdit3( "##colour", c, flags );
		if( changed )
			for( int k = 0; k < used; ++k )
				effect.SetValue( i + k, c[ k ] );
		if( ImGui::IsItemHovered() && ImGui::IsMouseClicked( ImGuiMouseButton_Right ) )
			for( int k = 0; k < used; ++k )
			{
				effect.SetValue( i + k, params[ i + k ].defaultValue );
				effect.Anim( i + k ) = Animation{};
			}
		ParamMenu( effect, i );
		ImGui::PopID();
		return used;
	}

	ParamName( p.displayName.c_str(), p.IsAnimatable() );
	float value = effect.EffectiveValue( i );
	switch( p.kind )
	{
	case ParamKind::Bool:
	{
		bool on = value >= 0.5f;
		if( ImGui::Checkbox( "##v", &on ) )
			effect.SetValue( i, on ? 1.0f : 0.0f );
		if( ImGui::IsItemHovered() && ImGui::IsMouseClicked( ImGuiMouseButton_Right ) )
			effect.SetValue( i, p.defaultValue );
		break;
	}
	case ParamKind::Event:
		if( ImGui::Button( p.displayName.c_str(), ImVec2( -FLT_MIN, 0 ) ) )
			effect.Trigger( i );
		break;
	case ParamKind::Option:
	{
		int current = -1;
		float best  = 1e30f;
		for( int k = 0; k < (int)p.optionValues.size(); ++k )
			if( std::fabs( p.optionValues[ k ] - value ) < best )
			{
				best    = std::fabs( p.optionValues[ k ] - value );
				current = k;
			}
		const char* preview = current >= 0 && current < (int)p.options.size() ? p.options[ current ].c_str() : "";
		if( ImGui::BeginCombo( "##v", preview ) )
		{
			for( int k = 0; k < (int)p.options.size(); ++k )
			{
				ImGui::PushID( k );
				if( ImGui::Selectable( p.options[ k ].empty() ? " " : p.options[ k ].c_str(), k == current ) && k < (int)p.optionValues.size() )
					effect.SetValue( i, p.optionValues[ k ] );
				ImGui::PopID();
			}
			ImGui::EndCombo();
		}
		if( ImGui::IsItemHovered() && ImGui::IsMouseClicked( ImGuiMouseButton_Right ) )
			effect.SetValue( i, p.defaultValue );
		break;
	}
	case ParamKind::Text:
	{
		std::string text = effect.Text( i );
		if( ImGui::InputText( "##v", &text ) )
			effect.SetText( i, text );
		break;
	}
	case ParamKind::File:
	{
		// A file picked in the dialog is applied the next time this parameter is drawn: the dialog may
		// finish on another frame, when the effect could already be gone.
		static std::map< std::pair< const Effect*, int >, std::string > chosen;
		auto key = std::make_pair( (const Effect*)&effect, i );
		auto found = chosen.find( key );
		if( found != chosen.end() )
		{
			effect.SetText( i, found->second );
			chosen.erase( found );
		}
		std::string text = effect.Text( i );
		ImGui::SetNextItemWidth( -ImGui::GetFrameHeight() * 1.6f );
		if( ImGui::InputTextWithHint( "##v", "file path", &text ) )
			effect.SetText( i, text );
		ImGui::SameLine();
		if( ImGui::Button( "..." ) )
			ShowFileDialog( FileDialog::OpenAnyFile, text, [ key ]( const std::vector< std::string >& paths ) { chosen[ key ] = paths[ 0 ]; } );
		break;
	}
	case ParamKind::Buffer: ImGui::TextDisabled( "audio input (not available yet)" ); break;
	default:
	{
		bool integer = p.kind == ParamKind::Integer;
		if( animated )
			ImGui::PushStyleColor( ImGuiCol_SliderGrab, ImVec4( 1.0f, 0.75f, 0.3f, 1 ) );
		float lo = std::min( p.min, p.max ), hi = std::max( p.min, p.max );
		if( FilledSlider( "##v", &value, lo, hi, integer ? "%.0f" : ( hi - lo > 50 ? "%.1f" : "%.3f" ) ) && !animated )
			effect.SetValue( i, value );
		if( animated )
			ImGui::PopStyleColor();
		ResetParam( effect, i );
		break;
	}
	}
	AnimatedMark( animated );
	if( mapApp && mapSlot >= 0 && p.kind != ParamKind::Text && p.kind != ParamKind::File && p.kind != ParamKind::Buffer )
		MapTarget( *mapApp, EffectParamAddress( mapScope, mapSlot, p.name ) );
	ParamMenu( effect, i );
	ImGui::PopID();
	return used;
}

std::string ShortOrigin( const std::string& origin )
{
	if( origin == "Built-in" )
		return origin;
	return fs::u8path( origin ).filename().u8string();
}
}

void DrawEffectParams( Effect& effect )
{
	if( !effect.Error().empty() )
		ImGui::TextColored( ImVec4( 0.9f, 0.35f, 0.3f, 1 ), "%s", effect.Error().c_str() );
	const std::vector< ParamInfo >& params = effect.Params();
	if( params.empty() )
	{
		ImGui::TextDisabled( "No parameters" );
		return;
	}
	if( !ImGui::BeginTable( "##params", 2, ImGuiTableFlags_SizingStretchProp ) )
		return;
	ImGui::TableSetupColumn( "label", ImGuiTableColumnFlags_WidthFixed, Ui( 100 ) );
	ImGui::TableSetupColumn( "value", ImGuiTableColumnFlags_WidthStretch );
	std::string group;
	bool groupOpen = true;
	for( int i = 0; i < (int)params.size(); )
	{
		const ParamInfo& p = params[ i ];
		if( p.group != group )
		{
			// Consecutive parameters with the same group fold together (FFGL 2.2 groups).
			group = p.group;
			if( !group.empty() )
			{
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex( 0 );
				ImGui::PushID( i );
				groupOpen = ImGui::TreeNodeEx( group.c_str(), ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAllColumns | ImGuiTreeNodeFlags_NoTreePushOnOpen );
				ImGui::PopID();
			}
			else
				groupOpen = true;
		}
		if( !p.visible || !groupOpen )
		{
			++i;
			continue;
		}
		i += ParamWidget( effect, i );
	}
	ImGui::EndTable();
}

bool AcceptEffectDrop( App& app, EffectChain& chain )
{
	bool dropped = false;
	if( ImGui::BeginDragDropTarget() )
	{
		if( const ImGuiPayload* payload = ImGui::AcceptDragDropPayload( kEffectPayload ) )
		{
			app.AddEffect( chain, std::string( (const char*)payload->Data ) );
			dropped = true;
		}
		ImGui::EndDragDropTarget();
	}
	return dropped;
}

void DrawEffectChain( App& app, EffectChain& chain, const char* id, const std::string& scope )
{
	ImGui::PushID( id );
	int removeAt = -1, moveFrom = -1, moveTo = -1;
	std::string insertId;
	int insertAt = -1;
	for( int i = 0; i < (int)chain.size(); ++i )
	{
		EffectSlot& slot = chain[ i ];
		ImGui::PushID( i );
		std::string title = slot.effect ? slot.effect->Name() : slot.missingId + " (missing)";
		if( slot.bypass )
			ImGui::PushStyleColor( ImGuiCol_Text, ImVec4( 0.5f, 0.5f, 0.5f, 1 ) );
		ImGui::SetNextItemOpen( slot.open, ImGuiCond_Always );
		slot.open = ImGui::CollapsingHeader( ( title + "###header" ).c_str(), ImGuiTreeNodeFlags_AllowOverlap );
		if( slot.bypass )
			ImGui::PopStyleColor();
		if( ImGui::BeginDragDropSource() )
		{
			SlotRef ref{ &chain, i };
			ImGui::SetDragDropPayload( kSlotPayload, &ref, sizeof( ref ) );
			ImGui::TextUnformatted( title.c_str() );
			ImGui::EndDragDropSource();
		}
		if( ImGui::BeginDragDropTarget() )
		{
			if( const ImGuiPayload* payload = ImGui::AcceptDragDropPayload( kSlotPayload ) )
			{
				SlotRef ref = *(const SlotRef*)payload->Data;
				if( ref.chain == &chain )
				{
					moveFrom = ref.index;
					moveTo   = i;
				}
			}
			if( const ImGuiPayload* payload = ImGui::AcceptDragDropPayload( kEffectPayload ) )
			{
				insertId = (const char*)payload->Data;
				insertAt = i;
			}
			ImGui::EndDragDropTarget();
		}

		// Header buttons on the right: enable, up, down, remove.
		float button = ImGui::GetFrameHeight();
		ImGui::SameLine( ImGui::GetContentRegionMax().x - button * 4 - Ui( 10 ) );
		bool enabled = !slot.bypass;
		if( ImGui::Checkbox( "##enabled", &enabled ) )
			slot.bypass = !enabled;
		if( ImGui::IsItemHovered() )
			ImGui::SetTooltip( enabled ? "On (click to bypass)" : "Bypassed" );
		ImGui::SameLine();
		ImGui::BeginDisabled( i == 0 );
		if( ImGui::ArrowButton( "##up", ImGuiDir_Up ) )
		{
			moveFrom = i;
			moveTo   = i - 1;
		}
		ImGui::EndDisabled();
		ImGui::SameLine();
		ImGui::BeginDisabled( i + 1 == (int)chain.size() );
		if( ImGui::ArrowButton( "##down", ImGuiDir_Down ) )
		{
			moveFrom = i;
			moveTo   = i + 1;
		}
		ImGui::EndDisabled();
		ImGui::SameLine();
		if( ImGui::Button( "x", ImVec2( button, button ) ) )
			removeAt = i;

		if( slot.open )
		{
			ImGui::Indent( Ui( 4 ) );
			if( slot.effect )
			{
				if( ImGui::BeginTable( "##mix", 2, ImGuiTableFlags_SizingStretchProp ) )
				{
					ImGui::TableSetupColumn( "label", ImGuiTableColumnFlags_WidthFixed, Ui( 100 ) );
					ImGui::TableSetupColumn( "value", ImGuiTableColumnFlags_WidthStretch );
					ParamLabel( "Mix" );
					float percent = slot.mix * 100;
					if( FilledSlider( "##mix", &percent, 0, 100, "%.0f %%" ) )
						slot.mix = percent / 100;
					ResetValue( &slot.mix, 1.0f );
					ImGui::EndTable();
				}
				if( !scope.empty() )
				{
					mapApp   = &app;
					mapScope = scope;
					mapSlot  = i;
				}
				DrawEffectParams( *slot.effect );
				mapApp  = nullptr;
				mapSlot = -1;
			}
			else
			{
				ImGui::TextColored( ImVec4( 0.9f, 0.35f, 0.3f, 1 ), "Not installed: %s", slot.missingId.c_str() );
				ImGui::TextDisabled( "Its settings are kept. Add the plugin to a plugin folder and rescan." );
			}
			ImGui::Unindent( Ui( 4 ) );
		}
		ImGui::PopID();
	}

	// Drop zone and Add menu.
	ImGui::Spacing();
	float width = ImGui::GetContentRegionAvail().x;
	ImGui::PushStyleColor( ImGuiCol_Button, ImVec4( 0.15f, 0.15f, 0.15f, 1 ) );
	ImGui::Button( chain.empty() ? "Drop effects here" : "Drop more effects here", ImVec2( width - Ui( 110 ), ImGui::GetFrameHeight() * 1.4f ) );
	ImGui::PopStyleColor();
	AcceptEffectDrop( app, chain );
	ImGui::SameLine();
	ImGui::SetNextItemWidth( Ui( 104 ) );
	if( ImGui::BeginCombo( "##add", "Add Effect", ImGuiComboFlags_HeightLarge ) )
	{
		std::string origin;
		for( const EffectRegistry::Entry& e : app.registry->Entries() )
		{
			if( e.source )
				continue;
			std::string group = e.library ? "FFGL" : "Built-in";
			if( group != origin )
			{
				ImGui::SeparatorText( group.c_str() );
				origin = group;
			}
			if( ImGui::Selectable( e.name.c_str() ) )
				insertId = e.id;
		}
		ImGui::EndCombo();
	}

	if( !insertId.empty() )
		app.AddEffect( chain, insertId, insertAt );
	if( removeAt >= 0 && removeAt < (int)chain.size() )
		chain.erase( chain.begin() + removeAt );
	else if( moveFrom >= 0 && moveTo >= 0 && moveFrom < (int)chain.size() && moveTo < (int)chain.size() && moveFrom != moveTo )
	{
		EffectSlot moving = std::move( chain[ moveFrom ] );
		chain.erase( chain.begin() + moveFrom );
		chain.insert( chain.begin() + moveTo, std::move( moving ) );
	}
	ImGui::PopID();
}

void DrawEffectsPanel( App& app )
{
	if( !ImGui::Begin( panel::Effects, &app.panels.effects ) )
	{
		ImGui::End();
		return;
	}
	static std::string search;
	static int tab = 0; // 0 effects, 1 sources
	if( ImGui::BeginTabBar( "##kinds" ) )
	{
		if( ImGui::BeginTabItem( "Effects" ) )
		{
			tab = 0;
			ImGui::EndTabItem();
		}
		if( ImGui::BeginTabItem( "Sources" ) )
		{
			tab = 1;
			ImGui::EndTabItem();
		}
		if( ImGui::BeginTabItem( "Plugin Folders" ) )
		{
			tab = 2;
			ImGui::EndTabItem();
		}
		ImGui::EndTabBar();
	}

	if( tab == 2 )
	{
		ImGui::TextWrapped( "FFGL 2 plugins (.dll on Windows, .bundle on macOS) are loaded from these folders and their subfolders." );
		std::vector< std::string > defaults = EffectRegistry::DefaultFolders( app.configDir );
		std::string remove;
		for( const std::string& folder : app.PluginFolders() )
		{
			bool isDefault = std::find( defaults.begin(), defaults.end(), folder ) != defaults.end();
			std::error_code ignored;
			bool exists = fs::is_directory( fs::u8path( folder ), ignored );
			ImGui::PushID( folder.c_str() );
			if( !isDefault && ImGui::SmallButton( "x" ) )
				remove = folder;
			if( !isDefault )
				ImGui::SameLine();
			if( exists )
				ImGui::TextUnformatted( folder.c_str() );
			else
				ImGui::TextDisabled( "%s (not found)", folder.c_str() );
			ImGui::PopID();
		}
		if( !remove.empty() )
			app.RemovePluginFolder( remove );
		static std::string newFolder;
		ImGui::SetNextItemWidth( -Ui( 120 ) );
		ImGui::InputTextWithHint( "##newFolder", "another folder with FFGL plugins", &newFolder );
		ImGui::SameLine();
		if( ImGui::Button( "Add Folder" ) && !newFolder.empty() )
		{
			app.AddPluginFolder( newFolder );
			newFolder.clear();
		}
		ImGui::SameLine();
		if( ImGui::Button( "Choose Folder..." ) )
			ShowFileDialog( FileDialog::PickFolder, app.configDir, [ &app ]( const std::vector< std::string >& paths ) { app.AddPluginFolder( paths[ 0 ] ); } );
		if( ImGui::Button( "Rescan" ) )
			app.RescanPlugins();
		if( !app.registry->Failures().empty() )
		{
			ImGui::SeparatorText( "Not loaded" );
			for( const EffectRegistry::Failure& f : app.registry->Failures() )
			{
				ImGui::TextColored( ImVec4( 0.9f, 0.55f, 0.3f, 1 ), "%s", fs::u8path( f.path ).filename().u8string().c_str() );
				ImGui::SameLine();
				ImGui::TextDisabled( "%s", f.reason.c_str() );
			}
		}
		ImGui::End();
		return;
	}

	ImGui::SetNextItemWidth( -FLT_MIN );
	ImGui::InputTextWithHint( "##search", "Search", &search );
	ImGui::BeginChild( "##list" );
	std::string needle = Lower( search );
	std::string group;
	int shown = 0;
	for( const EffectRegistry::Entry& e : app.registry->Entries() )
	{
		if( e.source != ( tab == 1 ) )
			continue;
		if( !needle.empty() && Lower( e.name ).find( needle ) == std::string::npos )
			continue;
		std::string g = e.library ? "FFGL" : "Built-in";
		if( g != group )
		{
			ImGui::SeparatorText( g.c_str() );
			group = g;
		}
		ImGui::PushID( e.id.c_str() );
		if( ImGui::Selectable( e.name.c_str(), false, ImGuiSelectableFlags_AllowDoubleClick ) && ImGui::IsMouseDoubleClicked( ImGuiMouseButton_Left ) )
		{
			Composition& comp = app.composition;
			if( e.source )
			{
				int layer  = std::clamp( comp.selectedLayer, 0, std::max( 0, (int)comp.layers.size() - 1 ) );
				int column = comp.selectedColumn >= 0 ? comp.selectedColumn : 0;
				app.LoadGenerator( layer, column, e.id );
			}
			else if( Clip* clip = comp.SelectedClip() )
				app.AddEffect( clip->effects, e.id );
			else if( !comp.layers.empty() )
				app.AddEffect( comp.layers[ std::clamp( comp.selectedLayer, 0, (int)comp.layers.size() - 1 ) ].effects, e.id );
		}
		if( ImGui::IsItemHovered() && ImGui::BeginTooltip() )
		{
			ImGui::TextUnformatted( e.name.c_str() );
			ImGui::TextDisabled( "%s", ShortOrigin( e.origin ).c_str() );
			if( !e.description.empty() )
				ImGui::TextWrapped( "%s", e.description.c_str() );
			ImGui::TextDisabled( e.source ? "Drag onto an empty clip cell, or double-click to put it in the selected cell."
			                              : "Drag onto a clip, a layer, the composition monitor or an effects list. Double-click adds it to the selected clip." );
			ImGui::EndTooltip();
		}
		if( ImGui::BeginDragDropSource() )
		{
			ImGui::SetDragDropPayload( kEffectPayload, e.id.c_str(), e.id.size() + 1 );
			ImGui::TextUnformatted( e.name.c_str() );
			ImGui::EndDragDropSource();
		}
		ImGui::PopID();
		++shown;
	}
	if( !shown )
		ImGui::TextDisabled( search.empty() ? "Nothing here yet." : "No match." );
	ImGui::EndChild();
	ImGui::End();
}
}
