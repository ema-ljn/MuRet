import math
import shutil
from collections.abc import Iterator
from pathlib import Path
from typing import Any

import pytest
from playwright.sync_api import Page, expect

from tests.adapters.web.conftest import Collected, start_web, stop_web
from tests.adapters.web.test_browser import READY, READY_MS
from tests.models import at, view_of

pytestmark = pytest.mark.browser

PLAYER = "player/Player.bmd"
FREE_ACTION = 1
BONE = 2
GIZMO = "window.mutoolsStudio.gizmo()"
DRAG_STEPS = 8


@pytest.fixture
def edited_folder(tmp_path_factory: pytest.TempPathFactory, reference_data: Path) -> Path:
    folder = tmp_path_factory.mktemp("edited") / "Data"
    (folder / "player").mkdir(parents=True)
    shutil.copyfile(reference_data / PLAYER, folder / PLAYER)
    return folder


@pytest.fixture
def edited(edited_folder: Path) -> Iterator[str]:
    process, address = start_web(edited_folder, "--edit")
    yield address
    stop_web(process)


def on_a_bone(page: Page, edited: str) -> dict[str, Any]:
    page.goto(f"{edited}/?file={PLAYER}&action={FREE_ACTION}")
    page.wait_for_function(READY, timeout=READY_MS)
    page.locator(".mt-row", has_text="Bones").click()
    page.locator(".mt-row", has_text=f"{BONE}  Bip01 Pelvis").click()
    page.wait_for_function(f"{GIZMO} && {GIZMO}.bone === {BONE} && {GIZMO}.at")
    gizmo: dict[str, Any] = page.evaluate(GIZMO)
    return gizmo


def test_the_gizmo_is_on_the_picked_bone(page: Page, edited: str, collected: Collected) -> None:
    gizmo = on_a_bone(page, edited)

    assert (gizmo["bone"], gizmo["tool"], gizmo["dragging"]) == (BONE, "turn", False)
    assert collected.nothing, collected


def test_a_drag_of_the_gizmo_turns_the_bone_in_the_session(page: Page, edited: str) -> None:
    x, y = on_a_bone(page, edited)["at"]

    page.mouse.move(x, y)
    page.mouse.down()
    page.mouse.move(x + 40, y + 15, steps=DRAG_STEPS)
    page.mouse.up()

    expect(page.locator(".mt-editor-answer")).to_contain_text(f"Turned bone {BONE} at key")
    expect(page.locator(".mt-save")).to_have_text("Save, 1 edit")
    assert not page.evaluate("window.mutoolsViewer.playing")


def test_ctrl_s_saves_the_turn_to_the_file(page: Page, edited: str, edited_folder: Path) -> None:
    on_a_bone(page, edited)
    page.evaluate("window.mutoolsStudio.drag({ quaternion: [0, 0, Math.SQRT1_2, Math.SQRT1_2] })")
    expect(page.locator(".mt-save")).to_have_text("Save, 1 edit")

    page.keyboard.press("Control+S")

    expect(page.locator(".mt-editor-answer")).to_contain_text("Saved Player.bmd")
    saved = at(view_of(edited_folder, PLAYER), f"/bones/{BONE}/actions/{FREE_ACTION}/rotations/0")
    assert saved == pytest.approx([0.0, 0.0, math.pi / 2], abs=1e-6)
    assert (edited_folder / f"{PLAYER}.orig").is_file()
