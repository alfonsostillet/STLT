#include "EffectRegistry.h"
#include <algorithm>
#include <filesystem>
#include <json.hpp>
#include "Settings.h"
#include "fx/BuiltinEffects.h"
#include "fx/FfglPlugin.h"
#include "fx/IsfEffect.h"

namespace fs = std::filesystem;

namespace stltvj
{
namespace
{
bool IsPluginFile( const fs::path& path )
{
#if defined( _WIN32 )
	return path.extension() == ".dll";
#elif defined( __APPLE__ )
	return path.extension() == ".bundle";
#else
	return path.extension() == ".so";
#endif
}
}

EffectRegistry::EffectRegistry()
{
	for( const BuiltinDef& def : BuiltinDefs() )
	{
		Entry e;
		e.id     = std::string( "builtin:" ) + def.name;
		e.name   = def.name;
		e.origin = "Built-in";
		e.source = def.source;
		entries.push_back( e );
	}
}

EffectRegistry::~EffectRegistry() = default;

std::vector< std::string > EffectRegistry::DefaultFolders( const std::string& configDir )
{
	std::vector< std::string > folders;
	// The effects that come with STLT-VJ (STLT-CHASER, STLT-WIPER, STLT-OUTLINER) first, so they win over
	// copies installed for Resolume.
	fs::path here = fs::u8path( ExecutableDirectory() );
#if defined( __APPLE__ )
	folders.push_back( ( here.parent_path() / "Resources" / "plugins" ).u8string() );
#else
	folders.push_back( ( here / "plugins" ).u8string() );
#endif
	folders.push_back( ( fs::u8path( configDir ) / "plugins" ).u8string() );
	// ISF shaders: those that come with STLT-VJ, the user's own, and the system's ISF folder.
#if defined( __APPLE__ )
	folders.push_back( ( here.parent_path() / "Resources" / "isf" ).u8string() );
#else
	folders.push_back( ( here / "isf" ).u8string() );
#endif
	folders.push_back( ( fs::u8path( configDir ) / "isf" ).u8string() );
#if defined( _WIN32 )
	folders.push_back( "C:/ProgramData/ISF" );
#elif defined( __APPLE__ )
	folders.push_back( "/Library/Graphics/ISF" );
	folders.push_back( ( fs::u8path( HomeDirectory() ) / "Library" / "Graphics" / "ISF" ).u8string() );
#endif
	fs::path documents = fs::u8path( DocumentsDirectory() );
	// Plugins installed for Resolume, such as STLT-CHASER, work here too.
	folders.push_back( ( documents / "Resolume Arena" / "Extra Effects" ).u8string() );
	folders.push_back( ( documents / "Resolume Avenue" / "Extra Effects" ).u8string() );
	return folders;
}

void EffectRegistry::Scan( const std::vector< std::string >& folders )
{
	failures.clear();
	for( const std::string& folder : folders )
		ScanFolder( folder, 0 );
	// Built-in first (in their own order), then ISF by category and name, then FFGL by name.
	auto rank = []( const Entry& e ) { return e.library ? 2 : e.isf ? 1 : 0; };
	std::stable_sort( entries.begin(), entries.end(), [ & ]( const Entry& a, const Entry& b ) {
		if( rank( a ) != rank( b ) )
			return rank( a ) < rank( b );
		if( rank( a ) == 0 )
			return false;
		if( a.category != b.category )
			return a.category < b.category;
		return a.name < b.name;
	} );
}

void EffectRegistry::ScanFolder( const std::string& folder, int depth )
{
	std::error_code error;
	if( depth > 4 || !fs::is_directory( fs::u8path( folder ), error ) )
		return;
	std::vector< fs::path > children;
	for( const fs::directory_entry& entry : fs::directory_iterator( fs::u8path( folder ), fs::directory_options::skip_permission_denied, error ) )
		children.push_back( entry.path() );
	std::sort( children.begin(), children.end() );
	for( const fs::path& child : children )
	{
		std::error_code ignored;
		if( IsPluginFile( child ) )
			Add( child.u8string() );
		else if( child.extension() == ".fs" )
			AddIsf( child.u8string() );
		else if( fs::is_directory( child, ignored ) )
			ScanFolder( child.u8string(), depth + 1 );
	}
}

void EffectRegistry::Add( const std::string& path )
{
	if( std::find( loadedPaths.begin(), loadedPaths.end(), path ) != loadedPaths.end() )
		return;
	std::string error;
	std::shared_ptr< FfglLibrary > library = FfglLibrary::Load( path, error );
	if( !library )
	{
		failures.push_back( { path, error } );
		return;
	}
	loadedPaths.push_back( path );
	Entry e;
	e.id          = FfglId( *library );
	e.name        = library->name;
	e.origin      = path;
	e.description = library->description;
	e.source      = library->type == ffgl::TYPE_SOURCE;
	e.library     = library;
	if( Find( e.id ) )
	{
		failures.push_back( { path, "same plugin as " + Find( e.id )->origin + " (skipped)" } );
		return;
	}
	entries.push_back( e );
}

void EffectRegistry::AddIsf( const std::string& path )
{
	if( std::find( loadedPaths.begin(), loadedPaths.end(), path ) != loadedPaths.end() )
		return;
	loadedPaths.push_back( path );
	std::string fragment, vs, error;
	fs::path file = fs::u8path( path );
	if( !ReadTextFile( path, fragment ) )
	{
		failures.push_back( { path, "cannot be read" } );
		return;
	}
	fs::path vertex = file;
	vertex.replace_extension( ".vs" );
	ReadTextFile( vertex.u8string(), vs ); // its own vertex stage, when it has one
	auto shader = std::make_shared< IsfShader >();
	if( !ParseIsf( file.stem().u8string(), fragment, vs, *shader, error ) )
	{
		failures.push_back( { path, error } );
		return;
	}
	if( shader->usesImports || shader->usesAudio )
	{
		failures.push_back( { path, shader->usesImports ? "needs images of its own (not supported yet)" : "needs the sound as a texture (not supported yet)" } );
		return;
	}
	Entry e;
	e.id          = "isf:" + shader->name;
	e.name        = shader->name;
	e.origin      = path;
	e.description = shader->description;
	e.source      = shader->source;
	e.category    = shader->categories.empty() ? "Other" : shader->categories[ 0 ];
	e.isf         = shader;
	if( Find( e.id ) )
	{
		failures.push_back( { path, "same name as " + Find( e.id )->origin + " (skipped)" } );
		return;
	}
	entries.push_back( e );
}

const EffectRegistry::Entry* EffectRegistry::Find( const std::string& id ) const
{
	for( const Entry& e : entries )
		if( e.id == id )
			return &e;
	return nullptr;
}

std::unique_ptr< Effect > EffectRegistry::Create( const std::string& id ) const
{
	const Entry* e = Find( id );
	if( !e )
		return nullptr;
	if( e->library )
		return std::make_unique< FfglEffect >( e->library );
	if( e->isf )
		return CreateIsfEffect( e->isf, e->id );
	return CreateBuiltin( e->name );
}

void EffectRegistry::Restore( EffectSlot& slot ) const
{
	if( slot.effect || slot.missingId.empty() )
		return;
	slot.effect = Create( slot.missingId );
	if( !slot.effect )
		return;
	LoadEffectState( *slot.effect, slot.savedState );
	slot.missingId.clear();
	slot.savedState.clear();
}

void EffectRegistry::Restore( EffectChain& chain ) const
{
	for( EffectSlot& slot : chain )
		Restore( slot );
}

std::string SaveEffectState( const Effect& effect )
{
	using nlohmann::json;
	json state;
	state[ "id" ]   = effect.Id();
	state[ "name" ] = effect.Name();
	json values     = json::object();
	json anims      = json::object();
	for( size_t i = 0; i < effect.Params().size(); ++i )
	{
		const ParamInfo& p = effect.Params()[ i ];
		if( p.kind == ParamKind::Event || p.kind == ParamKind::Buffer || p.name.empty() )
			continue;
		if( p.IsText() )
			values[ p.name ] = effect.Text( (int)i );
		else
			values[ p.name ] = effect.Value( (int)i );
		const Animation& a = effect.Anim( (int)i );
		if( a.mode != AnimMode::Off )
		{
			anims[ p.name ] = { { "mode", AnimModeName( a.mode ) }, { "beats", a.beats }, { "from", a.from }, { "to", a.to }, { "reverse", a.reverse } };
			if( a.seconds )
				anims[ p.name ][ "seconds" ] = true;
		}
	}
	state[ "params" ] = values;
	if( !anims.empty() )
		state[ "animations" ] = anims;
	return state.dump();
}

void LoadEffectState( Effect& effect, const std::string& text )
{
	using nlohmann::json;
	json state = json::parse( text, nullptr, false );
	if( state.is_discarded() || !state.is_object() )
		return;
	if( state.contains( "params" ) && state[ "params" ].is_object() )
		for( auto& [ name, value ] : state[ "params" ].items() )
		{
			int index = effect.FindParam( name );
			if( index < 0 )
				continue;
			if( value.is_string() && effect.Params()[ index ].IsText() )
				effect.SetText( index, value.get< std::string >() );
			else if( value.is_number() )
				effect.SetValue( index, value.get< float >() );
		}
	if( state.contains( "animations" ) && state[ "animations" ].is_object() )
		for( auto& [ name, a ] : state[ "animations" ].items() )
		{
			int index = effect.FindParam( name );
			if( index < 0 || !a.is_object() )
				continue;
			Animation& anim = effect.Anim( index );
			std::string mode = a.value( "mode", "Off" );
			for( int m = 0; m < (int)AnimMode::Count; ++m )
				if( mode == AnimModeName( (AnimMode)m ) )
					anim.mode = (AnimMode)m;
			anim.beats   = a.value( "beats", 4.0f );
			anim.from    = a.value( "from", 0.0f );
			anim.to      = a.value( "to", 1.0f );
			anim.reverse = a.value( "reverse", false );
			anim.seconds = a.value( "seconds", false );
		}
}
}
