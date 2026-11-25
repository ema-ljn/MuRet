import os
from pathlib import Path

import pytest

from mubmd.adapters.storage import LocalFileStore
from mubmd.core.errors import ErrorCode, MubmdError

STORE = LocalFileStore()


@pytest.fixture
def reference(tmp_path: Path) -> Path:
    data = tmp_path / "Data"
    (data / "Local").mkdir(parents=True)
    (data / "Local" / "Text.bmd").write_bytes(b"original")
    (tmp_path / "Data.sha256").write_text("")
    return data


@pytest.fixture
def target(tmp_path: Path) -> Path:
    path = tmp_path / "copy" / "Text.bmd"
    path.parent.mkdir()
    path.write_bytes(b"original")
    return path


def test_reference_folder_is_read_only(reference: Path) -> None:
    path = reference / "Local" / "Text.bmd"
    assert STORE.is_reference(reference)
    assert STORE.is_reference(path)
    with pytest.raises(MubmdError) as caught:
        STORE.write(path, b"new", expected_sha256=STORE.sha256(path))
    assert caught.value.code is ErrorCode.REFERENCE_READ_ONLY
    assert caught.value.suggestion is not None
    assert caught.value.suggestion.startswith("mubmd workspace init")
    assert path.read_bytes() == b"original"


def test_a_folder_without_a_manifest_is_writable(target: Path) -> None:
    assert not STORE.is_reference(target)


def test_a_symlink_into_the_reference_is_read_only(reference: Path, tmp_path: Path) -> None:
    link = tmp_path / "link"
    link.symlink_to(reference / "Local", target_is_directory=True)
    assert STORE.is_reference(link / "Text.bmd")
    with pytest.raises(MubmdError) as caught:
        STORE.write(link / "Text.bmd", b"new", expected_sha256=STORE.sha256(link / "Text.bmd"))
    assert caught.value.code is ErrorCode.REFERENCE_READ_ONLY


def test_write_replaces_the_file(target: Path) -> None:
    STORE.write(target, b"new", expected_sha256=STORE.sha256(target))
    assert target.read_bytes() == b"new"


def test_write_keeps_the_first_backup(target: Path) -> None:
    STORE.write(target, b"second", expected_sha256=STORE.sha256(target))
    STORE.write(target, b"third", expected_sha256=STORE.sha256(target))
    assert target.with_name("Text.bmd.bak").read_bytes() == b"original"
    assert target.read_bytes() == b"third"


def test_a_stale_file_is_not_overwritten(target: Path) -> None:
    read_hash = STORE.sha256(target)
    target.write_bytes(b"the other writer")
    with pytest.raises(MubmdError) as caught:
        STORE.write(target, b"mine", expected_sha256=read_hash)
    assert caught.value.code is ErrorCode.STALE_FILE
    assert target.read_bytes() == b"the other writer"


def test_a_new_file_must_not_exist_yet(target: Path) -> None:
    with pytest.raises(MubmdError) as caught:
        STORE.write(target, b"mine", expected_sha256=None)
    assert caught.value.code is ErrorCode.STALE_FILE


def test_a_new_file_is_written(tmp_path: Path) -> None:
    STORE.write(tmp_path / "new.bmd", b"mine", expected_sha256=None)
    assert (tmp_path / "new.bmd").read_bytes() == b"mine"
    assert not (tmp_path / "new.bmd.bak").exists()


def test_a_failed_write_leaves_the_original(target: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    def fail(source: object, destination: object) -> None:
        raise OSError("disk full")

    monkeypatch.setattr(os, "replace", fail)
    with pytest.raises(OSError, match="disk full"):
        STORE.write(target, b"new", expected_sha256=STORE.sha256(target))
    assert target.read_bytes() == b"original"
    assert list(target.parent.glob("*.tmp")) == []


def test_a_missing_folder_is_not_found(tmp_path: Path) -> None:
    with pytest.raises(MubmdError) as caught:
        STORE.write(tmp_path / "missing" / "Text.bmd", b"x", expected_sha256=None)
    assert caught.value.code is ErrorCode.FILE_NOT_FOUND


def test_reading_a_missing_file_is_not_found(tmp_path: Path) -> None:
    with pytest.raises(MubmdError) as caught:
        STORE.read(tmp_path / "Text.bmd")
    assert caught.value.code is ErrorCode.FILE_NOT_FOUND


def test_list_bmd_finds_any_letter_case(tmp_path: Path) -> None:
    (tmp_path / "Local").mkdir()
    for name in ("Local/TEXT.BMD", "a.bmd", "b.ozj", "Local/item.Bmd"):
        (tmp_path / name).write_bytes(b"")
    assert STORE.list_bmd(tmp_path) == (tmp_path / "Local/TEXT.BMD", tmp_path / "Local/item.Bmd", tmp_path / "a.bmd")


def test_copy_creates_folders_and_refuses_the_reference(reference: Path, tmp_path: Path) -> None:
    source = reference / "Local" / "Text.bmd"
    STORE.copy(source, tmp_path / "out" / "Local" / "Text.bmd")
    assert (tmp_path / "out" / "Local" / "Text.bmd").read_bytes() == b"original"
    with pytest.raises(MubmdError) as caught:
        STORE.copy(source, reference / "Local" / "Copy.bmd")
    assert caught.value.code is ErrorCode.REFERENCE_READ_ONLY
