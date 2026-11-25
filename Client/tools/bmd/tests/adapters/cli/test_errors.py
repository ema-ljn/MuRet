import json
import os
from pathlib import Path

import pytest

from mubmd.adapters.storage import LocalFileStore
from mubmd.core.documents.text_table import TextTable
from mubmd.core.formats.text_format import TextFormat
from tests.adapters.cli.conftest import Cli, Run


def check(run: Run, code: str, exit_code: int) -> dict[str, object]:
    assert run.out == ""
    assert run.code == exit_code
    envelope = json.loads(run.err)
    assert (envelope["ok"], envelope["schema"]) == (False, 1)
    error = envelope["error"]
    assert error.keys() == {"code", "message", "suggestion", "input", "pointer"}
    assert error["code"] == code
    assert isinstance(error["input"], dict)
    return error  # type: ignore[no-any-return]


def test_a_missing_argument_is_usage(cli: Cli, text: Path) -> None:
    error = check(cli("get", text), "USAGE", 2)
    assert error["suggestion"] == "mubmd get --help"


def test_an_unknown_flag_is_usage(cli: Cli, text: Path) -> None:
    check(cli("info", text, "--bogus"), "USAGE", 2)


def test_an_unknown_command_is_usage(cli: Cli) -> None:
    error = check(cli("edit"), "USAGE", 2)
    assert error["suggestion"] == "mubmd --help"


def test_a_bad_option_value_is_usage(cli: Cli, text: Path) -> None:
    check(cli("find", text, "a", "--limit", "0"), "USAGE", 2)


def test_a_missing_file_is_not_found(cli: Cli, tmp_path: Path) -> None:
    check(cli("info", tmp_path / "Text.bmd"), "FILE_NOT_FOUND", 20)


def test_a_file_of_a_later_stage_is_an_unknown_format(cli: Cli, working_copy: Path) -> None:
    (working_copy / "Local" / "item.bmd").write_bytes(b"x")
    error = check(cli("info", working_copy / "Local" / "item.bmd"), "UNKNOWN_FORMAT", 20)
    assert error["suggestion"] == "mubmd formats"


def test_a_missing_pointer_is_not_found(cli: Cli, text: Path) -> None:
    error = check(cli("get", text, "/texts/2048"), "POINTER_NOT_FOUND", 20)
    assert error["pointer"] == "/texts/2048"
    assert error["suggestion"] == f"mubmd get {text} '' --limit 3"


def test_a_bad_regex_is_an_invalid_value(cli: Cli, text: Path) -> None:
    check(cli("find", text, "(", "--regex"), "INVALID_VALUE", 2)


def test_a_long_text_is_too_long(cli: Cli, text: Path) -> None:
    error = check(cli("set", text, "/texts/1", "a" * 301), "TEXT_TOO_LONG", 2)
    assert error["pointer"] == "/texts/1"
    assert "301 bytes, the record holds 300" in str(error["message"])


def test_an_emoji_is_not_encodable(cli: Cli, text: Path) -> None:
    before = text.read_bytes()
    error = check(cli("set", text, "/texts/500", "😀"), "NOT_ENCODABLE", 2)
    assert error["pointer"] == "/texts/500"
    assert text.read_bytes() == before


def test_a_short_file_is_corrupt(cli: Cli, text: Path) -> None:
    text.write_bytes(b"short")
    check(cli("info", text), "CORRUPT_FILE", 30)


def test_the_reference_is_read_only(cli: Cli, reference: Path) -> None:
    error = check(cli("set", reference / "Local" / "Text.bmd", "/texts/1", "x"), "REFERENCE_READ_ONLY", 30)
    assert str(error["suggestion"]).startswith("mubmd workspace init")


def test_a_file_changed_meanwhile_is_stale(cli: Cli, text: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    read = LocalFileStore.read

    def read_then_change(self: LocalFileStore, path: Path) -> bytes:
        data = read(self, path)
        path.write_bytes(data[:-1] + b"\x01")
        return data

    monkeypatch.setattr(LocalFileStore, "read", read_then_change)
    check(cli("set", text, "/texts/500", "x"), "STALE_FILE", 30)


def test_a_failing_test_operation(cli: Cli, text: Path) -> None:
    patch = json.dumps([{"op": "test", "path": "/texts/500", "value": "otro"}])
    error = check(cli("apply", text, patch), "TEST_FAILED", 30)
    assert error["suggestion"] == f"mubmd get {text} /texts/500"


def test_bytes_that_do_not_decode_back_fail_the_write(cli: Cli, text: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    encode = TextFormat.encode

    def broken(self: TextFormat, document: TextTable) -> bytes:
        data = encode(self, document)
        return bytes([data[0] ^ 1]) + data[1:]

    monkeypatch.setattr(TextFormat, "encode", broken)
    check(cli("set", text, "/texts/500", "x"), "WRITE_VERIFY_FAILED", 30)


def test_init_into_a_folder_that_is_not_empty(cli: Cli, reference: Path, tmp_path: Path) -> None:
    (tmp_path / "work").mkdir()
    (tmp_path / "work" / "notes.txt").write_text("mine")
    error = check(cli("workspace", "init", tmp_path / "work", "--from", reference), "NOT_EMPTY", 30)
    assert "--yes" in str(error["suggestion"])


def test_a_disk_error_is_reported_as_json(cli: Cli, text: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    def fail(source: object, destination: object) -> None:
        raise OSError(28, "No space left on device")

    monkeypatch.setattr(os, "replace", fail)
    check(cli("set", text, "/texts/500", "x"), "IO_ERROR", 30)


def test_a_long_argument_is_cut_in_the_echoed_input(cli: Cli, text: Path) -> None:
    patch = json.dumps([{"op": "test", "path": "/texts/500", "value": "x" * 400}])
    error = check(cli("apply", text, patch), "TEST_FAILED", 30)
    echoed = error["input"]["argv"][2]  # type: ignore[index]
    assert len(echoed) == 201
    assert echoed.endswith("…")
