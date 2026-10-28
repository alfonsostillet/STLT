#include "Composition.h"
#include <algorithm>
#include <cmath>
#include <json.hpp>
#include "fx/EffectRegistry.h"

namespace stltvj
{
const char* BlendModeName( BlendMode mode )
{
	static const char* names[] = { "Alpha", "Add", "Screen", "Multiply", "Lighten", "Darken", "Difference", "Subtract", "Luma Key", "Luma Key Inverted" };
	return names[ std::clamp( (int)mode, 0, (int)BlendMode::Count - 1 ) ];
}

const char* PlayModeName( PlayMode mode )
{
	static const char* names[] = { "Loop", "Bounce", "Play Once & Hold", "Play Once & Clear" };
	return names[ std::clamp( (int)mode, 0, (int)PlayMode::Count - 1 ) ];
}

const char* FitModeName( FitMode mode )
{
	static const char* names[] = { "Fit", "Fill", "Stretch", "Native" };
	return names[ std::clamp( (int)mode, 0, (int)FitMode::Count - 1 ) ];
}

const char* TransitionModeName( TransitionMode mode )
{
	static const char* names[] = { "Fade", "Fade Through Black", "Wipe Left", "Wipe Right", "Wipe Up", "Wipe Down" };
	return names[ std::clamp( (int)mode, 0, (int)TransitionMode::Count - 1 ) ];
}

const char* ClipTargetName( ClipTarget target )
{
	static const char* names[] = { "Own Layer", "Selected Layer" };
	return names[ std::clamp( (int)target, 0, (int)ClipTarget::Count - 1 ) ];
}

const char* LaunchQuantizeName( LaunchQuantize quantize )
{
	static const char* names[] = { "Now", "Beat", "Bar" };
	return names[ std::clamp( (int)quantize, 0, (int)LaunchQuantize::Count - 1 ) ];
}

const char* ClipPropertyName( ClipProperty property )
{
	static const char* names[] = { "opacity", "positionX", "positionY", "scale", "rotation" };
	return names[ std::clamp( (int)property, 0, (int)ClipProperty::Count - 1 ) ];
}

double Clip::RangeStart() const
{
	return std::clamp( inPoint, 0.0, Duration() );
}

double Clip::RangeEnd() const
{
	double duration = Duration();
	double end      = outPoint < 0 ? duration : std::clamp( outPoint, 0.0, duration );
	return std::max( end, std::min( duration, RangeStart() + 1e-3 ) );
}

double Clip::Rate( double bpm ) const
{
	if( syncBeats > 0 && bpm > 0 )
		return ( RangeEnd() - RangeStart() ) / ( syncBeats * 60.0 / bpm );
	return speed;
}

void Clip::Restart()
{
	time       = direction < 0 ? RangeEnd() : RangeStart();
	bounceSign = 1;
	finished   = false;
}

void Clip::JumpTo( double seconds )
{
	time       = std::clamp( seconds, RangeStart(), RangeEnd() );
	bounceSign = 1;
	finished   = false;
}

int Clip::PlayDirection() const
{
	float rate = syncBeats > 0 ? 1.0f : speed;
	if( finished || direction == 0 || rate == 0 )
		return 0;
	return direction * bounceSign * ( rate < 0 ? -1 : 1 );
}

void Clip::Advance( double seconds, double bpm )
{
	double start = RangeStart(), end = RangeEnd(), length = end - start;
	if( Duration() <= 0 || direction == 0 || finished || length <= 0 )
		return;
	time += seconds * Rate( bpm ) * direction * bounceSign;
	switch( playMode )
	{
	case PlayMode::Loop:
		// Anywhere outside the range (the in or out point moved past the playhead) comes back into it.
		time = start + std::fmod( time - start, length );
		if( time < start )
			time += length;
		break;
	case PlayMode::Bounce:
		if( time < start - length || time > end + length )
			time = std::clamp( time, start, end );
		for( int guard = 0; guard < 4 && ( time < start || time > end ); ++guard )
		{
			time       = time < start ? 2 * start - time : 2 * end - time;
			bounceSign = -bounceSign;
		}
		time = std::clamp( time, start, end );
		break;
	case PlayMode::Once:
	case PlayMode::OnceClear:
		if( time < start || time >= end )
		{
			time     = std::clamp( time, start, end );
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

float Clip::Value( ClipProperty property ) const
{
	switch( property )
	{
	case ClipProperty::Opacity: return opacity;
	case ClipProperty::PositionX: return positionX;
	case ClipProperty::PositionY: return positionY;
	case ClipProperty::Scale: return scale;
	case ClipProperty::Rotation: return rotation;
	default: return 0;
	}
}

void Clip::PropertyRange( ClipProperty property, float compWidth, float compHeight, float& min, float& max )
{
	switch( property )
	{
	case ClipProperty::Opacity: min = 0, max = 1; break;
	case ClipProperty::PositionX: min = -compWidth / 2, max = compWidth / 2; break;
	case ClipProperty::PositionY: min = -compHeight / 2, max = compHeight / 2; break;
	case ClipProperty::Scale: min = 0, max = 2; break;
	case ClipProperty::Rotation: min = -180, max = 180; break;
	default: min = 0, max = 1; break;
	}
}

float Clip::Shown( ClipProperty property, double beat, float compWidth, float compHeight ) const
{
	const Animation& a = animations[ (int)property ];
	if( a.mode == AnimMode::Off )
		return Value( property );
	float min, max;
	PropertyRange( property, compWidth, compHeight, min, max );
	return min + ( max - min ) * a.Evaluate( beat );
}

float Layer::ShownOpacity( double beat ) const
{
	return opacityAnimation.mode == AnimMode::Off ? opacity : opacityAnimation.Evaluate( beat );
}

float Layer::CrossfadeGain( float x ) const
{
	x = std::clamp( x, 0.0f, 1.0f );
	if( crossfader == 1 )
		return std::min( 1.0f, 2 * ( 1 - x ) );
	if( crossfader == 2 )
		return std::min( 1.0f, 2 * x );
	return 1.0f;
}

double Layer::TransitionSeconds( double bpm ) const
{
	if( transitionDuration <= 0 )
		return 0;
	return transitionInBeats ? transitionDuration * 60.0 / std::max( bpm, 1.0 ) : transitionDuration;
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
	for( int l = 0; l < (int)layers.size(); ++l )
	{
		Layer& layer = layers[ l ];
		layer.clips.resize( columns.size() );
		if( layer.active >= 0 && layer.activeRow < 0 )
			layer.activeRow = l;
		if( layer.active >= (int)columns.size() || layer.activeRow >= (int)layers.size() )
			layer.active = layer.activeRow = -1;
		if( layer.previous >= (int)columns.size() || layer.previousRow >= (int)layers.size() || layer.previous < 0 )
			layer.previous = layer.previousRow = -1;
	}
	selectedLayer = std::clamp( selectedLayer, 0, std::max( 0, (int)layers.size() - 1 ) );
	selectedRow   = std::clamp( selectedRow, 0, std::max( 0, (int)layers.size() - 1 ) );
	if( selectedColumn >= (int)columns.size() )
		selectedColumn = -1;
}

Clip* Composition::SelectedClip()
{
	if( selectedRow < 0 || selectedRow >= (int)layers.size() || selectedColumn < 0 || selectedColumn >= ColumnCount() )
		return nullptr;
	Clip& clip = layers[ selectedRow ].clips[ selectedColumn ];
	return clip.Empty() ? nullptr : &clip;
}

Clip* Composition::ClipAt( int row, int column )
{
	if( row < 0 || row >= (int)layers.size() || column < 0 || column >= (int)layers[ row ].clips.size() )
		return nullptr;
	return &layers[ row ].clips[ column ];
}

Clip* Composition::ActiveClip( int layer )
{
	if( layer < 0 || layer >= (int)layers.size() || layers[ layer ].active < 0 )
		return nullptr;
	return ClipAt( layers[ layer ].activeRow, layers[ layer ].active );
}

Clip* Composition::OutgoingClip( int layer )
{
	if( layer < 0 || layer >= (int)layers.size() || layers[ layer ].previous < 0 )
		return nullptr;
	return ClipAt( layers[ layer ].previousRow, layers[ layer ].previous );
}

int Composition::LayerShowing( int row, int column ) const
{
	for( int l = 0; l < (int)layers.size(); ++l )
	{
		const Layer& layer = layers[ l ];
		if( ( layer.active == column && layer.activeRow == row ) || ( layer.previous == column && layer.previousRow == row ) )
			return l;
	}
	return -1;
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

static nlohmann::json SaveAnimation( const Animation& a )
{
	return { { "mode", AnimModeName( a.mode ) }, { "beats", a.beats }, { "from", a.from }, { "to", a.to }, { "reverse", a.reverse } };
}

static Animation LoadAnimation( const nlohmann::json& j )
{
	Animation a;
	if( !j.is_object() )
		return a;
	std::string mode = j.value( "mode", "" );
	for( int m = 0; m < (int)AnimMode::Count; ++m )
		if( mode == AnimModeName( (AnimMode)m ) )
			a.mode = (AnimMode)m;
	a.beats   = j.value( "beats", 4.0f );
	a.from    = j.value( "from", 0.0f );
	a.to      = j.value( "to", 1.0f );
	a.reverse = j.value( "reverse", false );
	return a;
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
	root[ "audioVolume" ] = c.audioVolume;
	root[ "speed" ]   = c.speed;
	root[ "bpm" ]     = c.bpm;
	root[ "launchQuantize" ] = LaunchQuantizeName( c.quantize );
	root[ "crossfader" ] = c.crossfader;
	root[ "clipTarget" ] = ClipTargetName( c.target );
	root[ "columns" ] = c.columns;
	root[ "effects" ] = SaveChain( c.effects );
	json layers       = json::array();
	for( const Layer& layer : c.layers )
	{
		json l;
		l[ "name" ]    = layer.name;
		l[ "master" ]  = layer.master;
		l[ "opacity" ] = layer.opacity;
		l[ "volume" ]  = layer.volume;
		l[ "speed" ]   = layer.speed;
		l[ "crossfader" ] = layer.crossfader == 1 ? "A" : layer.crossfader == 2 ? "B" : "";
		l[ "blend" ]   = BlendModeName( layer.blend );
		l[ "bypass" ]  = layer.bypass;
		l[ "effects" ] = SaveChain( layer.effects );
		if( layer.opacityAnimation.mode != AnimMode::Off )
			l[ "opacityAnimation" ] = SaveAnimation( layer.opacityAnimation );
		l[ "transition" ] = { { "mode", TransitionModeName( layer.transition ) }, { "duration", layer.transitionDuration }, { "beats", layer.transitionInBeats } };
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
			k[ "volume" ]    = clip.volume;
			if( clip.inPoint > 0 )
				k[ "in" ] = clip.inPoint;
			if( clip.outPoint >= 0 )
				k[ "out" ] = clip.outPoint;
			json cues = json::array();
			bool anyCue = false;
			for( double cue : clip.cues )
			{
				cues.push_back( cue >= 0 ? json( cue ) : json( nullptr ) );
				anyCue |= cue >= 0;
			}
			if( anyCue )
				k[ "cues" ] = cues;
			if( clip.syncBeats > 0 )
				k[ "syncBeats" ] = clip.syncBeats;
			json animations = json::object();
			for( int p = 0; p < (int)ClipProperty::Count; ++p )
				if( clip.animations[ p ].mode != AnimMode::Off )
					animations[ ClipPropertyName( (ClipProperty)p ) ] = SaveAnimation( clip.animations[ p ] );
			if( !animations.empty() )
				k[ "animations" ] = animations;
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
	c.audioVolume = std::clamp( root.value( "audioVolume", 1.0f ), 0.0f, 1.0f );
	c.speed   = root.value( "speed", c.speed );
	c.bpm     = root.value( "bpm", c.bpm );
	c.quantize = ByName( root.value( "launchQuantize", json() ), LaunchQuantizeName, LaunchQuantize::Now );
	c.crossfader = std::clamp( root.value( "crossfader", 0.5f ), 0.0f, 1.0f );
	c.target     = ByName( root.value( "clipTarget", json() ), ClipTargetName, ClipTarget::OwnLayer );
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
			layer.master  = std::clamp( l.value( "master", 1.0f ), 0.0f, 1.0f );
			layer.opacity = l.value( "opacity", 1.0f );
			layer.volume  = std::clamp( l.value( "volume", 1.0f ), 0.0f, 1.0f );
			layer.speed   = std::clamp( l.value( "speed", 1.0f ), 0.0f, 4.0f );
			std::string group = l.value( "crossfader", "" );
			layer.crossfader  = group == "A" ? 1 : group == "B" ? 2 : 0;
			layer.blend   = ByName( l.value( "blend", json() ), BlendModeName, BlendMode::Alpha );
			layer.bypass  = l.value( "bypass", false );
			if( l.contains( "effects" ) )
				layer.effects = LoadChain( l[ "effects" ] );
			if( l.contains( "opacityAnimation" ) )
				layer.opacityAnimation = LoadAnimation( l[ "opacityAnimation" ] );
			if( l.contains( "transition" ) && l[ "transition" ].is_object() )
			{
				const json& t            = l[ "transition" ];
				layer.transition         = ByName( t.value( "mode", json() ), TransitionModeName, TransitionMode::Fade );
				layer.transitionDuration = std::max( 0.0f, t.value( "duration", 0.0f ) );
				layer.transitionInBeats  = t.value( "beats", false );
			}
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
						clip.playMode  = k.value( "playMode", "" ) == "Play Once" ? PlayMode::Once // the name before Play Once & Clear
						                                                         : ByName( k.value( "playMode", json() ), PlayModeName, PlayMode::Loop );
						clip.opacity   = k.value( "opacity", 1.0f );
						clip.fit       = ByName( k.value( "fit", json() ), FitModeName, FitMode::Fit );
						if( k.contains( "position" ) && k[ "position" ].is_array() && k[ "position" ].size() == 2 )
						{
							clip.positionX = k[ "position" ][ 0 ].get< float >();
							clip.positionY = k[ "position" ][ 1 ].get< float >();
						}
						clip.scale    = k.value( "scale", 1.0f );
						clip.rotation = k.value( "rotation", 0.0f );
						clip.volume   = std::clamp( k.value( "volume", 1.0f ), 0.0f, 1.0f );
						clip.inPoint  = std::max( 0.0, k.value( "in", 0.0 ) );
						clip.outPoint = k.value( "out", -1.0 );
						if( k.contains( "cues" ) && k[ "cues" ].is_array() )
							for( int i = 0; i < kCuePoints && i < (int)k[ "cues" ].size(); ++i )
								clip.cues[ i ] = k[ "cues" ][ i ].is_number() ? k[ "cues" ][ i ].get< double >() : -1.0;
						clip.syncBeats = std::max( 0.0f, k.value( "syncBeats", 0.0f ) );
						if( k.contains( "animations" ) && k[ "animations" ].is_object() )
							for( int p = 0; p < (int)ClipProperty::Count; ++p )
								if( k[ "animations" ].contains( ClipPropertyName( (ClipProperty)p ) ) )
									clip.animations[ p ] = LoadAnimation( k[ "animations" ][ ClipPropertyName( (ClipProperty)p ) ] );
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
