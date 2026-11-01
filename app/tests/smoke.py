#!/usr/bin/env python3
"""Runs the real program on clips of each kind and checks what it draws (CI on Windows, with Mesa's software
OpenGL; also on Linux under Xvfb).

usage: smoke.py <stlt-vj executable> <fixtures folder>

Each case starts STLT-VJ with an empty setup and its own settings folder, launches a clip, captures the
composition after some frames and checks pixels whose colour is known. The program must exit by itself;
a crash, a hang or a wrong picture fails the run. When the STLT-CHASER effects sit next to the program
(plugins/), they must load and run on the composition.
"""
import os
import struct
import subprocess
import sys
import tempfile

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
