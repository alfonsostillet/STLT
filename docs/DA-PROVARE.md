# To try (Windows) at the next version

Fixes already made and checked by the automatic tests, to try on a real PC before considering them closed.

## From testing 0.3.0

- [ ] **A project that reopens as it was.** Prepare a set with some clips playing, save it (Ctrl+S), close STLT and reopen it: the clips must start again and the output must show the picture. Also try *Composition → Open...* on a saved project, and the timeline: the playhead must go back to where you left it.
  - If after reopening the grid is **empty** (not just stopped), report it: that would mean a different problem, in saving or in the Save/Open dialog.
- [ ] **The STLT-CHASER effect without the "MESSAGE FROM DEAR IMGUI" window** when you open its Delayer, Wiper and Outliner sections.
- [ ] **Timeline with the Chaser active in the grid.** Put STLT-CHASER on the composition in the grid view, go to the timeline: the output must show the timeline normally.

## Still to try from the 0.3.0 checklist

- [ ] **Spout and NDI**: an Advanced Output screen sent through Spout and NDI must show up in another program.

## The Output menu (new)

- [ ] **Windowed** on each display: the output window must open in the middle of the chosen display. **Fullscreen** on the display STLT is on says so in its tooltip; Escape on the output closes it.
- [ ] **Composition output sharing**: tick *Network streaming (NDI)* with the output Off: another program (OBS with NDI, Resolume, NDI Studio Monitor) must see "STLT Composition". Untick it: the source must go away. The same with *Texture sharing (Spout)*.
- [ ] **Identify Displays**: a big number with the display's name and size on each display, for four seconds.
- [ ] **Show Test Card** on Fullscreen and on Advanced: numbered grids on every output; untick it to go back.
- [ ] **Snapshot**: a PNG of the composition in Documents\STLT\Snapshots, opening in any viewer the right way up.
- [ ] *System Display Settings...* opens Windows' display settings.

## Maps that follow the selected layer (new)

- [ ] In the Shortcuts panel set *New maps act on* to **Selected layer**, Map MIDI (Ctrl+M), click a layer's Bypass or its opacity, move a pad or knob: selecting another layer (with the mouse or the arrows), the same pad must act on **that** layer.
- [ ] In the *Acts on* column of an existing map, switch between *By position*, *Selected layer* and *Selected clip*; hover it to see which address it acts on now. The choice must survive a restart and a preset saved and loaded.

## Controller lights (new)

- [ ] **APC mini mk2**: pads mapped to clips light up **blue** if there is a clip, **green** when it plays, **pulsing green** if it starts on the next beat (Launch on Beat or Bar); the track and scene buttons light up or blink (for example on Bypass, Solo, columns); the pad mapped to TAP blinks with the tempo. They go off when STLT closes.
  - Mappings made before this version don't know which controller they came from: the lights go to all the connected controllers. Remapping a pad sends them only to its own.
  - If a controller misbehaves, *Preferences → MIDI → Controller lights* turns them off.

## Parameters that follow the sound, and new animations (new)

- [ ] **Audio input**: in *Preferences → Audio → Audio Input* choose the microphone, the sound card or the mixer's input. The four bars (Level, Bass, Mid, Treble) must move with the sound; *Input Gain* raises or lowers them (double-click: 0 dB).
- [ ] **A parameter that follows the sound**: click a parameter's name (for example a clip's Opacity or an effect's parameter) → *Animate* → *Audio Bass*: the parameter must move between From and To along with the kick drum. Also try Level, Mid and Treble, and *Reverse*.
- [ ] **Period in seconds**: in *Animate*, next to *Period*, choose *Seconds* instead of *Beats*: the animation must keep its own pace even when the BPM changes.
- [ ] **Smooth Random**: like Random, but it glides from one value to the next instead of jumping.
- [ ] A project saved and reopened must remember all these settings.

## Ableton Link and MIDI clock (new)

- [ ] **Link**: with Ableton Live (or Traktor, rekordbox, Serato with Link on) on the same PC or the same network, press **LINK** next to the BPM. On the first start Windows may ask for network permission: allow it. The button must show the number of other apps (for example *LINK 1*); STLT's BPM becomes Live's and the four bar dots move with Live's metronome. Change the BPM in Live: it changes in STLT too. Change it in STLT (or with TAP): it changes in Live too.
- [ ] **MIDI clock**: in *Preferences → Sync → MIDI Clock* choose the input of the mixer or drum machine (it must be on in *Preferences → MIDI*). **CLOCK** lights up in the bar and the BPM follows the clock; pressing Play/Start on the device starts the bar again from 1. On Stop the animations go on at the previous tempo.
- [ ] **Delay**: if the pictures arrive after the music (projector, LED wall), move *Preferences → Sync → Delay* to the right until the flashes land on the kick drum.
- [ ] With Link or clock active, Ctrl+Z must not change the BPM.

## Timeline following timecode (new)

- [ ] **Art-Net** (the easiest to try): in *Preferences → Sync → Timecode* choose *Art-Net*; send timecode from a lighting desk or a program (for example TouchDesigner, or a free Art-Net generator). On the timeline turn on **TC** and write the start (for example 01:00:00:00): the playhead must go where the timecode says, play when it runs, stop when it stops, jump when it jumps. Windows may ask for network permission: allow it.
- [ ] **MTC**: from Ableton Live (or Reaper) on the same PC with a virtual MIDI port (loopMIDI): in Live's *Preferences → Link/Tempo/MIDI*, on the loopMIDI port turn on output *Sync* and the *MTC* type. In STLT turn on the port in *Preferences → MIDI* and choose *MTC* as the source. Play, stop and jumps in Live must move the timeline.
- [ ] **LTC**: an LTC audio file (generators can be found online) played into a sound card input (or with a cable from an output to an input): choose *LTC*, the input and the channel. The status below must show the timecode and the right frame rate.
- [ ] **Take**: with timecode arriving, move the playhead to where the show must start and press *Take*: from then on the timeline follows with that start.
- [ ] **Freewheel**: unplugging the cable (or stopping the sending) for a moment, the timeline must go on for the chosen time and then stop.
- [ ] While the timeline plays, Ctrl+Z must no longer send the playhead back (before, there was a fake undo step every two seconds).

## ISF effects (new)

- [ ] In the *Effects* panel there is an **ISF** section with about 200 effects by category (Blur, Color, Glitch, Halftone, Distortion, Kaleidoscope, Feedback...); in the *Sources* tab, the ISF sources (gradients, patterns, noise, shapes). Drag some onto clips, layers and the composition: they must work like the other effects, with their parameters (colours with the colour picker, points with X and Y, menus), animatable and mappable.
- [ ] Try above all the **Feedback** and **Glitch** ones (they use several passes and buffers): for example *Echo Trace*, *Comet Tails*, *RGB Trails 3.0*, *Time Glitch RGB*, *Slit Scan*.
- [ ] A project saved with ISF effects reopens with the same effects and values.
- [ ] If you have other ISF shaders (for example from VDMX, or downloaded), put them in `C:\ProgramData\ISF` or in the `isf` folder of STLT's settings: they must appear after a restart.

## Polygons of any shape (new)

- [ ] **+ Polygon → Draw...**: click on the canvas (Input or Output) to place the points one after the other; to close, click the first point (it grows when you are over it), double-click or press Enter. Backspace removes the last point, Esc cancels. Points snap to those of the other slices (Ctrl leaves them free).
- [ ] **+ Polygon → Triangle, Rectangle, Pentagon, Hexagon, Octagon, Circle**: ready-made shapes to modify.
- [ ] On an existing polygon: **double-click the outline** to add a point, or right-click the outline → *Add Point Here*; Del removes the selected point. In the properties, *Points* says how many there are (the (?) explains how).
- [ ] Concave shapes (a star, an L) must fill correctly, with no triangles sticking out.

## Quick touches (new)

- [ ] **Output** menu: *Advanced* only turns the Advanced output on and off; **Advanced Output Settings...** (Ctrl+Shift+A) opens the editor without touching the output. Open and close the editor several times: the output doesn't turn off.
- [ ] The **Deck 1, Deck 2, +** tabs are under the cells, level with the crossfader; *+ Layer* is at the bottom of the Layers panel.
- [ ] **Separators**: between panels there is a dark gap (it lights up when you pass over it to drag it), no lines under the tabs, no X on the right of each bar; a tab's X appears only when you pass over it.
- [ ] **STLT-CHASER**: with sequences written in the editor, the *Sequences* group has a button for each sequence with its name; the one playing has ▶ and is highlighted. Ctrl+M, click a button and press a pad: the pad changes sequence. Without sequences, the menu with "open the editor" stays.

## Separate Grid and Timeline, tracks on screens, Multiview (new)

- [ ] **Advanced Output**: at the top the **Grid** and **Timeline** tabs, two different setups. The one of the view in use says *(on show)*; going from grid to timeline (or with Tab) changes the output too. The one not on show can be edited as well: the bottom says so, with a button to go to it.
- [ ] On the timeline, under each video track's name there is *→ All Screens*. Click it and choose a screen: the track goes only there (track 1 → screen 2, track 3 → screen 3...). The screen shows only its tracks; screens with no tracks show the composition (the tracks on *All Screens*).
- [ ] In the Advanced Output, *Timeline* tab: under each screen is the list of tracks it receives; in the screen's settings, *Tracks* has a tick per track (the same as the menu on the timeline).
- [ ] Removing the last track from a screen (putting it back on *All Screens*), the screen goes back to showing the composition.
- [ ] Slices on a screen with tracks cut up the picture of those tracks (try: two layers on the left and cut up as in your test).
- [ ] Old files with "canvases" open by themselves: each canvas becomes the screen that used it.
- [ ] **Timeline like an editing program**: video tracks on top, audio below in green, with a gap in between. **Double-click** a header to rename it (Enter to confirm). **Drag** a header onto another track of the same kind to move it (or right-click → *Move Up* / *Move Down*). The headers' right edge drags to widen them; name, destination, M/S and opacity/volume read in full.
- [ ] **Monitor**: at the top the buttons *Composition*, *Multiview*, *Screen ▼*, *Track ▼* (or *Layer ▼* in the grid). Multiview: all the screens side by side with their names (with a single screen, the composition); double-click a screen to see it alone. *Track/Layer*: one track or layer alone, with its effects, even if muted.

## Advanced Output redone (new)

- [ ] At the top the **screens as cards** with a live preview (a green dot when they reach a display, a window or the network); click to work on one, right-click to turn it on or off, duplicate or delete it; *+ Screen* to add one.
- [ ] On the left only the slices of the chosen screen; right-click one: up, down, duplicate, delete.
- [ ] In the middle **Input** (*Takes from*) and **Output** (*Shows on*) side by side: dragging a point in one shows at once in the other. *Input* / *Output* at the top show a single view, large.
- [ ] At the bottom: whether the setup is on show, how many screens go where, *Unsaved changes*, *Discard Changes*, *Save*. The presets are in *Presets...* at the top.
- [ ] Tell me if it is clearer than before, and what you would change.

## Audio track volume with keyframes (new)

- [ ] On an audio track of the timeline press **Vol** (top right of the header, before M and S): the volume line appears on the track.
- [ ] **Double-click** the track to add a point where you click (height = volume); **drag** a point to move it (Shift: off the grid); **right-click** a point: curve (Linear, Ease In, Ease Out, Ease In Out, Hold) or *Delete*.
- [ ] During playback the volume follows the line. Moving the clips, the line stays where it is in time.
- [ ] The **diamond** next to the *Volume* slider adds or removes the point under the playhead; with points, moving the slider changes the point there.
- [ ] With *Vol* on, that track's clips don't move (so as not to move them by mistake): press *Vol* again to go back to moving them.
- [ ] Save, close and reopen: the points stay.
