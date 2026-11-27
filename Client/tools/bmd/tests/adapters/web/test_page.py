import shutil
from collections.abc import Callable
from pathlib import Path
from typing import Any

from nicegui import ui
from nicegui.testing import User

from mubmd.app.model_parts import model_parts
from tests.models import view_of

SWORD = "Item/Sword01.bmd"
SPIDER_WEB = "object35/Object36.bmd"
HELM_SWITCH = "On Player.bmd's skeleton"


def nodes(user: User, marker: str) -> list[dict[str, Any]]:
    (tree,) = user.find(kind=ui.tree, marker=marker).elements
    listed: list[dict[str, Any]] = tree.props["nodes"]
    return listed


def choose(user: User, part: str) -> None:
    user.find(kind=ui.tree, marker="parts").trigger("update:selected", part)


async def test_the_folder_lists_its_models(viewer: User) -> None:
    await viewer.open("/")

    folders = {node["id"]: node for node in nodes(viewer, "files")}
    assert [(child["id"], child["label"]) for child in folders["Item"]["children"]] == [
        ("Item/Sword01.bmd", "Sword01.bmd"),
        ("Item/waterring.bmd", "waterring.bmd"),
    ]
    assert "Local" not in folders
    await viewer.should_see("Pick a model")


async def test_a_model_opens_with_its_parts(viewer: User, reference_data: Path) -> None:
    await viewer.open(f"/?file={SWORD}")

    parts = model_parts(view_of(reference_data, SWORD)).children
    assert [node["label"] for node in nodes(viewer, "parts")] == [part.label for part in parts]
    await viewer.should_see("version")
    await viewer.should_see("Sword02.smd")


async def test_choosing_a_mesh_shows_its_facts(viewer: User) -> None:
    await viewer.open(f"/?file={SWORD}")

    choose(viewer, "/meshes/0")

    for text in ("mesh 0", "triangles", "68", "sword02.OZJ"):
        await viewer.should_see(text)


async def test_choosing_an_action_names_it(viewer: User) -> None:
    await viewer.open("/?file=NPC/BloodCastle01.bmd")

    choose(viewer, "/actions/0")

    await viewer.should_see("action 0 · 12 keys")


async def test_a_model_lists_its_findings(viewer: User) -> None:
    await viewer.open(f"/?file={SPIDER_WEB}")

    await viewer.should_see("OUT_OF_RANGE")


async def test_a_mesh_says_what_is_not_drawn(viewer: User) -> None:
    await viewer.open(f"/?file={SPIDER_WEB}")

    choose(viewer, "/meshes/1")

    await viewer.should_see("10 triangles not drawn")


async def test_a_mesh_says_why_its_texture_is_not_shown(viewer: User) -> None:
    await viewer.open("/?file=Item/waterring.bmd")

    choose(viewer, "/meshes/1")

    await viewer.should_see("not shown")
    await viewer.should_see("No waterring.OZJ in Item")


async def test_a_part_of_the_player_offers_its_skeleton(viewer: User, create_user: Callable[[], User]) -> None:
    other = create_user()

    await viewer.open("/?file=player/HelmMale01.bmd")
    await other.open(f"/?file={SWORD}")

    await viewer.should_see(HELM_SWITCH)
    await other.should_not_see(HELM_SWITCH)


async def test_a_file_that_fails_shows_its_message(viewer: User, web_copy: Path) -> None:
    shutil.copyfile(web_copy / SWORD, web_copy.parent / "outside.bmd")

    await viewer.open("/?file=../outside.bmd")

    await viewer.should_see("The served folder has no file")
    await viewer.should_not_see(marker="parts")


async def test_two_pages_keep_their_own_model(viewer: User, create_user: Callable[[], User]) -> None:
    other = create_user()
    await viewer.open(f"/?file={SWORD}")
    await other.open(f"/?file={SPIDER_WEB}")

    choose(other, "/meshes/1")

    await other.should_see("10 triangles not drawn")
    await viewer.should_see("Sword02.smd")
    await viewer.should_not_see("not drawn")
