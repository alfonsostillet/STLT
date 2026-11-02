#include "FfglPlugin.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace fs = std::filesystem;

namespace stltvj
{
namespace
{
float AsFloat( ffgl::UInt32 bits )
{
	float f;
	std::memcpy( &f, &bits, sizeof( f ) );
	return f;
}

ffgl::UInt32 AsBits( float f )
{
	ffgl::UInt32 bits;
	std::memcpy( &bits, &f, sizeof( bits ) );
	return bits;
}

// Plugin strings may come without a terminator when they fill their field.
std::string Text( const char* s, size_t max = 512 )
{
	if( !s || s == (const char*)(uintptr_t)ffgl::FAIL )
		return {};
	size_t n = 0;
	while( n < max && s[ n ] )
		++n;
	return std::string( s, n );
}

ParamKind KindOf( ffgl::UInt32 type )
{
	switch( type )
	{
	case ffgl::PARAM_BOOLEAN: return ParamKind::Bool;
	case ffgl::PARAM_EVENT: return ParamKind::Event;
	case ffgl::PARAM_RED: return ParamKind::Red;
	case ffgl::PARAM_GREEN: return ParamKind::Green;
	case ffgl::PARAM_BLUE: return ParamKind::Blue;
	case ffgl::PARAM_XPOS: return ParamKind::XPos;
	case ffgl::PARAM_YPOS: return ParamKind::YPos;
	case ffgl::PARAM_OPTION: return ParamKind::Option;
	case ffgl::PARAM_BUFFER: return ParamKind::Buffer;
	case ffgl::PARAM_INTEGER: return ParamKind::Integer;
	case ffgl::PARAM_FILE: return ParamKind::File;
	case ffgl::PARAM_TEXT: return ParamKind::Text;
	case ffgl::PARAM_HUE: return ParamKind::Hue;
	case ffgl::PARAM_SATURATION: return ParamKind::Saturation;
	case ffgl::PARAM_BRIGHTNESS: return ParamKind::Brightness;
	case ffgl::PARAM_ALPHA: return ParamKind::Alpha;
	default: return ParamKind::Float;
	}
}

#ifdef _WIN32
std::wstring Widen( const std::string& utf8 )
{
	int length = MultiByteToWideChar( CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0 );
	std::wstring wide( length > 0 ? length - 1 : 0, L'\0' );
	if( length > 1 )
		MultiByteToWideChar( CP_UTF8, 0, utf8.c_str(), -1, wide.data(), length );
	return wide;
}
#endif

// The file to hand to the dynamic loader: a macOS .bundle is a folder with the binary inside.
std::string BinaryPath( const std::string& path )
{
	fs::path p = fs::u8path( path );
	if( p.extension() != ".bundle" )
		return path;
	fs::path macos = p / "Contents" / "MacOS";
	fs::path named = macos / p.stem();
	std::error_code ignored;
	if( fs::exists( named, ignored ) )
		return named.u8string();
	for( const fs::directory_entry& entry : fs::directory_iterator( macos, ignored ) )
		if( entry.is_regular_file( ignored ) )
			return entry.path().u8string();
	return named.u8string();
}
}

std::shared_ptr< FfglLibrary > FfglLibrary::Load( const std::string& path, std::string& error )
{
	std::shared_ptr< FfglLibrary > lib( new FfglLibrary() );
	lib->path          = path;
	std::string binary = BinaryPath( path );
#ifdef _WIN32
	// Altered search path: DLLs the plugin depends on are found next to it.
	HMODULE module = LoadLibraryExW( Widen( binary ).c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH );
	if( !module )
	{
		error = "cannot load (error " + std::to_string( GetLastError() ) + ")";
		return nullptr;
	}
	lib->handle   = module;
	lib->plugMain = (ffgl::PlugMain)GetProcAddress( module, "plugMain" );
#else
	void* module = dlopen( binary.c_str(), RTLD_NOW | RTLD_LOCAL );
	if( !module )
	{
		const char* reason = dlerror();
		error              = std::string( "cannot load: " ) + ( reason ? reason : "unknown error" );
		return nullptr;
	}
	lib->handle   = module;
	lib->plugMain = (ffgl::PlugMain)dlsym( module, "plugMain" );
#endif
	if( !lib->plugMain )
	{
		error = "not an FFGL plugin (no plugMain)";
		return nullptr;
	}

#if defined( __linux__ )
	// The FFGL SDK only initialises GLEW on Windows; on Linux its plugins expect the host to have done it.
	using GlewInit = unsigned int ( * )();
	if( GlewInit glewInit = (GlewInit)dlsym( module, "glewInit" ) )
	{
		if( unsigned char* experimental = (unsigned char*)dlsym( module, "glewExperimental" ) )
			*experimental = 1;
		glewInit();
		while( glGetError() != GL_NO_ERROR )
		{
		}
	}
#endif

	const ffgl::PluginInfo* info = (const ffgl::PluginInfo*)lib->Call( ffgl::GET_INFO, 0u ).PointerValue;
	if( !info || info == (const void*)(uintptr_t)ffgl::FAIL )
	{
		error = "no plugin info";
		return nullptr;
	}
	lib->apiMajor = (int)info->apiMajorVersion;
	lib->apiMinor = (int)info->apiMinorVersion;
	lib->name     = Text( info->name, 16 );
	lib->uniqueId = Text( info->uniqueId, 4 );
	lib->type     = info->type;
	if( lib->apiMajor < 2 )
	{
		error = "FFGL " + std::to_string( lib->apiMajor ) + "." + std::to_string( lib->apiMinor ) + " plugin: only FFGL 2 is supported";
		return nullptr;
	}
	if( lib->type != ffgl::TYPE_EFFECT && lib->type != ffgl::TYPE_SOURCE )
	{
		error = "mixer plugins are not supported yet";
		return nullptr;
	}
	if( lib->Call( ffgl::INITIALISE_V2, 0u ).UIntValue != ffgl::SUCCESS )
	{
		error = "the plugin failed to initialise";
		return nullptr;
	}
	if( const ffgl::ExtendedInfo* extended = (const ffgl::ExtendedInfo*)lib->Call( ffgl::GET_EXTENDED_INFO, 0u ).PointerValue )
		if( extended != (const void*)(uintptr_t)ffgl::FAIL )
			lib->description = Text( extended->description );
	lib->wantsTime = lib->Call( ffgl::GET_PLUGIN_CAPS, ffgl::CAP_SET_TIME ).UIntValue == 1;
	ffgl::UInt32 minInputs = lib->Call( ffgl::GET_PLUGIN_CAPS, ffgl::CAP_MINIMUM_INPUT_FRAMES ).UIntValue;
	lib->minInputs         = minInputs == ffgl::FAIL ? ( lib->type == ffgl::TYPE_SOURCE ? 0 : 1 ) : (int)minInputs;
	if( lib->type == ffgl::TYPE_EFFECT && lib->minInputs > 1 )
	{
		error = "effects with more than one input are not supported yet";
		return nullptr;
	}

	ffgl::UInt32 count = lib->Call( ffgl::GET_NUM_PARAMETERS, 0u ).UIntValue;
	if( count == ffgl::FAIL )
		count = 0;
	for( ffgl::UInt32 i = 0; i < count && i < 1024; ++i )
		lib->params.push_back( lib->DescribeParam( (int)i, nullptr ) );
	return lib;
}

FfglLibrary::~FfglLibrary()
{
	if( plugMain )
		Call( ffgl::DEINITIALISE, 0u );
#ifdef _WIN32
	if( handle )
		FreeLibrary( (HMODULE)handle );
#else
	if( handle )
		dlclose( handle );
#endif
}

ffgl::Mixed FfglLibrary::Call( ffgl::UInt32 code, ffgl::Mixed input, ffgl::InstanceId instance ) const
{
	return plugMain( code, input, instance );
}

ffgl::Mixed FfglLibrary::Call( ffgl::UInt32 code, ffgl::UInt32 input, ffgl::InstanceId instance ) const
{
	ffgl::Mixed in;
	in.PointerValue = nullptr;
	in.UIntValue    = input;
	return plugMain( code, in, instance );
}

ffgl::Mixed FfglLibrary::Call( ffgl::UInt32 code, void* input, ffgl::InstanceId instance ) const
{
	ffgl::Mixed in;
	in.PointerValue = input;
	return plugMain( code, in, instance );
}

void FfglLibrary::ReadOptions( ParamInfo& p, int index, ffgl::InstanceId instance ) const
{
	p.options.clear();
	p.optionValues.clear();
	ffgl::UInt32 n = Call( ffgl::GET_NUM_PARAMETER_ELEMENTS, (ffgl::UInt32)index, instance ).UIntValue;
	if( n == ffgl::FAIL )
		return;
	for( ffgl::UInt32 e = 0; e < n && e < 4096; ++e )
	{
		ffgl::ElementQuery query{ (ffgl::UInt32)index, e };
		p.options.push_back( Text( (const char*)Call( ffgl::GET_PARAMETER_ELEMENT_NAME, &query, instance ).PointerValue ) );
		ffgl::Mixed value = Call( ffgl::GET_PARAMETER_ELEMENT_VALUE, &query, instance );
		p.optionValues.push_back( value.UIntValue == ffgl::FAIL ? (float)e : AsFloat( value.UIntValue ) );
	}
}

ParamInfo FfglLibrary::DescribeParam( int index, ffgl::InstanceId instance ) const
{
	ParamInfo p;
	ffgl::UInt32 i  = (ffgl::UInt32)index;
	p.name          = Text( (const char*)Call( ffgl::GET_PARAMETER_NAME, i ).PointerValue );
	p.displayName   = p.name;
	ffgl::UInt32 type = Call( ffgl::GET_PARAMETER_TYPE, i ).UIntValue;
	p.kind          = KindOf( type );
	if( p.IsText() )
		p.defaultText = Text( (const char*)Call( ffgl::GET_PARAMETER_DEFAULT, i ).PointerValue, 4096 );
	else
		p.defaultValue = AsFloat( Call( ffgl::GET_PARAMETER_DEFAULT, i ).UIntValue );
	if( p.kind == ParamKind::Float || p.kind == ParamKind::Integer || p.kind == ParamKind::XPos || p.kind == ParamKind::YPos )
	{
		ffgl::GetRange range{ i, 0, 1 };
		if( Call( ffgl::GET_RANGE, &range ).UIntValue == ffgl::SUCCESS && range.max != range.min )
		{
			p.min = range.min;
			p.max = range.max;
		}
	}
	if( p.kind == ParamKind::Option )
	{
		ReadOptions( p, index, instance );
		if( !p.optionValues.empty() )
		{
			p.min = *std::min_element( p.optionValues.begin(), p.optionValues.end() );
			p.max = *std::max_element( p.optionValues.begin(), p.optionValues.end() );
		}
	}
	char group[ 256 ] = {};
	ffgl::GetString groupQuery{ i, group, sizeof( group ) - 1 };
	if( Call( ffgl::GET_PARAM_GROUP, &groupQuery ).UIntValue == ffgl::SUCCESS )
		p.group = Text( group, sizeof( group ) - 1 );
	ffgl::UInt32 visible = Call( ffgl::GET_PARAMETER_VISIBILITY, i, instance ).UIntValue;
	p.visible            = visible != 0; // FAIL (old plugins) also reads as visible
	if( instance )
	{
		char display[ 256 ] = {};
		ffgl::GetString displayQuery{ i, display, sizeof( display ) - 1 };
		if( Call( ffgl::GET_PARAM_DISPLAY_NAME, &displayQuery, instance ).UIntValue == ffgl::SUCCESS && display[ 0 ] )
			p.displayName = Text( display, sizeof( display ) - 1 );
	}
	return p;
}

std::string FfglId( const FfglLibrary& library )
{
	return "ffgl:" + library.uniqueId + ":" + library.name;
}

FfglEffect::FfglEffect( std::shared_ptr< FfglLibrary > lib ) : library( std::move( lib ) )
{
	params = library->params;
	InitValues();
}

FfglEffect::~FfglEffect()
{
	if( instance )
	{
		ResetGlState();
		library->Call( ffgl::DEINSTANTIATE_GL, 0u, instance );
		ResetGlState();
	}
}

std::string FfglEffect::Id() const
{
	return FfglId( *library );
}

void FfglEffect::PushValue( int index, float value )
{
	if( !instance || params[ index ].IsText() || params[ index ].kind == ParamKind::Buffer )
		return;
	ffgl::SetParameter set{ (ffgl::UInt32)index, {} };
	set.value.PointerValue = nullptr;
	set.value.UIntValue    = AsBits( value );
	library->Call( ffgl::SET_PARAMETER, &set, instance );
}

void FfglEffect::PushText( int index, const std::string& text )
{
	if( !instance )
		return;
	ffgl::SetParameter set{ (ffgl::UInt32)index, {} };
	set.value.PointerValue = (void*)text.c_str();
	library->Call( ffgl::SET_PARAMETER, &set, instance );
}

void FfglEffect::PushAll()
{
	for( size_t i = 0; i < params.size(); ++i )
	{
		if( params[ i ].IsText() )
			PushText( (int)i, texts[ i ] );
		else if( params[ i ].kind != ParamKind::Buffer )
		{
			PushValue( (int)i, effective[ i ] );
			pushed[ i ] = effective[ i ];
		}
	}
}

// Reads what changed inside the plugin since the last frame: option lists, visibility, names, values.
void FfglEffect::PollChanges()
{
	if( !instance )
		return;
	ffgl::GetParamEvents query{ 0, nullptr };
	if( library->Call( ffgl::GET_PARAMETER_EVENTS, &query, instance ).UIntValue != ffgl::SUCCESS || query.count == 0 )
		return;
	std::vector< ffgl::ParamEvent > events( std::min< ffgl::UInt32 >( query.count, 4096 ) );
	query.count  = (ffgl::UInt32)events.size();
	query.events = events.data();
	if( library->Call( ffgl::GET_PARAMETER_EVENTS, &query, instance ).UIntValue != ffgl::SUCCESS )
		return;
	for( ffgl::UInt32 e = 0; e < query.count && e < events.size(); ++e )
	{
		int index = (int)events[ e ].index;
		if( index < 0 || index >= (int)params.size() )
			continue;
		ParamInfo fresh = library->DescribeParam( index, instance );
		ParamInfo& p    = params[ index ];
		if( events[ e ].flags & ffgl::EVENT_ELEMENTS )
		{
			p.options      = fresh.options;
			p.optionValues = fresh.optionValues;
		}
		if( events[ e ].flags & ffgl::EVENT_VISIBILITY )
			p.visible = fresh.visible;
		if( events[ e ].flags & ffgl::EVENT_DISPLAY_NAME )
			p.displayName = fresh.displayName;
		if( ( events[ e ].flags & ffgl::EVENT_VALUE ) && !p.IsText() && animations[ index ].mode == AnimMode::Off )
		{
			float v         = AsFloat( library->Call( ffgl::GET_PARAMETER, (ffgl::UInt32)index, instance ).UIntValue );
			values[ index ] = v;
			pushed[ index ] = v;
		}
	}
}

bool FfglEffect::Process( GLuint input, int width, int height, GLuint targetFbo, const FrameInfo& frame )
{
	if( failed )
		return false;
	ResetGlState();
	glBindFramebuffer( GL_FRAMEBUFFER, targetFbo );
	glViewport( 0, 0, width, height );
	ffgl::Viewport viewport{ 0, 0, (uint32_t)width, (uint32_t)height };
	if( !instance )
	{
		ffgl::Mixed result = library->Call( ffgl::INSTANTIATE_GL, &viewport );
		ResetGlState();
		glBindFramebuffer( GL_FRAMEBUFFER, targetFbo );
		glViewport( 0, 0, width, height );
		if( !result.PointerValue || result.PointerValue == (void*)(uintptr_t)ffgl::FAIL )
		{
			failed = true;
			error  = "the plugin could not create an instance";
			return false;
		}
		instance       = result.PointerValue;
		instanceWidth  = width;
		instanceHeight = height;
		ffgl::HostInfo host{ "STLT", STLT_VERSION };
		library->Call( ffgl::SET_HOSTINFO, &host, instance );
		// Option lists and visibility can depend on the instance (dynamic menus).
		for( size_t i = 0; i < params.size(); ++i )
		{
			ParamInfo fresh = library->DescribeParam( (int)i, instance );
			params[ i ].visible     = fresh.visible;
			params[ i ].displayName = fresh.displayName;
			if( params[ i ].kind == ParamKind::Option )
			{
				params[ i ].options      = fresh.options;
				params[ i ].optionValues = fresh.optionValues;
			}
		}
		PushAll();
	}
	else if( width != instanceWidth || height != instanceHeight )
	{
		library->Call( ffgl::RESIZE, &viewport, instance );
		instanceWidth  = width;
		instanceHeight = height;
	}

	double beatsPerBar = 4;
	double bar         = frame.beat / beatsPerBar;
	ffgl::BeatInfo beat{ (float)frame.bpm, (float)( bar - std::floor( bar ) ) };
	library->Call( ffgl::SET_BEATINFO, &beat, instance );
	if( library->wantsTime )
	{
		double time = frame.time;
		library->Call( ffgl::SET_TIME, &time, instance );
	}

	ffgl::Texture texture{ (ffgl::UInt32)width, (ffgl::UInt32)height, (ffgl::UInt32)width, (ffgl::UInt32)height, input };
	ffgl::Texture* inputs[ 1 ] = { &texture };
	ffgl::ProcessOpenGL process{ input ? 1u : 0u, input ? inputs : nullptr, targetFbo };
	ResetGlState();
	glBindFramebuffer( GL_FRAMEBUFFER, targetFbo );
	glViewport( 0, 0, width, height );
	bool ok = library->Call( ffgl::PROCESS_OPENGL, &process, instance ).UIntValue == ffgl::SUCCESS;
	// Whatever the plugin left behind, our own passes start from the default state.
	ResetGlState();
	glBindFramebuffer( GL_FRAMEBUFFER, targetFbo );
	glViewport( 0, 0, width, height );
	return ok;
}
}
