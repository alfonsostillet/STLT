#include "Composition.h"
#include <algorithm>
#include <cmath>
#include <json.hpp>
#include "fx/EffectRegistry.h"

namespace stltvj
{
const char* BlendModeName( BlendMode mode )
{
	static const char* names[] = { "Alpha", "Add", "Screen", "Multiply", "Lighten", "Darken", "Difference", "Subtract" };
	return names[ std::clamp( (int)mode, 0, (int)BlendMode::Count - 1 ) ];
}

const char* PlayModeName( PlayMode mode )
{
	static const char* names[] = { "Loop", "Bounce", "Play Once" };
	return names[ std::clamp( (int)mode, 0, (int)PlayMode::Count - 1 ) ];
}

const char* FitModeName( FitMode mode )
{
	static const char* names[] = { "Fit", "Fill", "Stretch", "Native" };
	return names[ std::clamp( (int)mode, 0, (int)FitMode::Count - 1 ) ];
}

void Clip::Restart()
{
	time       = direction < 0 ? Duration() : 0.0;
	bounceSign = 1;
	finished   = false;
}

int Clip::PlayDirection() const
{
	if( finished || direction == 0 || speed == 0 )
		return 0;
	return direction * bounceSign * ( speed < 0 ? -1 : 1 );
}

void Clip::Advance( double seconds )
{
	double duration = Duration();
	if( duration <= 0 || direction == 0 || finished )
		return;
	time += seconds * speed * direction * bounceSign;
	switch( playMode )
	{
	case PlayMode::Loop:
		time = std::fmod( time, duration );
		if( time < 0 )
			time += duration;
		break;
	case PlayMode::Bounce:
		for( int guard = 0; guard < 4 && ( time < 0 || time > duration ); ++guard )
		{
			time       = time < 0 ? -time : 2 * duration - time;
			bounceSign = -bounceSign;
		}
		time = std::clamp( time, 0.0, duration );
		break;
	case PlayMode::Once:
		if( time < 0 || time >= duration )
		{
			time     = std::clamp( time, 0.0, duration );
			finished = true;
		}
		break;
	default: break;
	}
}

int64_t Clip::CurrentFrame() const
{
	if( !Loaded() )
		return 0;
	return (int64_t)player->Source().FrameAt( time );
}

Composition Composition::MakeDefault()
{
	Composition c;
	for( int i = 0; i < 9; ++i )
		c.AddColumn();
	for( int i = 0; i < 3; ++i )
		c.AddLayer();
	c.selectedLayer = 0;
	return c;
}

void Composition::AddLayer()
{
	Layer layer;
	layer.name = "Layer " + std::to_string( layers.size() + 1 );
	layer.clips.resize( columns.size() );
	layers.push_back( std::move( layer ) );
}

void Composition::AddColumn()
{
	columns.push_back( "Column " + std::to_string( columns.size() + 1 ) );
	Normalise();
}

void Composition::Normalise()
{
	for( Layer& layer : layers )
	{
		layer.clips.resize( columns.size() );
		if( layer.active >= (int)columns.size() )
			layer.active = -1;
	}
	selectedLayer = std::clamp( selectedLayer, 0, std::max( 0, (int)layers.size() - 1 ) );
	if( selectedColumn >= (int)columns.size() )
		selectedColumn = -1;
}

Clip* Composition::SelectedClip()
{
	if( selectedLayer < 0 || selectedLayer >= (int)layers.size() || selectedColumn < 0 || selectedColumn >= ColumnCount() )
		return nullptr;
	Clip& clip = layers[ selectedLayer ].clips[ selectedColumn ];
	return clip.Empty() ? nullptr : &clip;
}

static nlohmann::json SaveSlot( const EffectSlot& slot )
{
	using nlohmann::json;
	json state = json::parse( slot.effect ? SaveEffectState( *slot.effect ) : slot.savedState, nullptr, false );
	if( state.is_discarded() || !state.is_object() )
		state = json::object();
	if( !slot.effect && !state.contains( "id" ) )
		state[ "id" ] = slot.missingId;
	state[ "bypass" ] = slot.bypass;
	state[ "mix" ]    = slot.mix;
	return state;
}

static nlohmann::json SaveChain( const EffectChain& chain )
{
	nlohmann::json list = nlohmann::json::array();
	for( const EffectSlot& slot : chain )
		list.push_back( SaveSlot( slot ) );
	return list;
}

// Effects come back as ids plus settings; the registry turns them into live effects (EffectRegistry::Restore).
static EffectChain LoadChain( const nlohmann::json& list )
{
	EffectChain chain;
	if( !list.is_array() )
		return chain;
	for( const nlohmann::json& item : list )
	{
		if( !item.is_object() || !item.contains( "id" ) || !item[ "id" ].is_string() )
			continue;
		EffectSlot slot;
		slot.missingId  = item[ "id" ].get< std::string >();
		slot.savedState = item.dump();
		slot.bypass     = item.value( "bypass", false );
		slot.mix        = item.value( "mix", 1.0f );
		chain.push_back( std::move( slot ) );
	}
	return chain;
}

std::string SaveComposition( const Composition& c )
{
	using nlohmann::json;
	json root;
	root[ "format" ] = "stlt-vj-composition";
	root[ "version" ] = 1;
	root[ "name" ]    = c.name;
	root[ "width" ]   = c.width;
	root[ "height" ]  = c.height;
	root[ "master" ]  = c.master;
	root[ "speed" ]   = c.speed;
	root[ "bpm" ]     = c.bpm;
	root[ "columns" ] = c.columns;
	root[ "effects" ] = SaveChain( c.effects );
	json layers       = json::array();
	for( const Layer& layer : c.layers )
	{
		json l;
		l[ "name" ]    = layer.name;
		l[ "opacity" ] = layer.opacity;
		l[ "blend" ]   = BlendModeName( layer.blend );
		l[ "bypass" ]  = layer.bypass;
		l[ "effects" ] = SaveChain( layer.effects );
		json clips     = json::array();
		for( const Clip& clip : layer.clips )
		{
			if( clip.Empty() )
			{
				clips.push_back( nullptr );
				continue;
			}
			json k;
			if( clip.IsGenerator() )
			{
				json state       = json::parse( clip.generator ? SaveEffectState( *clip.generator ) : clip.generatorState, nullptr, false );
				k[ "generator" ] = state.is_object() ? state : json{ { "id", clip.generatorId } };
			}
			else
				k[ "path" ] = clip.path;
			k[ "name" ]      = clip.name;
			k[ "speed" ]     = clip.speed;
			k[ "direction" ] = clip.direction;
			k[ "playMode" ]  = PlayModeName( clip.playMode );
			k[ "opacity" ]   = clip.opacity;
			k[ "fit" ]       = FitModeName( clip.fit );
			k[ "position" ]  = { clip.positionX, clip.positionY };
			k[ "scale" ]     = clip.scale;
			k[ "rotation" ]  = clip.rotation;
			k[ "effects" ]   = SaveChain( clip.effects );
			clips.push_back( k );
		}
		l[ "clips" ] = clips;
		layers.push_back( l );
	}
	root[ "layers" ] = layers;
	return root.dump( 2 );
}

template< typename Enum >
static Enum ByName( const nlohmann::json& value, const char* ( *name )( Enum ), Enum fallback )
{
	if( !value.is_string() )
		return fallback;
	for( int i = 0; i < (int)Enum::Count; ++i )
		if( value.get< std::string >() == name( (Enum)i ) )
			return (Enum)i;
	return fallback;
}

bool LoadComposition( const std::string& text, Composition& out, std::string& error )
{
	using nlohmann::json;
	json root = json::parse( text, nullptr, false );
	if( root.is_discarded() || !root.is_object() || root.value( "format", "" ) != "stlt-vj-composition" )
	{
		error = "Not an STLT-VJ composition";
		return false;
	}
	Composition c;
	c.name    = root.value( "name", c.name );
	c.width   = std::clamp( root.value( "width", c.width ), 16, 16384 );
	c.height  = std::clamp( root.value( "height", c.height ), 16, 16384 );
	c.master  = root.value( "master", c.master );
	c.speed   = root.value( "speed", c.speed );
	c.bpm     = root.value( "bpm", c.bpm );
	if( root.contains( "effects" ) )
		c.effects = LoadChain( root[ "effects" ] );
	if( root.contains( "columns" ) && root[ "columns" ].is_array() )
		for( const json& name : root[ "columns" ] )
			c.columns.push_back( name.is_string() ? name.get< std::string >() : "Column" );
	if( root.contains( "layers" ) && root[ "layers" ].is_array() )
		for( const json& l : root[ "layers" ] )
		{
			Layer layer;
			layer.name    = l.value( "name", "Layer " + std::to_string( c.layers.size() + 1 ) );
			layer.opacity = l.value( "opacity", 1.0f );
			layer.blend   = ByName( l.value( "blend", json() ), BlendModeName, BlendMode::Alpha );
			layer.bypass  = l.value( "bypass", false );
			if( l.contains( "effects" ) )
				layer.effects = LoadChain( l[ "effects" ] );
			if( l.contains( "clips" ) && l[ "clips" ].is_array() )
				for( const json& k : l[ "clips" ] )
				{
					Clip clip;
					if( k.is_object() )
					{
						clip.path      = k.value( "path", "" );
						if( k.contains( "generator" ) && k[ "generator" ].is_object() && k[ "generator" ].contains( "id" ) )
						{
							clip.generatorId    = k[ "generator" ].value( "id", "" );
							clip.generatorState = k[ "generator" ].dump();
						}
						if( k.contains( "effects" ) )
							clip.effects = LoadChain( k[ "effects" ] );
						clip.name      = k.value( "name", "" );
						clip.speed     = k.value( "speed", 1.0f );
						clip.direction = std::clamp( k.value( "direction", 1 ), -1, 1 );
						clip.playMode  = ByName( k.value( "playMode", json() ), PlayModeName, PlayMode::Loop );
						clip.opacity   = k.value( "opacity", 1.0f );
						clip.fit       = ByName( k.value( "fit", json() ), FitModeName, FitMode::Fit );
						if( k.contains( "position" ) && k[ "position" ].is_array() && k[ "position" ].size() == 2 )
						{
							clip.positionX = k[ "position" ][ 0 ].get< float >();
							clip.positionY = k[ "position" ][ 1 ].get< float >();
						}
						clip.scale    = k.value( "scale", 1.0f );
						clip.rotation = k.value( "rotation", 0.0f );
					}
					layer.clips.push_back( std::move( clip ) );
				}
			c.layers.push_back( std::move( layer ) );
		}
	if( c.columns.empty() )
		for( int i = 0; i < 9; ++i )
			c.columns.push_back( "Column " + std::to_string( i + 1 ) );
	if( c.layers.empty() )
		c.AddLayer();
	size_t widest = c.columns.size();
	for( const Layer& layer : c.layers )
		widest = std::max( widest, layer.clips.size() );
	while( c.columns.size() < widest )
		c.columns.push_back( "Column " + std::to_string( c.columns.size() + 1 ) );
	c.Normalise();
	out = std::move( c );
	return true;
}
}
