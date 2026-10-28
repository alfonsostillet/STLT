#include "Theme.h"
#include <cstddef>

namespace stltvj
{
// From the generated EmbeddedFonts.cpp.
extern const unsigned char kFont_Inter_Regular[];
extern const size_t kFont_Inter_Regular_size;
extern const unsigned char kFont_Inter_SemiBold[];
extern const size_t kFont_Inter_SemiBold_size;
extern const unsigned char kFont_JetBrainsMono_Medium[];
extern const size_t kFont_JetBrainsMono_Medium_size;

namespace
{
UiFonts fonts;

ImVec4 Color( ImU32 c, float alpha = -1 )
{
	ImVec4 v = ImGui::ColorConvertU32ToFloat4( c );
	if( alpha >= 0 )
		v.w = alpha;
	return v;
}

ImVec4 Rgb( int r, int g, int b, float a = 1.0f )
{
	return ImVec4( r / 255.0f, g / 255.0f, b / 255.0f, a );
}

ImFont* AddFont( const unsigned char* data, size_t size, float pixels )
{
	ImFontConfig config;
	config.FontDataOwnedByAtlas = false; // static data
	config.OversampleH          = 2;
	config.OversampleV          = 1;
	config.PixelSnapH           = false;
	static const ImWchar ranges[] = { 0x0020, 0x00FF, 0x0100, 0x017F, 0x2010, 0x203A, 0x2190, 0x2193, 0x2212, 0x2212, 0x20AC, 0x20AC, 0x25B2, 0x25C4, 0 };
	return ImGui::GetIO().Fonts->AddFontFromMemoryTTF( (void*)data, (int)size, pixels, &config, ranges );
}
}

const UiFonts& Fonts()
{
	return fonts;
}

void ApplyTheme( float scale )
{
	ImGuiStyle& style = ImGui::GetStyle();
	style             = ImGuiStyle();

	// Room to breathe, soft corners, hairline borders.
	style.WindowPadding            = ImVec2( 10, 8 );
	style.FramePadding             = ImVec2( 8, 5 );
	style.ItemSpacing              = ImVec2( 8, 6 );
	style.ItemInnerSpacing         = ImVec2( 6, 4 );
	style.CellPadding              = ImVec2( 6, 4 );
	style.IndentSpacing            = 14;
	style.ScrollbarSize            = 10;
	style.GrabMinSize              = 10;
	style.WindowBorderSize         = 1;
	style.ChildBorderSize          = 0;
	style.PopupBorderSize          = 1;
	style.FrameBorderSize          = 0;
	style.TabBorderSize            = 0;
	style.TabBarBorderSize         = 1;
	style.TabBarOverlineSize       = 2;
	style.WindowRounding           = 8;
	style.ChildRounding            = 6;
	style.FrameRounding            = 5;
	style.PopupRounding            = 8;
	style.ScrollbarRounding        = 8;
	style.GrabRounding             = 4;
	style.TabRounding              = 6;
	style.WindowTitleAlign         = ImVec2( 0.0f, 0.5f );
	style.WindowMenuButtonPosition = ImGuiDir_None; // the docking menu triangle on every tab: clutter
	style.SeparatorTextBorderSize  = 1;
	style.SeparatorTextPadding     = ImVec2( 0, 6 );
	style.SeparatorTextAlign       = ImVec2( 0, 0.5f );
	style.DockingSeparatorSize     = 2;
	style.DisabledAlpha            = 0.45f;

	ImVec4* c                               = style.Colors;
	c[ ImGuiCol_Text ]                      = Color( theme::Text );
	c[ ImGuiCol_TextDisabled ]              = Color( theme::TextDim );
	c[ ImGuiCol_WindowBg ]                  = Color( theme::Panel );
	c[ ImGuiCol_ChildBg ]                   = Rgb( 0, 0, 0, 0 );
	c[ ImGuiCol_PopupBg ]                   = Rgb( 26, 29, 34, 0.98f );
	c[ ImGuiCol_Border ]                    = Color( theme::Line );
	c[ ImGuiCol_BorderShadow ]              = Rgb( 0, 0, 0, 0 );
	c[ ImGuiCol_FrameBg ]                   = Color( theme::Raised );
	c[ ImGuiCol_FrameBgHovered ]            = Color( theme::RaisedHover );
	c[ ImGuiCol_FrameBgActive ]             = Rgb( 47, 52, 61 );
	c[ ImGuiCol_TitleBg ]                   = Color( theme::Base );
	c[ ImGuiCol_TitleBgActive ]             = Rgb( 17, 19, 23 );
	c[ ImGuiCol_TitleBgCollapsed ]          = Color( theme::Base );
	c[ ImGuiCol_MenuBarBg ]                 = Color( theme::Base );
	c[ ImGuiCol_ScrollbarBg ]               = Rgb( 0, 0, 0, 0 );
	c[ ImGuiCol_ScrollbarGrab ]             = Rgb( 255, 255, 255, 0.10f );
	c[ ImGuiCol_ScrollbarGrabHovered ]      = Rgb( 255, 255, 255, 0.18f );
	c[ ImGuiCol_ScrollbarGrabActive ]       = Color( theme::Accent, 0.7f );
	c[ ImGuiCol_CheckMark ]                 = Color( theme::Accent );
	c[ ImGuiCol_SliderGrab ]                = Color( theme::Accent );
	c[ ImGuiCol_SliderGrabActive ]          = Rgb( 160, 240, 220 );
	c[ ImGuiCol_Button ]                    = Color( theme::Raised );
	c[ ImGuiCol_ButtonHovered ]             = Color( theme::RaisedHover );
	c[ ImGuiCol_ButtonActive ]              = Color( theme::AccentDim );
	c[ ImGuiCol_Header ]                    = Rgb( 255, 255, 255, 0.05f );
	c[ ImGuiCol_HeaderHovered ]             = Rgb( 255, 255, 255, 0.08f );
	c[ ImGuiCol_HeaderActive ]              = Color( theme::AccentFill );
	c[ ImGuiCol_Separator ]                 = Color( theme::Line );
	c[ ImGuiCol_SeparatorHovered ]          = Color( theme::Accent, 0.6f );
	c[ ImGuiCol_SeparatorActive ]           = Color( theme::Accent );
	c[ ImGuiCol_ResizeGrip ]                = Rgb( 0, 0, 0, 0 );
	c[ ImGuiCol_ResizeGripHovered ]         = Color( theme::Accent, 0.6f );
	c[ ImGuiCol_ResizeGripActive ]          = Color( theme::Accent );
	c[ ImGuiCol_Tab ]                       = Rgb( 0, 0, 0, 0 );
	c[ ImGuiCol_TabHovered ]                = Color( theme::RaisedHover );
	c[ ImGuiCol_TabSelected ]               = Color( theme::Raised );
	c[ ImGuiCol_TabSelectedOverline ]       = Color( theme::Accent );
	c[ ImGuiCol_TabDimmed ]                 = Rgb( 0, 0, 0, 0 );
	c[ ImGuiCol_TabDimmedSelected ]         = Rgb( 26, 29, 34 );
	c[ ImGuiCol_TabDimmedSelectedOverline ] = Rgb( 255, 255, 255, 0.15f );
	c[ ImGuiCol_DockingPreview ]            = Color( theme::Accent, 0.35f );
	c[ ImGuiCol_DockingEmptyBg ]            = Color( theme::Base );
	c[ ImGuiCol_PlotLines ]                 = Color( theme::Accent );
	c[ ImGuiCol_PlotHistogram ]             = Color( theme::Accent );
	c[ ImGuiCol_TableHeaderBg ]             = Rgb( 255, 255, 255, 0.04f );
	c[ ImGuiCol_TableBorderStrong ]         = Color( theme::Line );
	c[ ImGuiCol_TableBorderLight ]          = Rgb( 255, 255, 255, 0.04f );
	c[ ImGuiCol_TableRowBg ]                = Rgb( 0, 0, 0, 0 );
	c[ ImGuiCol_TableRowBgAlt ]             = Rgb( 255, 255, 255, 0.025f );
	c[ ImGuiCol_TextLink ]                  = Color( theme::Accent );
	c[ ImGuiCol_TextSelectedBg ]            = Color( theme::Accent, 0.30f );
	c[ ImGuiCol_DragDropTarget ]            = Color( theme::Accent );
	c[ ImGuiCol_NavCursor ]                 = Color( theme::Accent );
	c[ ImGuiCol_ModalWindowDimBg ]          = Rgb( 0, 0, 0, 0.55f );

	style.ScaleAllSizes( scale );
	ImGuiIO& io = ImGui::GetIO();
	io.Fonts->Clear();
	fonts.body    = AddFont( kFont_Inter_Regular, kFont_Inter_Regular_size, 15.0f * scale );
	fonts.bold    = AddFont( kFont_Inter_SemiBold, kFont_Inter_SemiBold_size, 15.0f * scale );
	fonts.mono    = AddFont( kFont_JetBrainsMono_Medium, kFont_JetBrainsMono_Medium_size, 14.0f * scale );
	fonts.display = AddFont( kFont_JetBrainsMono_Medium, kFont_JetBrainsMono_Medium_size, 20.0f * scale );
	io.FontDefault = fonts.body;
}
}
