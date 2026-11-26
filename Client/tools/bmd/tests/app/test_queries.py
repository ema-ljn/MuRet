from pathlib import Path

import pytest

from mubmd.app.queries import find, format_schema, get, info, list_formats
from mubmd.core.errors import ErrorCode, MubmdError
from tests.app.conftest import DIALOG, ITEM_SET_OPTIONS, MOVE_REQUIREMENTS, REFERENCE_TEXT, TEXT
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
    text, item, *_ = list_formats()
    assert text.name == "text"
    assert [file.name for file in text.files] == ["Text.bmd", "Texttest.bmd", "TKOU.bmd"]
    assert format_schema("text").fields[0].pointer == "/texts/<0..2047>"
    assert item.name == "item"
    assert [file.name for file in item.files] == ["item.bmd", "itemtest.bmd", "n380.bmd"]
    assert format_schema("item").fields[0].pointer == "/records/<0..8191>/name"


def test_find_reads_a_text_with_a_tail(tables: FakeStore) -> None:
    matches = find(tables, DIALOG, "Aew").items
    assert [(match.index, match.pointer, match.text) for match in matches] == [
        (0, "/records/0/text", "Aew! Precisa de alguma coisa?")
    ]


def test_find_field_keeps_one_field(tables: FakeStore) -> None:
    everywhere = find(tables, MOVE_REQUIREMENTS, "Arena").items
    assert [match.pointer for match in everywhere] == ["/records/0/name", "/records/0/search_name"]
    named = find(tables, MOVE_REQUIREMENTS, "Arena", field="name").items
    assert [match.pointer for match in named] == ["/records/0/name"]


def test_find_field_names_the_field_of_a_list(tables: FakeStore) -> None:
    matches = find(tables, DIALOG, "I will go find it.", field="answers").items
    assert [(match.index, match.pointer) for match in matches] == [
        (53, "/records/53/answers/1"),
        (54, "/records/54/answers/0"),
    ]
    assert find(tables, DIALOG, "I will go find it.", field="text").total == 0


@pytest.mark.parametrize(("path", "query"), [(ITEM_SET_OPTIONS, "ffff"), (MOVE_REQUIREMENTS, "d9cbb9")])
def test_find_never_matches_hex(tables: FakeStore, path: Path, query: str) -> None:
    assert find(tables, path, query).total == 0


def test_find_refuses_a_field_the_format_does_not_have(tables: FakeStore) -> None:
    with pytest.raises(MubmdError) as caught:
        find(tables, MOVE_REQUIREMENTS, "Arena", field="nombre")
    assert caught.value.code is ErrorCode.INVALID_VALUE
    assert caught.value.suggestion == "mubmd schema move_requirement"
