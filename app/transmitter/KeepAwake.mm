// macOS puts programs whose window is hidden into App Nap, which delays their timers and network: the
// transmitter asks not to be (it works for the VJs, not for the window).
#import <Foundation/Foundation.h>

void KeepAwake()
{
	static id activity = [[NSProcessInfo processInfo]
	    beginActivityWithOptions:NSActivityUserInitiatedAllowingIdleSystemSleep | NSActivityLatencyCritical
	                      reason:@"Sending the DJ's decks to the VJs"];
	(void)activity;
}
