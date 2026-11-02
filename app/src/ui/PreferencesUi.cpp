#include "ui/PreferencesUi.h"
#include <imgui.h>
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include "App.h"
#include "audio/AudioEngine.h"
#include "audio/AudioInput.h"
#include "gl/Gl.h"
#include "media/FrameSource.h"
#include "ui/ControlUi.h"
#include "ui/EffectsUi.h"
#include "ui/Theme.h"

namespace stltvj
{
namespace
{
float Ui( float pixels )
{
	return pixels * ImGui::GetFontSize() / 15.0f;
}

enum class Section
{
	General,
	Audio,
	Video,
	Midi,
	Osc,
	Sync,
	Defaults,
	Count
};

const char* SectionName( Section s )
{
	static const char* names[] = { "General", "Audio", "Video", "MIDI", "OSC", "Sync", "Defaults" };
	return names[ (int)s ];
}

// The names Resolume gives the sizes, in its order.
const char* FitLabel( FitMode fit )
{
	switch( fit )
	{
	case FitMode::Fill: return "Fill";
	case FitMode::Fit: return "Fit";
	case FitMode::Stretch: return "Stretch";
	default: return "Original";
	}
}
const FitMode kFitOrder[] = { FitMode::Fill, FitMode::Fit, FitMode::Stretch, FitMode::Native };

// A setting's caption over its control, as in Resolume's preferences.
void Caption( const char* text )
{
	ImGui::Spacing();
	ImGui::TextUnformatted( text );
}

void Help( const char* text )
{
	ImGui::PushTextWrapPos( 0 );
	ImGui::TextDisabled( "%s", text );
	ImGui::PopTextWrapPos();
}

template< typename Enum >
bool Combo( const char* id, Enum* value, const char* ( *name )( Enum ) )
{
	bool changed = false;
	if( ImGui::BeginCombo( id, name( *value ) ) )
	{
		for( int i = 0; i < (int)Enum::Count; ++i )
			if( ImGui::Selectable( name( (Enum)i ), (int)*value == i ) )
			{
				*value  = (Enum)i;
				changed = true;
			}
		ImGui::EndCombo();
	}
	return changed;
}

bool General( App& app )
{
	Preferences& p = app.prefs;
	bool changed   = false;
	changed |= ImGui::Checkbox( "Open the last session at start", &p.reopenLastSession );
	Help( "The composition as it was when the app closed (also when it was never saved)." );
	Caption( "Start in" );
	ImGui::SetNextItemWidth( Ui( 260 ) );
	const char* views[] = { "The view in use when it closed", "Session (the deck)", "Arrangement (the timeline)" };
	if( ImGui::BeginCombo( "##startView", views[ p.startView ] ) )
	{
		for( int v = 0; v < 3; ++v )
			if( ImGui::Selectable( views[ v ], p.startView == v ) )
			{
				p.startView = v;
				changed     = true;
			}
		ImGui::EndCombo();
	}
	Caption( "Settings folder" );
	ImGui::TextDisabled( "%s", app.configDir.c_str() );
	Help( "Preferences, shortcuts and their presets, workspaces, output presets and the last session are kept here." );
	return changed;
}

bool Audio( App& app )
{
	Preferences& p = app.prefs;
	bool restart   = false;
	static std::vector< std::string > devices;
	static bool listed = false;
	if( !listed )
	{
		devices = AudioEngine::OutputDevices();
		listed  = true;
	}
	ImGui::SetCursorPosX( ImGui::GetContentRegionMax().x - Ui( 90 ) );
	if( ImGui::Button( "Refresh", ImVec2( Ui( 90 ), 0 ) ) )
		devices = AudioEngine::OutputDevices();
	Caption( "Audio Output Device" );
	ImGui::SetNextItemWidth( Ui( 360 ) );
	std::string shown = p.audioDevice.empty() ? "System default" : p.audioDevice;
	if( ImGui::BeginCombo( "##device", shown.c_str() ) )
	{
		if( ImGui::Selectable( "System default", p.audioDevice.empty() ) )
		{
			p.audioDevice.clear();
			restart = true;
		}
		for( const std::string& name : devices )
			if( ImGui::Selectable( name.c_str(), p.audioDevice == name ) )
			{
				p.audioDevice = name;
				restart       = true;
			}
		if( devices.empty() )
			ImGui::TextDisabled( "No outputs found" );
		ImGui::EndCombo();
	}
	Caption( "Buffer Size" );
	ImGui::SetNextItemWidth( Ui( 200 ) );
	int rate = app.audio.SampleRate() > 0 ? app.audio.SampleRate() : 48000;
	char label[ 64 ];
	std::snprintf( label, sizeof( label ), "%d (%.1f ms)", p.audioBuffer, 1000.0 * p.audioBuffer / rate );
	if( ImGui::BeginCombo( "##buffer", label ) )
	{
		for( int frames : { 64, 128, 256, 512, 1024, 2048 } )
		{
			std::snprintf( label, sizeof( label ), "%d (%.1f ms)", frames, 1000.0 * frames / rate );
			if( ImGui::Selectable( label, p.audioBuffer == frames ) )
			{
				p.audioBuffer = frames;
				restart       = true;
			}
		}
		ImGui::EndCombo();
	}
	Help( "Smaller: the sound keeps closer to the picture; larger: safer on a busy computer (crackles mean it is too small)." );
	Caption( "In use" );
	ImGui::TextDisabled( "%s", app.audio.Status().c_str() );
	Caption( "Master Volume" );
	ImGui::SetNextItemWidth( Ui( 260 ) );
	float percent = app.composition.audioVolume * 100;
	if( ImGui::SliderFloat( "##master", &percent, 0, 100, "%.0f %%" ) )
		app.composition.audioVolume = percent / 100;

	// The sound coming in, for parameters that follow it (Animate → Audio Level, Bass, Mid, Treble).
	static std::vector< std::string > inputs;
	static bool inputsListed = false;
	if( !inputsListed )
	{
		inputs       = AudioInput::Devices();
		inputsListed = true;
	}
	bool inputChanged = false, deviceChanged = false;
	ImGui::Spacing();
	ImGui::SeparatorText( "Audio Input" );
	ImGui::SetNextItemWidth( Ui( 360 ) );
	shown = p.audioInput.empty() ? "System default" : p.audioInput;
	if( ImGui::BeginCombo( "##input", shown.c_str() ) )
	{
		if( ImGui::Selectable( "System default", p.audioInput.empty() ) && !p.audioInput.empty() )
		{
			p.audioInput.clear();
			inputChanged = deviceChanged = true;
		}
		for( const std::string& name : inputs )
			if( ImGui::Selectable( name.c_str(), p.audioInput == name ) && p.audioInput != name )
			{
				p.audioInput = name;
				inputChanged = deviceChanged = true;
			}
		if( inputs.empty() )
			ImGui::TextDisabled( "No inputs found" );
		ImGui::EndCombo();
	}
	ImGui::SameLine();
	if( ImGui::Button( "Refresh##inputs" ) )
		inputs = AudioInput::Devices();
	Help( "A microphone, a line in or the mixer's feed. It is opened only while a parameter follows the sound "
	      "(Animate → Audio Level, Bass, Mid or Treble) or while this page is open." );
	Caption( "Input Gain" );
	ImGui::SetNextItemWidth( Ui( 260 ) );
	float db = 20 * std::log10( std::max( p.audioInputGain, 0.001f ) );
	if( ImGui::SliderFloat( "##inputGain", &db, -20, 24, "%+.1f dB" ) )
	{
		p.audioInputGain = std::pow( 10.0f, db / 20 );
		inputChanged     = true; // only saved: the gain applies to the running input at once
	}
	if( ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked( ImGuiMouseButton_Left ) )
	{
		p.audioInputGain = 1;
		inputChanged     = true;
	}
	Help( "Raise it when the meters stay low, lower it when they stay full. Double-click: 0 dB." );
	app.audioInputViewed = 30; // keeps the input open while the meters are shown
	if( app.audioInput.Running() )
	{
		for( int b = 0; b < AudioInput::BandCount; ++b )
		{
			ImGui::ProgressBar( app.audioInput.Value( (AudioInput::Band)b ), ImVec2( Ui( 260 ), Ui( 14 ) ), "" );
			ImGui::SameLine();
			ImGui::TextUnformatted( AudioInput::BandName( (AudioInput::Band)b ) );
		}
		ImGui::TextDisabled( "%s", app.audioInput.Status().c_str() );
	}
	else
		ImGui::TextDisabled( "%s", app.audioInput.Status().empty() ? "Not listening" : app.audioInput.Status().c_str() );
	if( deviceChanged )
		app.RestartAudioInput();
	if( restart )
		app.ApplyPreferences( true );
	return restart || inputChanged;
}

// Where the tempo and the beat come from besides the composition's own BPM.
bool Sync( App& app )
{
	Preferences& p  = app.prefs;
	TempoSync& sync = app.tempoSync;
	bool changed    = false;
	ImGui::SeparatorText( "Ableton Link" );
	if( TempoSync::LinkBuiltIn() )
	{
		if( ImGui::Checkbox( "Link##link", &p.linkEnabled ) )
			changed = true;
		Help( "The tempo and beat shared with other apps on this network that have Link: Ableton Live, Traktor, "
		      "rekordbox, Serato, other VJ apps. Changing the BPM here changes it for all of them. Also the LINK button "
		      "next to the BPM." );
		if( sync.LinkEnabled() )
		{
			int peers = sync.LinkPeers();
			ImGui::TextDisabled( peers == 0 ? "No other app found yet" : peers == 1 ? "1 other app" : "%d other apps", peers );
		}
	}
	else
		ImGui::TextDisabled( "Not in this build" );

	ImGui::SeparatorText( "MIDI Clock" );
	std::vector< std::string > inputs = app.remote.OpenInputs();
	auto shownName = []( const std::string& input ) { return input.empty() ? std::string( "Off" ) : input == "*" ? std::string( "Any MIDI input" ) : input; };
	ImGui::SetNextItemWidth( Ui( 360 ) );
	if( ImGui::BeginCombo( "##clock", shownName( p.midiClockInput ).c_str() ) )
	{
		std::vector< std::string > choices = { "", "*" };
		choices.insert( choices.end(), inputs.begin(), inputs.end() );
		if( !p.midiClockInput.empty() && std::find( choices.begin(), choices.end(), p.midiClockInput ) == choices.end() )
			choices.push_back( p.midiClockInput ); // chosen before, not connected now
		for( const std::string& c : choices )
			if( ImGui::Selectable( shownName( c ).c_str(), p.midiClockInput == c ) && p.midiClockInput != c )
			{
				p.midiClockInput = c;
				changed          = true;
			}
		ImGui::EndCombo();
	}
	Help( "Follow the MIDI clock of a DJ mixer, a drum machine or another computer: the tempo, and the beat from "
	      "its Start and Song Position. While it ticks it leads (and Link's other apps get its tempo); when it stops, "
	      "the beat goes on at its last tempo. The input must be on in Preferences > MIDI." );
	std::string status = sync.Status();
	if( !status.empty() )
		ImGui::TextDisabled( "%s", status.c_str() );

	ImGui::SeparatorText( "Timecode" );
	{
		int source = p.timecodeSource;
		ImGui::SetNextItemWidth( Ui( 200 ) );
		if( ImGui::BeginCombo( "##tcSource", TimecodeInput::SourceName( (TimecodeInput::Source)source ) ) )
		{
			for( int i = 0; i < (int)TimecodeInput::Source::Count; ++i )
				if( ImGui::Selectable( TimecodeInput::SourceName( (TimecodeInput::Source)i ), source == i ) && source != i )
				{
					p.timecodeSource = i;
					changed          = true;
				}
			ImGui::EndCombo();
		}
		Help( "The timecode the timeline follows when its TC button is on: LTC (SMPTE on a sound card's input), "
		      "MTC (from a DAW or a console over MIDI) or Art-Net (from the lighting desk's network)." );
		if( p.timecodeSource == (int)TimecodeInput::Source::Ltc )
		{
			static std::vector< std::string > devices = AudioInput::Devices();
			Caption( "LTC input" );
			ImGui::SetNextItemWidth( Ui( 300 ) );
			if( ImGui::BeginCombo( "##ltcDevice", p.timecodeLtcDevice.empty() ? "System default" : p.timecodeLtcDevice.c_str() ) )
			{
				if( ImGui::Selectable( "System default", p.timecodeLtcDevice.empty() ) && !p.timecodeLtcDevice.empty() )
				{
					p.timecodeLtcDevice.clear();
					changed = true;
				}
				for( const std::string& d : devices )
					if( ImGui::Selectable( d.c_str(), p.timecodeLtcDevice == d ) && p.timecodeLtcDevice != d )
					{
						p.timecodeLtcDevice = d;
						changed             = true;
					}
				ImGui::EndCombo();
			}
			ImGui::SameLine();
			if( ImGui::Button( "Refresh##ltc" ) )
				devices = AudioInput::Devices();
			ImGui::SameLine();
			ImGui::SetNextItemWidth( Ui( 90 ) );
			int channel = p.timecodeLtcChannel + 1;
			if( ImGui::InputInt( "Channel##ltc", &channel ) )
			{
				p.timecodeLtcChannel = std::clamp( channel, 1, 64 ) - 1;
				changed              = true;
			}
		}
		else if( p.timecodeSource == (int)TimecodeInput::Source::Mtc )
		{
			Caption( "MTC input" );
			ImGui::SetNextItemWidth( Ui( 300 ) );
			if( ImGui::BeginCombo( "##mtcInput", shownName( p.timecodeMtcInput ).c_str() ) )
			{
				std::vector< std::string > choices = { "*" };
				choices.insert( choices.end(), inputs.begin(), inputs.end() );
				for( const std::string& c : choices )
					if( ImGui::Selectable( shownName( c ).c_str(), p.timecodeMtcInput == c ) && p.timecodeMtcInput != c )
					{
						p.timecodeMtcInput = c;
						changed            = true;
					}
				ImGui::EndCombo();
			}
			Help( "From a DAW on this computer, a virtual MIDI port (loopMIDI on Windows, the IAC bus on a Mac) carries it. "
			      "The input must be on in Preferences > MIDI." );
		}
		else if( p.timecodeSource == (int)TimecodeInput::Source::ArtNet )
			Help( "ArtTimeCode on UDP port 6454, from any sender on the network." );
		if( p.timecodeSource != 0 )
		{
			Caption( "Freewheel" );
			ImGui::SetNextItemWidth( Ui( 260 ) );
			if( ImGui::SliderFloat( "##freewheel", &p.timecodeFreewheelMs, 0, 5000, "%.0f ms", ImGuiSliderFlags_AlwaysClamp ) )
				changed = true;
			Help( "When the timecode stops arriving (a cable, a lost packet) the timeline runs on this long before it stops." );
			std::string tc = app.timecode.Status();
			if( !tc.empty() )
				ImGui::TextDisabled( "%s", tc.c_str() );
		}
	}

	ImGui::SeparatorText( "Delay" );
	ImGui::SetNextItemWidth( Ui( 260 ) );
	if( ImGui::SliderFloat( "##latency", &p.syncLatencyMs, -200, 200, "%+.0f ms", ImGuiSliderFlags_AlwaysClamp ) )
		changed = true;
	if( ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked( ImGuiMouseButton_Left ) )
	{
		p.syncLatencyMs = 0;
		changed         = true;
	}
	Help( "Projectors and LED processors show the picture late: when the visuals trail the music, move this to the "
	      "right until the flashes land on the kick. Applies to Link, the MIDI clock and the timecode. Double-click: 0." );
	if( changed )
		app.ApplySync();
	return changed;
}

bool Video( App& app )
{
	Preferences& p = app.prefs;
	bool changed   = false;
	if( HasFfmpeg() )
	{
		if( ImGui::Checkbox( "Hardware decoding (H.264, HEVC, VP9...)", &p.hardwareDecoding ) )
		{
			changed = true;
			app.ApplyPreferences( false );
		}
		Help( "The graphics chip decodes non-HAP video, sparing the CPU. Applies to clips loaded from now on. HAP needs none of it." );
		ImGui::SetNextItemWidth( Ui( 160 ) );
		if( ImGui::SliderInt( "Scratch memory per clip", &p.frameCacheMegabytes, 0, 2048, p.frameCacheMegabytes ? "%d MB" : "off",
		                      ImGuiSliderFlags_AlwaysClamp ) )
		{
			changed = true;
			app.ApplyPreferences( false );
		}
		Help( "H.264 and other non-HAP clips keep their recently decoded frames in memory, so scratching back over them and "
		      "jumping to cue points nearby is instant. About 3 MB a frame in HD, 12 MB in 4K. HAP needs none of it." );
	}
	Caption( "GPU" );
	ImGui::TextDisabled( "%s, OpenGL %s", Caps().renderer, Caps().version );
	ImGui::Spacing();
	ImGui::SeparatorText( "FreeFrame (FFGL) Plugin Directories" );
	DrawPluginFolders( app );
	return changed;
}

bool Defaults( App& app )
{
	Preferences& p = app.prefs;
	bool changed   = false;
	Help( "The values new clips and layers start with. They are used when clips are added to the deck or the timeline; the "
	      "clips already there keep theirs." );
	ImGui::SeparatorText( "Clips" );
	Caption( "Resize (how the clip covers the composition)" );
	for( int i = 0; i < 4; ++i )
	{
		if( i > 0 )
			ImGui::SameLine();
		bool on = p.clipFit == kFitOrder[ i ];
		if( on )
			ImGui::PushStyleColor( ImGuiCol_Button, ImGui::ColorConvertU32ToFloat4( theme::AccentDim ) );
		if( ImGui::Button( FitLabel( kFitOrder[ i ] ), ImVec2( Ui( 90 ), 0 ) ) )
		{
			p.clipFit = kFitOrder[ i ];
			changed   = true;
		}
		if( on )
			ImGui::PopStyleColor();
	}
	Help( "Fill: covers the whole output, cropping the edges. Fit: the whole picture shows, with bars if its shape differs. "
	      "Stretch: covers it exactly, out of shape if need be. Original: pixel for pixel." );
	if( ImGui::Button( "Resize Every Clip Now" ) )
	{
		int count = app.ResizeAllClips( p.clipFit );
		app.SetStatus( "Resized " + std::to_string( count ) + " clips to " + FitLabel( p.clipFit ) );
	}
	if( ImGui::IsItemHovered() )
		ImGui::SetTooltip( "The clips already in the composition (every deck, and the timeline) to %s too", FitLabel( p.clipFit ) );
	Caption( "Play Mode" );
	ImGui::SetNextItemWidth( Ui( 220 ) );
	changed |= Combo( "##playMode", &p.clipPlayMode, PlayModeName );
	Caption( "Transport" );
	int transport = p.clipBpmSync ? 1 : 0;
	ImGui::SetNextItemWidth( Ui( 140 ) );
	if( ImGui::Combo( "##transport", &transport, "Timeline\0BPM Sync\0" ) )
	{
		p.clipBpmSync = transport == 1;
		changed       = true;
	}
	if( p.clipBpmSync )
	{
		ImGui::SameLine();
		ImGui::SetNextItemWidth( Ui( 100 ) );
		if( ImGui::DragFloat( "beats", &p.clipSyncBeats, 0.25f, 0.25f, 256, "%g" ) )
			changed = true;
	}
	Caption( "Trigger Style" );
	ImGui::SetNextItemWidth( Ui( 140 ) );
	changed |= Combo( "##trigger", &p.clipTrigger, TriggerStyleName );

	ImGui::SeparatorText( "Layers" );
	Caption( "Blend Mode" );
	ImGui::SetNextItemWidth( Ui( 220 ) );
	changed |= Combo( "##blend", &p.layerBlend, BlendModeName );
	Caption( "Transition" );
	ImGui::SetNextItemWidth( Ui( 220 ) );
	changed |= Combo( "##transition", &p.layerTransition, TransitionModeName );
	ImGui::SameLine();
	ImGui::SetNextItemWidth( Ui( 120 ) );
	changed |= ImGui::SliderFloat( "##transitionTime", &p.layerTransitionTime, 0, 10, p.layerTransitionTime <= 0 ? "Cut" : "%.2f s" );
	if( changed )
		app.ApplyPreferences( false );
	return changed;
}
}

void DrawPreferences( App& app )
{
	if( !app.showPreferences )
		return;
	static Section section = Section::General;
	ImGui::SetNextWindowSize( ImVec2( Ui( 920 ), Ui( 620 ) ), ImGuiCond_FirstUseEver );
	ImGui::SetNextWindowPos( ImGui::GetMainViewport()->GetCenter(), ImGuiCond_FirstUseEver, ImVec2( 0.5f, 0.5f ) );
	ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding, ImVec2( 0, 0 ) );
	bool open = ImGui::Begin( "Preferences", &app.showPreferences, ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoCollapse );
	ImGui::PopStyleVar();
	if( !open )
	{
		ImGui::End();
		return;
	}
	// Sections at the left, right-aligned, the chosen one lit.
	float sideW = Ui( 210 );
	ImGui::PushStyleColor( ImGuiCol_ChildBg, ImGui::ColorConvertU32ToFloat4( theme::Raised ) );
	ImGui::BeginChild( "##sections", ImVec2( sideW, 0 ) );
	ImGui::Dummy( ImVec2( 1, Ui( 90 ) ) );
	ImGui::PushFont( Fonts().bold );
	for( int s = 0; s < (int)Section::Count; ++s )
	{
		bool on    = section == (Section)s;
		ImVec2 p   = ImGui::GetCursorScreenPos();
		float h    = Ui( 40 );
		if( ImGui::InvisibleButton( SectionName( (Section)s ), ImVec2( sideW, h ) ) )
			section = (Section)s;
		ImDrawList* d = ImGui::GetWindowDrawList();
		if( on )
			d->AddRectFilled( p, ImVec2( p.x + sideW, p.y + h ), theme::AccentDim );
		else if( ImGui::IsItemHovered() )
			d->AddRectFilled( p, ImVec2( p.x + sideW, p.y + h ), theme::RaisedHover );
		ImVec2 t = ImGui::CalcTextSize( SectionName( (Section)s ) );
		d->AddText( ImVec2( p.x + sideW - t.x - Ui( 20 ), p.y + ( h - t.y ) / 2 ), on ? theme::Text : theme::TextDim, SectionName( (Section)s ) );
	}
	ImGui::PopFont();
	ImGui::EndChild();
	ImGui::PopStyleColor();
	ImGui::SameLine( 0, 0 );

	// The chosen section: its title on a coloured band, then its settings.
	ImGui::BeginChild( "##section", ImVec2( 0, 0 ) );
	ImVec2 p = ImGui::GetCursorScreenPos();
	float w  = ImGui::GetContentRegionAvail().x;
	ImGui::GetWindowDrawList()->AddRectFilled( p, ImVec2( p.x + w, p.y + Ui( 90 ) ), theme::AccentDim );
	char title[ 64 ];
	std::snprintf( title, sizeof( title ), "%s Preferences", SectionName( section ) );
	ImGui::GetWindowDrawList()->AddText( Fonts().bold, ImGui::GetFontSize() * 2.0f, ImVec2( p.x + Ui( 50 ), p.y + Ui( 28 ) ), theme::Text, title );
	ImGui::Dummy( ImVec2( 1, Ui( 100 ) ) );
	ImGui::SetCursorPosX( Ui( 50 ) );
	ImGui::BeginChild( "##settings", ImVec2( w - Ui( 90 ), 0 ) );
	bool changed = false;
	switch( section )
	{
	case Section::General: changed = General( app ); break;
	case Section::Audio: changed = Audio( app ); break;
	case Section::Video: changed = Video( app ); break;
	case Section::Midi:
		if( DrawMidiInputs( app ) )
		{
			app.SaveControlSettings();
			app.remote.Configure( app.control, true );
		}
		break;
	case Section::Osc:
		if( DrawOscSettings( app ) )
		{
			app.SaveControlSettings();
			app.remote.Configure( app.control, true );
		}
		break;
	case Section::Sync: changed = Sync( app ); break;
	case Section::Defaults: changed = Defaults( app ); break;
	default: break;
	}
	if( changed )
		app.SavePreferences();
	ImGui::EndChild();
	ImGui::EndChild();
	ImGui::End();
}
}
