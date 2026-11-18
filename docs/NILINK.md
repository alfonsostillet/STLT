# NI LINK

NI LINK sends a DJ's decks from Traktor to the VJ's program on another computer, as Pioneer's Pro DJ Link and Denon's StageLinQ do for their players. When the DJ starts a track, the clip linked to it starts on its layer, at the track's place, and follows it: pause, cue, hotcues, loops and the pitch fader. The deck that is on leads the tempo, and its stems (drums, bass, other, vocals) can move any parameter.

It is a prototype, made to show Native Instruments. Traktor itself is not changed. NI LINK adds a small script of its own to the mapping of the Kontrol D2 controller, the same way Traktor draws its controllers' screens.

## The parts

- **The transmitter** runs on the DJ's computer, next to Traktor. It is `NILink.exe` on Windows and `NI LINK.app` on macOS. It has a small window and no settings: three lights (Traktor, the network, the VJs listening) and the four decks.
- **The script in Traktor** reads the decks about 33 times a second and posts them to the transmitter on the same computer. It is two files, `TraktorNILink.qml` and `NILinkDeck.qml`, plus one line added to the D2's `D2.qml`. The transmitter installs it and can remove it. The sources are in `app/resources/nilink/qml`.
- **The receiver** is built into STLT. It finds the transmitters on its networks by itself.

## Setting up

### On the DJ's computer

1. Open the transmitter.
2. If it says *Not set up in Traktor yet*, press **Set up Traktor**. Windows asks for an administrator, because Traktor is under Program Files. macOS asks for an administrator's password, and the first time it may also ask to let the transmitter change apps (*App Management*).
3. In Traktor, go to *Preferences > Controller Manager*, press *Add... > Native Instruments > Kontrol D2*, then restart Traktor. No D2 hardware is needed: the mapping is what loads the script.
4. The TRAKTOR light turns green, showing *Traktor connected, 33 updates/s*.

A Traktor that has the earlier prototype's script (`TraktorVJLink.qml`, from the *traktor_modulare* folder) is updated in the same step: its files go, and its copy of Traktor's own `D2.qml` is kept as the original.

**Remove from Traktor** (bottom right of the window) puts Traktor's own `D2.qml` back and deletes the script. The original `D2.qml` is kept as `D2.qml.nilink-backup` while NI LINK is installed.

The same can be done from a command line:

- `NILink --install` puts the script in and quits; the exit code is 0 when it worked and 3 when permission was refused.
- `NILink --uninstall` takes it out and quits.

### On the VJ's computer

Open STLT. Nothing needs setting: NI LINK is on by default (*Preferences > Sync*). The first time, Windows asks whether STLT may use the network; allow it on private networks.

### The network

- **Best**: a direct Ethernet cable between the two computers, or both on the same wired switch. Without a router, both computers give themselves an address (169.254.x.x) and find each other all the same.
- **Wi-Fi works**, with more jitter. The receiver measures the network and takes most of it out (see *Measurements*).
- **When broadcasts don't get through** (some venue networks), enter the DJ computer's address in STLT's *Preferences > Sync > NI LINK addresses*, for example `192.168.1.14`.

### Trying the network before Traktor

`NILink --demo` sends four made-up decks:

- deck A plays "Kerala" by Bonobo;
- deck B has "Kong" loaded and paused;
- deck C has "Cirrus" loaded;
- deck D is empty.

In STLT, link a clip to deck A in the Traktor panel: it starts on its layer at once.

## How a clip follows a track

- **Linking.** In the **Traktor** panel (*View*), drag a deck onto a clip of the grid. Or right-click a clip and choose a track loaded now. The clip is then linked to that track by its title, artist and length, saved with the composition. The file's path is not used, because it is the DJ computer's path.
- **Linked clips in the grid.** A linked clip shows its deck's letter in the deck's colour while the track is loaded, and a link sign while no deck has it. The *Traktor* section of the Clip panel shows the track, where it plays, an offset in seconds and *Unlink*.
- **Starting.** A layer plays its linked clip when the track plays with its channel fader up. While the DJ only cues a track in the headphones (fader down), the visual waits. When two linked tracks play at once, the louder one holds the layer. The clip that is on keeps the layer until another is clearly louder.
- **The fader.** A linked clip's opacity follows its track's channel fader, so the DJ's mix is the visuals' mix.
- **Following.** The clip's playhead is the track's position, updated every frame. Small differences are taken up over a few frames; jumps (a cue, a hotcue, a beatjump, a loop going round) are followed at once.
- **Pausing.** When the deck pauses, the clip holds its frame.
- **Long and short movies.** A movie at least half as long as the track follows the track and holds its last frame at the end. A shorter movie is a loop that goes round with the track (or bounces, or plays once, as its play mode says).
- **Tempo.** With *Preferences > Sync > Follow Traktor's tempo* on, the BPM and the beat come from the deck that is on: playing, with the highest fader. Bars fall on Traktor's grid. A MIDI clock coming in leads over Traktor. With Ableton Link on, Link's other peers get the tempo too.
- **Stems.** Any parameter's *Animate* menu has *Traktor Drums*, *Traktor Bass*, *Traktor Other*, *Traktor Vocals* and *Traktor Level*: the levels of the stems (and of the channel) of the deck that is on. Stems need a Stem file in Traktor; *Traktor Level* works with any track.
- **Latency.** *Preferences > Sync > Latency* takes the decks this much ahead, for a projector that shows late.

## Measurements

From the tests (`app/tests/nilink_test.cpp`, which print them). The real thing on two computers should be measured too: see *Still to do*.

| Case | Error in the position shown | A jump followed in |
|---|---|---|
| The script's samples alone (each up to 30 ms late) | 1.6 ms on average, 3.9 ms at worst | one packet |
| Transmitter and receiver on one computer | 0.9 to 2 ms on average | about 30 ms |
| A bad network: 1 to 8 ms each way, 5% lost | 1.6 ms on average, 3.9 ms at worst | 35 to 70 ms |

How the jitter comes out:

- Traktor's values reach the script up to one update (30 ms) late.
- While a deck plays steadily, its samples lie on one line, position = rate × time + constant. The least late samples are the ones furthest ahead on that line, so the line is drawn through those.
- The clocks of the two computers are compared as NTP does. The round trips that waited in a queue are left out, and a line through the rest follows the clocks drifting apart. Its error is under 0.05 ms in the tests.

## The protocol

UDP and IPv4. Every value is little-endian. A string is one length byte followed by UTF-8. A packet starts with `NILK`, the version (1), its type, two spare bytes and the sender's id. The id is random and new each time a program starts.

| Type | Direction | What it carries |
|---|---|---|
| 1 Announce | transmitter → broadcast, port 52130, every second | data port, computer name, program ("Traktor", or empty while Traktor sends nothing) |
| 2 Subscribe | receiver → transmitter, every second | the round trip it measured, its name, what it has for each deck (0 nothing, 1 a linked clip, 2 that clip on air) |
| 3 Ping / 4 Pong | receiver ⇄ transmitter, 10 a second for 3 s, then 2 a second | t0; then t0, t1 (received), t2 (answered) |
| 5 Decks | transmitter → each subscriber, about 33 a second | sequence number, the transmitter's time, and for each deck: loaded / playing / in loop, track version, position, rate, beat phase, beat, BPM, loop start and length, four stem levels, channel level, fader |
| 6 Track | transmitter → each subscriber, when it changes (three times) and one deck every quarter second | deck, version, title, artist, key, file, collection entry, length, BPM, stems, eight hotcues (type, start, length, name) |
| 7 Bye | either way | going away |

Ports:

- The transmitter's socket is 52131, or any free port when that one is taken.
- Traktor's script posts to `http://127.0.0.1:9125/traktor`.
- A subscriber not heard for 5 s is dropped.
- A transmitter not heard for 3 s is shown as offline, and forgotten after a minute.

Data goes to each receiver on its own (unicast), because Wi-Fi delivers broadcasts late and at a low rate.

## Traktor's side

- **Paths.** The deck values and their paths in Traktor 4.5.1 (`app.traktor.decks.N.…`) are listed in the prototype's notes (`traktor_modulare/docs/plugin_vj.md`).
- **Timing.** The script posts when deck A to D's playhead moves, which happens as Traktor updates, and every 30 ms otherwise.
- **Never `abort()`.** The script never aborts a request: in Qt an aborted request can leave the connections stuck. The transmitter answers `Connection: close`, so Traktor carries on by itself when the transmitter restarts.
- **Loaded twice.** Traktor may load the script twice. The transmitter follows one session.
- **Where the D2 mapping is:**
  - Windows: `C:\Program Files\Native Instruments\Traktor Pro 4\Resources64\qml\CSI\D2`
  - macOS: `Traktor Pro 4.app/Contents/Resources/qml/CSI/D2`
- **BPM.** BPMs arrive as Traktor has them. A track gridded at half tempo, such as Kerala at 62.5 BPM, gives 62.5.

## Still to do

- **Measuring on two computers**, by cable and by Wi-Fi, and checking the script on macOS.
