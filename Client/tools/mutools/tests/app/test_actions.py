import json
from collections.abc import Callable
from pathlib import Path

import pytest

from mutools.app.actions import export_action, import_action, remove_action
from mutools.app.documents import open_document
from mutools.core.errors import ErrorCode, MutoolsError
from mutools.core.json_types import JsonObject, JsonValue
from tests.app.conftest import REFERENCE, TEXT, WORK
from tests.app.fake_store import FakeStore

RIDER = WORK / "skill" / "Rider01.bmd"
OTHER_RIDER = WORK / "skill" / "Rider02.bmd"
REFERENCE_RIDER = REFERENCE / "skill" / "Rider01.bmd"
BLOOD_CASTLE = WORK / "NPC" / "BloodCastle01.bmd"
ACTION = Path("/tmp/action.json")
RIDER_ACTIONS = 4


@pytest.fixture
def models(reference_data: Path, text_bytes: bytes) -> FakeStore:
    rider = (reference_data / "skill" / "Rider01.bmd").read_bytes()
    return FakeStore(
        {
            RIDER: rider,
            OTHER_RIDER: rider,
            REFERENCE_RIDER: rider,
            BLOOD_CASTLE: (reference_data / "NPC" / "BloodCastle01.bmd").read_bytes(),
            TEXT: text_bytes,
        },
        reference=REFERENCE,
    )


def actions_of(store: FakeStore, path: Path) -> list[JsonValue]:
    actions = open_document(store, path).view["actions"]
    assert isinstance(actions, list)
    return actions


def written(store: FakeStore, path: Path) -> JsonObject:
    value = json.loads(store.files[path])
    assert isinstance(value, dict)
    return value


def refused(call: Callable[..., object], *args: object, **options: object) -> MutoolsError:
    with pytest.raises(MutoolsError) as caught:
        call(*args, **options)
    return caught.value


def test_an_export_writes_the_action_file(models: FakeStore) -> None:
    result = export_action(models, RIDER, 2, ACTION, dry_run=False)

    exported = written(models, ACTION)
    assert (exported["format"], exported["keys"]) == ("mutools-action", 7)
    assert (result.output, result.action, result.keys, result.size) == (ACTION, 2, 7, len(models.files[ACTION]))


def test_an_export_reads_the_reference(models: FakeStore) -> None:
    export_action(models, REFERENCE_RIDER, 0, ACTION, dry_run=False)

    assert ACTION in models.files


def test_a_dry_export_writes_nothing(models: FakeStore) -> None:
    result = export_action(models, RIDER, 0, ACTION, dry_run=True)

    assert ACTION not in models.files
    assert result.size > 0


def test_an_export_never_writes_a_bmd(models: FakeStore) -> None:
    assert refused(export_action, models, RIDER, 0, OTHER_RIDER, dry_run=False).code is ErrorCode.USAGE


def test_a_table_has_no_actions(models: FakeStore) -> None:
    assert refused(export_action, models, TEXT, 0, ACTION, dry_run=False).code is ErrorCode.USAGE


def test_an_imported_action_is_added_after_the_last(models: FakeStore) -> None:
    export_action(models, RIDER, 2, ACTION, dry_run=False)

    edit = import_action(models, OTHER_RIDER, ACTION, dry_run=False)

    assert (edit.action, edit.actions, edit.result.changed) == (RIDER_ACTIONS, RIDER_ACTIONS + 1, True)
    assert actions_of(models, OTHER_RIDER)[-1] == actions_of(models, RIDER)[2]


def test_an_action_goes_over_the_one_named(models: FakeStore) -> None:
    export_action(models, RIDER, 0, ACTION, dry_run=False)

    edit = import_action(models, RIDER, ACTION, at=3, dry_run=False)

    assert (edit.action, edit.actions) == (3, RIDER_ACTIONS)
    assert models.writes == 2


def test_an_action_put_back_over_itself_writes_nothing(models: FakeStore) -> None:
    export_action(models, RIDER, 1, ACTION, dry_run=False)

    edit = import_action(models, RIDER, ACTION, at=1, dry_run=False)

    assert not edit.result.changed
    assert models.writes == 1


def test_a_dry_import_writes_nothing(models: FakeStore) -> None:
    export_action(models, RIDER, 0, ACTION, dry_run=False)
    before = models.files[RIDER]

    edit = import_action(models, RIDER, ACTION, dry_run=True)

    assert edit.result.changed and edit.result.dry_run
    assert models.files[RIDER] == before


def test_an_import_never_writes_the_reference(models: FakeStore) -> None:
    export_action(models, RIDER, 0, ACTION, dry_run=False)

    assert refused(import_action, models, REFERENCE_RIDER, ACTION, dry_run=False).code is ErrorCode.REFERENCE_READ_ONLY


def test_an_action_of_another_skeleton_is_refused(models: FakeStore) -> None:
    export_action(models, RIDER, 0, ACTION, dry_run=False)

    error = refused(import_action, models, BLOOD_CASTLE, ACTION, dry_run=False)

    assert (error.code, error.pointer) == (ErrorCode.INVALID_VALUE, "/bones")


def test_a_file_that_is_not_json_is_refused(models: FakeStore) -> None:
    models.files[ACTION] = b"not json"

    assert refused(import_action, models, RIDER, ACTION, dry_run=False).code is ErrorCode.INVALID_VALUE


def test_the_last_action_goes_without_a_flag(models: FakeStore) -> None:
    edit = remove_action(models, RIDER, RIDER_ACTIONS - 1, yes=False, dry_run=False)

    assert (edit.action, edit.actions, edit.result.changed) == (RIDER_ACTIONS - 1, RIDER_ACTIONS - 1, True)
    assert len(actions_of(models, RIDER)) == RIDER_ACTIONS - 1


def test_another_action_needs_yes(models: FakeStore) -> None:
    error = refused(remove_action, models, RIDER, 0, yes=False, dry_run=False)

    assert (error.code, error.suggestion) == (ErrorCode.USAGE, "--yes")
    assert models.writes == 0


def test_with_yes_the_later_actions_move_down(models: FakeStore) -> None:
    second = actions_of(models, RIDER)[1]

    remove_action(models, RIDER, 0, yes=True, dry_run=False)

    assert actions_of(models, RIDER)[0] == second


def test_removing_an_action_that_is_not_there_is_not_found(models: FakeStore) -> None:
    error = refused(remove_action, models, RIDER, RIDER_ACTIONS, yes=True, dry_run=False)

    assert (error.code, error.pointer) == (ErrorCode.POINTER_NOT_FOUND, f"/actions/{RIDER_ACTIONS}")
