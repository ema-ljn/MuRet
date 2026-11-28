from collections.abc import Sequence
from dataclasses import dataclass
from typing import Final

from mutools.core.findings import Finding, Severity
from mutools.core.formats.emote_table import AddedEmote

TRIGGER_TAKEN: Final = "TRIGGER_TAKEN"
TEXTS_FILE: Final = "Local/Text.bmd"
# 0x576eb0 looks for texts 270 to 348 and 783 of Text.bmd, and for 15 strings of main.exe (0x6c90e0..0x6c912c, CP949).
ORIGINAL_TRIGGER_TEXTS: Final = (*range(270, 349), 783)
ORIGINAL_TRIGGER_STRINGS: Final = (
    b";",
    b"\xa4\xd0.\xa4\xd0",
    b"\xa4\xcc.\xa4\xcc",
    b"T_T",
    b"\xa4\xd1.\xa4\xd1",
    b"\xa4\xd1.,\xa4\xd1",
    b"\xa4\xd1,.\xa4\xd1",
    b"-.-",
    b"-_-",
    b"^^",
    b"^.^",
    b"^_^",
    b"/\xa4\xd1",
    b"\xa4\xd1^",
    b"hustle",
)


@dataclass(frozen=True, slots=True)
class OriginalTrigger:
    said: bytes
    origin: str


def original_triggers(texts: Sequence[bytes] | None) -> tuple[OriginalTrigger, ...]:
    """The original's 95 emote triggers; without Text.bmd's records, only the 15 of main.exe."""
    strings = tuple(OriginalTrigger(said, "main.exe") for said in ORIGINAL_TRIGGER_STRINGS)
    if texts is None:
        return strings
    from_texts = tuple(
        OriginalTrigger(texts[index].split(b"\0", 1)[0], f"text {index} of {TEXTS_FILE}")
        for index in ORIGINAL_TRIGGER_TEXTS
    )
    return (*from_texts, *strings)


def _taken(trigger: str, original: OriginalTrigger, encoding: str, pointer: str) -> Finding:
    if not original.said:
        message = (
            f"The original's emote trigger in {original.origin} is empty, and the chat finds an empty trigger in "
            "every message before it tries an added one: no added emote plays"
        )
    else:
        shown = original.said.decode(encoding, errors="backslashreplace")
        message = (
            f"Saying {trigger!r} plays the original's emote of {shown!r} ({original.origin}) instead: the chat tries "
            "the original's 95 triggers first"
        )
    return Finding(TRIGGER_TAKEN, Severity.WARNING, pointer, message)


def taken_triggers(
    emote: AddedEmote, originals: tuple[OriginalTrigger, ...], encoding: str, pointer: str = ""
) -> tuple[Finding, ...]:
    """The triggers that hold one of the original's, compared byte for byte as 0x576960 does."""
    findings: list[Finding] = []
    for index, trigger in enumerate(emote.triggers):
        said = trigger.encode(encoding)
        taken = next((original for original in originals if original.said in said), None)
        if taken is not None:
            findings.append(_taken(trigger, taken, encoding, f"{pointer}/triggers/{index}"))
    return tuple(findings)
