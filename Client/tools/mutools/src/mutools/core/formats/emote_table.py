import math
import re
from dataclasses import dataclass
from typing import Final, Literal

from mutools.core.errors import ErrorCode, MutoolsError

EMOTE_TABLE_HEADER: Final = "MuRet emotes 1"
EMOTE_TABLE_FOLDER: Final = "MuRet"
EMOTE_TABLE_NAME: Final = "Emotes.txt"
CHAT_ENCODING: Final = "cp1252"
FIRST_ADDED_ACTION: Final = 205
LAST_ACTION: Final = 255
MOST_KEYS_PER_STEP: Final = 10.0
ORIGINAL_KEYS_PER_STEP: Final = 0.32
MOST_TABLE_BYTES: Final = 64 * 1024
# What std::from_chars takes as a positive number, without the sign, spaces or underscores float() also takes.
DECIMAL: Final = re.compile(r"(?:[0-9]+\.?[0-9]*|\.[0-9]+)(?:[eE][+-]?[0-9]+)?")
LOOP: Final = "loop"
ONCE: Final = "once"
FIXED_COLUMNS: Final = 4
COLUMN_END: Final = "\t"
LINE_END: Final = "\n"

type Plays = Literal["loop", "once"]
PLAYS: Final[dict[str, Plays]] = {LOOP: LOOP, ONCE: ONCE}


@dataclass(frozen=True, slots=True)
class AddedEmote:
    action: int
    elf_action: int
    keys_per_step: float
    plays: Plays
    triggers: tuple[str, ...]


def _invalid(message: str, pointer: str = "") -> MutoolsError:
    return MutoolsError(ErrorCode.INVALID_VALUE, message, pointer=pointer)


def checked_emote(emote: AddedEmote) -> AddedEmote:
    """The rules the client reads the file with (Client/src/game/Tables/AddedEmotes.h)."""
    if not FIRST_ADDED_ACTION <= emote.action <= LAST_ACTION:
        raise _invalid(
            f"Action {emote.action} is not one of {FIRST_ADDED_ACTION} to {LAST_ACTION}: an added emote comes after "
            "Player.bmd's 205 actions, and the client sends an action as one byte",
            "/action",
        )
    if not 0 <= emote.elf_action <= LAST_ACTION:
        raise _invalid(f"The elf's action {emote.elf_action} is not one of 0 to {LAST_ACTION}", "/elf_action")
    if not (math.isfinite(emote.keys_per_step) and 0 < emote.keys_per_step <= MOST_KEYS_PER_STEP):
        raise _invalid(
            f"{emote.keys_per_step:g} keys per step is not above 0 and at most {MOST_KEYS_PER_STEP:g}; the original's "
            f"emotes play {ORIGINAL_KEYS_PER_STEP:g} a step",
            "/keys_per_step",
        )
    if not emote.triggers:
        raise _invalid("An emote needs a word that starts it", "/triggers")
    for index, trigger in enumerate(emote.triggers):
        if not trigger or any(character in trigger for character in "\t\r\n\0"):
            raise _invalid(
                f"Trigger {trigger!r} is empty or holds a tab, a line end or a zero, and the chat has none of them",
                f"/triggers/{index}",
            )
    return emote


def _corrupt(line: int, message: str) -> MutoolsError:
    return MutoolsError(ErrorCode.CORRUPT_FILE, f"Line {line} of the emote table: {message}")


def _whole(text: str, line: int, what: str) -> int:
    if not text.isdecimal() or not text.isascii():
        raise _corrupt(line, f"{what} {text!r} is not a number")
    return int(text)


def _keys_per_step(text: str, line: int) -> float:
    if DECIMAL.fullmatch(text) is None:
        raise _corrupt(line, f"the keys per step {text!r} are not a number above 0")
    return float(text)


def _row(text: str, line: int) -> AddedEmote:
    columns = text.split(COLUMN_END)
    if len(columns) <= FIXED_COLUMNS:
        raise _corrupt(line, f"it has {len(columns)} columns, and a row needs at least {FIXED_COLUMNS + 1}")
    action, elf_action, keys, plays, *triggers = columns
    if plays not in PLAYS:
        raise _corrupt(line, f"{plays!r} is neither {LOOP} nor {ONCE}")
    emote = AddedEmote(
        _whole(action, line, "the action"),
        _whole(elf_action, line, "the elf's action"),
        _keys_per_step(keys, line),
        PLAYS[plays],
        tuple(triggers),
    )
    try:
        return checked_emote(emote)
    except MutoolsError as error:
        raise _corrupt(line, error.message) from error


def read_emote_table(data: bytes, encoding: str) -> tuple[AddedEmote, ...]:
    """Data/MuRet/Emotes.txt, read as the client reads it; an action comes once."""
    try:
        text = data.decode(encoding)
    except UnicodeDecodeError as error:
        raise MutoolsError(ErrorCode.CORRUPT_FILE, f"The emote table is not {encoding} text: {error}") from error
    header, *rows = text.split(LINE_END)
    if header.removesuffix("\r") != EMOTE_TABLE_HEADER:
        raise _corrupt(1, f"it is not {EMOTE_TABLE_HEADER!r}")
    emotes: list[AddedEmote] = []
    for line, row in enumerate(rows, start=2):
        content = row.removesuffix("\r")
        if not content:
            continue
        emote = _row(content, line)
        if any(other.action == emote.action for other in emotes):
            raise _corrupt(line, f"action {emote.action} already has a row")
        emotes.append(emote)
    return tuple(emotes)


def emote_table_bytes(emotes: tuple[AddedEmote, ...], encoding: str) -> bytes:
    rows = [
        COLUMN_END.join(
            (str(emote.action), str(emote.elf_action), repr(emote.keys_per_step), emote.plays, *emote.triggers)
        )
        for emote in emotes
    ]
    text = LINE_END.join((EMOTE_TABLE_HEADER, *rows)) + LINE_END
    try:
        data = text.encode(encoding)
    except UnicodeEncodeError as error:
        raise MutoolsError(
            ErrorCode.NOT_ENCODABLE,
            f"A trigger has {error.object[error.start : error.end]!r}, which {encoding} cannot write; the client "
            "compares a trigger with the chat's bytes",
            suggestion=None if encoding == CHAT_ENCODING else f"--encoding {CHAT_ENCODING}",
        ) from error
    if len(data) > MOST_TABLE_BYTES:
        raise _invalid(
            f"The emote table would be {len(data):,} bytes, and the client reads one of at most {MOST_TABLE_BYTES:,}",
            "/triggers",
        )
    return data
