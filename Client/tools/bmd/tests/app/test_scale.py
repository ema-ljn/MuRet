import dataclasses
from pathlib import Path

import pytest

from mubmd.app.session import EditSession
from mubmd.core.documents.model import Model
from tests.app.conftest import WORK
from tests.app.fake_store import FakeStore
from tests.models import CODEC, model_file

PLAYER = WORK / "player" / "Player.bmd"
PLAYER_SIZE = 2_414_277


@pytest.fixture(scope="module")
def player_bytes(reference_data: Path) -> bytes:
    return (reference_data / "player" / "Player.bmd").read_bytes()


@pytest.fixture
def session(player_bytes: bytes) -> EditSession:
    return EditSession.open(FakeStore({PLAYER: player_bytes}), PLAYER)


def decoded(data: bytes) -> Model:
    return CODEC.decode(data, model_file("Player.bmd"))


def test_an_edit_shares_the_rest_of_the_model(session: EditSession) -> None:
    session.set("/bones/0/name", "Root")
    edited, original = session.view["bones"], session.original["bones"]
    assert isinstance(edited, list)
    assert isinstance(original, list)
    assert edited[1] is original[1]
    assert session.view["actions"] is session.original["actions"]
    (change,) = session.changes
    assert (change.pointer, change.new) == ("/bones/0/name", "Root")
    assert session.undo()
    assert session.changes == ()


def test_saving_the_biggest_model_changes_only_its_edit(player_bytes: bytes) -> None:
    store = FakeStore({PLAYER: player_bytes})
    session = EditSession.open(store, PLAYER)
    session.set("/bones/0/name", "Root")
    session.save()
    saved = store.files[PLAYER]
    reference = decoded(player_bytes)
    renamed = dataclasses.replace(reference.bones[0], name=b"Root".ljust(32, b"\0"))
    assert len(saved) == PLAYER_SIZE
    assert decoded(saved) == dataclasses.replace(reference, bones=(renamed, *reference.bones[1:]))
