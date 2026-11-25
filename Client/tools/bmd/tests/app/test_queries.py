import pytest

from mubmd.app.queries import find, format_schema, get, info, list_formats
from mubmd.core.errors import ErrorCode, MubmdError
from tests.app.conftest import REFERENCE_TEXT, TEXT
from tests.app.fake_store import FakeStore


def test_info_summarizes_the_file(store: FakeStore) -> None:
    result = info(store, TEXT)
    assert (result.format, result.encoding, result.cipher) == ("text", "cp1252", "XOR3")
    assert (result.records, result.used_records, result.size) == (2048, 1645, 614_400)
    assert result.read_by_client
    assert not result.read_only
    assert result.findings == 1


def test_info_marks_the_reference_read_only(store: FakeStore) -> None:
    assert info(store, REFERENCE_TEXT).read_only


def test_find_pages_its_matches(store: FakeStore) -> None:
    everything = find(store, TEXT, "usuario", limit=1000)
    first = find(store, TEXT, "usuario", limit=2)
    second = find(store, TEXT, "usuario", limit=2, offset=2)
    assert everything.total > 4
    assert first.items == everything.items[:2]
    assert first.next_offset == 2
    assert second.items == everything.items[2:4]
    assert first.total == everything.total
    assert find(store, TEXT, "usuario", limit=1000, offset=everything.total - 1).next_offset is None


def test_find_ignores_case_and_returns_pointers(store: FakeStore) -> None:
    matches = find(store, TEXT, "EL USUARIO SALIÓ").items
    assert [(match.index, match.pointer, match.text) for match in matches] == [
        (500, "/texts/500", "El usuario salió del juego"),
        (506, "/texts/506", "El usuario salió del juego"),
    ]


def test_find_takes_a_regex(store: FakeStore) -> None:
    matches = find(store, TEXT, r"^Zen$", regex=True).items
    assert [match.index for match in matches] == [100, 224]


def test_a_bad_regex_is_invalid(store: FakeStore) -> None:
    with pytest.raises(MubmdError) as caught:
        find(store, TEXT, "(", regex=True)
    assert caught.value.code is ErrorCode.INVALID_VALUE


def test_get_cuts_long_lists(store: FakeStore) -> None:
    result = get(store, TEXT, "/texts", limit=3)
    assert result.truncated
    assert result.value == [
        "",
        "Atencao !!! Hack detectado, a utilizacao de programas hack causa banimento permanente!!",
        "AntiHack corrompido. Por favor reinstale o MU.",
    ]


def test_get_of_a_leaf_is_not_truncated(store: FakeStore) -> None:
    result = get(store, TEXT, "/texts/100")
    assert (result.pointer, result.value, result.truncated) == ("/texts/100", "Zen", False)


def test_get_of_a_missing_pointer_is_not_found(store: FakeStore) -> None:
    with pytest.raises(MubmdError) as caught:
        get(store, TEXT, "/texts/2048")
    assert caught.value.code is ErrorCode.POINTER_NOT_FOUND


def test_formats_and_their_fields_are_described() -> None:
    (text,) = list_formats()
    assert text.name == "text"
    assert [file.name for file in text.files] == ["Text.bmd", "Texttest.bmd", "TKOU.bmd"]
    assert format_schema("text").fields[0].pointer == "/texts/<0..2047>"
