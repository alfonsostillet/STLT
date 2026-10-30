# STLT-VJ

A live video player built around HAP for speed, that also plays everything else (H.264, HEVC, ProRes, DXV, DNxHD, VP9, PNG, JPEG, GIF, ...), with a workspace you arrange yourself.
It is an independent project, written from public specifications (HAP, FFGL, OpenGL) and from how a VJ setup is used on stage. It is not affiliated with Resolume or any other product and contains none of their code or artwork.

![STLT-VJ: the clip deck on top, monitors, composition, clip and performance panels below](../docs/images/stlt-vj.png)

> 🇮🇹 In breve: STLT-VJ riproduce clip su più layer con pannelli agganciabili e flottanti. Legge HAP (il formato più veloce) e anche H.264, ProRes, DXV, PNG, JPEG e gli altri formati comuni, ma **con HAP le prestazioni sono nettamente migliori** (H.264 e HEVC usano la decodifica hardware della scheda video quando c'è, ma restano più lenti nei salti): decodifica 5 volte più veloce, salti istantanei a qualsiasi frame e quasi niente CPU (vedi *Performance: HAP or not*). Trascina i file sulla griglia, dal pannello Files o con *Composition → Import Media...* (selettore file del sistema), clicca l'immagine di una clip per lanciarla e il suo nome per vederla nel Preview Monitor senza lanciarla; clicca l'intestazione di una colonna per lanciare tutta la colonna. Ogni layer ha nella sua scheda blend mode (Add, Screen...), volume e opacità. Dal pannello *Effects* trascina un effetto su una clip, sull'intestazione di un layer o sul Composition Monitor; trascina una *Source* su una cella vuota. I plugin FFGL (anche STLT-CHASER) vengono caricati dalla cartella *Extra Effects* di Resolume. Click destro su un parametro per animarlo a tempo di BPM. Il menu *Output* (o il pulsante in alto a destra) sceglie dove va la composizione, come in Resolume: Off, Window, Fullscreen su un display, oppure Advanced; Esc sull'uscita la spegne. *Output → Advanced...* (Ctrl+Shift+A) apre l'editor di mappatura (le modifiche si vedono subito ma restano solo premendo *Save*; *Discard Changes* torna all'ultima versione salvata; se tutti gli screen sono *Virtual Output* scegli il *Device* dello screen): più screen (display, finestra o virtuali), slice e poligoni con selezione dell'input, warp a 4 punti, griglia lineare o curva per superfici piegate, *Contour* per adattare il contorno dello slice alla superficie reale punto per punto (Edit Points, con Point Mode Bezier o Linear a destra, doppio clic sul contorno per aggiungere un punto) o tutto insieme (Transform: scala e ruota), soft edge per la fusione tra proiettori, maschere (rettangolo o ellisse, anche invertite e sfumate), test pattern, uscite Spout (Windows), Syphon (Mac) e NDI in rete, preset e import dell'`AdvancedOutput.xml` di Resolume (anche warp, soft edge e maschere). *Workspace → Save Workspace As...* salva la disposizione dei pannelli. Ctrl+Z / Ctrl+Shift+Z annullano e ripetono qualsiasi modifica (anche nell'Advanced Output). Ogni layer ha la sua testata come in Resolume (X, B, S, blend con Luma, fader M/A/V, gruppo del crossfader, velocità, loop, trasporto, frecce per clip precedente e successiva); con *Clips to → Selected Layer* qualsiasi clip parte nel layer selezionato. Le frecce ← → della tastiera lanciano la clip precedente e successiva del layer selezionato, ↑ ↓ cambiano layer. Ogni clip ha punti di in e out (i triangoli sotto la timeline), 8 cue point mappabili su tasti o MIDI (la × sul pulsante o il tasto destro li cancella) e il BPM Sync (dura N battute); ogni layer ha le transizioni (dissolvenza, nero, tendine) in secondi o battute; *Launch* nella barra fa partire le clip al prossimo beat o battuta; opacità, posizione, scala e rotazione si animano a tempo cliccando il loro nome. Le clip con audio suonano a tempo con l'immagine, con volume per clip, layer e generale. Dal menu e dal pannello *Shortcuts*, due mappe separate: Ctrl+K (tasti, in blu) o Ctrl+M (MIDI, in verde), clicca un controllo e premi un tasto o muovi una manopola MIDI; le mappe restano caricate per tutti i progetti e nella sezione *Presets* si salvano con un nome, si ricaricano, si esportano e importano come file. I layer hanno il loro pannello (*Layers*), accanto al pannello *Clips* con deck e celle: scorrono insieme. In *Fullscreen* i monitor con lo stesso nome (su Windows spesso "Generic PnP Monitor") sono distinti per numero e posizione, quello principale è segnato *(main)*. Nel pannello Clip, *Trigger* sceglie come risponde la clip: Normal (parte), Toggle (parte, e alla pressione dopo si ferma) o Piano (suona solo finché tieni premuto mouse, tasto o pad); *Ignore column* la esclude dal lancio della colonna. I *deck* (schede sopra la griglia, + per aggiungerne) sono pagine di clip sugli stessi layer: cambiando deck quello che suona continua a suonare. Il menu *Move* sposta più clip insieme (clic, Ins o rettangolo per sceglierle, poi trascinale o usa le frecce); OSC su porta UDP con indirizzi come quelli di Resolume.

## What works now

- **HAP playback**: HAP, HAP Alpha, HAP Q, HAP Q Alpha, HAP Alpha-Only; HAP R and HAP HDR where the GPU driver has BPTC (not on macOS). HAP, HAP Alpha and HAP Q are tested with real clips; the other variants follow the specification but have not been tried with real files yet.
- **Every other format** through FFmpeg: H.264, HEVC, ProRes, DXV, DNxHD, VP8/VP9, MPEG-4, Motion JPEG and more, in .mov, .mp4, .mkv, .avi, .mxf and similar files, plus still images (PNG with transparency, JPEG, BMP, TIFF, WebP) and animated GIFs. These play fine but cost far more than HAP: see below. The Clip panel marks clips that are not HAP.
- **Native file dialogs**: *Composition → Open / Save As / Import Media*, *Load Media...* on a clip cell, *Browse...* in the Files panel, plugin folders and FFGL file parameters use the system's own dialogs. On Windows they run beside playback, so the output never stops while one is open.
- **Deck**: layers × columns of clips with thumbnails. Click a clip's picture to launch it; click its name to select it without launching: the Clip panel shows it and the **Preview Monitor** plays it, apart from the output. Click a column header to launch the column, drag clips between cells, drop files from the desktop or the Files panel.
- **Layer headers**, laid out as in Resolume, left of each row: **X** clears the layer; **B** blacks it out, **S** solos it, with the **blend mode** under them (Alpha, Add, Screen, Multiply, Lighten, Darken, Difference, Subtract, Luma Key, Luma Key Inverted); three faders, **M** (master: video and sound together), **A** (audio) and **V** (video), with the crossfader group **A**/**B** under them; the playing clip's picture and name; **S** the layer's speed; the playing clip's loop mode (Loop, Bounce, Play Once & Hold, Play Once & Clear) and timing (Timeline, BPM Sync); ◀ ❚❚ ▶ and its position; and **|◀ ▶|** for the previous and next clip. Click a header (or the layer's name) to select the layer. Right-click any fader or slider: back to its default. The headers stay in place while the deck scrolls sideways.
- **Composition** row above the layers: clear everything, black out the output, master, speed, every clip backwards / pause / forwards, previous and next column. **Crossfader** under the layers: layers in group A on the left, group B on the right, both in full in the middle.
- **Clips to** (toolbar): *Own Layer*, a clip plays on the layer of its row; *Selected Layer* ("layer based"), select a layer and any clip from any row plays there (its cell then shows which layer). A clip plays on one layer at a time.
- **Previous / next clip**: the |◀ ▶| arrows of a layer, the Left and Right arrow keys for the selected layer (Up and Down select the layer above or below), or mapped keys, MIDI and OSC (`/composition/layers/L/connectnextclip`, `/composition/selectedlayer/connectnextclip`, `connectprevclip`; `/composition/connectnextcolumn`).
- **Clips**: forward / reverse / pause, loop, bounce or play once, speed, opacity, fit (Fit, Fill, Stretch, Native), position, scale, rotation. The transport is a timeline as in Resolume: click or drag to move the playhead, drag the **in and out** triangles under it to play and loop only a part (right-click a triangle to reset it).
- **Trigger** (Clip panel), as in Resolume, for clicks, keys, MIDI pads and OSC: **Normal** launches the clip; **Toggle** launches it, and the next press stops it; **Piano** plays it only while the mouse button, key or pad is held. **Ignore column** leaves the clip, and its layer, alone when its column is launched. The cell shows TOGGLE, PIANO or NO COL.
- **Cue points**: 8 per clip. Click an empty cue button to place it at the playhead, a set one to play from there (launching the clip if it is not playing). Delete a cue with the **×** on its button or a right-click, all of them with the × at the end of the row; on the timeline, drag a cue's number to move it, right-click it to delete it. Each cue maps to a key or a MIDI pad (Ctrl+K or Ctrl+M, click the cue button, press the key or the pad); OSC: `/composition/layers/L/clips/C/cue/N`, or `/composition/layers/L/cue/N` for whatever clip plays on the layer.
- **BPM Sync**: switch a clip's transport from *Timeline* to *BPM Sync* and it lasts a number of beats (between its in and out points) whatever the tempo: *Beats* with −, +, /2 and ×2.
- **Transitions** (Layer panel): the next clip launched on a layer takes over through a fade, a fade through black or a wipe (left, right, up, down), over seconds or beats; its sound crossfades too. Duration 0 is a cut. Mappable: `/composition/layers/L/transition/duration`.
- **Launch on the beat** (toolbar, *Launch*): clips and columns start now, on the next beat or on the next bar; a waiting cell pulses amber. RESYNC starts what waits.
- **Composition**: any size up to 16K, master, global speed, BPM with tap tempo.
- **Sound**: clips with a soundtrack play it on the system's default output, in step with the picture: it follows loops, jumps and cue points, its pitch follows the speed (as on a turntable), and it is silent while a clip plays backwards or the composition is paused. Volume per clip, per layer and for the whole composition (all mappable to MIDI and OSC); bypassed layers, and layers another layer is soloed over, are silent. The Performance panel names the output in use.
- **Output** (*Output* menu, the output chip at the top right, or the Output panel), as in Resolume: **Off**, **Window**, **Fullscreen** on a chosen display, or **Advanced** (the Advanced Output setup, below). The Advanced setup is kept while another mode is in use. Escape on a window or fullscreen output turns it off, so a fullscreen output on the interface's own display never traps you. Displays are told apart by name *and* place on the desktop, so two that share a name (on Windows most are "Generic PnP Monitor") are two choices, numbered as the system lists them, the main one marked. Choosing *Advanced...* also opens the Advanced Output editor; if every screen there is a Virtual Output nothing is shown, and the editor says so: pick the screen and choose its *Device* (a display or Window).
- **Advanced Output** (*Output → Advanced...*, Ctrl+Shift+A): see below. Edits show live but are kept only when you press **Save** at the top of the editor; **Discard Changes** goes back to the last saved setup. Closing the editor, or quitting, with unsaved changes asks whether to save them.
- **Keyboard, MIDI and OSC shortcuts** (*Shortcuts* menu and panel): two separate maps, as in Resolume. **Map Keys** (Ctrl+K) shows the controls in blue with their keys: click a control (a clip, a column, a layer's buttons, opacity or volume, master, speed, BPM, tap, any effect parameter), then press a key of the computer's keyboard (with Ctrl, Shift or Alt if you like). **Map MIDI** (Ctrl+M) shows them in green with their MIDI: click a control, then move a MIDI knob or pad. Escape leaves mapping mode; right-click a mapped control to forget its keys (or its MIDI, in the MIDI map); the Shortcuts panel lists them all; they are kept between sessions, whatever project is open. **Presets** (Shortcuts panel and menu) save the maps under a name and load them back, for another show or controller; *Import...* and *Export...* carry them as files to another computer. The panel says which preset is in use and when the maps have changed since. A key acts like a MIDI pad: buttons act on the press, switches flip, sliders go full while it is held. OSC arrives on a UDP port with addresses modelled on Resolume's: `/composition/layers/1/clips/2/connect`, `/composition/columns/3/connect`, `/composition/layers/2/video/opacity 0.5`, `/composition/master`, `/composition/tempocontroller/tempo`, `/composition/tempocontroller/tempotap`, `/composition/layers/1/video/effects/1/params/amount` (values 0..1). In mapping mode, hovering a control shows its address.
- **Decks**, as in Resolume: pages of clips over the same layers, as tabs above the deck; **+** adds one, double-click or right-click a tab to rename or remove it. Each deck has its own columns and clips; changing deck never stops what plays, and a dot on a tab shows a deck whose clips are playing while another is shown (click a layer's picture to go to its clip's deck). Mappable to keys and MIDI; OSC `/composition/decks/N/select`, `/composition/selectnextdeck`, `/composition/selectprevdeck`. Compositions saved before decks open as Deck 1.
- **Move** (*Move → Move Clips*): pick several clips (click them, press Ins over them, or drag a box from an empty cell; *Pick All Clips* in the menu), then drag one of them, or press the arrows, and they all move by the same number of rows and columns. Clips that play keep playing; a move that would leave the deck or land on another clip is refused. *Done* (or Escape) turns Move off and the clips stay where they are; Ctrl+Z undoes a move.
- **Look**: a dark interface in a few steps of depth, one teal accent for what is live or selected, text kept at 4.5:1 contrast or more, Inter for text and JetBrains Mono for figures that change (BPM, frame rate), both compiled into the program; sliders fill up to their value, so levels read at a glance from across a dark room.
- **Layers and Clips panels**: the layers' headers (with the composition's row on top and the crossfader under them) are a panel of their own, beside the Clips panel with the deck tabs, columns and cells; the two scroll up and down together, so each header stays beside its clips.
- **Workspace**: every panel can be docked, tabbed, split, or pulled out as its own window onto another monitor. Layouts are saved as named workspaces; the last layout and composition come back on the next start.
- **Even motion**: clips move by whole display refreshes (the frame clock locks to vsync and learns the display's true rate, 59.94 Hz, 144 Hz...), and each refresh shows the frame at its middle, so a 30 fps clip on a 60 Hz display shows every frame exactly twice instead of once or three times by the timer's jitter. HAP clips decode on two threads each, six frames ahead; a launch never decodes on the main thread (a cut keeps the outgoing clip until the new one's first frame is ready).
- **Performance panel**: frame time graph, the display's measured rate and whether the frame clock is locked to it, decode and upload time per layer, late frames.
- **Effects**: chains on every clip, layer and on the composition, each effect with bypass, dry/wet mix and reordering (drag the headers or use the arrows). Drag effects from the Effects panel onto a clip cell, a layer name, the Composition Monitor or any effects list.
- **Built-in effects**: Brightness & Contrast, Hue & Saturation, Invert, Colorize, Threshold, Mirror, Kaleidoscope, Tile, Pixelate, RGB Split, Blur, Edge Detect, Strobe (beat-synced), Trails.
- **Sources** (generated clips, at the composition size): Solid Color, Gradient, Lines, Checker, and FFGL source plugins. Drop one on an empty cell.
- **FFGL 2 host**: loads `.dll` (Windows) and `.bundle` (macOS) effect and source plugins from the app's plugin folder, Resolume's *Extra Effects* folders and any folder you add (Effects → Plugin Folders). Parameters show as sliders, checkboxes, buttons, menus, text and colour pickers, grouped as the plugin asks; menus, names and visibility that change live (FFGL 2.2/2.3) update in the panel. Plugins get the BPM, bar phase and time. STLT-CHASER, STLT-WIPER and STLT-OUTLINER run here as they do in Resolume.
- **Beat-synced parameters**: click a parameter's name → Animate: saw, ping-pong, sine, square or random, over 1/4 to 64 beats, between two values. Works on effect parameters, a clip's opacity, position, scale and rotation, and a layer's opacity; a moving setting's name lights up. RESYNC restarts the beat.

## Advanced Output

![The Advanced Output editor: screens and slices on the left, the output transformation canvas with a keystoned slice, a curved grid and a polygon, slice settings on the right](../docs/images/stlt-vj-advanced-output.png)

- **Screens**: as many as you need, each fullscreen on a display (chosen by name, so it finds the projector again after a restart), in a window, or virtual. Per screen: resolution, opacity, brightness, contrast, red/green/blue, and a **test pattern** that draws every slice as a coloured grid with its frame and centre, for lining up projectors.
- **Slices**: a rectangle of the composition (*Input Selection*) placed on the screen (*Output Transformation*). **Polygons** take any shape of the composition and pin each point anywhere on the screen.
- **Warping**: *Perspective* (four corners, for keystone), *Grid* (any number of points joined by straight lines), *Curved Grid* (the same points joined by smooth curves, for bending onto curved or uneven surfaces) or *Contour* (below). Switching keeps the current shape; grids can be refined or flattened.
- **Contour** (Resolume's *Edit Points* on the output): the slice's outline is a chain of points you fit to the real surface, and the picture is stretched to fill it. Four corners alone give exactly the perspective warp; add points anywhere along the sides (double-click the outline, or right-click → *Add Point Here*) and drag them onto the edges of the screen, the arch, the column. *Point Mode* in the slice settings joins them with **Bézier** curves or **Linear** segments. In Bézier mode the selected point shows its two handles and its neighbours' facing ones: handles in line stay in line (Alt moves one alone), Alt-drag on a point pulls out fresh handles, double-click a point to switch it between smooth and straight, *Smooth All* rounds the whole outline, *Reset Contour* goes back to the four corners. Corners can't be deleted; other points go with Delete or right-click → *Delete Point*.
- **Edit Points / Transform** (slice settings, on the output): *Edit Points* drags single points; *Transform* puts a box around the slice to scale it from a side or corner (Alt: from the centre, Shift: keep proportions) and a knob above it to rotate it (Shift: 15° steps), for any warp or polygon.
- **Masks** (*+ Mask*: rectangle or ellipse): shapes that black out what lies below them in the list, or everything outside them when inverted, with a feather for a soft edge and a strength. Their points are edited like a contour (Bézier or linear, add, delete, smooth, Transform), so a mask can follow a doorway, a column or the edge of a screen.
- **Sharing**: each screen can also go to other programs: on this computer through **Spout** (Windows) or **Syphon** (macOS), the GPU texture passed on without a copy, and over the network as an **NDI** source (any system; it needs the free NDI runtime, installed with [NDI Tools](https://ndi.video/tools/)). Sources are called "STLT-VJ *screen name*"; the screen settings show the receivers.
- **Soft edge** on each side, with power, luminance and gamma, to blend overlapping projectors.
- **Editing**: drag a slice to move it, its points to shape it; polygons get new points by double-clicking their outline too (on either tab, the other side gets the matching point); rectangles stay rectangles on the input (Alt frees a corner); Shift locks an axis; handles snap to the canvas edges and centre and to the other slices (Ctrl to drop the snap); arrows nudge the selected point, or the whole slice when none is selected, by 1 pixel (Shift: 10); the selected point's exact position is in the slice settings; wheel zooms, middle button or Space pans; right-click a slice for Match Output to Input, Fit to Screen, flips. Flip, opacity, exact input position and size are in the slice settings.
- **Presets** of the whole setup, and **import from Resolume**: *Import from Resolume* reads Arena's or Avenue's `AdvancedOutput.xml` straight from Documents, or pick any file. Screens, slices (input and output rectangles), polygons and masks come across, with the slices' warp grids (linear as *Grid*, Bézier as *Curved Grid*) and soft edges. Resolume does not document this file: the importer reads the variants it knows and keeps the plain output rectangle for anything else, so check warped slices after an import (and send us the file if one comes out wrong).
- The setup is saved on exit and restored on start, independently of the composition.

![A contour slice: the top pulled into an arch with Bézier handles, a point added on the left side and pulled out, the picture following the outline](../docs/images/stlt-vj-contour.png)

Rendering: each slice is a fine triangle mesh that follows the warp, so perspective and curves stay smooth; the composition is sampled once per screen pixel, soft edges are computed per pixel, and each screen gets its own vsynced window.

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
- **Hardware decoding**: H.264, HEVC, VP9, AV1, MPEG-2/4 (and ProRes on macOS) go to the computer's video decoder when there is one (VideoToolbox on macOS, D3D11 or DXVA2 on Windows, VA-API or VDPAU on Linux), which frees the CPU for more layers; the Clip panel says which decoder a clip uses, and the Performance panel can turn it off. It does not change the jumping cost: the hardware still decodes from the previous full picture.
- **ProRes, DNxHD and Motion JPEG** decode any frame on its own, so jumps are fine, but they are heavy on the CPU and large on disk.
- **DXV** (Resolume's codec) is decoded by FFmpeg on the CPU here, unlike HAP whose compressed textures go to the GPU as they are.
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
| Space | Pause / resume everything |
| Left / Right | Previous / next clip on the selected layer (not while the Advanced Output editor is open) |
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
```

Command line options: `--fresh` (start empty and save nothing), `--clip LAYER,COLUMN,PATH` (load and launch a clip), `--source LAYER,COLUMN,EFFECT-ID` (a generated clip), `--fx comp|layer:L|clip:L,C=EFFECT-ID` (add an effect), `--launch LAYER,COLUMN` (play a clip of the restored session), `--list-effects` (print effect ids and plugins that did not load), `--output-setup FILE.json|AdvancedOutput.xml`, `--advanced-output input|output` (open the editor), `--size WxH`, `--capture FILE.bmp --frames N` (screenshot after N frames, then quit).

FFGL plugins are loaded from `plugins/` in the settings folder, `Documents/Resolume Arena/Extra Effects`, `Documents/Resolume Avenue/Extra Effects` and the folders listed in `plugin-folders.txt`. Only FFGL 2 effects and sources are loaded; mixers and multi-input effects are listed as skipped. A composition that uses a plugin this computer does not have keeps the plugin's settings and shows it as missing.

Settings live in `%APPDATA%\STLT-VJ`, `~/Library/Application Support/STLT-VJ` or `~/.config/stlt-vj`: `layout.ini` (current panel layout), `workspaces/` and `last-session.stltvj`.

## Code map

| Folder | What |
| --- | --- |
| `src/media` | QuickTime/MP4 demuxer, HAP frame decoding, thread pool, positional file reads. No OpenGL: tested headless. |
| `src/render` | Clip player (decode thread + pixel buffers + textures), GPU compositor with effect chains, output window. |
| `src/output` | Advanced output: screens, slices, warps, contours, masks, soft edges, Resolume import, and the renderer with its output windows. |
| `src/fx` | Effect parameters and beat animations, built-in GLSL effects and sources, the FFGL 2 host, the effect registry. |
| `src/ui` | Panels, theme and fonts, widgets (filled sliders), workspaces. |
| `src/App.*` | Main loop and the actions panels share (load, launch, move, save, undo). |
| `src/audio` | Clip sound: voices that follow their clip's playhead, and the mix on the audio device (miniaudio). |
| `src/control` | Remote control: the OSC/MIDI address space, OSC parsing, MIDI messages and mappings, the UDP and MIDI inputs. |
| `src/History.*` | Undo and redo by snapshots of the composition and the output setup. |

## Next milestones

1. FFGL mixers as layer transitions, audio/FFT input for plugins that ask for it, clip parameter animation (opacity, transform).
