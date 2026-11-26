from pathlib import Path

import pytest

from mubmd.core.catalog import identify
from mubmd.core.checks.text_checks import check_text_table
from mubmd.core.documents.text_table import TEXT_RECORD_COUNT, TextTable
from mubmd.core.errors import ErrorCode, MubmdError
from mubmd.core.formats.text_format import TextFormat
from mubmd.core.ports import CatalogFile

TEXT = CatalogFile("Text.bmd", "cp1252", read_by_client=True)
CODEC = TextFormat()


def decode(reference_data: Path, name: str) -> TextTable:
    data = (reference_data / "Local" / name).read_bytes()
    return CODEC.decode(data, identify(name, data).file)


@pytest.mark.parametrize("name", ["Text.bmd", "Texttest.bmd", "TKOU.bmd"])
def test_round_trip_is_byte_exact(reference_data: Path, name: str) -> None:
    data = (reference_data / "Local" / name).read_bytes()
    table = CODEC.decode(data, identify(name, data).file)
    assert CODEC.encode(table) == data
    assert CODEC.encode(CODEC.from_view(CODEC.to_view(table))) == data


def test_reads_the_real_texts(reference_data: Path) -> None:
    table = decode(reference_data, "Text.bmd")
    assert table.text(500) == "El usuario salió del juego"
    assert table.text(100) == "Zen"
    assert decode(reference_data, "TKOU.bmd").text(500) == "对方中断连接了"
    assert sum(1 for index in range(TEXT_RECORD_COUNT) if table.text(index)) == 1645


def test_the_view_holds_every_record(reference_data: Path) -> None:
    view = CODEC.to_view(decode(reference_data, "Text.bmd"))
    assert view["format_version"] == 1
    assert view["format"] == "text"
    assert view["encoding"] == "cp1252"
    texts = view["texts"]
    assert isinstance(texts, list)
    assert len(texts) == TEXT_RECORD_COUNT
    assert texts[500] == "El usuario salió del juego"
    assert "trailing" not in view


def test_a_record_that_does_not_decode_is_kept_as_hex(reference_data: Path) -> None:
    texts = CODEC.to_view(decode(reference_data, "TKOU.bmd"))["texts"]
    assert isinstance(texts, list)
    assert isinstance(texts[1829], dict)
    assert len(str(texts[1829]["hex"])) == 600


def test_record_7_fills_its_300_bytes(reference_data: Path) -> None:
    table = decode(reference_data, "Text.bmd")
    text = table.text(7)
    assert text is not None
    assert len(text.encode("cp1252")) == 300
    findings = check_text_table(table, None)
    assert [(finding.code, finding.pointer) for finding in findings] == [("TEXT_FILLS_RECORD", "/texts/7")]


def test_a_short_file_is_corrupt(text_bytes: bytes) -> None:
    with pytest.raises(MubmdError) as caught:
        CODEC.decode(text_bytes[:-1], TEXT)
    assert caught.value.code is ErrorCode.CORRUPT_FILE


def test_bytes_past_the_records_are_kept(text_bytes: bytes) -> None:
    data = text_bytes + b"\x07"
    table = CODEC.decode(data, TEXT)
    assert table.trailing == b"\x07"
    assert CODEC.to_view(table)["trailing"] == "07"
    assert CODEC.encode(CODEC.from_view(CODEC.to_view(table))) == data


def test_a_hex_record_must_hold_300_bytes(text_bytes: bytes) -> None:
    view = CODEC.to_view(CODEC.decode(text_bytes, TEXT))
    texts = view["texts"]
    assert isinstance(texts, list)
    texts[3] = {"hex": "00"}
    with pytest.raises(MubmdError) as caught:
        CODEC.from_view(view)
    assert caught.value.code is ErrorCode.INVALID_VALUE
    assert caught.value.pointer == "/texts/3"


def test_a_view_of_another_version_is_rejected(text_bytes: bytes) -> None:
    view = CODEC.to_view(CODEC.decode(text_bytes, TEXT))
    view["format_version"] = 2
    with pytest.raises(MubmdError) as caught:
        CODEC.from_view(view)
    assert caught.value.code is ErrorCode.INVALID_VALUE
    assert caught.value.pointer == "/format_version"


def test_the_fields_point_to_the_research() -> None:
    (field,) = CODEC.describe_fields()
    assert field.pointer == "/texts/<0..2047>"
    assert field.reference == "Client/docs/research/05-formatos-de-datos.md#tablas-bmd"


def test_the_summary_counts_used_records(reference_data: Path) -> None:
    summary = CODEC.summarize(decode(reference_data, "Text.bmd"))
    assert (summary.cipher, summary.records, summary.used_records) == ("XOR3", 2048, 1645)


def test_text_check_value_warns_of_a_text_that_fills_its_record(reference_data: Path) -> None:
    result = CODEC.check_value("/texts/5", "a" * 300, decode(reference_data, "Text.bmd"))
    assert result.limit == 300
    assert [(finding.code, finding.pointer) for finding in result.warnings] == [("TEXT_FILLS_RECORD", "/texts/5")]


def test_text_check_value_refuses_what_set_would_refuse(reference_data: Path) -> None:
    with pytest.raises(MubmdError) as caught:
        CODEC.check_value("/texts/5", "a" * 301, decode(reference_data, "Text.bmd"))
    assert caught.value.code is ErrorCode.TEXT_TOO_LONG
    assert caught.value.pointer == "/texts/5"


def test_text_check_value_compares_with_the_text_before(reference_data: Path) -> None:
    table = decode(reference_data, "Text.bmd")
    assert table.text(577) == "Aumenta %d%% de Daño"
    result = CODEC.check_value("/texts/577", "Aumenta %s%% de Daño", table)
    assert [finding.code for finding in result.warnings] == ["PRINTF_CHANGED"]


@pytest.mark.parametrize("pointer", ["/texts/2048", "/texts", "/texts/5/0", "/encoding", "/trailing", ""])
def test_text_check_value_needs_a_text_of_the_table(reference_data: Path, pointer: str) -> None:
    with pytest.raises(MubmdError) as caught:
        CODEC.check_value(pointer, "a", decode(reference_data, "Text.bmd"))
    assert caught.value.code is ErrorCode.POINTER_NOT_FOUND
    assert caught.value.pointer == pointer
