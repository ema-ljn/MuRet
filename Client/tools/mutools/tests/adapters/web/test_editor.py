import re
from pathlib import Path

import pytest
from nicegui.testing import User

from mutools.adapters.storage import LocalFileStore
from mutools.adapters.web.elements import Shortcuts, Viewport
from mutools.adapters.web.server import build
from mutools.app.model_scene import angle_quaternion
from mutools.core.json_types import JsonObject
from tests.adapters.web.test_page import chosen, press, presses, settle, texts
from tests.models import at, view_of

PLAYER = "player/Player.bmd"
HELM = "player/HelmMale01.bmd"
FREE_ACTION = 1
BONE = 4
DUMMY_BONE = 9
PLAYER_ACTIONS = 205
PAUSED_AT = 2


@pytest.fixture
def editor(user: User, web_copy: Path) -> User:
    build(LocalFileStore(), web_copy, edit=True)
    user.javascript_rules[re.compile(r'.*runMethod\(\d+, "pause"')] = lambda _: PAUSED_AT
    return user


async def opened_on_a_bone(user: User, bone: int = BONE) -> None:
    await user.open(f"/?file={PLAYER}&action={FREE_ACTION}")
    await press(user, "mt-row", "Bones")
    (row,) = [text for text in texts(user, "mt-row") if text.startswith(f"{bone}  ")]
    await press(user, "mt-row", row)


async def turned(user: User, key: int = 3) -> None:
    posed = {"action": FREE_ACTION, "key": key, "bone": BONE, "tool": "turn"}
    user.find(kind=Viewport).trigger("posed", posed | {"quaternion": list(angle_quaternion(0.5, -0.25, 1.0))})
    await settle()


def saved_view(web_copy: Path) -> JsonObject:
    return view_of(web_copy, PLAYER)


async def test_the_studio_edits_only_when_asked(viewer: User) -> None:
    await viewer.open(f"/?file={PLAYER}")

    await viewer.should_not_see(marker="editor")
    await viewer.should_see("Working copy")


async def test_the_header_says_the_copy_is_being_edited(editor: User) -> None:
    await editor.open(f"/?file={PLAYER}")

    await editor.should_see("Working copy, editing")
    await editor.should_see("Pick a bone in the inspector's list of bones to turn or move it")


async def test_the_bone_picked_in_the_inspector_is_the_one_edited(editor: User) -> None:
    await opened_on_a_bone(editor)

    await editor.should_see("Bone 4, Bip01 L Calf, at the key under the playhead.")
    assert chosen(editor, "mt-tool") == ["Turn"]


async def test_a_dummy_bone_has_no_pose_to_edit(editor: User) -> None:
    await opened_on_a_bone(editor, DUMMY_BONE)

    await editor.should_see(f"Bone {DUMMY_BONE} is a dummy bone, which has no pose.")


async def test_a_turn_from_the_vitrine_is_an_edit_to_save(editor: User) -> None:
    await opened_on_a_bone(editor)

    await turned(editor)

    await editor.should_see("Turned bone 4 at key 3.")
    assert texts(editor, "mt-save") == ["Save, 1 edit"]


async def test_undo_takes_the_edit_back(editor: User) -> None:
    await opened_on_a_bone(editor)
    await turned(editor)

    await press(editor, "mt-edit-press", "Undo")

    await editor.should_see("Undid the last edit.")
    assert texts(editor, "mt-save") == ["Nothing to save"]


async def test_ctrl_z_and_ctrl_y_undo_and_redo(editor: User) -> None:
    await opened_on_a_bone(editor)
    await turned(editor)

    editor.find(kind=Shortcuts).trigger("undo")
    await settle()
    assert texts(editor, "mt-save") == ["Nothing to save"]
    editor.find(kind=Shortcuts).trigger("redo")
    await settle()
    assert texts(editor, "mt-save") == ["Save, 1 edit"]


async def test_save_writes_the_model(editor: User, web_copy: Path) -> None:
    await opened_on_a_bone(editor)
    await turned(editor)

    await press(editor, "mt-save", "Save, 1 edit")

    await editor.should_see("Saved Player.bmd:")
    assert at(saved_view(web_copy), f"/bones/{BONE}/actions/{FREE_ACTION}/rotations/3") == [0.5, -0.25, 1.0]
    assert texts(editor, "mt-save") == ["Nothing to save"]


async def test_copy_key_copies_the_key_under_the_playhead(editor: User, web_copy: Path) -> None:
    keys = at(saved_view(web_copy), f"/actions/{FREE_ACTION}/keys")
    await opened_on_a_bone(editor)

    await press(editor, "mt-edit-press", "Copy key")

    await editor.should_see(f"Copied key {PAUSED_AT} after it.")
    await editor.should_see(f"{keys + 1} keys")


async def test_remove_key_removes_the_key_under_the_playhead(editor: User, web_copy: Path) -> None:
    keys = at(saved_view(web_copy), f"/actions/{FREE_ACTION}/keys")
    await opened_on_a_bone(editor)

    await press(editor, "mt-edit-press", "Remove key")

    await editor.should_see(f"Removed key {PAUSED_AT}.")
    await editor.should_see(f"{keys - 1} keys")


async def test_copy_action_plays_the_copy(editor: User) -> None:
    await opened_on_a_bone(editor)

    await press(editor, "mt-edit-press", "Copy action")

    await editor.should_see(f"Copied action {FREE_ACTION} as action {PLAYER_ACTIONS}.")
    assert len(presses(editor, "mt-segment")) == PLAYER_ACTIONS + 1
    assert chosen(editor, "mt-segment") == [texts(editor, "mt-segment")[-1]]


async def test_a_part_on_the_players_skeleton_says_to_open_the_player(editor: User) -> None:
    await editor.open(f"/?file={HELM}&on=donor")

    await editor.should_see("These are the actions of Player.bmd.")
    await press(editor, "mt-edit-press", "Open Player.bmd")

    await editor.should_see("Pick a bone in the inspector's list of bones to turn or move it")
