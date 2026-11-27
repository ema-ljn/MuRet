from pathlib import Path

import pytest

from mutools.adapters.storage import LocalFileStore
from mutools.adapters.web.models import KEPT_SESSIONS, Models
from mutools.core.errors import ErrorCode, MutoolsError
from tests.conftest import MODEL_FILES, TABLE_FILES

SWORD = "Item/Sword01.bmd"
PLAYER = "player/Player.bmd"


def test_the_models_of_the_folder(web_copy: Path) -> None:
    listed = Models(LocalFileStore(), web_copy).listed()

    assert set(listed) == {*MODEL_FILES, PLAYER}
    assert not set(listed) & set(TABLE_FILES)
    assert list(listed) == sorted(listed)


def test_the_folders_count_their_models(web_copy: Path) -> None:
    models = Models(LocalFileStore(), web_copy)

    folders = dict(models.folders(models.listed()))

    assert (folders["Item"], folders["player"]) == (2, 4)
    assert [folder for folder, _ in models.folders(models.listed())] == sorted(folders, key=str.lower)


def test_an_entry_sums_up_a_model_until_it_changes(web_copy: Path) -> None:
    models = Models(LocalFileStore(), web_copy)

    entry = models.entry(SWORD)
    (web_copy / SWORD).write_bytes((web_copy / "player" / "HelmMale01.bmd").read_bytes())
    changed = models.entry(SWORD)

    assert (entry.folder, entry.name, entry.meshes, entry.actions, len(entry.stamp)) == ("Item", "Sword01", 1, 1, 16)
    assert models.entry(SWORD) is changed
    assert (changed.meshes, changed.stamp != entry.stamp) == (2, True)


def test_a_model_is_opened_once_while_it_does_not_change(web_copy: Path) -> None:
    models = Models(LocalFileStore(), web_copy)

    assert models.session(SWORD) is models.session(SWORD)


def test_only_the_models_used_last_stay_open(web_copy: Path) -> None:
    models = Models(LocalFileStore(), web_copy)
    first = models.session(SWORD)

    for name in [name for name in MODEL_FILES if name != SWORD][:KEPT_SESSIONS]:
        models.session(name)

    assert models.session(SWORD) is not first


def test_the_donor_of_a_part_is_the_folders_player(web_copy: Path) -> None:
    models = Models(LocalFileStore(), web_copy)

    assert models.donor("player/HelmMale01.bmd") == PLAYER
    assert models.donor("player/Helper03.bmd") is None
    assert models.donor(SWORD) is None


@pytest.mark.parametrize(
    "file",
    [
        "//host/share/x.bmd",
        "\\\\host\\share\\x.bmd",
        "C:/x.bmd",
        "C:x.bmd",
        "/x.bmd",
        "\\x.bmd",
        "Item/../Item/Sword01.bmd",
        "Item\\..\\x.bmd",
    ],
)
def test_a_name_that_could_leave_the_folder_is_refused_before_the_disk_is_touched(
    web_copy: Path, monkeypatch: pytest.MonkeyPatch, file: str
) -> None:
    models = Models(LocalFileStore(), web_copy)

    def touched(*_: object, **__: object) -> None:
        raise AssertionError("the disk was touched")

    monkeypatch.setattr(Path, "resolve", touched)

    with pytest.raises(MutoolsError) as error:
        models.path(file)

    assert error.value.code is ErrorCode.FILE_NOT_FOUND


@pytest.mark.parametrize("kept", [0, 1000])
def test_a_player_that_does_not_open_is_no_donor(web_copy: Path, kept: int) -> None:
    player = web_copy / PLAYER
    player.write_bytes(player.read_bytes()[:kept])

    assert Models(LocalFileStore(), web_copy).donor("player/HelmMale01.bmd") is None
