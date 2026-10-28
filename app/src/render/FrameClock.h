#pragma once
#include <algorithm>
#include <cmath>

namespace stltvj
{
// How far the composition moves each frame. With vsync, frames come every display period give or take the
// system timer's jitter (and the scheduler's). Moving clips by the measured time would turn that jitter into
// uneven frames: a 30 fps clip on a 60 Hz display would show some frames once and others three times. So the
// composition moves by whole display periods (missed vsyncs count as more periods); the period itself is learnt
// from the measurements (59.94 Hz, 144 Hz...), and what the clock gains or loses on the steps is paid back at once
// when it reaches half a period, so time never drifts. Without vsync, or after a stall, it goes by the clock.
class FrameClock
{
public:
	// `period`: the display's nominal refresh period, the starting guess.
	explicit FrameClock( double period = 1.0 / 60 ) : period( period ) {}

	double Step( double measured )
	{
		double periods = std::round( measured / period );
		locked         = periods >= 1 && periods <= 4 && std::fabs( measured - periods * period ) < period * 0.25;
		if( !locked )
		{
			error = 0;
			// The display may simply run at another rate than assumed: take it when frames keep coming steadily.
			if( measured > 0.003 && measured < 0.05 )
				period += ( measured - period ) * 0.05;
			return measured;
		}
		if( periods == 1 )
			period += ( measured - period ) * 0.01;
		double step = periods * period;
		error += measured - step;
		if( std::fabs( error ) > period * 0.5 )
		{
			step += error;
			error = 0;
		}
		return step;
	}

	double Period() const { return period; }
	bool Locked() const { return locked; }

private:
	double period;
	double error = 0;
	bool locked  = false;
};
}
