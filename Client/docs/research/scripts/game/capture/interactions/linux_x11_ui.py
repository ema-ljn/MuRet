#!/usr/bin/env python3
"""The window-directed X11 interactions used for the Q-C184 Linux replay.

Coordinates are client pixels from a fresh screenshot. A delivered event is not
proof of a game action: inspect the next screenshot and the private client log.
"""

import argparse
from pathlib import Path
import subprocess
import time


def run(*args: str) -> str:
    return subprocess.check_output(args, text=True, timeout=15).strip()


def check_window(window: str) -> None:
    if run("xdotool", "getwindowname", window) != "MuRet":
        raise ValueError("the target window must be named MuRet")


def press(window: str, key: str) -> None:
    try:
        run("xdotool", "keydown", "--window", window, key)
        time.sleep(0.18)
    finally:
        run("xdotool", "keyup", "--window", window, key)


def click(window: str, x: int, y: int) -> None:
    geometry = dict(line.split("=", 1) for line in run(
        "xdotool", "getwindowgeometry", "--shell", window
    ).splitlines())
    if not (0 <= x < int(geometry["WIDTH"]) and 0 <= y < int(geometry["HEIGHT"])):
        raise ValueError("coordinates must be inside the client area")
    run("xdotool", "mousemove", "--window", window, str(x), str(y))
    # --window sends ButtonPress to this window; the generic XTest path did not
    # reach SDL on this Xwayland session. SDL updates motion from this event too.
    try:
        run("xdotool", "mousedown", "--window", window, "1")
        time.sleep(0.18)
    finally:
        run("xdotool", "mouseup", "--window", window, "1")


def capture(window: str, output: Path, display: str) -> None:
    # Reserve the name exclusively before ffmpeg writes the single frame.
    with output.open("xb"):
        pass
    output.chmod(0o600)
    try:
        run("ffmpeg", "-hide_banner", "-loglevel", "error", "-y", "-f", "x11grab",
            "-window_id", window, "-i", display, "-frames:v", "1", "-update", "1", str(output))
    except BaseException:
        output.unlink()
        raise


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--window", required=True, type=lambda value: str(int(value, 0)))
    actions = parser.add_subparsers(dest="action", required=True)
    actions.add_parser("state")
    actions.add_parser("focus")
    key = actions.add_parser("key")
    key.add_argument("key")
    pointer = actions.add_parser("click")
    pointer.add_argument("x", type=int)
    pointer.add_argument("y", type=int)
    motion = actions.add_parser("move")
    motion.add_argument("x", type=int)
    motion.add_argument("y", type=int)
    text = actions.add_parser("type")
    text.add_argument("text", help="non-secret ASCII test name, never credentials")
    shot = actions.add_parser("capture")
    shot.add_argument("output", type=Path)
    shot.add_argument("--display", default=None, help="defaults to the DISPLAY environment variable")
    args = parser.parse_args()
    check_window(args.window)
    if args.action == "state":
        print(run("xdotool", "getwindowgeometry", "--shell", args.window))
    elif args.action == "focus":
        # Takes the X11 keyboard focus; do not confuse it with compositor focus.
        run("xdotool", "windowfocus", "--sync", args.window)
    elif args.action == "key":
        press(args.window, args.key)
    elif args.action == "click":
        click(args.window, args.x, args.y)
    elif args.action == "move":
        from x11_motion import move
        move(args.window, args.x, args.y)
    elif args.action == "type":
        if not args.text.isascii() or not args.text.isalnum():
            raise ValueError("only non-empty ASCII alphanumeric test names are supported")
        run("xdotool", "type", "--window", args.window, "--delay", "150", "--", args.text)
    elif args.action == "capture":
        import os
        display = args.display or os.environ["DISPLAY"]
        capture(args.window, args.output, display)


if __name__ == "__main__":
    main()
