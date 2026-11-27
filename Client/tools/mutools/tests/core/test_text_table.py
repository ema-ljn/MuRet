import pytest

from mutools.core.documents.text_table import TEXT_RECORD_COUNT, TEXT_RECORD_SIZE, TextTable
from mutools.core.errors import ErrorCode, MutoolsError


def table() -> TextTable:
    records = [bytes(TEXT_RECORD_SIZE)] * TEXT_RECORD_COUNT
    records[500] = b"Zen".ljust(TEXT_RECORD_SIZE, b"\0")
    return TextTable(tuple(records), "cp1252")


def test_with_text_encodes_in_the_table_encoding() -> None:
    edited = table().with_text(500, "€")
    assert edited.records[500] == b"\x80".ljust(TEXT_RECORD_SIZE, b"\0")
    assert edited.text(500) == "€"


def test_an_emoji_is_not_encodable() -> None:
    with pytest.raises(MutoolsError) as caught:
        table().with_text(500, "😀")
    assert caught.value.code is ErrorCode.NOT_ENCODABLE
    assert caught.value.pointer == "/texts/500"


def test_a_text_over_300_bytes_is_too_long() -> None:
    with pytest.raises(MutoolsError) as caught:
        table().with_text(500, "a" * 301)
    assert caught.value.code is ErrorCode.TEXT_TOO_LONG
    assert "301 bytes, the record holds 300" in caught.value.message


def test_a_text_of_exactly_300_bytes_fits() -> None:
    assert table().with_text(500, "a" * 300).text(500) == "a" * 300


def test_a_text_cannot_hold_a_zero() -> None:
    with pytest.raises(MutoolsError) as caught:
        table().with_text(500, "a\0b")
    assert caught.value.code is ErrorCode.INVALID_VALUE


def test_an_index_outside_the_table_is_not_found() -> None:
    with pytest.raises(MutoolsError) as caught:
        table().with_text(2048, "x")
    assert caught.value.code is ErrorCode.POINTER_NOT_FOUND


def test_editing_keeps_the_original_table() -> None:
    original = table()
    original.with_text(500, "Joyas")
    assert original.text(500) == "Zen"


def test_bytes_after_the_zero_make_the_text_unreadable() -> None:
    records = list(table().records)
    records[1] = b"ab\0c".ljust(TEXT_RECORD_SIZE, b"\0")
    assert TextTable(tuple(records), "cp1252").text(1) is None


def test_a_text_whose_bytes_read_back_differently_is_not_encodable() -> None:
    with pytest.raises(MutoolsError) as caught:
        TextTable(table().records, "shift_jis").with_text(500, "100¥")
    assert caught.value.code is ErrorCode.NOT_ENCODABLE


def test_a_text_whose_bytes_hold_a_zero_is_not_encodable() -> None:
    with pytest.raises(MutoolsError) as caught:
        TextTable(table().records, "utf-16").with_text(500, "H")
    assert caught.value.code is ErrorCode.NOT_ENCODABLE
