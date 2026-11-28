from collections.abc import Callable
from pathlib import Path

import pytest

from mutools.app.documents import open_document
from mutools.app.gltf_actions import export_gltf, import_gltf
from mutools.app.gltf_rig import ChosenAction, Rig, model_rig
from mutools.core.errors import ErrorCode, MutoolsError
from tests.app.conftest import REFERENCE, TEXT, WORK
from tests.app.fake_store import FakeStore

RIDER = WORK / "skill" / "Rider01.bmd"
REFERENCE_RIDER = REFERENCE / "skill" / "Rider01.bmd"
PLAYER = WORK / "player" / "Player.bmd"
HELM = WORK / "player" / "HelmMale01.bmd"
OUTPUT = Path("/tmp/rider.glb")
SOURCE = Path("/tmp/blender.glb")
RIDER_ACTIONS = 4
RIDER_JOINTS = 21
FILES = (
    "skill/Rider01.bmd",
    "Item/unicon.OZJ",
    "Item/unicon01.OZT",
    "player/Player.bmd",
    "player/HelmMale01.bmd",
    "player/head_02.OZJ",
    "player/hide.OZJ",
)


@pytest.fixture
def store(reference_data: Path, text_bytes: bytes) -> FakeStore:
    files = {WORK / name: (reference_data / name).read_bytes() for name in FILES}
    rider = files[RIDER]
    return FakeStore({**files, REFERENCE_RIDER: rider, TEXT: text_bytes, SOURCE: b""}, reference=REFERENCE)


class Writer:
    def __init__(self) -> None:
        self.rigs: list[Rig] = []

    def __call__(self, rig: Rig) -> bytes:
        self.rigs.append(rig)
        return b"glTF from mutools"


def reader_of(store: FakeStore, path: Path) -> Callable[[bytes], Rig]:
    view = open_document(store, path).view
    actions = view["actions"]
    assert isinstance(actions, list)
    rig = model_rig(view, [], [ChosenAction(index, "Stop") for index in range(len(actions))])
    return lambda _: rig


def refused(call: Callable[..., object], *args: object, **options: object) -> MutoolsError:
    with pytest.raises(MutoolsError) as caught:
        call(*args, **options)
    return caught.value


def test_an_export_writes_the_glb(store: FakeStore) -> None:
    result = export_gltf(store, Writer(), RIDER, OUTPUT, dry_run=False)

    assert store.files[OUTPUT] == b"glTF from mutools"
    assert (result.joints, result.meshes, result.animations, result.size) == (RIDER_JOINTS, 2, RIDER_ACTIONS, 17)


def test_a_dry_run_writes_nothing(store: FakeStore) -> None:
    export_gltf(store, Writer(), RIDER, OUTPUT, dry_run=True)

    assert OUTPUT not in store.files


def test_the_output_is_a_glb(store: FakeStore) -> None:
    error = refused(export_gltf, store, Writer(), RIDER, Path("/tmp/rider.gltf"), dry_run=False)

    assert error.code is ErrorCode.USAGE


def test_only_the_chosen_actions_are_exported(store: FakeStore) -> None:
    writer = Writer()

    export_gltf(store, writer, RIDER, OUTPUT, actions=[2], dry_run=False)

    assert [animation.name for animation in writer.rigs[0].animations] == ["002 Action 2"]


def test_an_action_the_model_lacks_is_refused(store: FakeStore) -> None:
    error = refused(export_gltf, store, Writer(), RIDER, OUTPUT, actions=[RIDER_ACTIONS], dry_run=False)

    assert error.code is ErrorCode.POINTER_NOT_FOUND


def test_a_part_adds_its_meshes_to_the_skeleton(store: FakeStore) -> None:
    writer = Writer()

    result = export_gltf(store, writer, PLAYER, OUTPUT, parts=[HELM], actions=[0], dry_run=False)

    assert [mesh.name for mesh in writer.rigs[0].meshes] == ["HelmMale01 0", "HelmMale01 1"]
    assert result.untextured == ()


def test_a_part_of_another_skeleton_is_refused(store: FakeStore) -> None:
    error = refused(export_gltf, store, Writer(), RIDER, OUTPUT, parts=[HELM], dry_run=False)

    assert error.code is ErrorCode.INVALID_VALUE


def test_a_texture_that_is_not_found_leaves_its_mesh_untextured(store: FakeStore) -> None:
    del store.files[WORK / "Item" / "unicon01.OZT"]
    writer = Writer()

    result = export_gltf(store, writer, RIDER, OUTPUT, dry_run=False)

    assert [(found.model, found.mesh) for found in result.untextured] == [(RIDER, 1)]
    assert [mesh.image for mesh in writer.rigs[0].meshes] == [0, None]


def test_an_import_adds_the_animation_after_the_last_action(store: FakeStore) -> None:
    result = import_gltf(store, reader_of(store, RIDER), RIDER, SOURCE, animation="2", dry_run=False)

    actions = open_document(store, RIDER).view["actions"]
    assert isinstance(actions, list)
    assert (result.animation, result.keys, result.edit.action, len(actions)) == ("002 Stop", 7, 4, 5)


def test_an_import_over_the_same_action_changes_nothing(store: FakeStore) -> None:
    result = import_gltf(store, reader_of(store, RIDER), RIDER, SOURCE, animation="2", at=2, dry_run=False)

    assert (result.edit.result.changed, store.writes) == (False, 0)


def test_an_import_of_a_dry_run_writes_nothing(store: FakeStore) -> None:
    import_gltf(store, reader_of(store, RIDER), RIDER, SOURCE, animation="2", dry_run=True)

    assert store.writes == 0


def test_an_import_over_an_action_the_model_lacks_is_refused(store: FakeStore) -> None:
    error = refused(import_gltf, store, reader_of(store, RIDER), RIDER, SOURCE, animation="2", at=9, dry_run=False)

    assert error.code is ErrorCode.POINTER_NOT_FOUND


def test_an_import_reads_only_a_glb(store: FakeStore) -> None:
    error = refused(import_gltf, store, reader_of(store, RIDER), RIDER, Path("/tmp/a.fbx"), dry_run=False)

    assert error.code is ErrorCode.USAGE


def test_an_import_never_writes_the_reference(store: FakeStore) -> None:
    error = refused(import_gltf, store, reader_of(store, RIDER), REFERENCE_RIDER, SOURCE, animation="2", dry_run=False)

    assert error.code is ErrorCode.REFERENCE_READ_ONLY


def test_only_a_model_takes_an_animation(store: FakeStore) -> None:
    error = refused(import_gltf, store, reader_of(store, RIDER), TEXT, SOURCE, animation="2", dry_run=False)

    assert error.code is ErrorCode.USAGE
