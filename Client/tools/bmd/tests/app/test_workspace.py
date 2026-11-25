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


def test_a_folder_that_is_not_empty_needs_yes() -> None:
    store = store_with_reference()
    store.files[DEST / "notes.txt"] = b"mine"
    with pytest.raises(MubmdError) as caught:
        workspace_init(store, SOURCE, DEST, yes=False)
    assert caught.value.code is ErrorCode.NOT_EMPTY
    assert caught.value.suggestion is not None
    assert "--yes" in caught.value.suggestion
    assert workspace_init(store, SOURCE, DEST, yes=True).changed


def test_a_source_without_its_manifest_is_not_found() -> None:
    store = FakeStore({Path("/other/Data/Text.bmd"): b"text"})
    with pytest.raises(MubmdError) as caught:
        workspace_init(store, Path("/other/Data"), DEST, yes=False)
    assert caught.value.code is ErrorCode.FILE_NOT_FOUND
