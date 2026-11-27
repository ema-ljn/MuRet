from pathlib import Path

from mubmd.app.documents import format_name_of
from tests.app.conftest import WORK
from tests.app.fake_store import FakeStore


def test_a_files_format_is_known_by_its_name_and_its_first_bytes() -> None:
    sword, text, other = WORK / "Item" / "Sword01.bmd", WORK / "Local" / "Text.bmd", WORK / "Item" / "Other.bmd"
    store = FakeStore({sword: b"BMD\x0a rest", text: b"", other: b"not a model"})
    assert [format_name_of(store, path) for path in (sword, text, other)] == ["model", "text", None]


def test_a_missing_file_has_no_format() -> None:
    assert format_name_of(FakeStore({}), Path("/work/Data/Item/Missing.bmd")) is None
