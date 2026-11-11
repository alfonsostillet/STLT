#include "Preferences.h"
#include <algorithm>
#include <json.hpp>

namespace stltvj
{
namespace
{
template< typename Enum >
Enum ByName( const nlohmann::json& value, const char* ( *name )( Enum ), Enum fallback )
{
	if( !value.is_string() )
		return fallback;
	for( int i = 0; i < (int)Enum::Count; ++i )
		if( value.get< std::string >() == name( (Enum)i ) )
			return (Enum)i;
	return fallback;
}
}

std::string Preferences::Save() const
{
	nlohmann::json root;
	root[ "format" ]            = "stlt-vj-preferences";
	root[ "reopenLastSession" ] = reopenLastSession;
	root[ "startView" ]         = startView;
	root[ "layerSpeedIsClipSpeed" ] = layerSpeedIsClipSpeed;
	root[ "interfaceScale" ]    = interfaceScale;
	root[ "audioDevice" ]       = audioDevice;
	root[ "audioBuffer" ]       = audioBuffer;
	root[ "audioInput" ]        = audioInput;
	root[ "audioInputGain" ]    = audioInputGain;
	root[ "linkEnabled" ]       = linkEnabled;
	root[ "midiClockInput" ]    = midiClockInput;
	root[ "syncLatencyMs" ]     = syncLatencyMs;
	root[ "timecodeSource" ]    = timecodeSource;
	root[ "timecodeLtcDevice" ] = timecodeLtcDevice;
	root[ "timecodeLtcChannel" ] = timecodeLtcChannel;
	root[ "timecodeMtcInput" ]  = timecodeMtcInput;
	root[ "timecodeFreewheelMs" ] = timecodeFreewheelMs;
	root[ "hardwareDecoding" ]  = hardwareDecoding;
	root[ "frameCacheMegabytes" ] = frameCacheMegabytes;
	root[ "clipFit" ]           = FitModeName( clipFit );
	root[ "clipPlayMode" ]      = PlayModeName( clipPlayMode );
	root[ "clipBpmSync" ]       = clipBpmSync;
	root[ "clipSyncBeats" ]     = clipSyncBeats;
	root[ "clipTrigger" ]       = TriggerStyleName( clipTrigger );
	root[ "layerBlend" ]        = BlendModeName( layerBlend );
	root[ "layerTransition" ]   = TransitionModeName( layerTransition );
	root[ "layerTransitionTime" ] = layerTransitionTime;
	return root.dump( 1 );
}

bool Preferences::Load( const std::string& text )
{
	nlohmann::json root = nlohmann::json::parse( text, nullptr, false );
	if( root.is_discarded() || !root.is_object() || root.value( "format", "" ) != "stlt-vj-preferences" )
		return false;
	Preferences p;
	p.reopenLastSession = root.value( "reopenLastSession", true );
	p.startView         = std::clamp( root.value( "startView", 0 ), 0, 2 );
	p.layerSpeedIsClipSpeed = root.value( "layerSpeedIsClipSpeed", false );
	p.interfaceScale        = std::clamp( root.value( "interfaceScale", 1.0f ), kMinInterfaceScale, kMaxInterfaceScale );
	p.audioDevice       = root.value( "audioDevice", "" );
	p.audioBuffer       = std::clamp( root.value( "audioBuffer", 256 ), 64, 4096 );
	p.audioInput        = root.value( "audioInput", "" );
	p.audioInputGain    = std::clamp( root.value( "audioInputGain", 1.0f ), 0.0f, 16.0f );
	p.linkEnabled       = root.value( "linkEnabled", false );
	p.midiClockInput    = root.value( "midiClockInput", "" );
	p.syncLatencyMs     = std::clamp( root.value( "syncLatencyMs", 0.0f ), -500.0f, 500.0f );
	p.timecodeSource    = std::clamp( root.value( "timecodeSource", 0 ), 0, 3 );
	p.timecodeLtcDevice = root.value( "timecodeLtcDevice", "" );
	p.timecodeLtcChannel = std::clamp( root.value( "timecodeLtcChannel", 0 ), 0, 63 );
	p.timecodeMtcInput  = root.value( "timecodeMtcInput", "*" );
	p.timecodeFreewheelMs = std::clamp( root.value( "timecodeFreewheelMs", 500.0f ), 0.0f, 10000.0f );
	p.hardwareDecoding  = root.value( "hardwareDecoding", true );
	p.frameCacheMegabytes = std::clamp( root.value( "frameCacheMegabytes", 256 ), 0, 4096 );
	p.clipFit           = ByName( root.value( "clipFit", nlohmann::json() ), FitModeName, FitMode::Fit );
	p.clipPlayMode      = ByName( root.value( "clipPlayMode", nlohmann::json() ), PlayModeName, PlayMode::Loop );
	p.clipBpmSync       = root.value( "clipBpmSync", false );
	p.clipSyncBeats     = std::clamp( root.value( "clipSyncBeats", 16.0f ), 0.25f, 256.0f );
	p.clipTrigger       = ByName( root.value( "clipTrigger", nlohmann::json() ), TriggerStyleName, TriggerStyle::Normal );
	p.layerBlend        = ByName( root.value( "layerBlend", nlohmann::json() ), BlendModeName, BlendMode::Alpha );
	p.layerTransition   = ByName( root.value( "layerTransition", nlohmann::json() ), TransitionModeName, TransitionMode::Fade );
	p.layerTransitionTime = std::clamp( root.value( "layerTransitionTime", 0.0f ), 0.0f, 10.0f );
	*this = p;
	return true;
}

void Preferences::ApplyToClip( Clip& clip ) const
{
	clip.fit      = clipFit;
	clip.playMode = clipPlayMode;
	clip.trigger  = clipTrigger;
	clip.syncBeats = clipBpmSync && !clip.IsGenerator() ? clipSyncBeats : 0;
}
}
