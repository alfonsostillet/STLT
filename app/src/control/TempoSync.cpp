#include "control/TempoSync.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>

#ifdef STLTVJ_HAS_LINK
#include <ableton/Link.hpp>
#endif

namespace stltvj
{
namespace
{
constexpr int kTicksPerBeat   = 24;
constexpr size_t kTempoTicks  = 96;   // the tempo from up to four beats of ticks
constexpr double kTickGap     = 0.25; // longer between ticks: the clock stopped (under 10 BPM)
constexpr double kSilence     = 0.5;  // no tick for this long: the clock is gone
constexpr double kQuantum     = 4;    // Link's bar
}

void MidiClock::Reset()
{
	std::lock_guard< std::mutex > lock( mutex );
	ticks.clear();
	lastTick   = -1;
	lastBpm    = 0;
	count      = 0;
	playing    = false;
	started    = false;
	placed     = false;
	pendingSpp = -1;
	++jumps;
}

void MidiClock::Message( const unsigned char* bytes, size_t size, double time )
{
	if( !size )
		return;
	std::lock_guard< std::mutex > lock( mutex );
	switch( bytes[ 0 ] )
	{
	case 0xF8: // tick
	{
		if( lastTick >= 0 && time - lastTick > kTickGap )
			ticks.clear();
		ticks.push_back( time );
		if( ticks.size() > kTempoTicks )
			ticks.erase( ticks.begin() );
		// A tempo change: the last five intervals no longer fit the rest (5 %, well above a millisecond of jitter
		// at club tempos), so the tempo starts again from the last three, which are already the new one.
		if( ticks.size() >= 12 )
		{
			double all  = ( ticks.back() - ticks.front() ) / ( ticks.size() - 1 );
			double last = ( ticks.back() - ticks[ ticks.size() - 6 ] ) / 5;
			if( std::fabs( last - all ) > all * 0.05 )
				ticks.erase( ticks.begin(), ticks.end() - 4 );
		}
		lastTick = time;
		if( playing || !started ) // ticks with no Start seen: a song already running when we joined
			++count;
		break;
	}
	case 0xFA: // Start: from the top, at the next tick
		count      = -1;
		playing    = true;
		started    = true;
		placed     = true;
		pendingSpp = -1;
		++jumps;
		break;
	case 0xFB: // Continue: from where it stopped, or from the Song Position given since
		playing = true;
		started = true;
		if( pendingSpp >= 0 )
		{
			count      = (int64_t)pendingSpp * 6 - 1;
			placed     = true;
			pendingSpp = -1;
			++jumps;
		}
		break;
	case 0xFC: // Stop
		playing = false;
		started = true;
		break;
	case 0xF2: // Song Position: sixteenths from the top
		if( size >= 3 )
		{
			int sixteenths = ( bytes[ 1 ] & 0x7F ) | ( ( bytes[ 2 ] & 0x7F ) << 7 );
			if( playing && started )
			{
				count  = (int64_t)sixteenths * 6 - 1; // the next tick is that position, as after Continue
				placed = true;
				++jumps;
			}
			else
				pendingSpp = sixteenths;
		}
		break;
	default: break;
	}
}

MidiClock::State MidiClock::At( double now ) const
{
	std::lock_guard< std::mutex > lock( mutex );
	State s;
	s.receiving = lastTick >= 0 && now - lastTick < kSilence;
	s.playing   = playing || !started;
	s.placed    = placed;
	s.jumps     = jumps;
	// The tempo: the least-squares slope of tick times, which averages out the jitter of each tick's arrival.
	size_t n = ticks.size();
	if( n >= 6 )
	{
		double meanI = ( n - 1 ) / 2.0, meanT = 0;
		for( double t : ticks )
			meanT += t;
		meanT /= n;
		double num = 0, den = 0;
		for( size_t i = 0; i < n; ++i )
		{
			num += ( i - meanI ) * ( ticks[ i ] - meanT );
			den += ( i - meanI ) * ( i - meanI );
		}
		double perTick = num / den;
		if( perTick > 0 )
			lastBpm = std::clamp( 60.0 / ( perTick * kTicksPerBeat ), 20.0, 500.0 );
	}
	s.bpm = s.receiving ? lastBpm : 0; // with too few ticks since a change, the tempo measured before
	if( count < 0 )
		s.beat = 0; // started, waiting for the first tick
	else
	{
		s.beat = (double)count / kTicksPerBeat;
		// Between ticks, time moves the beat on, never past the next tick.
		if( s.playing && s.bpm > 0 && lastTick >= 0 )
			s.beat += std::clamp( now - lastTick, 0.0, 60.0 / s.bpm / kTicksPerBeat ) * s.bpm / 60.0;
	}
	return s;
}

#ifdef STLTVJ_HAS_LINK
struct TempoSync::Link
{
	ableton::Link session;
	explicit Link( double bpm ) : session( bpm ) {}
};
#else
struct TempoSync::Link
{
};
#endif

TempoSync::TempoSync()  = default;
TempoSync::~TempoSync() = default;

double TempoSync::Now()
{
	return std::chrono::duration< double >( std::chrono::steady_clock::now().time_since_epoch() ).count();
}

bool TempoSync::LinkBuiltIn()
{
#ifdef STLTVJ_HAS_LINK
	return true;
#else
	return false;
#endif
}

void TempoSync::SetClockInput( const std::string& input )
{
	{
		std::lock_guard< std::mutex > lock( inputMutex );
		if( clockInput == input )
			return;
		clockInput = input;
	}
	clock.Reset();
}

std::string TempoSync::ClockInput() const
{
	std::lock_guard< std::mutex > lock( inputMutex );
	return clockInput;
}

void TempoSync::OnMidi( const std::string& device, const unsigned char* bytes, size_t size, double time )
{
	{
		std::lock_guard< std::mutex > lock( inputMutex );
		if( clockInput.empty() || ( clockInput != "*" && clockInput != device ) )
			return;
	}
	clock.Message( bytes, size, time );
}

void TempoSync::EnableLink( bool on, double bpm, double beat )
{
#ifdef STLTVJ_HAS_LINK
	if( on == ( link != nullptr ) )
		return;
	if( !on )
	{
		link->session.enable( false );
		link.reset();
		return;
	}
	// A session of our own at our tempo and beat; when other peers are found, theirs is adopted.
	link = std::make_unique< Link >( std::clamp( bpm, 20.0, 500.0 ) );
	link->session.enable( true );
	auto state = link->session.captureAppSessionState();
	state.requestBeatAtTime( beat, link->session.clock().micros(), kQuantum );
	link->session.commitAppSessionState( state );
	sentBpm = 0;
#else
	(void)on;
	(void)bpm;
	(void)beat;
#endif
}

bool TempoSync::LinkEnabled() const
{
	return link != nullptr;
}

int TempoSync::LinkPeers() const
{
#ifdef STLTVJ_HAS_LINK
	return link ? (int)link->session.numPeers() : 0;
#else
	return 0;
#endif
}

bool TempoSync::Update( double now, double& bpm, double& beat )
{
	double before = beat;
	following     = Source::Internal;
	double at     = now + latency;

	// The MIDI clock, while it ticks.
	if( !ClockInput().empty() )
	{
		MidiClock::State c = clock.At( at );
		lastClock          = c;
		if( c.receiving && c.bpm > 0 )
		{
			following = Source::Clock;
			if( std::fabs( c.bpm - bpm ) > 0.02 ) // steady figures: the measure wanders by hundredths
				bpm = c.bpm;
			if( c.playing )
			{
				if( c.jumps != clockJumps )
					clockBase = c.placed ? 0 : beat - c.beat; // the sender's position, or ours to carry on from
				else if( !clockWasPlaying && !c.placed )
					clockBase = beat - c.beat;
				clockJumps = c.jumps;
				beat       = c.beat + clockBase;
			}
			// Stopped: the beat goes on at the clock's last tempo, so the pictures keep moving.
			clockWasPlaying = c.playing;
		}
		else
			clockWasPlaying = false;
	}
	else
		clockWasPlaying = false;

#ifdef STLTVJ_HAS_LINK
	if( link )
	{
		auto time  = link->session.clock().micros() + std::chrono::microseconds( (int64_t)( latency * 1e6 ) );
		auto state = link->session.captureAppSessionState();
		if( following == Source::Clock )
		{
			// The clock leads; Link's other peers get its tempo.
			if( std::fabs( state.tempo() - bpm ) > 0.005 )
			{
				state.setTempo( bpm, time );
				link->session.commitAppSessionState( state );
			}
		}
		else
		{
			if( sentBpm > 0 && std::fabs( bpm - sentBpm ) > 1e-6 )
			{
				state.setTempo( bpm, time ); // changed here (typed, tapped, mapped): for the whole session
				link->session.commitAppSessionState( state );
			}
			bpm       = state.tempo();
			beat      = state.beatAtTime( time, kQuantum );
			following = Source::Link;
		}
	}
#endif
	sentBpm = bpm;
	return beat < before - 0.5;
}

void TempoSync::Resync( double now, double& beat )
{
#ifdef STLTVJ_HAS_LINK
	if( following == Source::Link && link )
	{
		auto time  = link->session.clock().micros() + std::chrono::microseconds( (int64_t)( latency * 1e6 ) );
		auto state = link->session.captureAppSessionState();
		state.requestBeatAtTime( 0, time, kQuantum );
		link->session.commitAppSessionState( state );
		beat = state.beatAtTime( time, kQuantum );
		return;
	}
#endif
	if( following == Source::Clock )
	{
		MidiClock::State c = clock.At( now + latency );
		clockBase          = -c.beat;
	}
	beat = 0;
}

std::string TempoSync::Status() const
{
	char text[ 160 ];
	std::string input = ClockInput();
	std::string from  = input == "*" ? "any input" : input;
	if( following == Source::Clock )
	{
		std::snprintf( text, sizeof( text ), "MIDI clock from %s: %.1f BPM%s", from.c_str(), lastClock.bpm, lastClock.playing ? "" : ", stopped" );
		return text;
	}
	std::string status;
	if( !input.empty() )
		status = "MIDI clock: waiting for " + from;
	if( link )
	{
		int peers = LinkPeers();
		std::snprintf( text, sizeof( text ), "Link: %d other%s", peers, peers == 1 ? "" : "s" );
		status = status.empty() ? text : std::string( text ) + "; " + status;
	}
	return status;
}
}
