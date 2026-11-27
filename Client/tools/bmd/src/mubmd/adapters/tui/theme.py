from dataclasses import asdict
from typing import Final

from textual.theme import Theme

from mubmd.adapters import palette

LORENCIA: Final = Theme(name="lorencia", **asdict(palette.LORENCIA))
LORENCIA_LIGHT: Final = Theme(name="lorencia-light", **asdict(palette.LORENCIA_LIGHT))
