# STLT

A live video player built around HAP for speed, that also plays everything else (H.264, HEVC, ProRes, DXV, DNxHD, VP9, PNG, JPEG, GIF, ...), with a workspace you arrange yourself.
It is an independent project, written from public specifications (HAP, FFGL, OpenGL) and from how a VJ setup is used on stage. It is not affiliated with Resolume or any other product and contains none of their code or artwork.

![The Grid: layers on the left, clips in columns on the right, monitors, composition, clip and performance panels below](../docs/images/stlt-grid.webp)

## What works now

- **HAP playback**: HAP, HAP Alpha, HAP Q, HAP Q Alpha, HAP Alpha-Only; HAP R and HAP HDR where the GPU driver has BPTC (not on macOS). HAP, HAP Alpha and HAP Q are tested with real clips; the other variants follow the specification but have not been tried with real files yet.
- **DXV 3** (Resolume's codec: Normal and High Quality, with or without alpha) read by STLT itself and sent to the GPU as compressed textures, as fast as HAP (see *Performance*).
- **Every other format** through FFmpeg: H.264, HEVC, ProRes, older DXV, DNxHD, VP8/VP9, MPEG-4, Motion JPEG and more, in .mov, .mp4, .mkv, .avi, .mxf and similar files, plus still images (PNG with transparency, JPEG, BMP, TIFF, WebP) and animated GIFs. These play fine but cost far more than HAP: see below. The Clip panel marks clips that are not HAP.
- **Native file dialogs**: *Composition → Open / Save As / Import Media*, *Load Media...* on a clip cell, *Browse...* in the Files panel, plugin folders and FFGL file parameters use the system's own dialogs. On Windows they run beside playback, so the output never stops while one is open.
- **Deck**: layers × columns of clips with thumbnails. Click a clip's picture to launch it; click its name to select it without launching: the Clip panel shows it and the **Preview Monitor** plays it, apart from the output. Click a column header to launch the column, drag clips between cells, drop files from the desktop or the Files panel.
- **Layer headers**, left of each row: **X** clears the layer; **B** blacks it out, **S** solos it, with the **blend mode** under them (Alpha, Add, Screen, Multiply, Lighten, Darken, Difference, Subtract, Luma Key, Luma Key Inverted); three faders, **M** (master: video and sound together), **A** (audio) and **V** (video), with the crossfader group **A**/**B** under them; the playing clip's picture and name; **S** the layer's speed; the playing clip's loop mode (Loop, Bounce, Play Once & Hold, Play Once & Clear) and timing (Timeline, BPM Sync); ◀ ❚❚ ▶ and its position; and **|◀ ▶|** for the previous and next clip. Click a header (or the layer's name) to select the layer. Right-click any fader or slider: back to its default. The headers stay in place while the deck scrolls sideways.
- **Composition** row above the layers: clear everything, black out the output, master, speed, every clip backwards / pause / forwards, previous and next column. **Crossfader** under the layers: layers in group A on the left, group B on the right, both in full in the middle.
- **Clips to** (toolbar): *Own Layer*, a clip plays on the layer of its row; *Selected Layer* ("layer based"), select a layer and any clip from any row plays there (its cell then shows which layer). A clip plays on one layer at a time.
- **Previous / next clip**: the |◀ ▶| arrows of a layer, the Left and Right arrow keys for the selected layer (Up and Down select the layer above or below), or mapped keys, MIDI and OSC (`/composition/layers/L/connectnextclip`, `/composition/selectedlayer/connectnextclip`, `connectprevclip`; `/composition/connectnextcolumn`).
- **Clips**: forward / reverse / pause, loop, bounce or play once, speed, opacity, fit (Fit, Fill, Stretch, Native), position, scale, rotation. The transport is a timeline: click or drag to move the playhead, drag the **in and out** triangles under it to play and loop only a part (right-click a triangle to reset it).
- **Trigger** (Clip panel), for clicks, keys, MIDI pads and OSC: **Normal** launches the clip; **Toggle** launches it, and the next press stops it; **Piano** plays it only while the mouse button, key or pad is held. **Ignore column** leaves the clip, and its layer, alone when its column is launched. The cell shows TOGGLE, PIANO or NO COL.
- **Cue points**: 8 per clip. Click an empty cue button to place it at the playhead, a set one to play from there (launching the clip if it is not playing). Delete a cue with the **×** on its button or a right-click, all of them with the × at the end of the row; on the timeline, drag a cue's number to move it, right-click it to delete it. Each cue maps to a key or a MIDI pad (Ctrl+K or Ctrl+M, click the cue button, press the key or the pad); OSC: `/composition/layers/L/clips/C/cue/N`, or `/composition/layers/L/cue/N` for whatever clip plays on the layer.
- **BPM Sync**: switch a clip's transport from *Timeline* to *BPM Sync* and it lasts a number of beats (between its in and out points) whatever the tempo: *Beats* with −, +, /2 and ×2.
- **Transitions** (Layer panel): the next clip launched on a layer takes over through a fade, a fade through black or a wipe (left, right, up, down), over seconds or beats; its sound crossfades too. Duration 0 is a cut. Mappable: `/composition/layers/L/transition/duration`.
- **Launch on the beat** (toolbar, *Launch*): clips and columns start now, on the next beat or on the next bar; a waiting cell pulses amber. RESYNC starts what waits.
- **Composition**: any size up to 16K, master, global speed, BPM with tap tempo.
- **Tempo from outside** (*Preferences → Sync*): **Ableton Link** (the LINK button next to the BPM; with a number, how many other apps share it) keeps the BPM and the bar in step with Ableton Live, DJ software and other VJ apps on the same network, and a BPM changed here changes for all of them. **MIDI clock** from a chosen input (a DJ mixer, a drum machine, another computer) leads the tempo while it ticks, with the beat placed by its Start and Song Position (CLOCK lights up in the toolbar; Link's other apps get its tempo); when it stops, the beat goes on at its last tempo. *Delay* takes the beat up to 200 ms ahead, for projectors and LED walls that show the picture late.
- **Sound**: clips with a soundtrack play it on the system's default output, in step with the picture: it follows loops, jumps and cue points, its pitch follows the speed (as on a turntable), and it is silent while a clip plays backwards or the composition is paused. Volume per clip, per layer and for the whole composition (all mappable to MIDI and OSC); bypassed layers, and layers another layer is soloed over, are silent. The Performance panel names the output in use.
- **Output** (*Output* menu, the output chip at the top right, or the Output panel): **Off**, **Window**, **Fullscreen** on a chosen display, or **Advanced** (the Advanced Output setup, below). The Advanced setup is kept while another mode is in use. Escape on a window or fullscreen output turns it off, so a fullscreen output on the interface's own display never traps you. Displays are told apart by name *and* place on the desktop, so two that share a name (on Windows most are "Generic PnP Monitor") are two choices, numbered as the system lists them, the main one marked. The editor opens on its own, without changing what is on show: *Advanced Output Settings...* (Ctrl+Shift+A). If every screen there is a Virtual Output nothing is shown, and the editor says so: pick the screen and choose its *Device* (a display or Window).
- **Advanced Output** (*Output → Advanced Output Settings...*, Ctrl+Shift+A): see below. Two setups, the tabs at the top: **Grid** for the Session view (the deck) and **Timeline** for the Arrangement view; the one of the view in use is on show, and either can be edited. The editor reads from top to bottom: every **screen** as a card with what it shows now (a green dot when it reaches a display, a window or the network; right-click: on/off, duplicate, delete; *+ Screen*), then the chosen screen's slices, polygons and masks on the left (right-click one: up, down, duplicate, delete), **Input** (*Takes from*: the slice's picture, the composition or, in the Timeline setup, a screen's own tracks) and **Output** (*Shows on*: the screen) side by side in the middle, or one of them large, and the settings on the right in sections that fold. Along the bottom: whether the setup is on show, and the saving: edits show live but are kept only when you press **Save**; **Discard Changes** goes back to the last saved setup. Closing the editor, or quitting, with unsaved changes asks whether to save them. *Presets...* at the top saves and loads whole setups and imports Resolume's.
- **Keyboard, MIDI and OSC shortcuts** (*Shortcuts* menu and panel): two separate maps. **Map Keys** (Ctrl+K) shows the controls in blue with their keys: click a control (a clip, a column, a layer's buttons, opacity or volume, master, speed, BPM, tap, any effect parameter), then press a key of the computer's keyboard (with Ctrl, Shift or Alt if you like). **Map MIDI** (Ctrl+M) shows them in green with their MIDI: click a control, then move a MIDI knob or pad. Escape leaves mapping mode; right-click a mapped control to forget its keys (or its MIDI, in the MIDI map); the Shortcuts panel lists them all; they are kept between sessions, whatever project is open. **Presets** (Shortcuts panel and menu) save the maps under a name and load them back, for another show or controller; *Import...* and *Export...* carry them as files to another computer. The panel says which preset is in use and when the maps have changed since. A key acts like a MIDI pad: buttons act on the press, switches flip, sliders go full while it is held. OSC arrives on a UDP port with addresses like these: `/composition/layers/1/clips/2/connect`, `/composition/columns/3/connect`, `/composition/layers/2/video/opacity 0.5`, `/composition/master`, `/composition/tempocontroller/tempo`, `/composition/tempocontroller/tempotap`, `/composition/layers/1/video/effects/1/params/amount` (values 0..1). In mapping mode, hovering a control shows its address.
- **Maps that follow the selection**: each map *acts on* its control **by position** (layer 2's opacity stays layer 2's), on the **selected layer** (the same control of whichever layer is selected when you use it: one pad for play/pause, opacity, clear or next clip of the layer you are on), or on the **selected clip** (the same control of the clip selected in the grid). *New maps act on* at the top of the Shortcuts panel chooses it for the maps you learn next; the *Acts on* column changes it for each map. Controls of the composition (master, BPM, columns...) always act on themselves. In mapping mode a control's label says *(sel. layer)* or *(sel. clip)*; a held key or note is let go on the layer it pressed, even if the selection moved in between.
- **Controller lights**: the pads and buttons mapped to MIDI light up on the controller: a clip in its cell, playing, or starting on the next beat; a column playing; a layer bypassed or solo; the deck shown; the TAP button blinking with the beat. On an **Akai APC mini mk2** the pads show STLT's blue for a clip ready, green for playing and pulsing green for starting, and the track and scene buttons light or blink; other controllers get a note on (127) for on. The lights go to the output of the input the mapping was learnt on (Windows' "MIDIOUT2 (...)" for "MIDIIN2 (...)"), and off when STLT closes; *Preferences → MIDI → Controller lights* turns them off.
- **Decks**: pages of clips over the same layers, as tabs above the deck; **+** adds one, double-click or right-click a tab to rename or remove it. Each deck has its own columns and clips; changing deck never stops what plays, and a dot on a tab shows a deck whose clips are playing while another is shown (click a layer's picture to go to its clip's deck). Mappable to keys and MIDI; OSC `/composition/decks/N/select`, `/composition/selectnextdeck`, `/composition/selectprevdeck`. Compositions saved before decks open as Deck 1.
- **Move** (*Move → Move Clips*): pick several clips (click them, press Ins over them, or drag a box from an empty cell; *Pick All Clips* in the menu), then drag one of them, or press the arrows, and they all move by the same number of rows and columns. Clips that play keep playing; a move that would leave the deck or land on another clip is refused. *Done* (or Escape) turns Move off and the clips stay where they are; Ctrl+Z undoes a move.
- **Preferences** (*Edit → Preferences*, Ctrl+,): **General** (reopen the last session, the view to start in), **Audio** (output device, buffer size), **Video** (hardware decoding, FFGL plugin folders), **MIDI** and **OSC**, and **Defaults** for new clips (size Fill / Fit / Stretch / Original, play mode, Timeline or BPM Sync, trigger style) and new layers (blend, transition). Defaults apply to clips added from then on; *Resize Every Clip Now* sets the size of those already there.
- **Clip menu**, for the selected clip: transport, play mode, trigger style, ignore column trigger, **Resize** (Fill, Fit, Stretch, Original); Cut / Copy / Paste (Ctrl+X / C / V), Copy / Paste Effects, Rename, Clear; and *Resize Every Clip* (every deck and the timeline).
- **Look**: a dark interface in a few steps of depth, one powder-blue accent for what is live or selected, text kept at 4.5:1 contrast or more, Inter for text and JetBrains Mono for figures that change (BPM, frame rate), both compiled into the program; sliders fill up to their value, so levels read at a glance from across a dark room.
- **Layers and Clips panels**: the layers' headers (with the composition's row on top and the crossfader under them) are a panel of their own, beside the Clips panel with the deck tabs, columns and cells; the two scroll up and down together, so each header stays beside its clips.
- **Workspace**: every panel can be docked, tabbed, split, or pulled out as its own window onto another monitor. Layouts are saved as named workspaces; the last layout and composition come back on the next start.
- **Even motion**: clips move by whole display refreshes (the frame clock locks to vsync and learns the display's true rate, 59.94 Hz, 144 Hz...), and each refresh shows the frame at its middle, so a 30 fps clip on a 60 Hz display shows every frame exactly twice instead of once or three times by the timer's jitter. HAP clips decode on two threads each, six frames ahead; a launch never decodes on the main thread (a cut keeps the outgoing clip until the new one's first frame is ready).
- **Performance panel**: frame time graph, the display's measured rate and whether the frame clock is locked to it, decode and upload time per layer, late frames.
- **Effects**: chains on every clip, layer and on the composition, each effect with bypass, dry/wet mix and reordering (drag the headers or use the arrows). Drag effects from the Effects panel onto a clip cell, a layer name, the Composition Monitor or any effects list.
- **Built-in effects**: Brightness & Contrast, Hue & Saturation, Invert, Colorize, Threshold, Mirror, Kaleidoscope, Tile, Pixelate, RGB Split, Blur, Edge Detect, Strobe (beat-synced), Trails.
- **Sources** (generated clips, at the composition size): Solid Color, Gradient, Lines, Checker, and FFGL source plugins. Drop one on an empty cell.
- **ISF shaders** (Interactive Shader Format, isf.video, as in VDMX, MadMapper and Millumin): about 200 effects and sources come with STLT (VIDVOX's own, MIT licence: blurs, colour, glitch, halftones, distortion, kaleidoscopes, feedback, masks, patterns, gradients and more), in the Effects panel under *ISF* by category. Any other ISF shader works too: drop its `.fs` (and `.vs`) into the `isf` folder of the settings folder, the system's ISF folder or a folder of Effects → Plugin Folders. Multi-pass shaders, persistent and float buffers, their own vertex stage, and colour, point, menu, button and switch inputs are supported; shaders that need image files of their own or the sound as a texture are listed as skipped. `STLT --check-isf [FOLDER]` builds and runs every shader found and tells which fail.
- **FFGL 2 host**: loads `.dll` (Windows) and `.bundle` (macOS) effect and source plugins from the app's plugin folder and any folder you add (Effects → Plugin Folders). Parameters show as sliders, checkboxes, buttons, menus, text and colour pickers, grouped as the plugin asks; menus, names and visibility that change live (FFGL 2.2/2.3) update in the panel. Plugins get the BPM, bar phase and time. STLT-CHASER, STLT-WIPER and STLT-OUTLINER run here as they do in Resolume.
- **Animated parameters**: click a parameter's name → Animate: saw, ping-pong, sine, square, random or smooth random, between two values, over 1/4 to 64 beats or over a period in seconds (0.05 s to 10 minutes, free of the tempo). Works on effect parameters, a clip's opacity, position, scale and rotation, and a layer's opacity; a moving setting's name lights up. RESYNC restarts the beat.
- **Parameters that follow the sound**: Animate → *Audio Level*, *Bass* (20-200 Hz), *Mid* (200-2000 Hz) or *Treble* (2-12 kHz) moves the parameter between its two values with the sound coming in (a microphone, a line in, the mixer's feed), measured as a VU meter does (-60 to 0 dB, falling back in a third of a second). Choose the input and its gain in *Preferences → Audio → Audio Input*, where meters show what comes in; the input is opened only while something listens.

## Arrangement view: the timeline

As in Ableton Live, two views: **Session** (the three upright lines at the top right: the deck of clips to launch) and **Arrangement** (the three lying lines: a timeline for shows that are set in time, such as a concert played to a click). Tab switches; the output shows the view in use, and each view keeps its own layout of panels. In Arrangement the Clips panel stays at hand, so the deck's clips can be dragged onto the timeline.

- **Tracks**, laid out as in an editing program: the video tracks on top (each with opacity, blend mode, mute and solo; the upper ones drawn over the lower ones), the audio tracks under them in green (volume, mute, solo). *+ Video Track*, *+ Audio Track*. **Double-click** a track's header to rename it; **drag** a header onto another track of the same kind to move it there (or right-click: *Move Up*, *Move Down*); right-click for its blend or to delete it. Drag the headers' right edge to make them wider.
- **Clips**: drag clips from the Clips panel, or files from Files or the desktop, onto a track. A clip starts where it is dropped (on the beat or bar with *Snap*; Shift places it freely) and lasts as long as its media (stills and sources: four bars). Drag a clip to move it (to another track too), Alt-drag to copy it; drag its left edge to trim its start (the media's in point follows), its right edge to set its length. Longer than its media, it loops it or holds the last frame (*Past end*). Right-click: *Duplicate* (Ctrl+D), *Split at Playhead* (Ctrl+E), *Fit to Media*, *Delete* (Del). A clip placed over others overwrites them, as in an editor. The inspector on the right sets name, start, length, media in point, speed, opacity and volume exactly. Movies with sound play it; audio tracks take sound files (WAV, MP3, the sound of a movie...).
- **A clip's own settings** (the inspector at the right of the timeline, MadMapper-style; the deck's Clip and Layer panels rest in this view): blend mode, fit, opacity, scale, position, rotation and volume, and its **effects** (drop them from the Effects panel onto the clip; each has a *Mix* for how much of it shows). The deck's tempo, launch and speed controls leave the top bar too.
- **Keyframes**: the diamond beside a property sets a keyframe at the playhead; move the playhead and change the value for the next one: in between the value moves in a straight line (a fade in, a slow zoom, a pan, a fade of the music...). With keyframes, changing a value sets the keyframe at the playhead. They show as diamonds along the clip (a click puts the playhead there); *Previous* / *Next* jump between them, *Clear All* removes them. Splitting, trimming and overwriting keep them at their moments in time.
- **Tracks on different screens**: under each video track's name, *→ All Screens* says where its picture goes; click it to send the track to **one screen** instead (the screens of the Advanced Output's *Timeline* setup): track 1 to screen 2, track 3 to screen 3... A screen that gets tracks shows them, mixed at its own size, and its slices cut up that picture; a screen with none shows the composition (the tracks set to *All Screens*). The same choice is in the Advanced Output: each screen's card lists the tracks it shows, and its settings have a tick box per track. A slice can also take from another screen's tracks (*Takes from*), for a wide LED wall split over two processors.
- **Monitor**: the Composition Monitor's buttons, as on Modul8, choose what it shows: **Composition**, **Multiview** (every screen of the output side by side, named; the composition when there is one screen; double-click a screen to see it alone), **Screen** (one of them) or **Layer** / **Track** (one deck layer or timeline track alone, with its effects, even when muted).
- **Volume line on audio tracks**: *Vol* in an audio track's header opens a line over the track with the volume along the timeline: double-click the track to add a point, drag a point (Shift: off the grid), right-click it for its curve (Linear, Ease In, Ease Out, Ease In Out, Hold) or to delete it. The points stay at their moments when clips move. With points, the track's slider shows the volume at the playhead and an edit sets the point there; the diamond beside it adds or removes the point at the playhead. While the line is open the track's clips stay put; close it with *Vol* again.
- **Following a timecode** (the **TC** button in the timeline's bar; the source in *Preferences → Sync → Timecode*): the playhead goes where the show's timecode says, plays and stops with it, and jumps when it jumps. **LTC** (SMPTE on a sound card's input, any channel), **MTC** (from a DAW or a console over MIDI; on one computer, through a virtual MIDI port) or **Art-Net** timecode (from the lighting desk's network, UDP 6454). The field next to TC is the timecode at the timeline's start (shows often begin at 01:00:00:00); *Take* makes the timecode coming in land where the playhead is. 24, 25, 29.97 drop frame and 30 fps are recognised by themselves; *Freewheel* keeps the timeline running through a short dropout. Pioneer CDJs reach STLT through Pioneer's PRO DJ LINK Bridge as Ableton Link. See [docs/TIMECODE.md](../docs/TIMECODE.md).
- **Ruler and tempo**: bars and beats from the show's BPM and metre (2/4 to 12/4), and the timecode (HH:MM:SS:FF at 24, 25, 29.97, 30, 50 or 60 fps). Click or drag the ruler to move the playhead; Ctrl + wheel zooms, the wheel scrolls; *Follow* keeps the playhead in view.
- **Blocks and cues**: blocks are the songs. Drag on the block lane (or *+ Block*) to make one; drag it to move it, its edges to resize it; right-click or the inspector for its name, colour and **end action**: **Next** (plays on into what follows), **Loop** (starts the block again until another cue), **Hold** (stops on the last frame), **Black** (stops, and the output goes black). Every block is a **cue**: the cue buttons above the tracks (and double-click on a block) start it at once; Left and Right go to the previous and next cue. Cues, play and stop map to keys and MIDI like anything else; OSC `/timeline/cue/N`, `/timeline/nextcue`, `/timeline/prevcue`, `/timeline/play`, `/timeline/stop`.
- **Transport**: Space plays and pauses; Stop stops, and a second Stop goes back to the start. The playhead runs whichever view is shown.
- Saved with the composition; Ctrl+Z undoes timeline edits too.

## Advanced Output

![The Advanced Output in the Grid setup: a list of slices on the left, eight warped slices on the canvas, the slice settings on the right](../docs/images/stlt-output-grid.webp)

- **Screens**: as many as you need, each fullscreen on a display (chosen by name, so it finds the projector again after a restart), in a window, or virtual. Per screen: resolution, opacity, brightness, contrast, red/green/blue, and a **test pattern** that draws every slice as a coloured grid with its frame and centre, for lining up projectors.
- **Slices**: a rectangle of the composition or of a screen's tracks (*Input*) placed on the screen (*Output*). **Polygons** take any shape of the composition and pin each point anywhere on the screen: *+ Polygon → Draw...* to click the points one by one (click the first point, double-click or Enter to close), or start from a triangle, rectangle, pentagon, hexagon, octagon or circle; double-click the outline (or right-click it → *Add Point Here*) to add points later, Del to remove one.
- **Warping**: *Perspective* (four corners, for keystone), *Grid* (any number of points joined by straight lines), *Curved Grid* (the same points joined by smooth curves, for bending onto curved or uneven surfaces) or *Contour* (below). Switching keeps the current shape; grids can be refined or flattened.
- **Contour** (*Edit Points* on the output): the slice's outline is a chain of points you fit to the real surface, and the picture is stretched to fill it. Four corners alone give exactly the perspective warp; add points anywhere along the sides (double-click the outline, or right-click → *Add Point Here*) and drag them onto the edges of the screen, the arch, the column. *Point Mode* in the slice settings joins them with **Bézier** curves or **Linear** segments. In Bézier mode the selected point shows its two handles and its neighbours' facing ones: handles in line stay in line (Alt moves one alone), Alt-drag on a point pulls out fresh handles, double-click a point to switch it between smooth and straight, *Smooth All* rounds the whole outline, *Reset Contour* goes back to the four corners. Corners can't be deleted; other points go with Delete or right-click → *Delete Point*.
- **Edit Points / Transform** (slice settings, on the output): *Edit Points* drags single points; *Transform* puts a box around the slice to scale it from a side or corner (Alt: from the centre, Shift: keep proportions) and a knob above it to rotate it (Shift: 15° steps), for any warp or polygon.
- **Masks** (*+ Mask*: rectangle or ellipse): shapes that black out what lies below them in the list, or everything outside them when inverted, with a feather for a soft edge and a strength. Their points are edited like a contour (Bézier or linear, add, delete, smooth, Transform), so a mask can follow a doorway, a column or the edge of a screen.
- **Sharing**: each screen can also go to other programs: on this computer through **Spout** (Windows) or **Syphon** (macOS), the GPU texture passed on without a copy, and over the network as an **NDI** source (any system; it needs the free NDI runtime, installed with [NDI Tools](https://ndi.video/tools/)). Sources are called "STLT *screen name*"; the screen settings show the receivers.
- **Soft edge** on each side, with power, luminance and gamma, to blend overlapping projectors.
- **Editing**: drag a slice to move it, its points to shape it; polygons get new points by double-clicking their outline too (on either tab, the other side gets the matching point); rectangles stay rectangles on the input (Alt frees a corner); Shift locks an axis; handles snap to the canvas edges and centre and to the other slices (Ctrl to drop the snap); arrows nudge the selected point, or the whole slice when none is selected, by 1 pixel (Shift: 10); the selected point's exact position is in the slice settings; wheel zooms, middle button or Space pans; right-click a slice for Match Output to Input, Fit to Screen, flips. Flip, opacity, exact input position and size are in the slice settings.
- **Presets** of the whole setup, and **import from Resolume**: *Import from Resolume* reads Arena's or Avenue's `AdvancedOutput.xml` straight from Documents, or pick any file. Screens, slices (input and output rectangles), polygons and masks come across, with the slices' warp grids (linear as *Grid*, Bézier as *Curved Grid*) and soft edges. Resolume does not document this file: the importer reads the variants it knows and keeps the plain output rectangle for anything else, so check warped slices after an import (and send us the file if one comes out wrong).
- The setup is saved on exit and restored on start, independently of the composition.

![The Advanced Output in the Timeline setup: three screens as cards with the tracks each one receives, the selected slice taking from all the screens](../docs/images/stlt-output-timeline.webp)

Rendering: each slice is a fine triangle mesh that follows the warp, so perspective and curves stay smooth; the composition is sampled once per screen pixel, soft edges are computed per pixel, and each screen gets its own vsynced window.

## STLT-CHASER

STLT-CHASER comes with STLT: the same editor and the same three effects as for Resolume, linked to STLT.

- **Output → Chaser...** (under *Advanced Output Settings...*) opens the STLT-CHASER editor on STLT's Advanced Output: its slices, kept up to date as you change them in STLT. Draw the sequences there, slice by slice, with the Factory, views and envelopes (see the [STLT-CHASER guide](https://github.com/alfonsostillet/stlt-chaser)). Everything saves as you go and reaches the effects within half a second.
- **The effects** STLT-CHASER, STLT-WIPER and STLT-OUTLINER are in the Effects panel: drag one onto the composition, a layer or a clip, pick the *Sequence*, and animate *Step* (right-click it for BPM sync).
- The release folder holds them: the effects in `plugins`, the editor in `chaser`. STLT loads its own copies first, before any installed for Resolume.
- The sequences live in STLT's settings folder (`chaser`), apart from a Resolume setup's; *File → Load* in the editor opens one made for Resolume and moves it onto STLT's slices by name.

## Performance: HAP or not

HAP is the format to use for a show. Other formats work, but every one of them costs more, in several ways:

| 1920x1080, same content, 4-core CPU | HAP | HAP Q | H.264 |
| --- | --- | --- | --- |
| Decoding, frames in order | **1.0 ms** | **1.4 ms** | 6.8 ms, on all four cores |
| Decoding, jumping to random frames (scrubbing, cue points, reverse) | **1.1 ms** | **1.3 ms** | 201 ms |
| Data sent to the GPU per frame | 1.0 MB | 2.1 MB | 3.1 MB |
| CPU left for other layers | nearly all | nearly all | little |

Measured with `decodebench` on clips made from the same 4-second source; your numbers depend on the machine and the encoding, the proportions do not. What it means on stage:

- **H.264, HEVC and other long-GOP codecs** only store a full picture every second or so. Playing forwards is fine for a layer or two, but jumping in a clip, scrubbing, playing backwards or bouncing has to decode from the previous full picture every time: slow, and visible as stutter. They also use most of the CPU, so several of them at once compete with each other.
- **Scratching and jumping H.264 and the like**: STLT softens that cost. It knows where the full pictures are (from the file's index) and decodes on instead of starting over when no full picture lies between; it keeps the frames it decoded recently, and the ones just before a jump's target, in memory (*Preferences → Video → Scratch memory per clip*, 256 MB by default), so going back over them is instant; and while you scratch it drops the frames your hand has left behind and shows the nearest picture it has (the full picture, or how far it got), the exact frame following as soon as you stop. Scratching a 1080p H.264 clip with a full picture every 8 seconds, the picture stays about 2 frames behind the hand and changes about 90 times a second, against 24 frames behind and 6 times a second before (`decodebench clip.mp4 --scratch`). The first jump into an unseen part still costs the decoding from its full picture.
- **Hardware decoding**: H.264, HEVC, VP9, AV1, MPEG-2/4 (and ProRes on macOS) go to the computer's video decoder when there is one (VideoToolbox on macOS, D3D11 or DXVA2 on Windows, VA-API or VDPAU on Linux), which frees the CPU for more layers; the Clip panel says which decoder a clip uses, and the Performance panel can turn it off. It does not change the jumping cost: the hardware still decodes from the previous full picture.
- **ProRes, DNxHD and Motion JPEG** decode any frame on its own, so jumps are fine, but they are heavy on the CPU and large on disk.
- **DXV** (Resolume's codec, version 3: Normal and High Quality, with or without alpha) is read by STLT itself and goes to the GPU as compressed textures, like HAP: any frame in about a millisecond, in any order (1080p, this machine: Normal 0.5 ms, High Quality 1.5 ms, Normal with alpha 1.1 ms, High Quality with alpha 3.6 ms, against 16 to 25 ms a jump through FFmpeg before). High Quality's YCoCg colour and the premultiplied alpha are converted in the shader, so the colours match the source (FFmpeg's conversion got High Quality's colours and the alpha wrong). Older DXV files still play through FFmpeg.
- **Still images** cost nothing after loading: they are decoded once.

Convert anything you play live to HAP (commands below). Everything else is fine for quick previews, tests and one-off clips.

## Why HAP plays fast here

HAP frames are GPU textures (DXT1/DXT5/BC7/BC6H) compressed once more with Snappy. The player never turns a frame into pixels on the CPU:

1. A decoder thread per playing clip reads the compressed frame with a positional read (the OS read-ahead keeps the disk streaming).
2. The frame's Snappy chunks are decompressed in parallel on a shared thread pool, **directly into a mapped OpenGL pixel buffer**: no intermediate copy.
3. The render thread unmaps the buffer and `glCompressedTexSubImage2D` hands it to the GPU by DMA, then orphans the buffer so it never waits for the upload.
4. The decoder runs up to three frames ahead in the play direction, so a slow frame does not cause a drop; a seek or a trigger decodes the new frame right away.
5. HAP Q's YCoCg and HAP Q Alpha's separate alpha plane are converted in the shader.

Encode clips with several chunks so step 2 can use every core (`-chunks 4` to `-chunks 16` in FFmpeg; Alley and AfterCodecs have the same option). Measured on 4 cores: a 4K HAP frame with 16 chunks decodes in 1.7 ms instead of 4.0 ms on one thread.

Other formats use the same pipeline from FFmpeg's output: YUV pictures go to the GPU as three planes and are converted to RGB in the shader (BT.601 or BT.709, video or full range, as the file says); only formats that are not planar YUV are converted on the CPU.

## Converting clips to HAP

```
ffmpeg -i input.mp4 -c:v hap -format hap_q -chunks 8 output.mov        # HAP Q: best quality
ffmpeg -i input.mp4 -c:v hap -format hap -chunks 8 output.mov          # HAP: smallest
ffmpeg -i input.mov -c:v hap -format hap_alpha -chunks 8 output.mov    # HAP Alpha: keeps transparency
```

Width and height must be multiples of 4.

## Shortcuts

| Key | Action |
| --- | --- |
| Tab | Session / Arrangement view |
| Space | Session: pause / resume everything. Arrangement: play / pause the timeline |
| Left / Right | Session: previous / next clip on the selected layer. Arrangement: previous / next cue (not while the Advanced Output editor is open) |
| Del / Ctrl+D / Ctrl+E | Timeline: delete, duplicate, split at the playhead |
| Up / Down | Select the layer above / below |
| Ctrl+S / Ctrl+O | Save / open composition |
| Ctrl+I | Import media into the selected layer |
| Ctrl+Shift+A | Advanced Output editor |
| Ctrl+K | Map keys on or off: blue (Escape leaves it) |
| Ctrl+M | Map MIDI on or off: green (Escape leaves it) |
| Ins / arrows (Move mode) | Pick the clip under the mouse / move the picked clips by one cell |
| Escape on an output | Turn the output off |
| Ctrl+Z / Ctrl+Shift+Z (or Ctrl+Y) | Undo / redo: clips, layers, effects, settings and the whole Advanced Output (also in the *Edit* menu) |
| Right-click or double-click a slider | Back to its default (opacity and volume 100 %, speed 1x, position 0...) |
| Right-click a clip or column | Clip and column options |

## Building

CMake 3.20+ and a C++17 compiler (Visual Studio 2022, Xcode command line tools, or GCC/Clang with the X11 and GTK 3 development packages on Linux). GLFW, Dear ImGui, Snappy, HAP and Native File Dialog Extended are downloaded by CMake.

FFmpeg, for everything that is not HAP:
- **Windows**: CMake downloads a prebuilt FFmpeg 7.1 and copies its DLLs next to the executable.
- **macOS**: build a small, decode-only static FFmpeg once with `app/scripts/build-ffmpeg.sh ~/ffmpeg` and add `-DFFMPEG_ROOT=$HOME/ffmpeg` below.
- **Linux**: the system packages (`libavformat-dev libavcodec-dev libswscale-dev libavutil-dev`).
- `-DSTLTVJ_WITH_FFMPEG=OFF` builds a HAP-only app.

```
cmake -S app -B app/build -DCMAKE_BUILD_TYPE=Release
cmake --build app/build --config Release
ctest --test-dir app/build -C Release          # media and effect tests
app/build/decodebench clip.mov --random        # decode speed of any clip, no GPU needed
app/build/decodebench clip.mp4 --scratch       # how far behind a scratching hand the picture stays
```

STLT-CHASER is the `chaser` submodule (clone with `--recursive`, or `git submodule update --init --recursive`). Its effects build with `cmake -S chaser/plugin -B chaser/plugin/build` (Linux also needs `libglew-dev`) and go into `plugins/` next to the program (`STLT.exe`, `stlt` on Linux); its editor (Node 22) with `npm ci` and `npx electron-builder --dir` in `chaser/editor`, and goes into `chaser/` next to the program (`STLT.exe`, `stlt` on Linux). `STLTVJ_CHASER_EDITOR` points *Output → Chaser* at another editor executable.

Command line options: `--fresh` (start empty and save nothing), `--clip LAYER,COLUMN,PATH` (load and launch a clip), `--source LAYER,COLUMN,EFFECT-ID` (a generated clip), `--fx comp|layer:L|clip:L,C=EFFECT-ID` (add an effect), `--launch LAYER,COLUMN` (play a clip of the restored session), `--list-effects` (print effect ids and plugins that did not load), `--output-setup FILE.json|AdvancedOutput.xml`, `--advanced-output input|output` (open the editor), `--size WxH`, `--capture FILE.bmp --frames N` (screenshot after N frames, then quit; with `--capture-composition`, the composition at its own size instead of the window).

FFGL plugins are loaded from `plugins/` next to the program (STLT-CHASER's effects), `plugins/` in the settings folder and the folders listed in `plugin-folders.txt`. Only FFGL 2 effects and sources are loaded; mixers and multi-input effects are listed as skipped. A composition that uses a plugin this computer does not have keeps the plugin's settings and shows it as missing.

Settings live in `%APPDATA%\STLT`, `~/Library/Application Support/STLT` or `~/.config/stlt` (the first start copies over what STLT kept in its folder): `layout.ini` (current panel layout), `workspaces/` and `last-session.stltvj`.

## Code map

| Folder | What |
| --- | --- |
| `src/media` | QuickTime/MP4 demuxer, HAP frame decoding, thread pool, positional file reads. No OpenGL: tested headless. |
| `src/render` | Clip player (decode thread + pixel buffers + textures), GPU compositor with effect chains, output window. |
| `src/output` | Advanced output: screens, slices, warps, contours, masks, soft edges, Resolume import, and the renderer with its output windows. |
| `src/fx` | Effect parameters and animations (beats, seconds, sound), the ISF host, built-in GLSL effects and sources, the FFGL 2 host, the effect registry. |
| `src/ui` | Panels, theme and fonts, widgets (filled sliders), workspaces. |
| `src/App.*` | Main loop and the actions panels share (load, launch, move, save, undo). |
| `src/audio` | Clip sound: voices that follow their clip's playhead, and the mix on the audio device (miniaudio); the sound coming in, measured in bands for parameters to follow. |
| `src/control` | Remote control: the OSC/MIDI address space, OSC parsing, MIDI messages and mappings, the UDP and MIDI inputs, the controllers' lights; the tempo from Ableton Link or a MIDI clock; timecode (LTC, MTC, Art-Net). |
| `src/History.*` | Undo and redo by snapshots of the composition and the output setup. |

## Next milestones

1. FFGL mixers as layer transitions, audio/FFT input for plugins that ask for it, clip parameter animation (opacity, transform).
