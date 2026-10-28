#include "Theme.h"
#include <filesystem>

namespace stltvj
{
static ImVec4 Rgb( int r, int g, int b, float a = 1.0f )
{
	return ImVec4( r / 255.0f, g / 255.0f, b / 255.0f, a );
}

static void LoadFont( float size )
{
	ImGuiIO& io = ImGui::GetIO();
	const char* candidates[] = {
#if defined( _WIN32 )
		"C:\\Windows\\Fonts\\segoeui.ttf",
		"C:\\Windows\\Fonts\\arial.ttf",
#elif defined( __APPLE__ )
		"/System/Library/Fonts/Supplemental/Arial.ttf",
		"/Library/Fonts/Arial.ttf",
		"/System/Library/Fonts/Helvetica.ttc",
#else
		"/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
		"/usr/share/fonts/TTF/DejaVuSans.ttf",
		"/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
#endif
	};
	for( const char* path : candidates )
	{
		std::error_code ignored;
		if( std::filesystem::exists( path, ignored ) && io.Fonts->AddFontFromFileTTF( path, size ) )
			return;
	}
	ImFontConfig config;
	config.SizePixels = size;
	io.Fonts->AddFontDefault( &config );
}

void ApplyTheme( float contentScale )
{
	ImGuiStyle& style       = ImGui::GetStyle();
	style                   = ImGuiStyle();
	style.WindowPadding     = ImVec2( 6, 6 );
	style.FramePadding      = ImVec2( 6, 3 );
	style.ItemSpacing       = ImVec2( 4, 4 );
	style.ItemInnerSpacing  = ImVec2( 4, 3 );
	style.CellPadding       = ImVec2( 4, 2 );
	style.IndentSpacing     = 12;
	style.ScrollbarSize     = 10;
	style.GrabMinSize       = 8;
	style.WindowBorderSize  = 1;
	style.FrameBorderSize   = 0;
	style.TabBorderSize     = 0;
	style.WindowRounding    = 0;
	style.ChildRounding     = 0;
	style.FrameRounding     = 2;
	style.PopupRounding     = 2;
	style.ScrollbarRounding = 2;
	style.GrabRounding      = 2;
	style.TabRounding       = 2;
	style.WindowTitleAlign  = ImVec2( 0.5f, 0.5f );
	style.DockingSeparatorSize = 3;

	ImVec4* c                            = style.Colors;
	c[ ImGuiCol_Text ]                   = Rgb( 222, 222, 222 );
	c[ ImGuiCol_TextDisabled ]           = Rgb( 120, 120, 120 );
	c[ ImGuiCol_WindowBg ]               = Rgb( 32, 32, 32 );
	c[ ImGuiCol_ChildBg ]                = Rgb( 26, 26, 26 );
	c[ ImGuiCol_PopupBg ]                = Rgb( 38, 38, 38, 0.98f );
	c[ ImGuiCol_Border ]                 = Rgb( 18, 18, 18 );
	c[ ImGuiCol_BorderShadow ]           = Rgb( 0, 0, 0, 0 );
	c[ ImGuiCol_FrameBg ]                = Rgb( 50, 50, 50 );
	c[ ImGuiCol_FrameBgHovered ]         = Rgb( 62, 62, 62 );
	c[ ImGuiCol_FrameBgActive ]          = Rgb( 70, 70, 70 );
	c[ ImGuiCol_TitleBg ]                = Rgb( 40, 40, 40 );
	c[ ImGuiCol_TitleBgActive ]          = Rgb( 46, 46, 46 );
	c[ ImGuiCol_TitleBgCollapsed ]       = Rgb( 40, 40, 40 );
	c[ ImGuiCol_MenuBarBg ]              = Rgb( 36, 36, 36 );
	c[ ImGuiCol_ScrollbarBg ]            = Rgb( 26, 26, 26 );
	c[ ImGuiCol_ScrollbarGrab ]          = Rgb( 70, 70, 70 );
	c[ ImGuiCol_ScrollbarGrabHovered ]   = Rgb( 90, 90, 90 );
	c[ ImGuiCol_ScrollbarGrabActive ]    = Rgb( 102, 225, 192 );
	c[ ImGuiCol_CheckMark ]              = Rgb( 102, 225, 192 );
	c[ ImGuiCol_SliderGrab ]             = Rgb( 102, 225, 192 );
	c[ ImGuiCol_SliderGrabActive ]       = Rgb( 150, 240, 215 );
	c[ ImGuiCol_Button ]                 = Rgb( 54, 54, 54 );
	c[ ImGuiCol_ButtonHovered ]          = Rgb( 70, 70, 70 );
	c[ ImGuiCol_ButtonActive ]           = Rgb( 102, 225, 192 );
	c[ ImGuiCol_Header ]                 = Rgb( 52, 52, 52 );
	c[ ImGuiCol_HeaderHovered ]          = Rgb( 64, 64, 64 );
	c[ ImGuiCol_HeaderActive ]           = Rgb( 56, 120, 104 );
	c[ ImGuiCol_Separator ]              = Rgb( 20, 20, 20 );
	c[ ImGuiCol_SeparatorHovered ]       = Rgb( 102, 225, 192, 0.6f );
	c[ ImGuiCol_SeparatorActive ]        = Rgb( 102, 225, 192 );
	c[ ImGuiCol_ResizeGrip ]             = Rgb( 0, 0, 0, 0 );
	c[ ImGuiCol_ResizeGripHovered ]      = Rgb( 102, 225, 192, 0.6f );
	c[ ImGuiCol_ResizeGripActive ]       = Rgb( 102, 225, 192 );
	c[ ImGuiCol_Tab ]                    = Rgb( 38, 38, 38 );
	c[ ImGuiCol_TabHovered ]             = Rgb( 64, 64, 64 );
	c[ ImGuiCol_TabSelected ]            = Rgb( 56, 56, 56 );
	c[ ImGuiCol_TabSelectedOverline ]    = Rgb( 102, 225, 192 );
	c[ ImGuiCol_TabDimmed ]              = Rgb( 34, 34, 34 );
	c[ ImGuiCol_TabDimmedSelected ]      = Rgb( 46, 46, 46 );
	c[ ImGuiCol_TabDimmedSelectedOverline ] = Rgb( 70, 70, 70 );
	c[ ImGuiCol_DockingPreview ]         = Rgb( 102, 225, 192, 0.45f );
	c[ ImGuiCol_DockingEmptyBg ]         = Rgb( 20, 20, 20 );
	c[ ImGuiCol_PlotLines ]              = Rgb( 102, 225, 192 );
	c[ ImGuiCol_PlotHistogram ]          = Rgb( 102, 225, 192 );
	c[ ImGuiCol_TableHeaderBg ]          = Rgb( 44, 44, 44 );
	c[ ImGuiCol_TextSelectedBg ]         = Rgb( 102, 225, 192, 0.35f );
	c[ ImGuiCol_DragDropTarget ]         = Rgb( 102, 225, 192 );
	c[ ImGuiCol_NavCursor ]              = Rgb( 102, 225, 192 );
	c[ ImGuiCol_ModalWindowDimBg ]       = Rgb( 0, 0, 0, 0.5f );

	style.ScaleAllSizes( contentScale );
	ImGui::GetIO().Fonts->Clear();
	LoadFont( 15.0f * contentScale );
}
}
