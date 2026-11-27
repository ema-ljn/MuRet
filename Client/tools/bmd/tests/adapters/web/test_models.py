from pathlib import Path

from mubmd.adapters.storage import LocalFileStore
from mubmd.adapters.web.models import KEPT_SESSIONS, Models
from tests.conftest import MODEL_FILES, TABLE_FILES

SWORD = "Item/Sword01.bmd"
PLAYER = "player/Player.bmd"


def test_the_models_of_the_folder(web_copy: Path) -> None:
    listed = Models(LocalFileStore(), web_copy).listed()

    assert set(listed) == {*MODEL_FILES, PLAYER}
    assert not set(listed) & set(TABLE_FILES)
    assert list(listed) == sorted(listed)


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
