import pytest

from mutools.core.errors import ErrorCode, MutoolsError
from mutools.core.formats.emote_table import (
    MOST_TABLE_BYTES,
    AddedEmote,
    checked_emote,
    emote_table_bytes,
    read_emote_table,
)

WAVE = AddedEmote(205, 206, 0.32, "loop", ("wave", "/wave"))
SIT = AddedEmote(230, 230, 1.0, "once", ("sit",))


def refused(call: object, *args: object) -> MutoolsError:
    assert callable(call)
    with pytest.raises(MutoolsError) as caught:
        call(*args)
    return caught.value


def test_reads_each_row() -> None:
    text = b"MuRet emotes 1\r\n205\t206\t0.32\tloop\twave\t/wave\r\n\n230\t230\t1\tonce\tsit\n"

    assert read_emote_table(text, "cp1252") == (WAVE, SIT)


def test_writes_one_row_a_line_after_the_header() -> None:
    assert emote_table_bytes((WAVE, SIT), "cp1252") == (
        b"MuRet emotes 1\n205\t206\t0.32\tloop\twave\t/wave\n230\t230\t1.0\tonce\tsit\n"
    )


def test_what_it_writes_reads_back() -> None:
    emotes = (WAVE, AddedEmote(255, 0, 1e-05, "once", ("¡hola",)))

    assert read_emote_table(emote_table_bytes(emotes, "cp1252"), "cp1252") == emotes


def test_a_file_with_only_the_header_has_none() -> None:
    assert read_emote_table(b"MuRet emotes 1\n", "cp1252") == ()


@pytest.mark.parametrize("text", [b"", b"205\t205\t0.32\tloop\twave\n", b"MuRet emotes 2\n"])
def test_another_first_line_is_not_the_file(text: bytes) -> None:
    error = refused(read_emote_table, text, "cp1252")

    assert error.code is ErrorCode.CORRUPT_FILE
    assert error.message.startswith("Line 1 ")


@pytest.mark.parametrize(
    "row",
    [
        "205\t205\t0.32\tloop",
        "204\t204\t0.32\tloop\tw",
        "256\t205\t0.32\tloop\tw",
        "x\t205\t0.32\tloop\tw",
        "205\t256\t0.32\tloop\tw",
        "205\t205\t0\tloop\tw",
        "205\t205\t-1\tloop\tw",
        "205\t205\t10.5\tloop\tw",
        "205\t205\tfast\tloop\tw",
        "205\t205\t0.32\tsometimes\tw",
        "205\t205\t0.32\tloop\t",
        "205\t205\t0.32\tloop\tw\t",
        "205\t205\t+0.32\tloop\tw",
        "205\t205\t 0.32\tloop\tw",
        "205\t205\t0_32\tloop\tw",
        "205\t205\tinf\tloop\tw",
        "+205\t205\t0.32\tloop\tw",
    ],
)
def test_a_row_that_breaks_a_rule_is_named_by_its_line(row: str) -> None:
    error = refused(read_emote_table, f"MuRet emotes 1\n{row}\n".encode(), "cp1252")

    assert error.code is ErrorCode.CORRUPT_FILE
    assert error.message.startswith("Line 2 ")


def test_an_action_comes_once() -> None:
    error = refused(
        read_emote_table, b"MuRet emotes 1\n205\t205\t0.32\tloop\twave\n205\t205\t0.4\tonce\tbow\n", "cp1252"
    )

    assert error.message.startswith("Line 3 ")


def test_text_the_encoding_cannot_read_is_corrupt() -> None:
    assert refused(read_emote_table, b"MuRet emotes 1\n205\t205\t0.32\tloop\t\x81\n", "cp1252").code is (
        ErrorCode.CORRUPT_FILE
    )


@pytest.mark.parametrize(
    ("emote", "pointer"),
    [
        (AddedEmote(204, 204, 0.32, "loop", ("w",)), "/action"),
        (AddedEmote(256, 205, 0.32, "loop", ("w",)), "/action"),
        (AddedEmote(205, 256, 0.32, "loop", ("w",)), "/elf_action"),
        (AddedEmote(205, -1, 0.32, "loop", ("w",)), "/elf_action"),
        (AddedEmote(205, 205, 0.0, "loop", ("w",)), "/keys_per_step"),
        (AddedEmote(205, 205, 10.5, "loop", ("w",)), "/keys_per_step"),
        (AddedEmote(205, 205, float("nan"), "loop", ("w",)), "/keys_per_step"),
        (AddedEmote(205, 205, 0.32, "loop", ()), "/triggers"),
        (AddedEmote(205, 205, 0.32, "loop", ("w", "")), "/triggers/1"),
        (AddedEmote(205, 205, 0.32, "loop", ("a\tb",)), "/triggers/0"),
        (AddedEmote(205, 205, 0.32, "loop", ("a\nb",)), "/triggers/0"),
    ],
)
def test_an_emote_the_client_does_not_read_is_invalid(emote: AddedEmote, pointer: str) -> None:
    error = refused(checked_emote, emote)

    assert (error.code, error.pointer) == (ErrorCode.INVALID_VALUE, pointer)


def test_a_trigger_the_encoding_cannot_write_is_not_encodable() -> None:
    error = refused(emote_table_bytes, (AddedEmote(205, 205, 0.32, "loop", ("漢",)),), "cp1252")

    assert error.code is ErrorCode.NOT_ENCODABLE


def test_a_table_the_client_would_not_read_is_invalid() -> None:
    triggers = tuple(f"trigger{index:05}" for index in range(MOST_TABLE_BYTES // 10))

    error = refused(emote_table_bytes, (AddedEmote(205, 205, 0.32, "loop", triggers),), "cp1252")

    assert error.code is ErrorCode.INVALID_VALUE
