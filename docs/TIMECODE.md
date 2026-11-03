# The timeline locked to timecode: the protocols used live

A study for point 11 of the roadmap: the timeline (the *Arrangement* view) following an external timecode, as in a show where lights, sound and video all start from the same source.

## What is really used live

| Protocol | How it arrives | Who usually sends it | Accuracy | Notes |
|---|---|---|---|---|
| **LTC** (SMPTE audio) | an audio channel (jack, XLR, sound card) | recorders, Ableton/Reaper/Logic/QLab with a plugin, lighting desks, touring playback | to the frame, minimal latency | **The touring standard.** The only one Resolume Arena reads by itself (two inputs, in the Audio preferences). |
| **MTC** (MIDI Time Code) | MIDI (USB or network: rtpMIDI, loopMIDI) | DAWs (Ableton Live, Logic, Cubase, Reaper), QLab, lighting desks | ±1 frame (a quarter frame at a time) | The handiest if the playback is a computer: no audio cables. Resolume does not read it (it needs a converter to LTC). |
| **Art-Net Timecode** (ArtTimeCode) | Ethernet network, UDP 6454 | lighting desks (grandMA, Chamsys, Avolites, Onyx), TouchDesigner, media servers | to the frame | Handy if the lighting network is already there. Resolume does not read this one either. |
| **Pro DJ Link** (Pioneer/AlphaTheta) | the CDJs' Ethernet network | CDJ-2000NXS2, CDJ-3000, XDJ | position in the track and beat | Proprietary. Pioneer licenses it (ArKaos, Green Hippo, Resolume, TC Supply) and sends it out through the free **PRO DJ LINK Bridge** app, which also sends **Ableton Link**. |
| OSC | network | TouchDesigner, QLab, Max | depends | No standard for timecode: everyone uses their own addresses. |

Sources: [Resolume's article on SMPTE](https://resolume.com/article/46), the requests on Resolume's forum for [MTC and Art-Net](https://resolume.com/forum/viewtopic.php?p=99115) and for [Art-Net timecode](https://www.resolume.com/forum/viewtopic.php?p=92024), [Pioneer on Pro DJ Link licensing](https://www.pioneerdj.com/en/news/2019/pro-dj-link-network-information-licensed-to-vfx-companies/) and [PRO DJ LINK Bridge](https://www.pioneerdj.com/en-gb/product/software/pro-dj-link-bridge/software/overview/).

## What we do

1. **LTC, MTC and Art-Net Timecode, all three**: STLT reads directly what Resolume reads only through a converter (MTC, Art-Net). So with a DAW a virtual MIDI cable is enough, and with lights the network is enough.
2. **Pro DJ Link: through Ableton Link.** The protocol is proprietary and the clean way is PRO DJ LINK Bridge (free, from Pioneer), which sends the CDJs' tempo and bar as Ableton Link: STLT now follows it (the LINK button). The CDJs' position in the track (to lock the timeline to a specific track) would mean acting as a "virtual CDJ" on the Pioneer network, which officially only licensed companies do: not for now.

## How it works for the user

- **Preferences → Sync → Timecode**: the source (Off, LTC, MTC, Art-Net) and, for each, where it comes from: for LTC the audio input and the channel (left or right), for MTC the MIDI input, for Art-Net the network port and universe (stream).
- The **frame rate** is recognised by itself (24, 25, 29.97 drop frame, 30) and shows next to the timecode.
- On the timeline, next to the time, the incoming timecode and the **TC** light (green when the timeline is following it).
- **Start**: which timecode matches the start of the timeline (for example 01:00:00:00, as often in shows). A button takes the one arriving at that moment.
- **The timeline follows**: when the timecode runs the timeline plays at its position; when it stops the timeline stops; when it jumps (the sound engineer goes back) the timeline jumps. Small differences are caught up without jerks, large ones with a jump.
- **Freewheel**: if the signal drops for a moment (a cable, a lost packet), the timeline goes on by itself for a time you choose (for example half a second) before stopping.
- **Delay** (the one already in *Preferences → Sync*): compensates for the projector's or LED wall's delay for the timecode too.

## How it is made inside

- **LTC**: decoding of the biphase signal (80 bits per frame, sync word 0x3FFD) from the audio input already used for parameters that follow the sound. Written by us, with tests that generate LTC and read it back, also with noise and at low volume.
- **MTC**: quarter frames (F1) and full frames (SysEx F0 7F 7F 01 01), from the MIDI inputs, like the MIDI clock. A quarter-frame sequence completes the time every two frames: the two frames of delay are added.
- **Art-Net**: ArtTimeCode packets (OpCode 0x9700) over UDP on port 6454.
- A single **Timecode**: the time arriving, when it arrived, the frame rate, whether it is running; the timeline follows it as the composition follows Link and the MIDI clock today.
