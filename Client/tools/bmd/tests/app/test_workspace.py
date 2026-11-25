import hashlib
from pathlib import Path

import pytest

from mubmd.app.workspace import workspace_init
from mubmd.core.errors import ErrorCode, MubmdError
from tests.app.fake_store import FakeStore

SOURCE = Path("/ref/Data")
DEST = Path("/home/me/mu/Data")


def store_with_reference() -> FakeStore:
    files = {SOURCE / "Local" / "Text.bmd": b"text", SOURCE / "Dec2.dat": b"dec"}
    manifest = "".join(
        f"{hashlib.sha256(data).hexdigest()}  ./{path.relative_to(SOURCE).as_posix()}\n" for path, data in files.items()
    )
    files[Path("/ref/Data.sha256")] = manifest.encode()
    return FakeStore(files, reference=SOURCE)


def test_init_copies_and_checks_the_tree() -> None:
    store = store_with_reference()
    result = workspace_init(store, SOURCE, DEST, yes=False)
    assert (result.dest, result.files, result.changed) == (DEST, 2, True)
    assert store.files[DEST / "Local" / "Text.bmd"] == b"text"


def test_init_twice_changes_nothing_the_second_time() -> None:
    store = store_with_reference()
    workspace_init(store, SOURCE, DEST, yes=False)
    copies = dict(store.files)
    result = workspace_init(store, SOURCE, DEST, yes=False)
    assert not result.changed
    assert store.files == copies


def test_a_folder_that_is_not_a_working_copy_needs_yes() -> None:
    store = store_with_reference()
    store.files[DEST / "notes.txt"] = b"mine"
    with pytest.raises(MubmdError) as caught:
        workspace_init(store, SOURCE, DEST, yes=False)
    assert caught.value.code is ErrorCode.NOT_EMPTY
    assert caught.value.suggestion is None
    assert "--yes" in caught.value.message
    assert workspace_init(store, SOURCE, DEST, yes=True).changed


def test_an_edited_working_copy_is_kept() -> None:
    store = store_with_reference()
    workspace_init(store, SOURCE, DEST, yes=False)
    store.files[DEST / "Local" / "Text.bmd"] = b"my edit"
    result = workspace_init(store, SOURCE, DEST, yes=False)
    assert (result.changed, result.edited) == (False, 1)
    assert store.files[DEST / "Local" / "Text.bmd"] == b"my edit"


def test_yes_copies_the_reference_over_the_edits() -> None:
    store = store_with_reference()
    workspace_init(store, SOURCE, DEST, yes=False)
    store.files[DEST / "Local" / "Text.bmd"] = b"my edit"
    result = workspace_init(store, SOURCE, DEST, yes=True)
    assert (result.changed, result.edited) == (True, 0)
    assert store.files[DEST / "Local" / "Text.bmd"] == b"text"


def test_a_source_without_its_manifest_is_not_found() -> None:
    store = FakeStore({Path("/other/Data/Text.bmd"): b"text"})
    with pytest.raises(MubmdError) as caught:
        workspace_init(store, Path("/other/Data"), DEST, yes=False)
    assert caught.value.code is ErrorCode.FILE_NOT_FOUND


def test_a_dry_run_copies_nothing() -> None:
    store = store_with_reference()
    result = workspace_init(store, SOURCE, DEST, yes=False, dry_run=True)
    assert (result.files, result.changed) == (2, True)
    assert not store.exists(DEST)
