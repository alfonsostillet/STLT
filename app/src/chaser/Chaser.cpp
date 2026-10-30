#include "chaser/Chaser.h"
#include <algorithm>
#include <cmath>
#include <json.hpp>
#include <random>

namespace stltvj
{
const char* ChaserDriverName( ChaserDriver driver )
{
	static const char* names[] = { "Chaser", "Random" };
	return names[ std::clamp( (int)driver, 0, (int)ChaserDriver::Count - 1 ) ];
}

const char* FactoryOrderName( FactoryOrder order )
{
	static const char* names[] = { "Left to Right", "Right to Left", "Top to Bottom", "Bottom to Top", "Centre Out", "Outside In", "Random" };
	return names[ std::clamp( (int)order, 0, (int)FactoryOrder::Count - 1 ) ];
}

bool ChaserSequence::Lit( int step, const std::string& slice ) const
{
	if( step < 0 || step >= (int)steps.size() )
		return false;
	return std::find( steps[ step ].begin(), steps[ step ].end(), slice ) != steps[ step ].end();
}

void ChaserSequence::Set( int step, const std::string& slice, bool on )
{
	if( step < 0 || step >= (int)steps.size() )
		return;
	std::vector< std::string >& lit = steps[ step ];
	auto at                         = std::find( lit.begin(), lit.end(), slice );
	if( on && at == lit.end() )
		lit.push_back( slice );
	else if( !on && at != lit.end() )
		lit.erase( at );
}

void ChaserSequence::Toggle( int step, const std::string& slice )
{
	Set( step, slice, !Lit( step, slice ) );
}

void ChaserSequence::Resize( int count )
{
	steps.resize( (size_t)std::clamp( count, 1, 1024 ) );
}

void ChaserSequence::Repeat( bool pingPong )
{
	std::vector< std::vector< std::string > > used;
	for( const std::vector< std::string >& step : steps )
		if( !step.empty() )
			used.push_back( step );
	if( used.empty() )
		return;
	std::vector< std::vector< std::string > > pattern = used;
	if( pingPong && used.size() > 2 )
		for( size_t i = used.size() - 2; i >= 1; --i )
			pattern.push_back( used[ i ] );
	for( size_t i = 0; i < steps.size(); ++i )
		steps[ i ] = pattern[ i % pattern.size() ];
}

void ChaserSequence::Clear()
{
	for( std::vector< std::string >& step : steps )
		step.clear();
}

std::vector< SlicePlace > SlicePlaces( const AdvancedOutputSetup& setup )
{
	std::vector< SlicePlace > places;
	for( const Screen& screen : setup.screens )
		for( const Slice& slice : screen.slices )
		{
			if( slice.mask || slice.input.empty() )
				continue;
			SlicePlace p;
			p.id = slice.id;
			for( Vec2 v : slice.input )
			{
				p.x += v.x;
				p.y += v.y;
			}
			p.x /= (float)slice.input.size();
			p.y /= (float)slice.input.size();
			places.push_back( p );
		}
	return places;
}

std::vector< std::vector< std::string > > FactorySteps( const std::vector< SlicePlace >& places, float width, float height, const FactorySettings& f )
{
	std::vector< std::vector< std::string > > steps;
	if( places.empty() )
		return steps;
	float w = std::max( width, 1.0f ), h = std::max( height, 1.0f );
	float cx = w / 2, cy = h / 2, diagonal = std::hypot( w, h );
	// Each slice's key: how far along the order it comes (0..1).
	std::vector< std::pair< float, std::string > > keyed;
	std::mt19937 random( f.seed );
	for( const SlicePlace& p : places )
	{
		float x = p.x / w, y = p.y / h, key = 0;
		if( f.mirror && ( f.order == FactoryOrder::LeftToRight || f.order == FactoryOrder::RightToLeft ) )
			x = std::fabs( x - 0.5f ) * 2; // from the sides in (or the middle out, reversed)
		if( f.mirror && ( f.order == FactoryOrder::TopToBottom || f.order == FactoryOrder::BottomToTop ) )
			y = std::fabs( y - 0.5f ) * 2;
		switch( f.order )
		{
		case FactoryOrder::LeftToRight: key = f.mirror ? 1 - x : x; break;
		case FactoryOrder::RightToLeft: key = f.mirror ? x : 1 - x; break;
		case FactoryOrder::TopToBottom: key = f.mirror ? 1 - y : y; break;
		case FactoryOrder::BottomToTop: key = f.mirror ? y : 1 - y; break;
		case FactoryOrder::CentreOut: key = std::hypot( p.x - cx, p.y - cy ) / ( diagonal / 2 ); break;
		case FactoryOrder::OutsideIn: key = 1 - std::hypot( p.x - cx, p.y - cy ) / ( diagonal / 2 ); break;
		default: key = std::uniform_real_distribution< float >( 0, 1 )( random ); break;
		}
		keyed.push_back( { key, p.id } );
	}
	std::stable_sort( keyed.begin(), keyed.end(), []( const auto& a, const auto& b ) { return a.first < b.first; } );
	// Slices whose keys are within the tolerance light together (a column, a ring...).
	std::vector< std::vector< std::string > > groups;
	float tolerance = f.order == FactoryOrder::Random ? -1 : std::max( 0.0f, f.tolerance );
	float groupKey  = 0;
	for( const auto& [ key, id ] : keyed )
	{
		if( groups.empty() || key - groupKey > tolerance )
		{
			groups.push_back( {} );
			groupKey = key;
		}
		groups.back().push_back( id );
	}
	// Blocks of several groups a step.
	int block = std::max( 1, f.block );
	for( size_t g = 0; g < groups.size(); g += (size_t)block )
	{
		std::vector< std::string > step;
		for( size_t k = g; k < std::min( groups.size(), g + (size_t)block ); ++k )
			step.insert( step.end(), groups[ k ].begin(), groups[ k ].end() );
		steps.push_back( step );
	}
	if( f.fill )
		for( size_t i = 1; i < steps.size(); ++i )
			steps[ i ].insert( steps[ i ].begin(), steps[ i - 1 ].begin(), steps[ i - 1 ].end() );
	if( f.pingPong && steps.size() > 2 )
		for( size_t i = steps.size() - 2; i >= 1; --i )
			steps.push_back( steps[ i ] );
	return steps;
}

ChaserSequence* ChaserSetup::Active()
{
	if( sequences.empty() )
		return nullptr;
	active = std::clamp( active, 0, (int)sequences.size() - 1 );
	return &sequences[ active ];
}

const ChaserSequence* ChaserSetup::Active() const
{
	return const_cast< ChaserSetup* >( this )->Active();
}

int ChaserSetup::Length() const
{
	const ChaserSequence* s = Active();
	return s ? std::max( 1, (int)s->steps.size() ) : 1;
}

void ChaserSetup::Follow( double beat )
{
	if( playing )
		position = std::max( 0.0, beat * std::max( stepsPerBeat, 0.01f ) );
}

std::map< std::string, float > ChaserSetup::Gains( const std::vector< std::string >& ids ) const
{
	std::map< std::string, float > gains;
	float floor = std::clamp( dark, 0.0f, 1.0f );
	for( const std::string& id : ids )
		gains[ id ] = floor;
	const ChaserSequence* s = Active();
	int length              = Length();
	double p                = position;
	long long current       = (long long)std::floor( p );
	// A slice lit in a step: full for `sustain` steps from that step's start, then fading over `release`;
	// echoes show the steps before, each dimmer.
	float hold = std::max( sustain, 0.01f ), fade = std::max( release, 0.0f );
	int back   = (int)std::ceil( hold + fade ) + std::max( echoes, 0 ) + 1;
	auto litAt = [ & ]( long long step ) -> std::vector< std::string > {
		int index = (int)( ( step % length + length ) % length );
		if( driver == ChaserDriver::Random )
		{
			// The same slices for the same step every time it comes round.
			std::vector< std::string > pool = ids;
			std::mt19937 random( (unsigned)( step * 2654435761u ) );
			std::shuffle( pool.begin(), pool.end(), random );
			pool.resize( std::min( pool.size(), (size_t)std::max( randomCount, 0 ) ) );
			return pool;
		}
		return s && index < (int)s->steps.size() ? s->steps[ index ] : std::vector< std::string >{};
	};
	for( int k = 0; k <= back; ++k )
	{
		long long step = current - k;
		if( step < 0 && driver == ChaserDriver::Chaser && p < length )
			break; // nothing before the first time round
		double age = p - (double)step; // steps since that step began
		float level = 0;
		if( age < hold )
			level = 1;
		else if( fade > 0 && age < hold + fade )
			level = 1 - (float)( ( age - hold ) / fade );
		// Echoes: the steps just before the current one stay on, dimmer and dimmer.
		if( k >= 1 && k <= echoes )
			level = std::max( level, 1.0f - (float)k / ( echoes + 1 ) );
		if( level <= 0 )
			continue;
		for( const std::string& id : litAt( step ) )
		{
			auto at = gains.find( id );
			if( at != gains.end() )
				at->second = std::max( at->second, floor + ( 1 - floor ) * level );
		}
	}
	return gains;
}

std::string ChaserSetup::Save() const
{
	nlohmann::json root;
	root[ "format" ]       = "stlt-vj-chaser";
	root[ "active" ]       = active;
	root[ "enabled" ]      = enabled;
	root[ "driver" ]       = ChaserDriverName( driver );
	root[ "stepsPerBeat" ] = stepsPerBeat;
	root[ "sustain" ]      = sustain;
	root[ "release" ]      = release;
	root[ "echoes" ]       = echoes;
	root[ "randomCount" ]  = randomCount;
	root[ "dark" ]         = dark;
	nlohmann::json list    = nlohmann::json::array();
	for( const ChaserSequence& s : sequences )
		list.push_back( { { "name", s.name }, { "steps", s.steps } } );
	root[ "sequences" ] = list;
	return root.dump( 1 );
}

bool ChaserSetup::Load( const std::string& text )
{
	nlohmann::json root = nlohmann::json::parse( text, nullptr, false );
	if( root.is_discarded() || !root.is_object() || root.value( "format", "" ) != "stlt-vj-chaser" )
		return false;
	ChaserSetup c;
	c.active  = root.value( "active", 0 );
	c.enabled = root.value( "enabled", false );
	std::string driverName = root.value( "driver", "Chaser" );
	c.driver               = driverName == "Random" ? ChaserDriver::Random : ChaserDriver::Chaser;
	c.stepsPerBeat         = std::clamp( root.value( "stepsPerBeat", 1.0f ), 0.0625f, 16.0f );
	c.sustain              = std::clamp( root.value( "sustain", 1.0f ), 0.01f, 64.0f );
	c.release              = std::clamp( root.value( "release", 0.0f ), 0.0f, 64.0f );
	c.echoes               = std::clamp( root.value( "echoes", 0 ), 0, 16 );
	c.randomCount          = std::clamp( root.value( "randomCount", 3 ), 0, 1024 );
	c.dark                 = std::clamp( root.value( "dark", 0.0f ), 0.0f, 1.0f );
	if( root.contains( "sequences" ) && root[ "sequences" ].is_array() )
		for( const nlohmann::json& k : root[ "sequences" ] )
		{
			if( !k.is_object() )
				continue;
			ChaserSequence s;
			s.name = k.value( "name", "Sequence " + std::to_string( c.sequences.size() + 1 ) );
			if( k.contains( "steps" ) && k[ "steps" ].is_array() )
				for( const nlohmann::json& step : k[ "steps" ] )
				{
					std::vector< std::string > lit;
					if( step.is_array() )
						for( const nlohmann::json& id : step )
							if( id.is_string() )
								lit.push_back( id.get< std::string >() );
					s.steps.push_back( lit );
				}
			if( s.steps.empty() )
				s.steps.resize( 16 );
			s.Resize( (int)s.steps.size() );
			c.sequences.push_back( std::move( s ) );
		}
	c.active = std::clamp( c.active, 0, std::max( 0, (int)c.sequences.size() - 1 ) );
	*this    = std::move( c );
	return true;
}
}
