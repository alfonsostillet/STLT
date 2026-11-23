#!/usr/bin/env python3
"""NIMS PRO LINK end to end on one computer: the transmitter sends its made-up decks (--demo: deck A plays "Kerala" with
its fader up, deck B has "Kong" loaded and paused) and the real program, with clips put in their cells but not
launched, must find it by itself, launch the clip linked to the track that plays and leave the other alone. Then the VJ during the show (keys pressed at a
frame, --control-at): a clip launched or a layer stopped by hand on NIMS's layer stays (NIMS goes off), the NIMS switch
on again brings the linked clip back, and with NIMS off nothing is launched.

usage: nimslink_smoke.py <STLT executable> <transmitter executable> <fixtures folder>
"""
import os
import subprocess
import sys
import tempfile
import time

EXE, TRANSMITTER, FIXTURES = sys.argv[1], sys.argv[2], sys.argv[3]
failures = []


def read_bmp(path):
    import struct
    with open(path, "rb") as f:
        data = f.read()
    offset = struct.unpack_from("<I", data, 10)[0]
    width, height = struct.unpack_from("<ii", data, 18)
    row = (width * 3 + 3) & ~3

    def pixel(x, y):
        at = offset + (height - 1 - y) * row + x * 3
        return data[at + 2], data[at + 1], data[at]

    return width, height, pixel


def run(name, placed, frames=400, links=(), controls=()):
    home = tempfile.mkdtemp(prefix="stltvj-nimslink-")
    env = dict(os.environ, STLTVJ_NO_AUDIO="1", APPDATA=home, HOME=home, XDG_CONFIG_HOME=home, GALLIUM_DRIVER="llvmpipe")
    capture = os.path.join(home, "capture.bmp")
    args = [EXE, "--fresh", "--size", "1280x720", "--capture", capture, "--capture-composition", "--frames", str(frames)]
    for cell, path in placed:
        args += ["--place", cell + "," + path]
    for cell, track in links:
        args += ["--link-track", cell + "," + track]
    for frame, address in controls:
        args += ["--control-at", f"{frame},{address}"]
    result = subprocess.run(args, env=env, capture_output=True, text=True, timeout=180)
    if result.returncode != 0 or not os.path.exists(capture):
        failures.append(f"{name}: exit code {result.returncode}\n{(result.stdout + result.stderr)[-2000:]}")
        return None
    return read_bmp(capture)


def close(colour, expected, tolerance=16):
    return all(abs(c - e) <= tolerance for c, e in zip(colour, expected))


bars = [(480, 200, (0, 255, 0)), (800, 200, (255, 255, 0)), (1120, 200, (0, 0, 255))]


def expect(name, image, playing, shows=None):
    """playing: the linked clip's bars; else black, or `shows` (a colour everywhere: the VJ's clip)."""
    if image is None:
        return
    width, height, pixel = image
    for x, y, colour in bars:
        got = pixel(x, y)
        if playing and not close(got, colour):
            failures.append(f"{name}: pixel {x},{y} is {got}, expected about {colour} (the linked clip playing)")
        if not playing and not close(got, shows or (0, 0, 0)):
            failures.append(f"{name}: pixel {x},{y} is {got}, expected {shows or 'black'} ({'the VJ' + chr(39) + 's clip' if shows else 'nothing launched'})")


clip = os.path.join(FIXTURES, "dxv_ycg6.mov")
gray = os.path.join(FIXTURES, "gray.jpg")  # the VJ's own clip: grey all over
grey = (128, 128, 128)
kerala = [("1,1", "Bonobo - Kerala")]  # the track deck A plays
kong = [("1,1", "Bonobo - Kong")]      # deck B's track, paused

# Without a transmitter nothing starts.
expect("No transmitter", run("No transmitter", [("1,1", clip)], frames=120, links=kerala), playing=False)

transmitter = subprocess.Popen([TRANSMITTER, "--demo", "--headless", "--name", "DJ-TEST"], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
try:
    time.sleep(1)
    if transmitter.poll() is not None:
        failures.append("transmitter: quit at once\n" + transmitter.stdout.read())
    else:
        expect("Track playing", run("Track playing", [("1,1", clip)], links=kerala), playing=True)
        expect("Track paused", run("Track paused", [("1,1", clip)], links=kong), playing=False)
        expect("Not linked", run("Not linked", [("1,1", clip)]), playing=False)

        # The VJ during the show. NIMS has launched the linked clip on layer 1 well before frame 300 (the transmitter is
        # found in about a second); the capture is at frame 450.
        both = [("1,1", clip), ("1,2", gray)]
        vj_launch = (300, "/composition/layers/1/clips/2/connect")
        nims = "/composition/nims"  # the NIMS switch: a key flips it
        expect("VJ launches a clip on NIMS's layer",
               run("VJ launches", both, frames=450, links=kerala, controls=[vj_launch]), playing=False, shows=grey)
        expect("VJ stops NIMS's layer",
               run("VJ stops", both, frames=450, links=kerala, controls=[(300, "/composition/layers/1/clear")]), playing=False)
        expect("NIMS on again after the VJ's launch",
               run("NIMS on again", both, frames=450, links=kerala, controls=[vj_launch, (380, nims)]), playing=True)
        # Off at the end of the first frame: the receiver only starts during it, so nothing can have been launched yet
        # (off leaves alone a clip already playing).
        expect("NIMS off from the start",
               run("NIMS off", both, frames=450, links=kerala, controls=[(1, nims)]), playing=False)
        expect("NIMS off, then on",
               run("NIMS off then on", both, frames=450, links=kerala, controls=[(1, nims), (300, nims)]), playing=True)
finally:
    transmitter.terminate()
    try:
        transmitter.wait(timeout=10)
    except subprocess.TimeoutExpired:
        transmitter.kill()

if failures:
    print("NIMS PRO LINK smoke test FAILED:\n" + "\n".join(failures))
    sys.exit(1)
print("NIMS PRO LINK smoke test passed")
