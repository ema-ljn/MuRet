from pathlib import Path

import pytest

from mubmd.core.catalog import all_formats, format_by_name, identify
from mubmd.core.errors import ErrorCode, MubmdError


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


def test_a_file_of_a_later_stage_is_unknown(text_bytes: bytes) -> None:
    with pytest.raises(MubmdError) as caught:
        identify("Player.bmd", text_bytes)
    assert caught.value.code is ErrorCode.UNKNOWN_FORMAT
    assert caught.value.suggestion == "mubmd formats"


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
    with pytest.raises(MubmdError) as caught:
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
    ]
    assert format_by_name("text").size == 614_400
    assert format_by_name("skill").size == 256 * 0x3C + 4
    assert format_by_name("gate").size == 256 * 9
    assert format_by_name("slide").size == 0xA030
    assert format_by_name("move_requirement").size is None
    assert format_by_name("monster_skill").size is None


def test_an_unknown_format_name_is_reported() -> None:
    with pytest.raises(MubmdError) as caught:
        format_by_name("items")
    assert caught.value.code is ErrorCode.UNKNOWN_FORMAT
