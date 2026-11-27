from dataclasses import dataclass
from typing import Final


@dataclass(frozen=True, slots=True)
class Palette:
    primary: str
    secondary: str
    accent: str
    warning: str
    error: str
    success: str
    foreground: str
    background: str
    surface: str
    panel: str
    dark: bool


LORENCIA: Final = Palette(
    primary="#C9A227",
    secondary="#8C6A2F",
    accent="#E6C76A",
    warning="#D9822B",
    error="#B3261E",
    success="#7A9A3A",
    foreground="#E9DDC0",
    background="#0E0C09",
    surface="#17130E",
    panel="#231C12",
    dark=True,
)

LORENCIA_LIGHT: Final = Palette(
    primary="#8A6512",
    secondary="#A9844A",
    accent="#6E4B0B",
    warning="#B4610F",
    error="#9B1C1C",
    success="#4F6B1F",
    foreground="#2A2013",
    background="#F4EBD5",
    surface="#ECE1C6",
    panel="#E2D3AE",
    dark=False,
)

STUDIO: Final = Palette(
    primary="#9AA6FF",
    secondary="#A3A5AB",
    accent="#3A47B0",
    warning="#E0B25E",
    error="#E8867D",
    success="#7FCB9F",
    foreground="#E7E5DE",
    background="#111214",
    surface="#191A1D",
    panel="#1F2023",
    dark=True,
)
