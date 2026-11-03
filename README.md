<img src="docs/images/stlt-logo.png" alt="STLT" width="128">

# STLT

**STLT is software for VJs and multimedia visual installations.** It is free and open source, and it is really two programs in one, a click apart:

- **The Grid**, for playing live: a classic VJ setup where you throw clips onto layers and launch them on the spot.
- **The Timeline**, for building a show in advance: video and audio tracks, blocks and cues, then played live in perfect sync with the musicians, the actors, the director or the show.

Switch between them with the two icons at the top right (or the Tab key). Each one keeps its own panel layout and its own output setup, and both work with the same clips and effects.

> 🇮🇹 **[Leggi questa pagina in italiano](README.it.md)**

**[Download STLT](https://github.com/alfonsostillet/STLT/releases/latest)** for Windows, macOS (Apple Silicon) or Linux.

> **Mac users wanted.** The macOS version builds automatically but **has never been tested on a real Mac**. If you can try it, any feedback is very welcome, even just "it opens" or "it doesn't": please [open an issue](https://github.com/alfonsostillet/STLT/issues).

## The Grid: playing live

![The Grid: layers on the left, clips in columns on the right, monitors, composition, clip and performance panels below](docs/images/stlt-grid.webp)

- **Layers, columns and decks**: launch a single clip, a whole column or the next and previous clip. Decks are pages of clips over the same layers, and changing deck never stops what is playing.
- **A/B crossfader**, and on every layer blend modes, separate master, audio and video faders, speed, bypass and solo.
- **Your own workspace**: every panel can be moved, docked, tabbed, split or pulled out as a window onto another monitor. Layouts are saved by name.
- **Control over everything**: effect chains on the composition, on a layer or on a single clip, each with its own mix.
- **Lots of effects**: built-in effects and sources, about 200 ISF shaders, and FFGL 2 plugins, including the ones in Resolume's *Extra Effects* folder.
- **Animate any parameter**: to the BPM, over a time in seconds, or following the sound coming in (level, bass, mid, treble).
- **Multiview monitor**: see the composition, all the screens side by side, a single screen or a single layer. A separate Preview Monitor plays a clip before it goes out.
- **Clips**: in and out points, 8 cue points, BPM Sync, Normal, Toggle and Piano triggers, transitions, launching on the next beat or bar, and sound in step with the picture.
- **Keyboard, MIDI and OSC mapping**, with presets you can save and carry to another computer. Mapped controller pads light up, in colour on the Akai APC mini mk2.

### Slices on many screens

![The Advanced Output in the Grid setup: a list of slices on the left, eight warped slices on the canvas, the slice settings on the right](docs/images/stlt-output-grid.webp)

The **Advanced Output** maps your video onto any stage: as many screens as you need, slices and polygons of any shape, perspective, grid, curved grid and Bézier contour warps, masks, soft edges for blending projectors, test patterns and presets. It can also import Resolume's `AdvancedOutput.xml`.

### STLT-CHASER built in

![The STLT-CHASER editor opened on STLT's slices: eight slices on the canvas, the step sequencer at the bottom](docs/images/stlt-chaser.webp)

Bumps, chases, wipes, delays and outlines across your slices. *Output → Chaser...* opens the editor on the slices of your Advanced Output, and the STLT-CHASER, STLT-WIPER and STLT-OUTLINER effects are in the Effects panel. Each sequence is a button you can map to a MIDI pad or a key, to change sequence live.

It is built around **[STLT-CHASER](https://github.com/alfonsostillet/stlt-chaser)**, our free plugin for Resolume, and it works the same way. **For the instructions, follow the guide of the Resolume plugin**: [English](https://github.com/alfonsostillet/stlt-chaser#readme), [italiano](https://github.com/alfonsostillet/stlt-chaser/blob/main/docs/GUIDA.md).

## The Timeline: building a show

![The Timeline: blocks and cues above video and audio tracks, the clips and a multiview of three screens below](docs/images/stlt-timeline.webp)

Here you create a show from scratch, then send it out live, locked to whatever leads the performance.

- **Video and audio tracks**, as in an editing program: drag on clips from the Grid, from Files or from the desktop, then move, copy, trim and split them. Rename and reorder the tracks, with mute and solo on each.
- **Keyframes** on opacity, position, scale, rotation and volume, with effects on every clip, and a volume line on the audio tracks.
- **Tracks to screens**: send each video track to all the screens or to one of them, for example the main screen on one track and the side screens on others.

### Blocks

The **block track** divides the show into its pieces, usually the songs. Each block says what happens when it ends:

| At the end of the block | What happens |
| --- | --- |
| **Next** | plays on into what follows |
| **Loop** | starts again from the top, until you decide to change block |
| **Hold** | stops on the last frame |
| **Black** | stops, and the output goes to black |

So the show waits for the people on stage, not the other way round. Every block is a **cue**: jump to it with its button or the arrow keys, or map it to any key, MIDI control or OSC message.

### In sync with everything

- **Timecode**: the timeline follows **LTC**, **MTC** and **Art-Net**, starting, stopping and jumping with it, from an audio desk, a DAW or a lighting desk.
- **Tempo**: **Ableton Link** and **MIDI clock**, with an adjustable delay for projectors and LED walls that show the picture late. Pioneer CDJs can join through Pioneer's PRO DJ LINK Bridge.

![The Advanced Output in the Timeline setup: three screens as cards with the tracks each one receives, the selected slice taking from all the screens](docs/images/stlt-output-timeline.webp)

The Timeline has its **own Advanced Output setup**, separate from the Grid's, so a show's screens don't disturb your live mapping. Each screen shows the tracks it receives.

## Also inside

- **Fast playback**: HAP and DXV 3 go straight to the GPU, with instant jumps to any frame. H.264, HEVC, ProRes, DNxHD, VP9, PNG, JPEG, GIF and others also play, with hardware decoding where available.
- **Sending video to other programs**: **Spout** (Windows), **Syphon** (macOS) and **NDI** over the network, screen by screen.
- **Even motion**: the frame clock locks to the display's refresh, and the Performance panel shows the timing of every layer.
- **Undo and redo** for everything, including the whole Advanced Output.

Everything else, from shortcuts and OSC addresses to converting clips to HAP, is in the **[full guide](app/README.md)**.

## Install

Get the file for your system from the **[latest release](https://github.com/alfonsostillet/STLT/releases/latest)**. The programs are not signed, so your system may warn you the first time.

- **Windows**: extract the whole folder and start `STLT.exe`. If Windows warns you, choose *More info → Run anyway*.
- **macOS** (Apple Silicon, untested so far): extract it and move `STLT.app` wherever you like. The first time, right-click it and choose *Open*, or use *System Settings → Privacy & Security → Open Anyway*.
- **Linux** (built on Ubuntu 24.04): extract it and start `stlt`. It uses the system's FFmpeg, GLEW and GTK 3 libraries.

## Building

CMake 3.20+ and a C++17 compiler; see [app/README.md](app/README.md#building) for the platform packages. Clone with `--recursive` to get STLT-CHASER.

```bash
cmake -S app -B app/build -DCMAKE_BUILD_TYPE=Release
cmake --build app/build --config Release --parallel
```

Every push builds Windows, macOS and Linux versions and runs the tests (GitHub Actions, *STLT app*); the builds are attached to each run.

## Support

STLT is free. If you like the project and want to support future ones, you can [buy me a coffee](https://buymeacoffee.com/sud_stillet) ☕ if you are able to.

<a href="https://buymeacoffee.com/sud_stillet"><img src="https://cdn.buymeacoffee.com/buttons/v2/default-yellow.png" alt="Buy Me a Coffee" width="180"></a>

## Credits and license

This project was also made possible by using [Claude](https://claude.ai), Anthropic's AI.

STLT is an independent project. It is not affiliated with or endorsed by Resolume or by the makers of any other product.

GPL-3.0: see [LICENSE](LICENSE) and [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

## About this version

This is version 1.0, and it will surely be full of bugs. I wasn't even able to try the Mac version; what I know for sure is that the Windows one works. If anyone would like to send me feedback, I'd be glad to hear it: [open an issue](https://github.com/alfonsostillet/STLT/issues).
