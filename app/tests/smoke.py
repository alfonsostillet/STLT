#!/usr/bin/env python3
"""Runs the real program on clips of each kind and checks what it draws (CI on Windows, with Mesa's software
OpenGL; also on Linux under Xvfb).

usage: smoke.py <STLT executable> <fixtures folder>

Each case starts STLT with an empty setup and its own settings folder, launches a clip, captures the
composition after some frames and checks pixels whose colour is known. The program must exit by itself;
a crash, a hang or a wrong picture fails the run. When the STLT-CHASER effects sit next to the program
(plugins/), they must load and run on the composition.
"""
import json
import os
import socket
import struct
import subprocess
import sys
import tempfile
import threading
import time

EXE, FIXTURES = sys.argv[1], sys.argv[2]
failures = []


def read_bmp(path):
    """The 24-bit bottom-up BMP the program writes: (width, height, pixel(x, y) -> (r, g, b))."""
    with open(path, "rb") as f:
        data = f.read()
    offset = struct.unpack_from("<I", data, 10)[0]
    width, height = struct.unpack_from("<ii", data, 18)
    row = (width * 3 + 3) & ~3

    def pixel(x, y):
        at = offset + (height - 1 - y) * row + x * 3  # rows bottom-up, each pixel blue, green, red
        return data[at + 2], data[at + 1], data[at]

    return width, height, pixel


def run(name, args, timeout=180, home=None, fresh=True):
    home = home or tempfile.mkdtemp(prefix="stltvj-smoke-")
    env = dict(os.environ, STLTVJ_NO_AUDIO="1", APPDATA=home, HOME=home, XDG_CONFIG_HOME=home, GALLIUM_DRIVER="llvmpipe")
    capture = os.path.join(home, "capture.bmp")
    if os.path.exists(capture):
        os.remove(capture)
    command = [EXE] + (["--fresh"] if fresh else []) + ["--size", "1280x720", "--capture", capture, "--capture-composition", "--frames", "45"] + args
    try:
        result = subprocess.run(command, env=env, capture_output=True, text=True, timeout=timeout)
    except subprocess.TimeoutExpired:
        failures.append(f"{name}: did not finish within {timeout} s")
        return None, ""
    output = result.stdout + result.stderr
    if result.returncode != 0:
        failures.append(f"{name}: exit code {result.returncode}\n{output[-2000:]}")
        return None, output
    if not os.path.exists(capture):
        failures.append(f"{name}: no capture\n{output[-2000:]}")
        return None, output
    return read_bmp(capture), output


def close(colour, expected, tolerance):
    return all(abs(c - e) <= tolerance for c, e in zip(colour, expected))


def expect(name, image, points, tolerance):
    if image is None:
        return
    width, height, pixel = image
    if (width, height) != (1920, 1080):
        failures.append(f"{name}: composition is {width}x{height}, expected 1920x1080")
        return
    for x, y, colour in points:
        got = pixel(x, y)
        if not close(got, colour, tolerance):
            failures.append(f"{name}: pixel {x},{y} is {got}, expected about {colour}")


def clip(file):
    return ["--clip", "1,1," + os.path.join(FIXTURES, file), "--launch", "1,1"]


# HAP (DXT1): a flat green 64x48 clip, fitted into the middle of the composition.
image, _ = run("HAP", clip("green_hap.mov"))
expect("HAP", image, [(960, 540, (0, 255, 0))], 12)

# DXV 3, read on the GPU: colour bars along the top of the test pattern.
bars = [(480, 200, (0, 255, 0)), (800, 200, (255, 255, 0)), (1120, 200, (0, 0, 255)), (1440, 200, (255, 0, 255)), (1760, 200, (0, 255, 255))]
image, _ = run("DXV Normal", clip("dxv_dxt1.mov"))
expect("DXV Normal", image, bars, 60)  # DXT1 is coarse
image, _ = run("DXV High Quality", clip("dxv_ycg6.mov"))
expect("DXV High Quality", image, bars, 16)
image, _ = run("DXV High Quality alpha", clip("dxv_yg10.mov"))
expect("DXV High Quality alpha", image, bars, 16)

# The session comes back: a clip playing when the program closed plays again when it opens, with nothing asked.
session = tempfile.mkdtemp(prefix="stltvj-smoke-session-")
run("Session (first run)", clip("dxv_ycg6.mov"), home=session, fresh=False)
image, _ = run("Session (reopened)", [], home=session, fresh=False)
expect("Session (reopened)", image, bars, 16)

# H.264 through FFmpeg: grey frames from black up (luma 16 + 16 n), so the middle is neutral, whichever frame it is.
image, _ = run("H.264", clip("ramp_h264.mp4"))
if image is not None:
    r, g, b = image[2](960, 540)
    if not (abs(r - g) <= 12 and abs(g - b) <= 12):
        failures.append(f"H.264: middle pixel is {(r, g, b)}, expected a grey")

# The timeline following Art-Net timecode: a green clip from 4 s on, the timeline's start at 01:00:00:00. With no
# timecode the playhead stays at the top (black); with timecode from 01:00:05:00 it is in the clip (green).
def art_timecode(frame):
    hh, rest = divmod(frame, 25 * 3600)
    mm, rest = divmod(rest, 25 * 60)
    ss, ff = divmod(rest, 25)
    return b"Art-Net\0" + bytes([0x00, 0x97, 0, 14, 0, 0, ff, ss, mm, hh, 1])  # ArtTimeCode, 25 fps


def clip_settings(node, file="green_hap.mov"):
    if isinstance(node, dict):
        if str(node.get("path", "")).endswith(file):
            return dict(node)
        node = list(node.values())
    for child in node if isinstance(node, list) else []:
        found = clip_settings(child, file)
        if found:
            return found
    return None


def stop_playing(node):
    if isinstance(node, dict):
        node.pop("playing", None)
        node = list(node.values())
    for child in node if isinstance(node, list) else []:
        stop_playing(child)


tc_home = tempfile.mkdtemp(prefix="stltvj-smoke-tc-")
run("Timecode (setup)", clip("green_hap.mov"), home=tc_home, fresh=False)
sessions = [os.path.join(d, "last-session.stltvj") for d, _, files in os.walk(tc_home) if "last-session.stltvj" in files]
if not sessions:
    failures.append("Timecode: no session saved")
else:
    config = os.path.dirname(sessions[0])
    session = json.load(open(sessions[0], encoding="utf-8"))
    green = clip_settings(session)
    stop_playing(session)
    timeline = session.setdefault("timeline", {})
    timeline.update({"tracks": [{"name": "Video 1", "audio": False, "clips": [{"start": 4, "length": 120, "clip": green}]}],
                     "followTimecode": True, "timecodeStart": "01:00:00:00", "position": 0})
    json.dump(session, open(sessions[0], "w", encoding="utf-8"))
    with open(os.path.join(config, "view.txt"), "w") as f:
        f.write("arrangement")
    with open(os.path.join(config, "preferences.json"), "w") as f:
        json.dump({"format": "stlt-vj-preferences", "timecodeSource": 3}, f)
    image, _ = run("Timecode (none coming)", [], home=tc_home, fresh=False)
    expect("Timecode (none coming)", image, [(960, 540, (0, 0, 0))], 12)
    sending = threading.Event()
    sending.set()

    def send():
        out = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        frame = 25 * 3600 + 5 * 25
        while sending.is_set():
            out.sendto(art_timecode(frame), ("127.0.0.1", 6454))
            frame += 1
            time.sleep(0.04)

    sender = threading.Thread(target=send)
    sender.start()
    try:
        image, _ = run("Timecode (Art-Net)", [], home=tc_home, fresh=False)
    finally:
        sending.clear()
        sender.join()
    expect("Timecode (Art-Net)", image, [(960, 540, (0, 255, 0))], 12)

# Timeline tracks on canvases: the green clip's track goes to the composition, the colour bars' track to a canvas
# "LED". Screen 1 takes from the composition, screen 2 from the canvas: the bars must show on screen 2 only.
routing_home = tempfile.mkdtemp(prefix="stltvj-smoke-routing-")
run("Routing (setup)", ["--clip", "1,1," + os.path.join(FIXTURES, "green_hap.mov"), "--clip", "1,2," + os.path.join(FIXTURES, "dxv_ycg6.mov")],
    home=routing_home, fresh=False)
sessions = [os.path.join(d, "last-session.stltvj") for d, _, files in os.walk(routing_home) if "last-session.stltvj" in files]
if not sessions:
    failures.append("Routing: no session saved")
else:
    config = os.path.dirname(sessions[0])
    session = json.load(open(sessions[0], encoding="utf-8"))
    green, bars = clip_settings(session), clip_settings(session, "dxv_ycg6.mov")
    stop_playing(session)
    session.setdefault("timeline", {}).update({"position": 1, "tracks": [
        {"name": "Video 1", "audio": False, "clips": [{"start": 0, "length": 120, "clip": green}]},
        {"name": "Video 2", "audio": False, "output": "led", "clips": [{"start": 0, "length": 120, "clip": bars}]}]})
    json.dump(session, open(sessions[0], "w", encoding="utf-8"))
    with open(os.path.join(config, "view.txt"), "w") as f:
        f.write("arrangement")
    whole = [[0, 0], [1920, 0], [1920, 1080], [0, 1080]]
    def screen(name, source):
        slice_ = {"name": "Slice 1", "type": "slice", "input": whole, "corners": whole, "warp": "Perspective"}
        if source:
            slice_["source"] = source
        return {"name": name, "device": "Virtual Output", "size": [1920, 1080], "slices": [slice_]}
    setup = os.path.join(routing_home, "routing.json")
    json.dump({"format": "stlt-vj-advanced-output", "version": 1, "screens": [screen("Main", ""), screen("LED", "led")],
               "canvases": [{"id": "led", "name": "LED", "size": [1920, 1080]}]}, open(setup, "w"))
    blue_bar = (1120, 200)  # blue in the bars, green in the green clip
    image, _ = run("Routing composition", ["--output-setup", setup], home=routing_home, fresh=False)
    expect("Routing composition", image, [(blue_bar[0], blue_bar[1], (0, 255, 0))], 12)
    for number, colour in ((1, (0, 255, 0)), (2, (0, 0, 255))):
        image, _ = run(f"Routing screen {number}", ["--output-setup", setup, "--capture-screen", str(number)], home=routing_home, fresh=False)
        expect(f"Routing screen {number}", image, [(blue_bar[0], blue_bar[1], colour)], 16)
    # In the Session view (the deck) the canvas shows the composition, so screen 2 does not go black.
    with open(os.path.join(config, "view.txt"), "w") as f:
        f.write("session")
    image, _ = run("Routing screen 2 in Session", ["--output-setup", setup, "--capture-screen", "2", "--launch", "1,1"], home=routing_home, fresh=False)
    expect("Routing screen 2 in Session", image, [(blue_bar[0], blue_bar[1], (0, 255, 0))], 16)
    # The Grid's and the timeline's setups saved together: each view shows its own. The Grid's screen is faded out
    # (opacity 0, black), the timeline's is not.
    grid = {"format": "stlt-vj-advanced-output", "version": 1, "screens": [dict(screen("Main", ""), opacity=0)]}
    timeline_setup = {"format": "stlt-vj-advanced-output", "version": 1, "screens": [screen("Main", "")]}
    both = os.path.join(routing_home, "both.json")
    json.dump({"format": "stlt-output-setups", "version": 1, "grid": grid, "timeline": timeline_setup}, open(both, "w"))
    image, _ = run("Grid setup in Session", ["--output-setup", both, "--capture-screen", "1", "--launch", "1,1"], home=routing_home, fresh=False)
    expect("Grid setup in Session", image, [(960, 540, (0, 0, 0))], 12)
    with open(os.path.join(config, "view.txt"), "w") as f:
        f.write("arrangement")
    image, _ = run("Timeline setup in Arrangement", ["--output-setup", both, "--capture-screen", "1"], home=routing_home, fresh=False)
    # (Its setup has no canvas "led": both tracks go to the composition, the bars on top.)
    expect("Timeline setup in Arrangement", image, [(blue_bar[0], blue_bar[1], (0, 0, 255))], 16)

# ISF shaders, when they come with the program: every one builds and runs, and two of them on the green clip draw
# what they should (Color Invert: magenta; Fast Blur, in several passes: green still).
isf = os.path.join(os.path.dirname(os.path.abspath(EXE)), "isf")
if os.path.isdir(isf):
    home = tempfile.mkdtemp(prefix="stltvj-smoke-isf-")
    env = dict(os.environ, STLTVJ_NO_AUDIO="1", APPDATA=home, HOME=home, XDG_CONFIG_HOME=home, GALLIUM_DRIVER="llvmpipe")
    try:
        result = subprocess.run([EXE, "--fresh", "--size", "640x360", "--check-isf"], env=env, capture_output=True, text=True, timeout=600)
        output = result.stdout + result.stderr
        summary = [line for line in output.splitlines() if "ISF shaders checked" in line]
        checked = int(summary[-1].split()[0]) if summary else 0
        if result.returncode != 0 or checked < 150:
            failed = [line for line in output.splitlines() if line.startswith("FAILED")]
            failures.append(f"ISF: {summary[-1] if summary else 'no summary'}\n" + "\n".join(failed[:20]) + ("" if failed else output[-2000:]))
    except subprocess.TimeoutExpired:
        failures.append("ISF: --check-isf did not finish")
    image, _ = run("ISF Color Invert", clip("green_hap.mov") + ["--fx", "comp=isf:Color Invert"])
    expect("ISF Color Invert", image, [(960, 540, (255, 0, 255))], 12)
    image, _ = run("ISF Fast Blur", clip("green_hap.mov") + ["--fx", "comp=isf:Fast Blur"])
    expect("ISF Fast Blur", image, [(960, 540, (0, 255, 0))], 12)

# STLT-CHASER's effects, when they come with the program: listed, and running on the composition.
plugins = os.path.join(os.path.dirname(os.path.abspath(EXE)), "plugins")
if os.path.isdir(plugins):
    image, output = run("STLT-CHASER", clip("dxv_ycg6.mov") + ["--list-effects", "--fx", "comp=ffgl:STCH:STLT-CHASER"])
    for name in ("ffgl:STCH:STLT-CHASER", "ffgl:STWP:STLT-WIPER", "ffgl:STOL:STLT-OUTLINER"):
        if name not in output:
            failures.append(f"STLT-CHASER: {name} not loaded\n{output[-2000:]}")

if failures:
    print("SMOKE TEST FAILED:\n- " + "\n- ".join(failures))
    sys.exit(1)
print("smoke test passed")
