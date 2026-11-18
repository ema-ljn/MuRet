"""Movimiento lógico de SDL/X11 por MotionNotify; no mueve el puntero de Wayland."""

import ctypes
import subprocess


class _Motion(ctypes.Structure):
    # XMotionEvent de X11/Xlib.h: ctypes conserva la alineación de la ABI del host.
    _fields_ = [
        ("type", ctypes.c_int), ("serial", ctypes.c_ulong),
        ("send_event", ctypes.c_int), ("display", ctypes.c_void_p),
        ("window", ctypes.c_ulong), ("root", ctypes.c_ulong),
        ("subwindow", ctypes.c_ulong), ("time", ctypes.c_ulong),
        ("x", ctypes.c_int), ("y", ctypes.c_int),
        ("x_root", ctypes.c_int), ("y_root", ctypes.c_int),
        ("state", ctypes.c_uint), ("is_hint", ctypes.c_char),
        ("same_screen", ctypes.c_int),
    ]


class _Event(ctypes.Union):
    _fields_ = [("motion", _Motion), ("pad", ctypes.c_long * 24)]


def move(window: str, x: int, y: int) -> None:
    """Envía hover al cliente MuRet; confirmar el picking en un dibujo antes del clic."""
    name = subprocess.check_output(["xdotool", "getwindowname", window], text=True).strip()
    if name != "MuRet":
        raise ValueError("the target window must be named MuRet")
    geometry = dict(line.split("=", 1) for line in subprocess.check_output(
        ["xdotool", "getwindowgeometry", "--shell", window], text=True).splitlines())
    if not (0 <= x < int(geometry["WIDTH"]) and 0 <= y < int(geometry["HEIGHT"])):
        raise ValueError("coordinates must be inside the client area")
    lib = ctypes.CDLL("libX11.so.6")
    lib.XOpenDisplay.argtypes = [ctypes.c_char_p]
    lib.XOpenDisplay.restype = ctypes.c_void_p
    lib.XDefaultRootWindow.argtypes = [ctypes.c_void_p]
    lib.XDefaultRootWindow.restype = ctypes.c_ulong
    lib.XSendEvent.argtypes = [ctypes.c_void_p, ctypes.c_ulong, ctypes.c_int,
                              ctypes.c_long, ctypes.POINTER(_Event)]
    lib.XFlush.argtypes = [ctypes.c_void_p]
    lib.XCloseDisplay.argtypes = [ctypes.c_void_p]
    display = lib.XOpenDisplay(None)
    if not display:
        raise RuntimeError("cannot open the X11 display")
    try:
        event = _Event()
        event.motion = _Motion(type=6, send_event=1, display=display, window=int(window),
                               root=lib.XDefaultRootWindow(display), x=x, y=y, same_screen=1)
        if not lib.XSendEvent(display, int(window), 0, 1 << 6, ctypes.byref(event)):
            raise RuntimeError("XSendEvent rejected MotionNotify")
        lib.XFlush(display)
    finally:
        lib.XCloseDisplay(display)
