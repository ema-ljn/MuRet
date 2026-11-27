import errno
import os
import shutil
import subprocess
import sys
import time
from collections.abc import Iterator
from pathlib import Path

import pytest

from mutools.adapters.storage import LOCK_NAME, LocalFileStore, write_lock
from mutools.core.errors import ErrorCode, MutoolsError

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
    with pytest.raises(MutoolsError) as caught:
        STORE.write(path, b"new", expected_sha256=STORE.sha256(path))
    assert caught.value.code is ErrorCode.REFERENCE_READ_ONLY
    assert caught.value.suggestion is not None
    assert caught.value.suggestion.startswith("mutools workspace init")
    assert path.read_bytes() == b"original"


def test_a_folder_without_a_manifest_is_writable(target: Path) -> None:
    assert not STORE.is_reference(target)


def test_a_symlink_into_the_reference_is_read_only(reference: Path, tmp_path: Path) -> None:
    link = tmp_path / "link"
    link.symlink_to(reference / "Local", target_is_directory=True)
    assert STORE.is_reference(link / "Text.bmd")
    with pytest.raises(MutoolsError) as caught:
        STORE.write(link / "Text.bmd", b"new", expected_sha256=STORE.sha256(link / "Text.bmd"))
    assert caught.value.code is ErrorCode.REFERENCE_READ_ONLY


def test_write_replaces_the_file(target: Path) -> None:
    STORE.write(target, b"new", expected_sha256=STORE.sha256(target))
    assert target.read_bytes() == b"new"


def test_write_keeps_the_first_backup(target: Path) -> None:
    STORE.write(target, b"second", expected_sha256=STORE.sha256(target))
    STORE.write(target, b"third", expected_sha256=STORE.sha256(target))
    assert target.with_name("Text.bmd.orig").read_bytes() == b"original"
    assert target.read_bytes() == b"third"


def test_a_bak_from_the_reference_does_not_stop_the_backup(tmp_path: Path) -> None:
    table = tmp_path / "skill.bmd"
    theirs = tmp_path / "skill.bmd.bak"
    table.write_bytes(b"original")
    theirs.write_bytes(b"theirs")
    STORE.write(table, b"new", expected_sha256=STORE.sha256(table))
    assert table.with_name("skill.bmd.orig").read_bytes() == b"original"
    assert theirs.read_bytes() == b"theirs"


def test_a_stale_file_is_not_overwritten(target: Path) -> None:
    read_hash = STORE.sha256(target)
    target.write_bytes(b"the other writer")
    with pytest.raises(MutoolsError) as caught:
        STORE.write(target, b"mine", expected_sha256=read_hash)
    assert caught.value.code is ErrorCode.STALE_FILE
    assert target.read_bytes() == b"the other writer"


def test_a_new_file_must_not_exist_yet(target: Path) -> None:
    with pytest.raises(MutoolsError) as caught:
        STORE.write(target, b"mine", expected_sha256=None)
    assert caught.value.code is ErrorCode.STALE_FILE


def test_a_new_file_is_written(tmp_path: Path) -> None:
    STORE.write(tmp_path / "new.bmd", b"mine", expected_sha256=None)
    assert (tmp_path / "new.bmd").read_bytes() == b"mine"
    assert not (tmp_path / "new.bmd.orig").exists()


def test_a_failed_write_leaves_the_original(target: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    def fail(source: object, destination: object) -> None:
        raise OSError("disk full")

    monkeypatch.setattr(os, "replace", fail)
    with pytest.raises(OSError, match="disk full"):
        STORE.write(target, b"new", expected_sha256=STORE.sha256(target))
    assert target.read_bytes() == b"original"
    assert list(target.parent.glob("*.tmp")) == []


CHILD_WRITER = """
import sys
from pathlib import Path
from mutools.adapters.storage import LocalFileStore
from mutools.core.errors import MutoolsError

store, path = LocalFileStore(), Path(sys.argv[1])
try:
    store.write(path, b"child", expected_sha256=store.sha256(path))
    print("written")
except MutoolsError as error:
    print(error.code)
"""


def test_a_writer_that_lost_the_race_gets_stale_file(target: Path) -> None:
    with write_lock(target):
        child = subprocess.Popen([sys.executable, "-c", CHILD_WRITER, str(target)], stdout=subprocess.PIPE, text=True)
        deadline = time.monotonic() + 20
        while child.poll() is None and not list(target.parent.glob("*.tmp")) and time.monotonic() < deadline:
            time.sleep(0.01)
        target.write_bytes(b"the winner")
    out, _ = child.communicate(timeout=20)
    assert out.strip() == "STALE_FILE"
    assert target.read_bytes() == b"the winner"
    assert list(target.parent.glob("*.tmp")) == []


def test_a_backup_that_fails_halfway_leaves_no_backup(target: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    def half_copy(source: Path, destination: Path) -> None:
        Path(destination).write_bytes(b"ori")
        raise OSError(errno.ENOSPC, "No space left on device")

    monkeypatch.setattr(shutil, "copy2", half_copy)
    with pytest.raises(OSError, match="No space left"):
        STORE.write(target, b"new", expected_sha256=STORE.sha256(target))
    assert target.read_bytes() == b"original"
    assert sorted(path.name for path in target.parent.iterdir() if path.name != LOCK_NAME) == ["Text.bmd"]


def test_a_backup_link_into_the_reference_is_not_followed(reference: Path, target: Path) -> None:
    planted = reference / "Local" / "planted.bmd"
    backup = target.with_name("Text.bmd.orig")
    backup.symlink_to(planted)
    STORE.write(target, b"new", expected_sha256=STORE.sha256(target))
    assert not planted.exists()
    assert not backup.is_symlink()
    assert backup.read_bytes() == b"original"


def test_a_temporary_link_into_the_reference_is_not_followed(reference: Path, target: Path) -> None:
    planted = reference / "Local" / "planted.bmd"
    target.with_name(f"Text.bmd.{os.getpid()}.tmp").symlink_to(planted)
    with pytest.raises(FileExistsError):
        STORE.write(target, b"new", expected_sha256=STORE.sha256(target))
    assert not planted.exists()
    assert target.read_bytes() == b"original"


def test_a_lock_link_into_the_reference_is_not_followed(reference: Path, target: Path) -> None:
    original = reference / "Local" / "Text.bmd"
    (target.parent / LOCK_NAME).symlink_to(original)
    with pytest.raises(OSError):
        STORE.write(target, b"new", expected_sha256=STORE.sha256(target))
    assert original.read_bytes() == b"original"
    assert target.read_bytes() == b"original"


def test_a_missing_folder_is_not_found(tmp_path: Path) -> None:
    with pytest.raises(MutoolsError) as caught:
        STORE.write(tmp_path / "missing" / "Text.bmd", b"x", expected_sha256=None)
    assert caught.value.code is ErrorCode.FILE_NOT_FOUND


def test_reading_a_missing_file_is_not_found(tmp_path: Path) -> None:
    with pytest.raises(MutoolsError) as caught:
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
    with pytest.raises(MutoolsError) as caught:
        STORE.copy(source, reference / "Local" / "Copy.bmd")
    assert caught.value.code is ErrorCode.REFERENCE_READ_ONLY


def test_files_in_and_folders_in_list_one_level(tmp_path: Path) -> None:
    (tmp_path / "Item" / "deep").mkdir(parents=True)
    (tmp_path / "skill").mkdir()
    for name in ("a.OZJ", "Item/b.OZJ", "Item/deep/c.OZJ"):
        (tmp_path / name).touch()
    store = LocalFileStore()
    assert store.files_in(tmp_path) == (tmp_path / "a.OZJ",)
    assert store.files_in(tmp_path / "Item") == (tmp_path / "Item" / "b.OZJ",)
    assert store.folders_in(tmp_path) == (tmp_path / "Item", tmp_path / "skill")


def test_a_folder_that_is_not_there_lists_nothing(tmp_path: Path) -> None:
    store = LocalFileStore()
    assert store.files_in(tmp_path / "missing") == ()
    assert store.folders_in(tmp_path / "missing") == ()


def test_head_gives_the_first_bytes(tmp_path: Path) -> None:
    file = tmp_path / "a.bmd"
    file.write_bytes(b"BMD\x0a and more")
    store = LocalFileStore()
    assert store.head(file, 3) == b"BMD"
    assert store.head(file, 100) == b"BMD\x0a and more"


def test_head_of_a_missing_file_is_not_found(tmp_path: Path) -> None:
    with pytest.raises(MutoolsError) as caught:
        LocalFileStore().head(tmp_path / "missing.bmd", 3)
    assert caught.value.code is ErrorCode.FILE_NOT_FOUND


def test_a_folder_that_cannot_be_listed_lists_nothing(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    locked = tmp_path / "locked"
    locked.mkdir()
    listing = Path.iterdir

    def refuse(folder: Path) -> Iterator[Path]:
        if folder == locked:
            raise PermissionError(errno.EACCES, "Permission denied", str(folder))
        return listing(folder)

    monkeypatch.setattr(Path, "iterdir", refuse)
    store = LocalFileStore()
    assert store.files_in(locked) == ()
    assert store.folders_in(locked) == ()
