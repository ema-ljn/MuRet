import json
from pathlib import Path

import pytest

from mubmd.adapters.storage import LocalFileStore
from mubmd.app.edits import build, set_value
from mubmd.app.folders import diff, dump, roundtrip, validate
from mubmd.core.errors import ErrorCode, MubmdError
from mubmd.core.patch import Change
from tests.app.conftest import TEXT, WORK
from tests.app.fake_store import FakeStore


def test_roundtrip_of_a_working_copy_is_identical(working_copy: Path) -> None:
    (working_copy / "Local" / "skill.bmd").write_bytes(b"later stage")
    result = roundtrip(LocalFileStore(), working_copy)
    assert (result.checked, result.identical, result.differing, result.skipped) == (3, 3, (), 1)


def test_validate_a_folder_reports_the_files_with_findings(working_copy: Path) -> None:
    (working_copy / "Local" / "skill.bmd").write_bytes(b"later stage")
    result = validate(LocalFileStore(), working_copy)
    by_file = {Path(entry.path).name: [finding.code for finding in entry.findings] for entry in result.files}
    assert by_file == {
        "Text.bmd": ["TEXT_FILLS_RECORD"],
        "Texttest.bmd": ["TEXT_FILLS_RECORD", "TEXT_FILLS_RECORD"],
        "TKOU.bmd": ["TEXT_HAS_TAIL"],
    }
    assert (result.checked, result.skipped) == (3, 1)


def test_validate_one_file_always_reports_it(store: FakeStore) -> None:
    set_value(store, TEXT, "/texts/7", "corto", dry_run=False)
    result = validate(store, TEXT)
    assert [(entry.path, entry.findings) for entry in result.files] == [(TEXT, ())]
    assert (result.checked, result.skipped) == (1, 0)


def test_validate_reports_a_short_file_as_an_error(working_copy: Path) -> None:
    (working_copy / "Local" / "Texttest.bmd").write_bytes(b"short")
    result = validate(LocalFileStore(), working_copy)
    by_file = {Path(entry.path).name: entry.findings for entry in result.files}
    assert [(finding.code, finding.severity) for finding in by_file["Texttest.bmd"]] == [("CORRUPT_FILE", "error")]


def test_dump_then_build_gives_the_same_bytes(store: FakeStore, text_bytes: bytes) -> None:
    out = WORK / "text.json"
    dumped = dump(store, TEXT, out)
    assert dumped.size == len(store.files[out])
    assert json.loads(store.files[out])["texts"][500] == "El usuario salió del juego"
    target = WORK / "Local" / "Texttest.bmd"
    result = build(store, out, target, dry_run=False)
    assert result.changed
    assert store.files[target] == text_bytes


def test_build_over_the_same_bytes_changes_nothing(store: FakeStore) -> None:
    out = WORK / "text.json"
    dump(store, TEXT, out)
    writes = store.writes
    result = build(store, out, TEXT, dry_run=False)
    assert not result.changed
    assert store.writes == writes


def test_build_refuses_another_view_version(store: FakeStore) -> None:
    out = WORK / "text.json"
    dump(store, TEXT, out)
    view = json.loads(store.files[out])
    view["format_version"] = 2
    store.files[out] = json.dumps(view).encode()
    with pytest.raises(MubmdError) as caught:
        build(store, out, WORK / "Local" / "Texttest.bmd", dry_run=False)
    assert caught.value.code is ErrorCode.INVALID_VALUE


def test_build_writes_only_files_the_catalog_knows(store: FakeStore) -> None:
    out = WORK / "text.json"
    dump(store, TEXT, out)
    with pytest.raises(MubmdError) as caught:
        build(store, out, WORK / "Local" / "Mine.bmd", dry_run=False)
    assert caught.value.code is ErrorCode.UNKNOWN_FORMAT


def test_diff_between_a_file_and_its_edited_copy(store: FakeStore) -> None:
    copy = WORK / "Local" / "Texttest.bmd"
    store.files[copy] = store.files[TEXT]
    set_value(store, copy, "/texts/500", "Te fuiste", dry_run=False)
    assert diff(store, TEXT, copy).changes == (Change("/texts/500", "El usuario salió del juego", "Te fuiste"),)


def test_diff_against_a_json_view(store: FakeStore) -> None:
    out = WORK / "text.json"
    dump(store, TEXT, out)
    view = json.loads(store.files[out])
    view["texts"][100] = "Zen!"
    store.files[out] = json.dumps(view).encode()
    assert diff(store, TEXT, out).changes == (Change("/texts/100", "Zen", "Zen!"),)


def test_a_dry_run_dump_writes_nothing(store: FakeStore) -> None:
    result = dump(store, TEXT, WORK / "text.json", dry_run=True)
    assert result.size > 0
    assert not store.exists(WORK / "text.json")


def test_build_writes_a_view_in_its_own_encoding(working_copy: Path, tmp_path: Path) -> None:
    store = LocalFileStore()
    chinese, target = working_copy / "Local" / "TKOU.bmd", working_copy / "Local" / "Texttest.bmd"
    dump(store, chinese, tmp_path / "tkou.json")
    result = build(store, tmp_path / "tkou.json", target, dry_run=False)
    assert result.changed
    assert target.read_bytes() == chinese.read_bytes()
