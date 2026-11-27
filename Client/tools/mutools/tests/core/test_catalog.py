from pathlib import Path

import pytest

from mutools.core.catalog import all_formats, format_by_name, identify, target_of
from mutools.core.errors import ErrorCode, MutoolsError


@pytest.mark.parametrize("name", ["Text.bmd", "TEXT.BMD", "text.bmd"])
def test_text_is_found_by_name_without_case(name: str, text_bytes: bytes) -> None:
    found = identify(name, text_bytes)
    assert found.format.name == "text"
    assert found.file.encoding == "cp1252"
    assert found.file.read_by_client is True


def test_tkou_is_chinese_and_not_read_by_the_client(text_bytes: bytes) -> None:
    found = identify("TKOU.bmd", text_bytes)
    assert found.file.encoding == "gb2312"
    assert found.file.read_by_client is False


def test_a_test_twin_is_not_read_by_the_client(text_bytes: bytes) -> None:
    assert identify("Texttest.bmd", text_bytes).file.read_by_client is False


def test_a_file_that_is_no_table_and_no_model_is_unknown(text_bytes: bytes) -> None:
    with pytest.raises(MutoolsError) as caught:
        identify("Player.bmd", text_bytes)
    assert caught.value.code is ErrorCode.UNKNOWN_FORMAT
    assert caught.value.suggestion == "mutools formats"


@pytest.mark.parametrize(
    "name",
    [
        "Data/Item/item.bmd",
        "data/ITEM/Item.bmd",
        "Data/Item/Itemtest.bmd",
        "Data/skill/skill.bmd",
        "DATA/Skill/SKILL.BMD",
        "work/Item/item.bmd",
        "work/skill/skill.bmd",
        "Item/item.bmd",
        "Data/Local/../Item/item.bmd",
    ],
)
def test_a_table_name_inside_a_model_folder_is_unknown(name: str) -> None:
    with pytest.raises(MutoolsError) as caught:
        identify(name, b"")
    assert caught.value.code is ErrorCode.UNKNOWN_FORMAT


@pytest.mark.parametrize(
    ("name", "expected"),
    [
        ("Data/Local/skill.bmd", "skill"),
        ("Data/Local/item.bmd", "item"),
        ("work/Local/skill.bmd", "skill"),
        ("Data/Item/../Local/item.bmd", "item"),
        ("Skill.bmd", "skill"),
    ],
)
def test_a_table_outside_its_model_folder_is_found(name: str, expected: str) -> None:
    assert identify(name, b"").format.name == expected


@pytest.mark.parametrize(("name", "size"), [("item/item.bmd", 8192 * 0x54 + 8), ("Skill/skill.bmd", 256 * 0x3C + 4)])
def test_a_table_inside_a_folder_named_like_its_models_is_known_by_its_size(name: str, size: int) -> None:
    assert identify(name, bytes(size)).format.name == Path(name).stem


def test_formats_are_listed_and_found_by_name() -> None:
    assert [spec.name for spec in all_formats()] == [
        "text",
        "item",
        "skill",
        "gate",
        "filter",
        "filter_name",
        "item_set_type",
        "item_set_option",
        "credit",
        "slide",
        "move_requirement",
        "monster_skill",
        "dialog",
        "quest",
        "model",
    ]
    assert format_by_name("text").size == 614_400
    assert format_by_name("skill").size == 256 * 0x3C + 4
    assert format_by_name("gate").size == 256 * 9
    assert format_by_name("slide").size == 0xA030
    assert format_by_name("move_requirement").size is None
    assert format_by_name("monster_skill").size is None


def test_an_unknown_format_name_is_reported() -> None:
    with pytest.raises(MutoolsError) as caught:
        format_by_name("items")
    assert caught.value.code is ErrorCode.UNKNOWN_FORMAT


def test_a_model_is_known_by_its_first_bytes() -> None:
    found = identify("Data/Item/Sword01.bmd", b"BMD\x0a")
    assert (found.format.name, found.file.name) == ("model", "Sword01.bmd")
    assert (found.file.encoding, found.file.read_by_client) == ("cp949", None)


def test_a_models_name_alone_does_not_say_what_it_is() -> None:
    with pytest.raises(MutoolsError) as caught:
        identify("Data/Item/Sword01.bmd", b"")
    assert caught.value.code is ErrorCode.UNKNOWN_FORMAT


def test_a_tables_name_wins_over_the_bytes() -> None:
    assert identify("Data/Local/Text.bmd", b"BMD\x0a").format.name == "text"


def test_a_model_inside_a_model_folder_named_like_a_table_is_a_model() -> None:
    assert identify("Data/Item/item.bmd", b"BMD\x0a").format.name == "model"


def test_the_model_format_has_no_files_and_no_size() -> None:
    model = format_by_name("model")
    assert (model.size, model.files) == (None, ())


def test_a_model_view_builds_to_any_bmd_name() -> None:
    target = target_of(Path("work/Item/x.bmd"), format_by_name("model"))
    assert (target.format.name, target.file.name, target.file.encoding) == ("model", "x.bmd", "cp949")


def test_a_table_view_builds_to_its_file() -> None:
    target = target_of(Path("work/Gatetest.bmd"), format_by_name("gate"))
    assert (target.format.name, target.file.read_by_client) == ("gate", False)


@pytest.mark.parametrize(
    ("output", "view", "code"),
    [
        ("work/Gate.bmd", "model", ErrorCode.INVALID_VALUE),
        ("work/Local/Text.bmd", "gate", ErrorCode.INVALID_VALUE),
        ("work/x.bmd", "gate", ErrorCode.UNKNOWN_FORMAT),
        ("work/x.json", "model", ErrorCode.UNKNOWN_FORMAT),
    ],
)
def test_a_view_does_not_build_to_a_file_of_another_format(output: str, view: str, code: ErrorCode) -> None:
    with pytest.raises(MutoolsError) as caught:
        target_of(Path(output), format_by_name(view))
    assert caught.value.code is code
