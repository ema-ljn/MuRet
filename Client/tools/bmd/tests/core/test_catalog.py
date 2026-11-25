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
        identify("Skill.bmd", text_bytes)
    assert caught.value.code is ErrorCode.UNKNOWN_FORMAT
    assert caught.value.suggestion == "mubmd formats"


def test_formats_are_listed_and_found_by_name() -> None:
    assert [spec.name for spec in all_formats()] == ["text", "item"]
    assert format_by_name("text").size == 614_400


def test_an_unknown_format_name_is_reported() -> None:
    with pytest.raises(MubmdError) as caught:
        format_by_name("items")
    assert caught.value.code is ErrorCode.UNKNOWN_FORMAT
