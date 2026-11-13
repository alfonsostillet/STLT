# Where the work on STLT stands (13 November 2026)

This is a handover. It says where the project is, how work is done on it and what is left, so a new chat can start from here without the whole history.

## In short

- **STLT 2.0.0 is out**: https://github.com/alfonsostillet/STLT/releases/tag/v2.0.0, built from `a265f4a` on `main`. Three packages, each with STLT-CHASER inside:
  - Windows: `STLT-v2.0.0-windows.zip`, about 178 MB;
  - macOS (Apple Silicon): `STLT-v2.0.0-macos.zip`, about 104 MB;
  - Linux: `STLT-v2.0.0-linux.tar.gz`, about 110 MB.
- **2.0 is the new interface** (Ember look, the Advanced Output in three steps with Stage, the fuller Output menu, maps that follow the selection). `new-ui` was merged into `main` by fast-forward; the 1.x interface stays on `classic-ui` (`89d6f0e`, the 1.0.1 code).
- **The `chaser/` submodule** is at `8834bd5`, the `main` of alfonsostillet/stlt-chaser with PR #7 (the editor in STLT's look, the screen picker, better picking and band selection). No new release of the Resolume plugin has been made for it yet.
- **CI**: the Windows smoke test now fetches Mesa with the workflow's token (unauthenticated GitHub API calls ran out on shared runners). `audio_test` crashed once on Windows (a segfault after 0.09 s, run 43498083480) on a commit that passed it twice; it could not be reproduced on Linux under ASan or TSan. The release went out after rerunning the failed jobs; the cause is still to be found.
- **Not yet tried on real hardware** (the owner should check):
  - the macOS package: open `STLT.app` with right-click → *Open*, then *Output → Chaser...*, and check that the STLT-CHASER effects appear in the Effects panel;
  - the items in [DA-PROVARE.md](DA-PROVARE.md).
- **The website page**: a page presenting STLT and STLT-CHASER for sud-studio.com is ready in alfonsostillet/site, branch `stlt-page`, folder `stlt/` (not pushed), waiting for the site's code to be merged in.
- **Docs**: in English, with `README.it.md` as the Italian page. Resolume's name appears only where it states a fact (compatibility, formats, the Resolume plugin, the independence note).

## Repositories

- **alfonsostillet/STLT** (this one):
  - the program is in `app/` (C++17, OpenGL, ImGui with docking, CMake);
  - STLT-CHASER is the `chaser/` submodule;
  - CI is in `.github/workflows/app.yml`;
  - release notes are in `docs/releases/<tag>.md`.
- **alfonsostillet/stlt-chaser**: the FFGL plugins (STLT-CHASER, STLT-WIPER, STLT-OUTLINER) and the Electron editor.
  - Work has been done on the branch `claude/sleepy-lamport-mizont`. The agent proxy refuses force-pushes and branch or tag deletes, so `origin/main` is merged into the branch, then a PR goes to `main`.
  - After a merge, move STLT's submodule to the new `main`.

## The owner's standing rules

- **Talk to the owner in Italian.** Docs and code comments are in English, except `README.it.md`.
- **Windows first.** Check the Windows CI first. Never name an identifier `near` or `far` (`windef.h` macros), and mind `min`/`max` on MSVC.
- **Push straight to `main`** in STLT.
- **No pull requests** unless asked. For stlt-chaser, the owner has approved each one explicitly.
- **Releases only when asked.**
- **No model identifiers in commits.** Commit footer:
  ```
  Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
  Claude-Session: <session link>
  ```
- **Discuss UI and UX changes before making them, with mock-ups.** The owner tests on Windows with several screens and asked to agree on solutions first: send pictures (HTML mock-ups rendered to PNG) before touching the code.
- **Keep the old interface working.** The interface before the redesign is the branch `classic-ui`; the redesign is built on the branch `new-ui` and goes to `main` only when the owner approves it.

## Building and testing

```bash
cmake -S app -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

- **Executable**: the CMake target is `stlt-vj`, the output is `STLT.exe`, `STLT.app` or `stlt`.
- **Unit tests**: `ctest --test-dir build`. There are 7 suites: audio, clip, control, fx, history, media and output.
- **Smoke test**: it starts the real program and checks pixels. On Linux, run it under `xvfb-run`:
  ```bash
  python3 app/tests/smoke.py <executable> app/tests/fixtures
  ```
  With `plugins/` and the three plugins next to the executable, it also checks STLT-CHASER.
- **Plugins**:
  ```bash
  cmake -S chaser/plugin -B chaser/plugin/build
  cmake --build chaser/plugin/build
  ```
  `host_test` runs on Windows only; `document_test` runs everywhere.
- **STLT-CHASER editor**: in `chaser/editor`, run:
  ```bash
  npm ci && node --test "test/*.test.mjs"
  ```
- **UI screenshots for visual checks**: run the program with `--size 1600x900 --capture file.bmp --frames 40`.
  - Useful options: `--clip L,C,file`, `--launch L,C`, `--fx comp=ffgl:STCH:STLT-CHASER`, `--output-setup file.json`, `--advanced-output input|output` and `--capture-screen N`.
  - Environment: `STLTVJ_NO_AUDIO=1`, with `HOME` and `XDG_CONFIG_HOME` pointing to a scratch folder.
  - Settings are in `<config>/stlt`. The STLT-CHASER document is `<config>/stlt/chaser/current.stchase`.
- **Before every push**, as done so far: gcc, clang and ASan builds, `ctest`, and the smoke test (also under ASan, with `ASAN_OPTIONS=detect_leaks=0`).

## Making a release

1. Bump the version in `app/CMakeLists.txt` (`project(... VERSION)` and `STLT_VERSION`).
2. Write `docs/releases/vX.Y.Z.md`, Italian first, then English.
3. Wait for the push's CI to be green.
4. Run `app.yml` by *workflow_dispatch* on `main`, with input `release=vX.Y.Z`.
   - The job tags the commit, renames the files to `STLT-<tag>-windows.zip`, `-linux.tar.gz` and `-macos.zip`, and uses the notes as the release text.
   - Tags containing `beta` or `rc` become pre-releases.
5. Check that the release has the three assets.

## How the recent parts work

- **The interface ("Ember", branch `new-ui`).**
  - Colours and sizes are in `ui/Theme.h`: warm near-black surfaces, hairline borders, square corners, amber for what is on air or on, cyan for clips waiting for their beat. Text is 13 px at 100 % (`theme::BaseFont`); `Ui( pixels )` sizes are pixels at that size.
  - The deck (`DrawDeck` in `ui/Panels.cpp`) is drawn from `DeckGeometry`, shared by the Layers and Clips panels so headers and cells line up. A layer's header has four blocks: the layer (name, clear, black, solo, blend), its faders (V, A, M and the crossfader group), the clip on air (picture and a position bar to scratch) and its controls (speed, play mode, timing, previous/next, direction). The composition is one line; the column headers sit level with it.
  - Cells: click the picture to put the clip on air, the name to see it in the Preview Monitor. On air: amber frame and progress; waiting for the beat: dashed cyan frame and a clock; previewed: an outline on the name; missing: red name.
  - The toolbar ends with the output chip, the frame rate and the Grid/Timeline buttons.
- **Advanced Output editor (phase 1 of its redesign).** `ui/AdvancedOutputUi.cpp`: a path in three steps along the top (`Editor::Step`): 1 Screens (`DrawScreensStep`: a row per screen with its live picture, where it goes, this computer's displays as the system arranges them, a screen dragged onto a display goes there; one onto the display STLT is on asks first, `SendScreenTo`/`DrawOwnDisplayQuestion`; `DrawScreenProps` on the right), 2 Map (`DrawMapStep`: the slice list, Takes from → Goes to side by side, each slice in its own colour and number on both canvases, several picked with Ctrl/Shift or a box and moved together, `FindProblems` under them, `DrawSliceProps` in sections), 3 Stage (`ui/StageUi.cpp` on `output/Stage.h`: surfaces and blocks in metres, generic projectors from their data sheets, a 3D view drawn with ImGui's draw list (orbit, top, front, through a lens), each projector's lit parts with pixels per metre and lux, overlaps and coverage; *Make the slices from here* turns a projector into its screen's slices in perspective; saved as `stage` in the setup). Next for it: importing a LiDAR model, the live picture on the surfaces, calibrating on site by matching points. Under the steps `DrawPath` shows the whole path in one line (picture → slices → screens → where they go; orange when something is wrong). The header names the two setups (*For the Grid* / *For the Timeline*), says whether they are the same, copies one into the other (`CopyOtherSetup`), and has *How it works*.
- **Interface zoom.** `Preferences::interfaceScale` (50–200 %, per computer) times the display's scale. `App::ZoomInterface` (View menu, Ctrl + / Ctrl - / Ctrl 0) and the Preferences slider set it; `App::ApplyUiScale` rebuilds the fonts and the style between frames. After a zoom `Workspaces::Rescale` resizes floating panels and fits the deck (Layers width, deck height) again; other docked panels keep their share of the window.

- **Two output setups.**
  - `App::advancedOutput` is the Grid's (Session view) and `App::timelineOutput` is the Timeline's (Arrangement view).
  - `LiveOutput()` is the one on show, which follows the view. `EditedOutput()` and `outputTab` are the editor's tab.
  - Both are saved together (`stlt-output-setups`, with `SaveOutputSetups` and `LoadOutputSetups`). Older single-setup files load as both.
- **Tracks to screens.**
  - A Timeline screen can have its own picture: a `Canvas` with a `screen` field.
  - `SendTrackTo(app, track, screen)` sends a track to one screen, or to all of them with -1.
  - `SyncScreenPictures` keeps each picture's size and name with its screen.
  - Canvases are no longer shown in the UI.
- **Timeline UI.**
  - Video tracks are drawn above audio tracks (`ShownOrder`).
  - Track headers are wide and can be resized.
  - Double-click a header to rename the track; drag a header to reorder tracks (`MoveTrack`).
- **Monitor.**
  - Buttons for Composition, Multiview, Screen and Layer/Track (`App::monitorSource`).
  - A single layer comes from `Compositor::soloLayer` and `SoloTexture()`.
- **Output menu** (`DrawOutputModeItems` in `ui/Panels.cpp`, also the output chip).
  - Off; Fullscreen and Windowed for each display (`outputDisplay` serves both; a window opens centred on its display, `OutputWindow::CentreOn`).
  - *Advanced* only toggles the output (`ToggleAdvancedOutput`); *Advanced Output Settings...* (Ctrl+Shift+A) only opens the editor.
  - Composition output sharing: NDI and Spout/Syphon of the whole composition whatever the mode (`shareCompositionNdi/Local`, made by `UpdateQuickOutput` as a screen that goes nowhere but to sharing).
  - Identify Displays (`DrawDisplayIdentities`), System Display Settings (`OpenSystemDisplaySettings`), Show Test Card (`OutputRenderer::testCard`), Snapshot (`TakeSnapshot`: PNG in Documents/STLT/Snapshots, written on a thread by `output/PngFile`).
- **Maps that follow the selection.** Each key and MIDI map has a `MappingTarget` (`control/Control.h`): by position, the selected layer or the selected clip. `Retarget` rewrites the address when the map is used (`App::MappedAddress`); a held key or note is let go on the address it pressed. Saved as `"target"` in the maps (by position is not written). Shortcuts panel: *New maps act on* and an *Acts on* column.
- **Clicks act on the press.** Layers are selected, clips and columns launched when the mouse button goes down (as the arrow keys act at once); a clip dragged by its name moves without launching. Selecting a layer focuses the Layer panel only when its tab is hidden (`ShowLayerPanel`).
- **STLT-CHASER.**
  - The editor (branch `claude/ember-host` of stlt-chaser, the submodule's commit) has a screen picker: every screen side by side with its slices where they go (`editor/src/layout.mjs`, `stageLayout`), one screen at a time, or the composition. The smallest slice under the mouse is picked; a band takes the slices mostly inside it.
  - The plugin has up to 32 event parameters, "Sequence N", in the "Sequences" group. They are visible only for existing sequences and named after them. The playing one starts with "▶ ".
  - STLT draws event parameters as a row of buttons that can be mapped (`EffectsUi.cpp`).
- **macOS packaging.**
  - The plugins go in `STLT.app/Contents/Resources/plugins` and the editor in `Resources/chaser/STLT-CHASER.app`.
  - CI signs the app ad hoc.

## Possible next steps (proposals, none chosen by the owner)

- **Live video inputs**: webcam, capture cards, NDI in.
- **Autopilot**: automatic clip launching.
- **Grid layers to screens**: today only timeline tracks have a screen.
- **Outputs per timeline cue or block.**
- **ISF shaders with audio or image inputs.**
- **MIDI clock out.**
- **Text source and output recording.**
- **Code signing** on Windows and macOS. This needs the owner's certificates.
- **A CI test that starts the app on macOS.** Today CI only checks the signature and the plugins there.
- **A STLT-CHASER stage for the Timeline setup.** Today the editor uses the Grid setup's slices.
