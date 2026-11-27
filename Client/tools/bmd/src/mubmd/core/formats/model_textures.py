from dataclasses import dataclass
from typing import Final

OZT_LETTERS: Final = ("t", "T")
SCRIPT_LETTERS: Final = "RHSN"
SCRIPT_LENGTH: Final = 4
SLOT_PREFIXES: Final = (("ski", "skin"), ("level", "skin"), ("hid", "hidden"), ("hair", "hair"))


@dataclass(frozen=True, slots=True)
class TextureUse:
    file: str
    slot: str | None
    script: str

    @property
    def loader(self) -> str:
        return self.file.rpartition(".")[2]


def _script(name: str) -> str:
    letters = name.partition("_")[2].partition(".")[0][:SCRIPT_LENGTH]
    if not letters or any(letter not in SCRIPT_LETTERS for letter in letters):
        return ""
    return "".join(letter for letter in SCRIPT_LETTERS if letter in letters)


def texture_use(name: str) -> TextureUse:
    stem, _, rest = name.partition(".")
    extension = ".OZT" if rest[:1] in OZT_LETTERS else ".OZJ"
    slot = next((slot for prefix, slot in SLOT_PREFIXES if name.startswith(prefix)), None)
    return TextureUse(stem + extension, slot, _script(name))


def texture_text(name: bytes, encoding: str) -> str | None:
    end = name.find(b"\0")
    if end < 0:
        return None
    try:
        return name[:end].decode(encoding)
    except UnicodeDecodeError:
        return None
