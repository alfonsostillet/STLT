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

const char* TriggerStyleName( TriggerStyle style )
{
	static const char* names[] = { "Normal", "Toggle", "Piano" };
	return names[ std::clamp( (int)style, 0, (int)TriggerStyle::Count - 1 ) ];
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

int64_t Clip::FrameFor( double seconds, double bpm ) const
{
	if( !Loaded() )
		return 0;
	double t = time + 0.5 * seconds * Rate( bpm ) * PlayDirection();
	double start = RangeStart(), end = RangeEnd();
	if( playMode == PlayMode::Loop && end > start )
	{
		// Past the out point (a playhead that rounding left a hair short of it, too) is the start again.
		t = start + std::fmod( t - start, end - start );
		if( t < start )
			t += end - start;
	}
	return (int64_t)player->Source().FrameAt( std::clamp( t, start, std::max( start, end - 1e-6 ) ) );
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
	layer.name               = "Layer " + std::to_string( layers.size() + 1 );
	layer.blend              = newLayerBlend;
	layer.transition         = newLayerTransition;
	layer.transitionDuration = newLayerTransitionTime;
	layer.clips.resize( columns.size() );
	layers.push_back( std::move( layer ) );
	Normalise(); // a row in the other decks too
}

void Composition::RemoveLayer( int index )
{
	if( index < 0 || index >= (int)layers.size() || layers.size() <= 1 )
		return;
	layers.erase( layers.begin() + index );
	for( int d = 0; d < (int)decks.size(); ++d )
		if( d != deck && index < (int)decks[ d ].rows.size() )
			decks[ d ].rows.erase( decks[ d ].rows.begin() + index );
	// Clips playing from the rows above move down with them.
	for( Layer& layer : layers )
	{
		if( layer.activeRow == index )
			layer.active = layer.activeRow = -1;
		else if( layer.activeRow > index )
			--layer.activeRow;
		if( layer.previousRow == index )
			layer.previous = layer.previousRow = -1;
		else if( layer.previousRow > index )
			--layer.previousRow;
	}
	Normalise();
}

void Composition::AddDeck( const std::string& name )
{
	Deck d;
	d.name = name;
	for( int i = 0; i < 9; ++i )
		d.columns.push_back( "Column " + std::to_string( i + 1 ) );
	decks.push_back( std::move( d ) );
	Normalise();
}

void Composition::ShowDeck( int index )
{
	if( index < 0 || index >= (int)decks.size() || index == deck )
		return;
	// The deck on show goes back to its place; the other one comes into the layers. Moving the vectors keeps the
	// clips where they are in memory.
	Deck& shown = decks[ deck ];
	shown.columns = std::move( columns );
	shown.rows.resize( layers.size() );
	for( int l = 0; l < (int)layers.size(); ++l )
		shown.rows[ l ] = std::move( layers[ l ].clips );
	Deck& next = decks[ index ];
	columns    = std::move( next.columns );
	next.columns.clear();
	for( int l = 0; l < (int)layers.size(); ++l )
		layers[ l ].clips = l < (int)next.rows.size() ? std::move( next.rows[ l ] ) : std::vector< Clip >{};
	next.rows.clear();
	deck           = index;
	selectedColumn = -1;
	activeColumn   = -1;
	Normalise();
}

void Composition::RemoveDeck( int index )
{
	if( index < 0 || index >= (int)decks.size() || decks.size() <= 1 )
		return;
	if( index == deck )
		ShowDeck( index == 0 ? 1 : index - 1 );
	decks.erase( decks.begin() + index );
	if( deck > index )
		--deck;
	for( Layer& layer : layers )
	{
		if( layer.active >= 0 && layer.activeDeck == index )
			layer.active = layer.activeRow = -1;
		else if( layer.activeDeck > index )
			--layer.activeDeck;
		if( layer.previous >= 0 && layer.previousDeck == index )
			layer.previous = layer.previousRow = -1;
		else if( layer.previousDeck > index )
			--layer.previousDeck;
	}
	Normalise();
}

Clip* Composition::ClipIn( int d, int row, int column )
{
	if( d == deck )
		return ClipAt( row, column );
	if( d < 0 || d >= (int)decks.size() || row < 0 || row >= (int)decks[ d ].rows.size() || column < 0 || column >= (int)decks[ d ].rows[ row ].size() )
		return nullptr;
	return &decks[ d ].rows[ row ][ column ];
}

void Composition::AddColumn()
{
	columns.push_back( "Column " + std::to_string( columns.size() + 1 ) );
	Normalise();
}

void Composition::Normalise()
{
	if( decks.empty() )
		decks = FirstDeck();
	deck = std::clamp( deck, 0, (int)decks.size() - 1 );
	// Every deck has a row per layer, as wide as its columns.
	for( int d = 0; d < (int)decks.size(); ++d )
	{
		Deck& k = decks[ d ];
		if( d == deck )
		{
			k.columns.clear();
			k.rows.clear();
			continue;
		}
		size_t widest = k.columns.size();
		for( const std::vector< Clip >& row : k.rows )
			widest = std::max( widest, row.size() );
		while( k.columns.size() < widest )
			k.columns.push_back( "Column " + std::to_string( k.columns.size() + 1 ) );
		k.rows.resize( layers.size() );
		for( std::vector< Clip >& row : k.rows )
			row.resize( k.columns.size() );
	}
	auto width = [ & ]( int d ) { return d == deck ? (int)columns.size() : d >= 0 && d < (int)decks.size() ? (int)decks[ d ].columns.size() : 0; };
	for( int l = 0; l < (int)layers.size(); ++l )
	{
		Layer& layer = layers[ l ];
		layer.clips.resize( columns.size() );
		if( layer.active >= 0 && layer.activeRow < 0 )
			layer.activeRow = l;
		if( layer.active >= width( layer.activeDeck ) || layer.activeRow >= (int)layers.size() )
			layer.active = layer.activeRow = -1;
		if( layer.previous >= width( layer.previousDeck ) || layer.previousRow >= (int)layers.size() || layer.previous < 0 )
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
	return ClipIn( layers[ layer ].activeDeck, layers[ layer ].activeRow, layers[ layer ].active );
}

Clip* Composition::OutgoingClip( int layer )
{
	if( layer < 0 || layer >= (int)layers.size() || layers[ layer ].previous < 0 )
		return nullptr;
	return ClipIn( layers[ layer ].previousDeck, layers[ layer ].previousRow, layers[ layer ].previous );
}

int Composition::LayerShowingIn( int d, int row, int column ) const
{
	for( int l = 0; l < (int)layers.size(); ++l )
		if( layers[ l ].Plays( d, row, column ) || layers[ l ].FadesOut( d, row, column ) )
			return l;
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

static nlohmann::json SaveClip( const Clip& clip )
{
	using nlohmann::json;
	if( clip.Empty() )
		return nullptr;
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
	if( clip.trigger != TriggerStyle::Normal )
		k[ "trigger" ] = TriggerStyleName( clip.trigger );
	if( clip.ignoreColumn )
		k[ "ignoreColumn" ] = true;
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
	return k;
}

static nlohmann::json SaveTimeline( const Timeline& t );

std::string SaveComposition( const Composition& c, bool withShownDeck )
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
		if( withShownDeck && layer.active >= 0 )
			l[ "playing" ] = { { "deck", layer.activeDeck }, { "row", layer.activeRow }, { "column", layer.active } };
		layers.push_back( l );
	}
	root[ "layers" ] = layers;
	// The clips, deck by deck: its columns, then a row of clips per layer.
	json decks = json::array();
	for( int d = 0; d < (int)c.decks.size(); ++d )
	{
		bool shown = d == c.deck;
		json k;
		k[ "name" ]    = c.decks[ d ].name;
		k[ "columns" ] = shown ? c.columns : c.decks[ d ].columns;
		json rows      = json::array();
		for( int r = 0; r < (int)c.layers.size(); ++r )
		{
			json row = json::array();
			if( shown )
				for( const Clip& clip : c.layers[ r ].clips )
					row.push_back( SaveClip( clip ) );
			else if( r < (int)c.decks[ d ].rows.size() )
				for( const Clip& clip : c.decks[ d ].rows[ r ] )
					row.push_back( SaveClip( clip ) );
			rows.push_back( row );
		}
		k[ "rows" ] = rows;
		decks.push_back( k );
	}
	root[ "decks" ] = decks;
	root[ "timeline" ] = SaveTimeline( c.timeline );
	if( withShownDeck )
		root[ "deck" ] = c.deck;
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

static Clip LoadClip( const nlohmann::json& k )
{
	using nlohmann::json;
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
		clip.trigger      = ByName( k.value( "trigger", json() ), TriggerStyleName, TriggerStyle::Normal );
		clip.ignoreColumn = k.value( "ignoreColumn", false );
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
	return clip;
}

Clip CopyClip( const Clip& clip )
{
	return LoadClip( SaveClip( clip ) );
}

// The timeline: tempo, tracks with their clips (placed in time), and blocks.
static nlohmann::json SaveTimeline( const Timeline& t )
{
	using nlohmann::json;
	json root;
	root[ "bpm" ]         = t.bpm;
	root[ "beatsPerBar" ] = t.beatsPerBar;
	root[ "fps" ]         = t.fps;
	root[ "position" ]    = t.position; // the playhead, where it was (not playing)
	json tracks           = json::array();
	for( const TimelineTrack& track : t.tracks )
	{
		json k;
		k[ "name" ]    = track.name;
		k[ "audio" ]   = track.audio;
		k[ "opacity" ] = track.opacity;
		k[ "volume" ]  = track.volume;
		k[ "blend" ]   = BlendModeName( track.blend );
		k[ "mute" ]    = track.mute;
		k[ "solo" ]    = track.solo;
		k[ "effects" ] = SaveChain( track.effects );
		json clips     = json::array();
		for( const TimelineClip& clip : track.clips )
		{
			json c = { { "start", clip.start }, { "length", clip.length }, { "blend", BlendModeName( clip.blend ) }, { "clip", SaveClip( clip.clip ) } };
			json keys = json::object();
			for( int p = 0; p < (int)KeyProperty::Count; ++p )
				if( !clip.keys[ p ].empty() )
				{
					json list = json::array();
					for( const Keyframe& k : clip.keys[ p ] )
					{
						json key = { k.time, k.value };
						if( k.curve != KeyCurve::Linear )
							key.push_back( KeyCurveName( k.curve ) );
						list.push_back( key );
					}
					keys[ KeyPropertyName( (KeyProperty)p ) ] = list;
				}
			if( !keys.empty() )
				c[ "keys" ] = keys;
			json params = json::array();
			for( const ParamKeys& p : clip.paramKeys )
			{
				json list = json::array();
				for( const Keyframe& k : p.keys )
				{
					json key = { k.time, k.value };
					if( k.curve != KeyCurve::Linear )
						key.push_back( KeyCurveName( k.curve ) );
					list.push_back( key );
				}
				if( !list.empty() )
					params.push_back( { { "slot", p.slot }, { "param", p.param }, { "keys", list } } );
			}
			if( !params.empty() )
				c[ "paramKeys" ] = params;
			clips.push_back( c );
		}
		k[ "clips" ] = clips;
		tracks.push_back( k );
	}
	root[ "tracks" ] = tracks;
	json blocks      = json::array();
	for( const TimelineBlock& block : t.blocks )
		blocks.push_back( { { "name", block.name }, { "start", block.start }, { "end", block.end }, { "endAction", BlockEndName( block.endAction ) }, { "colour", block.colour } } );
	root[ "blocks" ] = blocks;
	return root;
}

static Timeline LoadTimeline( const nlohmann::json& root )
{
	using nlohmann::json;
	Timeline t;
	if( !root.is_object() )
		return t;
	t.bpm         = std::clamp( root.value( "bpm", 120.0 ), 20.0, 500.0 );
	t.beatsPerBar = std::clamp( root.value( "beatsPerBar", 4 ), 1, 32 );
	t.fps         = std::clamp( root.value( "fps", 25.0 ), 1.0, 120.0 );
	t.position    = std::max( 0.0, root.value( "position", 0.0 ) );
	if( root.contains( "tracks" ) && root[ "tracks" ].is_array() )
		for( const json& k : root[ "tracks" ] )
		{
			if( !k.is_object() )
				continue;
			TimelineTrack track;
			track.name    = k.value( "name", "Track " + std::to_string( t.tracks.size() + 1 ) );
			track.audio   = k.value( "audio", false );
			track.opacity = std::clamp( k.value( "opacity", 1.0f ), 0.0f, 1.0f );
			track.volume  = std::clamp( k.value( "volume", 1.0f ), 0.0f, 1.0f );
			track.blend   = ByName( k.value( "blend", json() ), BlendModeName, BlendMode::Alpha );
			track.mute    = k.value( "mute", false );
			track.solo    = k.value( "solo", false );
			if( k.contains( "effects" ) )
				track.effects = LoadChain( k[ "effects" ] );
			if( k.contains( "clips" ) && k[ "clips" ].is_array() )
				for( const json& c : k[ "clips" ] )
				{
					if( !c.is_object() || !c.contains( "clip" ) )
						continue;
					TimelineClip clip;
					clip.clip   = LoadClip( c[ "clip" ] );
					clip.start  = std::max( 0.0, c.value( "start", 0.0 ) );
					clip.length = std::max( 0.01, c.value( "length", 1.0 ) );
					clip.blend  = ByName( c.value( "blend", json() ), BlendModeName, BlendMode::Alpha );
					if( c.contains( "paramKeys" ) && c[ "paramKeys" ].is_array() )
						for( const json& p : c[ "paramKeys" ] )
						{
							if( !p.is_object() || !p.contains( "keys" ) || !p[ "keys" ].is_array() )
								continue;
							ParamKeys pk;
							pk.slot  = p.value( "slot", 0 );
							pk.param = p.value( "param", "" );
							for( const json& k : p[ "keys" ] )
								if( k.is_array() && k.size() >= 2 && k[ 0 ].is_number() && k[ 1 ].is_number() )
									pk.keys.push_back( { k[ 0 ].get< double >(), k[ 1 ].get< float >(), k.size() > 2 ? ByName( k[ 2 ], KeyCurveName, KeyCurve::Linear ) : KeyCurve::Linear } );
							std::stable_sort( pk.keys.begin(), pk.keys.end(), []( const Keyframe& a, const Keyframe& b ) { return a.time < b.time; } );
							if( !pk.param.empty() && !pk.keys.empty() )
								clip.paramKeys.push_back( std::move( pk ) );
						}
					if( c.contains( "keys" ) && c[ "keys" ].is_object() )
						for( int p = 0; p < (int)KeyProperty::Count; ++p )
						{
							const char* name = KeyPropertyName( (KeyProperty)p );
							if( !c[ "keys" ].contains( name ) || !c[ "keys" ][ name ].is_array() )
								continue;
							for( const json& k : c[ "keys" ][ name ] )
								if( k.is_array() && k.size() >= 2 && k[ 0 ].is_number() && k[ 1 ].is_number() )
									clip.keys[ p ].push_back( { k[ 0 ].get< double >(), k[ 1 ].get< float >(),
									                            k.size() > 2 ? ByName( k[ 2 ], KeyCurveName, KeyCurve::Linear ) : KeyCurve::Linear } );
							std::stable_sort( clip.keys[ p ].begin(), clip.keys[ p ].end(), []( const Keyframe& a, const Keyframe& b ) { return a.time < b.time; } );
						}
					if( !clip.clip.Empty() )
						track.clips.push_back( std::move( clip ) );
				}
			std::stable_sort( track.clips.begin(), track.clips.end(), []( const TimelineClip& a, const TimelineClip& b ) { return a.start < b.start; } );
			t.tracks.push_back( std::move( track ) );
		}
	if( root.contains( "blocks" ) && root[ "blocks" ].is_array() )
		for( const json& k : root[ "blocks" ] )
		{
			if( !k.is_object() )
				continue;
			TimelineBlock block;
			block.name      = k.value( "name", "" );
			block.start     = std::max( 0.0, k.value( "start", 0.0 ) );
			block.end       = std::max( block.start + 0.01, k.value( "end", block.start + 8 ) );
			block.endAction = ByName( k.value( "endAction", json() ), BlockEndName, BlockEnd::Next );
			block.colour    = k.value( "colour", 0 );
			t.blocks.push_back( block );
		}
	t.SortBlocks();
	return t;
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
			if( l.contains( "playing" ) && l[ "playing" ].is_object() )
			{
				const json& p    = l[ "playing" ];
				layer.activeDeck = p.value( "deck", 0 );
				layer.activeRow  = p.value( "row", -1 );
				layer.active     = p.value( "column", -1 );
			}
			if( l.contains( "clips" ) && l[ "clips" ].is_array() ) // before decks: the clips were the layer's
				for( const json& k : l[ "clips" ] )
					layer.clips.push_back( LoadClip( k ) );
			c.layers.push_back( std::move( layer ) );
		}
	if( c.layers.empty() )
		c.AddLayer();
	if( root.contains( "decks" ) && root[ "decks" ].is_array() && !root[ "decks" ].empty() )
	{
		// Every deck is stored apart; the one shown then moves into the layers.
		c.decks.clear();
		for( const json& k : root[ "decks" ] )
		{
			Deck d;
			d.name = k.value( "name", "Deck " + std::to_string( c.decks.size() + 1 ) );
			if( k.contains( "columns" ) && k[ "columns" ].is_array() )
				for( const json& name : k[ "columns" ] )
					d.columns.push_back( name.is_string() ? name.get< std::string >() : "Column" );
			if( d.columns.empty() )
				for( int i = 0; i < 9; ++i )
					d.columns.push_back( "Column " + std::to_string( i + 1 ) );
			if( k.contains( "rows" ) && k[ "rows" ].is_array() )
				for( const json& row : k[ "rows" ] )
				{
					std::vector< Clip > clips;
					if( row.is_array() )
						for( const json& clip : row )
							clips.push_back( LoadClip( clip ) );
					d.rows.push_back( std::move( clips ) );
				}
			c.decks.push_back( std::move( d ) );
		}
		int shown = std::clamp( root.value( "deck", 0 ), 0, (int)c.decks.size() - 1 );
		c.deck    = shown;
		Deck& d   = c.decks[ shown ];
		c.columns = std::move( d.columns );
		for( int l = 0; l < (int)c.layers.size(); ++l )
			c.layers[ l ].clips = l < (int)d.rows.size() ? std::move( d.rows[ l ] ) : std::vector< Clip >{};
		d.columns.clear();
		d.rows.clear();
	}
	if( c.columns.empty() )
		for( int i = 0; i < 9; ++i )
			c.columns.push_back( "Column " + std::to_string( i + 1 ) );
	if( root.contains( "timeline" ) )
		c.timeline = LoadTimeline( root[ "timeline" ] );
	// What played: only a clip that is there (a layer, deck, row and column that exist, a clip in the cell).
	for( Layer& layer : c.layers )
	{
		if( layer.active < 0 )
			continue;
		const Clip* playing = nullptr;
		if( layer.activeDeck >= 0 && layer.activeDeck < (int)c.decks.size() && layer.activeRow >= 0 && layer.activeRow < (int)c.layers.size() )
		{
			const std::vector< Clip >* row = nullptr;
			if( layer.activeDeck == c.deck )
				row = &c.layers[ layer.activeRow ].clips;
			else if( layer.activeRow < (int)c.decks[ layer.activeDeck ].rows.size() )
				row = &c.decks[ layer.activeDeck ].rows[ layer.activeRow ];
			// Players and generators are made by the caller: a movie or a generator in the cell is enough here.
			if( row && layer.active < (int)row->size() && ( !( *row )[ layer.active ].path.empty() || !( *row )[ layer.active ].generatorId.empty() ) )
				playing = &( *row )[ layer.active ];
		}
		if( !playing )
		{
			layer.active = layer.activeRow = -1;
			layer.activeDeck = 0;
		}
	}
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
