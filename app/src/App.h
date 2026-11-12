#pragma once
#include <deque>
#include <map>
#include <string>
#include <vector>
#include <memory>
#include "Composition.h"
#include "Preferences.h"
#include "History.h"
#include "audio/AudioEngine.h"
#include "audio/AudioInput.h"
#include "control/TempoSync.h"
#include "control/TimecodeInput.h"
#include "control/Feedback.h"
#include "control/RemoteInputs.h"
#include "fx/EffectRegistry.h"
#include "render/Compositor.h"
#include "render/Fbo.h"
#include "render/FrameClock.h"
#include "output/AdvancedOutput.h"
#include "output/OutputRenderer.h"
#include "ui/Workspaces.h"

struct GLFWwindow;

namespace stltvj
{
struct LaunchOptions
{
	std::string capturePath; // write the window to this .bmp after `captureFrames` frames, then quit
	int captureFrames = 60;
	bool captureComposition = false; // the composition (at its own size) rather than the window
	int captureScreen       = 0;     // --capture-screen N: what screen N of the Advanced Output shows (from 1)
	struct ClipArg
	{
		int layer, column;
		std::string path;
	};
	std::vector< ClipArg > clips;     // --clip layer,column,path (1-based, launched right away)
	std::vector< ClipArg > sources;   // --source layer,column,effect-id (a generator, launched right away)
	struct EffectArg
	{
		std::string scope; // "comp", "layer:L" or "clip:L,C"
		std::string id;
	};
	std::vector< EffectArg > effects; // --fx scope=effect-id
	std::vector< std::pair< int, int > > launches; // --launch layer,column: clips of the restored session
	bool fresh = false;               // ignore the last session
	bool listEffects = false;         // print the effect ids and plugin problems at start
	bool checkIsf    = false;         // --check-isf [FOLDER]: build and run every ISF shader found, then quit
	std::string isfFolder;            // and those in this folder too
	std::string outputSetup;          // --output-setup: an output setup (.json) or Resolume AdvancedOutput.xml to load
	int advancedOutputTab = -1;       // --advanced-output input|output: open the editor on that tab
	int width = 0, height = 0;        // window size override
	int oscPort = 0;                  // --osc PORT: listen for OSC on this port
};

// What the outputs show, as in Resolume's Output menu: nothing, the composition in a window or fullscreen on one
// display, or the Advanced Output setup (screens, slices, warps, masks). The setup is kept whatever the mode.
enum class OutputMode
{
	Off,
	Window,
	Fullscreen,
	Advanced,
};

class App
{
public:
	bool Init( const LaunchOptions& options );
	void Run();
	void Shutdown();
	// Builds and runs every ISF shader in the registry; prints each one's result and returns how many failed.
	int CheckIsf();

	// Actions shared by the panels and shortcuts.
	void LoadClip( int layer, int column, const std::string& path );
	void RemoveClip( int layer, int column );
	void MoveClip( int fromLayer, int fromColumn, int toLayer, int toColumn );
	// The two views, as in Ableton: Session (the deck of clips to launch) and Arrangement (the timeline). The
	// output shows the one in use.
	enum class View
	{
		Session,
		Arrangement
	};
	View view = View::Session;
	void SetView( View v );
	// Preferences (the Preferences window, Ctrl+,): saved at once; Apply passes them on (audio output, decoding,
	// layer defaults).
	Preferences prefs;
	bool showPreferences = false;
	void SavePreferences();
	void ApplyPreferences( bool restartAudio );
	int startViewPending = -1; // a view to switch to once the first frame is drawn (the start view)
	// The interface's size: the display's own scale times the user's zoom (prefs.interfaceScale). ZoomInterface
	// steps it by 10 % (+1 bigger, -1 smaller, 0 back to 100 %); the fonts and the style are rebuilt before the
	// next frame, and the panels keep their place.
	void ZoomInterface( int step );
	void SetInterfaceScale( float scale );
	float displayScale   = 1;
	float appliedUiScale = 0;
	double zoomShownUntil = 0; // the new size shows in the middle of the window for a moment
	double prefsSaveAt    = 0; // after a zoom: preferences.json is written once it has settled (and on quit)
	// STLT-CHASER (Output > Chaser): opens its editor on the Advanced Output's slices, which are kept written to
	// chaserStage for it.
	void OpenChaser();
	void UpdateChaserStage();
	std::string chaserStage, chaserStageText;
	double chaserStageChecked = 0;
	// Every clip (all decks, and the timeline) to this fit; the number changed.
	int ResizeAllClips( FitMode fit );
	// The selected deck clip to the clipboard (Ctrl+C; with `cut`, Ctrl+X, it leaves the deck); pasted into the
	// selected cell (Ctrl+V). Its effects alone: Copy / Paste Effects (added after the target's own).
	void CopyClip( bool cut );
	void PasteClip();
	void CopyEffects();
	void PasteEffects();
	bool HasClipboardClip() const { return clipboard != nullptr; }
	bool HasClipboardEffects() const { return effectsClipboard != nullptr; }
	// Players, sounds, effects and sources of a clip made from saved settings (a copy, a paste).
	void PrepareClip( Clip& clip );
	std::unique_ptr< Clip > clipboard, effectsClipboard;
	// The timeline: clips placed in time on tracks (see Timeline). Its playhead runs in either view.
	void TimelinePlay( bool play );
	void TimelineStop(); // stops and goes back to the start
	void TimelineCue( int block );
	void TimelineNextCue( int step );
	// A new timeline clip from a deck clip, a file or a source, at `start` on `track` (overwriting what it covers).
	int AddTimelineClip( int track, double start, Clip clip );
	int AddTimelineFile( int track, double start, const std::string& path );
	void UpdateTimeline( double dt );
	void PrepareTimelineClip( TimelineClip& clip );
	// A clip's trigger pressed (`down`) or released: a click on its picture, its key, MIDI pad or OSC message. What
	// it does depends on the clip's trigger style: Normal launches, Toggle launches or stops, Piano plays while held.
	void TriggerClip( int row, int column, bool down );
	// Decks: pages of clips over the same layers. What plays keeps playing when another deck is shown.
	void ShowDeck( int deck );
	void AddDeck(); // a new empty deck, shown
	void RemoveDeck( int deck );
	void DeleteLayer( int layer );
	// Move mode (the Move menu): clips are picked in the deck, then moved together. Playing clips keep playing.
	bool moveMode = false;
	std::vector< std::pair< int, int > > movePicked; // (row, column) of the picked clips
	void SetMoveMode( bool on );
	bool IsPicked( int row, int column ) const;
	void TogglePicked( int row, int column );
	void PickAllClips();
	// Moves the picked clips by `rows` and `columns` (columns are added when needed); false, with the reason in the
	// status bar, when they would leave the deck or land on clips that are not picked.
	bool MovePicked( int rows, int columns );
	// Launches the clip at row/column: on `onLayer`, else where the composition's clip target says (its own row's
	// layer, or the selected layer). With launch quantize set it waits for the next beat or bar unless `now`.
	void Launch( int row, int column, bool now = false, int onLayer = -1 );
	void LaunchColumn( int column, bool now = false );
	bool IsQueued( int row, int column ) const; // waiting for its beat (row -1: a column launch)
	// Plays the clip from cue point `cue` (0..7), launching it if it is not playing; nothing if the cue is not set.
	void JumpToCue( int row, int column, int cue );
	// Launches the next (step 1) or previous (-1) clip of a layer after the one playing, wrapping around.
	void LaunchNextClip( int layer, int step );
	// Launches the next or previous column after the one launched last.
	void LaunchNextColumn( int step );
	// The composition's transport: every playing clip backwards or forwards, or the whole composition paused.
	void SetAllDirections( int direction );
	void ClearLayer( int layer );
	// Selects the clip at row/column (and, with clips on their own rows' layers, its layer).
	void Select( int row, int column );
	void NewComposition();
	bool SaveCompositionTo( const std::string& path );
	bool OpenComposition( const std::string& path );
	void SetStatus( const std::string& message, bool error = false );
	void TapTempo();
	void ResyncBeat();

	// Effects and sources.
	void AddEffect( EffectChain& chain, const std::string& id, int position = -1 );
	void LoadGenerator( int layer, int column, const std::string& id );
	void RescanPlugins();
	std::vector< std::string > PluginFolders() const;
	void AddPluginFolder( const std::string& folder );
	void RemovePluginFolder( const std::string& folder );

	// Outputs. SetOutputMode switches what is shown (`display`: the monitor for Fullscreen); ShowScreenOn puts a
	// screen of the Advanced Output setup on a display or in a window, and makes Advanced the mode.
	void SetOutputMode( OutputMode mode, const std::string& display = {} );
	// Advanced on; or, when it is on, back to the mode before it (Fullscreen on the same display, a window...).
	void ToggleAdvancedOutput();
	OutputMode beforeAdvanced = OutputMode::Off;
	std::string OutputModeLabel() const; // "Off", "Window", "Fullscreen on DELL U2415", "Advanced"
	void ShowScreenOn( size_t screen, OutputDevice device, const std::string& display = {} );
	void StopOutputs() { SetOutputMode( OutputMode::Off ); }
	OutputMode outputMode = OutputMode::Off;
	std::string outputDisplay;
	bool SaveOutputPreset( const std::string& name );
	bool LoadOutputPreset( const std::string& name );
	std::vector< std::string > OutputPresets() const;
	bool ImportResolumeOutput( const std::string& path );
	std::string OutputPresetDirectory() const;
	// The Advanced Output setup is edited live, and kept only when saved: SaveOutputSetup writes it,
	// RevertOutputSetup goes back to the saved one. Unsaved edits are not kept when the app closes.
	bool OutputUnsaved() const;
	void SaveOutputSetup();
	void RevertOutputSetup();
	std::string savedOutput;            // the setup as last saved
	int outputSavePrompt     = 0;       // asking whether to save the setup: 1 the editor is closing, 2 the app is quitting
	bool advancedEditorWasOpen = false; // to catch the editor closing with unsaved edits

	// Undo and redo of the composition and the output setup (Ctrl+Z, Ctrl+Shift+Z or Ctrl+Y). Steps are recorded
	// when an edit ends: a value dragged, a field left, a menu choice, a drop.
	bool Undo();
	bool Redo();
	History history;

	// Remote control: OSC, MIDI and the computer's keyboard (see control/Control.h for the addresses). Learning:
	// the next key, MIDI note or controller is mapped to learnTarget, the control last clicked.
	bool ApplyControl( const std::string& address, bool hasValue, float value, bool momentary = false, float previous = 0 );
	void SaveControlSettings() const;
	// Shortcut presets: the key and MIDI maps saved by name (in the settings folder) or to a file. The maps in use
	// are kept between sessions whatever project is open; `shortcutPreset` names the preset they came from.
	std::vector< std::string > ShortcutPresets() const;
	bool SaveShortcutPreset( const std::string& name );
	bool LoadShortcutPreset( const std::string& name );
	bool DeleteShortcutPreset( const std::string& name );
	bool ExportShortcuts( const std::string& path );
	bool ImportShortcuts( const std::string& path );
	// Whether the maps differ from the preset they came from (or there is none and they are not empty).
	bool ShortcutsChanged() const { return control.SaveShortcuts() != shortcutPresetText; }
	std::string ShortcutPresetDirectory() const;
	std::string shortcutPresetText; // the maps of the preset in use (control.preset), as loaded or saved
	ControlSettings control;
	// Tempo and beat from Ableton Link or a MIDI clock (Preferences > Sync). Before `remote`, whose MIDI threads
	// feed it, so that it outlives them.
	TempoSync tempoSync;
	// The timecode the timeline can follow (Preferences > Sync; the timeline's TC button). Before `remote` too.
	TimecodeInput timecode;
	bool timecodeLocked = false; // the timeline follows a timecode that is coming in
	double timecodeRetried = 0;  // when a source that would not open was last tried again
	bool FollowTimecode( double dt );
	RemoteInputs remote;
	AudioEngine audio;
	// The sound coming in, for parameters that follow it: opened while an animation listens (and closed ten seconds
	// after the last stops), measured every frame into the animations' clock.
	AudioInput audioInput;
	double audioInputTried = -1e9;
	int audioInputViewed   = 0; // frames for which the input stays open because its meters are on screen
	double undoBpm = 120; // the BPM undo steps hold: frozen while the tempo comes from outside
	void ApplySync();
	void UpdateAudioInput( double now, double dt );
	void RestartAudioInput() { audioInput.Stop(); audioInputTried = -1e9; }
	bool learning  = false; // mapping mode: a click picks a control, the next key or MIDI message maps it
	bool learnKeys = false; // mapping keys of the computer's keyboard (Ctrl+K), else MIDI (Ctrl+M)
	// Turns keyboard or MIDI mapping on, or off when it is the one on.
	void ToggleMapping( bool keys );
	std::string learnTarget;
	MappingTarget learnMapTarget = MappingTarget::Position; // what new mappings act on (when their control can follow it)
	// The address a mapping acts on now (its target applied to the selection).
	std::string MappedAddress( const std::string& address, MappingTarget target ) const
	{
		return Retarget( address, target, composition.selectedLayer, composition.selectedRow, composition.selectedColumn );
	}
	std::string lastControl; // the last message, for the panel

	// Panel state.
	Composition composition;
	Compositor compositor;
	std::unique_ptr< EffectRegistry > registry;
	std::vector< std::string > extraPluginFolders; // added by the user, besides the default ones
	FrameInfo frame;
	// The Advanced Output has two setups: the deck's (Grid) for the Session view and the timeline's for the
	// Arrangement view, whose tracks can go to its screens. The one of the view in use is on show.
	AdvancedOutputSetup advancedOutput; // the Grid's
	AdvancedOutputSetup timelineOutput;
	AdvancedOutputSetup& LiveOutput() { return view == View::Arrangement ? timelineOutput : advancedOutput; }
	// The setup the Advanced Output editor works on (its Grid and Timeline tabs): 0 the Grid's, 1 the timeline's.
	int outputTab = 0;
	AdvancedOutputSetup& EditedOutput() { return outputTab == 1 ? timelineOutput : advancedOutput; }
	bool EditingLive() const { return ( outputTab == 1 ) == ( view == View::Arrangement ); }
	// What draws the edited setup's screens: the live renderer, or one of its own for the setup not on show.
	OutputRenderer& EditedRenderer() { return EditingLive() ? outputs : editOutputs; }
	std::string OutputText() const { return SaveOutputSetups( advancedOutput, timelineOutput ); }
	// The Composition Monitor's source (Modul8 style buttons): the composition, every screen side by side, one
	// screen, or one layer (a timeline track in the Arrangement view) alone.
	enum class MonitorSource
	{
		Composition,
		Multiview,
		Screen,
		Layer
	};
	MonitorSource monitorSource = MonitorSource::Multiview;
	int monitorScreen = 0, monitorLayer = 0;
	bool monitorShown = false; // the monitor drew last frame (its screens and layer are drawn for it)
	OutputRenderer outputs;      // the setup on show: shown in Advanced mode, drawn for the editor's views and the monitor
	OutputRenderer editOutputs;  // the other setup, while the editor works on it
	OutputRenderer quickOutputs; // the composition alone, in a window or fullscreen (Window and Fullscreen modes)
	AdvancedOutputSetup quickOutput;
	Workspaces workspaces;
	PanelVisibility panels;
	Fbo preview;
	GLFWwindow* window = nullptr;

	std::string compositionPath;
	std::string browserDirectory;
	std::vector< std::string > droppedPaths; // from the OS, placed by the clip grid on the next frame
	float dropX = 0, dropY = 0;              // where they were dropped, in screen coordinates
	std::string status;
	bool statusIsError     = false;
	double statusTime      = 0;
	std::deque< float > frameTimes;   // ms, for the performance panel
	std::vector< double > taps;
	std::string configDir;
	bool quitRequested = false;

private:
	void Frame();
	void ApplyUiScale();
	void UpdatePlayback( double dt );
	// How far the composition moves this frame, given what the system clock measured (render/FrameClock.h).
	double FrameStep( double measured );
	std::unique_ptr< FrameClock > frameClock;

public:
	double FramePeriod() const { return frameClock ? frameClock->Period() : 0; }
	bool ClockLocked() const { return frameClock && frameClock->Locked(); }

private:
	void DrawUi();
	void Capture();
	ClipPlayer* PlayerAt( int layer, int column );
	void RestoreEffects(); // turns the effects of a just-loaded composition into live ones
	void SavePluginFolders() const;
	History::State Snapshot() const;
	void PollRemote();
	// Controllers' lights for the MIDI mappings (control/Feedback.h): what each mapped pad shows, sent when it
	// changes; `off` turns them all off (when the app closes).
	void UpdateLights( bool off = false );
	Light LightFor( const std::string& address );
	struct LightSent
	{
		std::string device;
		MidiKey key;
		std::vector< unsigned char > message;
	};
	std::map< std::string, LightSent > lightsSent; // by device and key
	int lightsVersion = -1;
	void PollKeyboard();
	void UpdateAudio();
	// The Preview Monitor plays the selected clip even when it is not playing on its layer, with the clip's own
	// player (started for the preview and stopped when another clip is selected).
	void UpdatePreview( double dt );
	ClipPlayer* previewPlayer = nullptr;
	void UpdateQuickOutput();
	// Ends a layer's transition: the outgoing clip stops.
	void FinishTransition( int layer );
	// Stops the clip at row/column wherever it plays or fades out.
	void StopCell( int row, int column );
	struct QueuedLaunch
	{
		int layer;       // the layer it goes to (-1: the whole column)
		int row, column; // the clip
		double beat;     // when it goes
	};
	std::vector< QueuedLaunch > queuedLaunches;
	void RunQueuedLaunches( bool all );
	void SaveOutputMode() const;
	std::string quickOutputKey; // what quickOutput was made for
	std::vector< std::pair< int, std::string > > keysHeld; // mapped keys down (ImGuiKey), to release them
	double lastRemoteConfigure = -1e9;
	std::map< int, float > lastMidiValues; // by note or controller: buttons act on the press
	std::map< int, std::string > midiHeld;  // by note or controller held: the address it pressed (the selection may change)
	std::map< std::string, float > lastOscValues; // by address: a 0 after a press is a release (Piano clips)
	// Takes a composition back to an earlier state, keeping the players, generators and effects that are still
	// there (playback goes on) and opening the rest.
	void RestoreComposition( const std::string& text );
	bool wasEditing   = false;
	double lastRecord = 0;

	LaunchOptions options;
	double lastTime  = 0;
	double startTime = 0;
	int frameCount  = 0;
	bool outputVsync = false;
};
}
