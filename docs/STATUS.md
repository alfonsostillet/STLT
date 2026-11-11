# Where the work on STLT stands (11 November 2026)

This is a handover. It says where the project is, how work is done on it and what is left, so a new chat can start from here without the whole history.

## In short

- **STLT 1.0.0 is out**: https://github.com/alfonsostillet/STLT/releases/tag/v1.0.0. It is a full release, not a pre-release, with three complete packages, each with STLT-CHASER inside:
  - Windows: `STLT-v1.0.0-windows.zip`, about 178 MB;
  - macOS (Apple Silicon): `STLT-v1.0.0-macos.zip`, about 104 MB;
  - Linux: `STLT-v1.0.0-linux.tar.gz`, about 110 MB.
- **Release build**: `08209f4`. CI was green on Windows (and Windows ASan), Linux and macOS.
- **Docs after the release**: the README and the docs were rewritten. The docs are in English and `README.it.md` is the Italian page. Resolume's name appears only where it states a fact (compatibility, formats, the Resolume plugin, the independence note).
- **The `chaser/` submodule** is at `feb8f31`, the `main` of alfonsostillet/stlt-chaser, including PR #4 (sequence buttons).
- **Not yet tried on real hardware** (the owner should check):
  - the macOS package: open `STLT.app` with right-click → *Open*, then *Output → Chaser...*, and check that the STLT-CHASER effects appear in the Effects panel;
  - the items in [DA-PROVARE.md](DA-PROVARE.md), especially "Quick touches" and "Separate Grid and Timeline, tracks on screens, Multiview".

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
- **Output menu.**
  - *Advanced* only toggles the output (`ToggleAdvancedOutput`).
  - *Advanced Output Settings...* (Ctrl+Shift+A) only opens the editor.
- **STLT-CHASER.**
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
