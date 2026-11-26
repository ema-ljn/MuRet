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
        ("skill/skill.bmd", "skill"),
        ("Skill.bmd", "skill"),
    ],
)
def test_a_table_outside_its_model_folder_is_found(name: str, expected: str) -> None:
    assert identify(name, b"").format.name == expected


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
    ]
    assert format_by_name("text").size == 614_400
    assert format_by_name("skill").size == 256 * 0x3C + 4
    assert format_by_name("gate").size == 256 * 9


def test_an_unknown_format_name_is_reported() -> None:
    with pytest.raises(MubmdError) as caught:
        format_by_name("items")
    assert caught.value.code is ErrorCode.UNKNOWN_FORMAT
