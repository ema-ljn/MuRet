import asyncio
import shutil
from collections.abc import Callable
from pathlib import Path
from typing import Any

from nicegui import ui
from nicegui.testing import User, UserInteraction

from mutools.adapters.web.elements import Transport
from mutools.adapters.web.page import address
from mutools.adapters.web.widgets import ON, Press
from mutools.app.model_parts import model_parts
from tests.models import view_of

SWORD = "Item/Sword01.bmd"
SPIDER_WEB = "object35/Object36.bmd"
MONSTER = "Monster/Monster01.bmd"
HELM_SWITCH = "On Player.bmd's skeleton"
SETTLING_STEPS = 5
SWORD_FACTS = "1 mesh, 1 bone, 1 action and 68 triangles. Version 10, not encrypted, 7 KB."


def presses(user: User, classes: str) -> list[Press]:
    elements = sorted(user.find(kind=Press).elements, key=lambda element: element.id)
    return [element for element in elements if classes in element.classes and not element.is_deleted]


def texts(user: User, classes: str) -> list[str]:
    return [press.text for press in presses(user, classes)]


def chosen(user: User, classes: str) -> list[str]:
    return [press.text for press in presses(user, classes) if ON in press.classes]


async def settle() -> None:
    for _ in range(SETTLING_STEPS):
        await asyncio.sleep(0)


async def press(user: User, classes: str, text: str) -> None:
    (found,) = [element for element in presses(user, classes) if element.text == text]
    UserInteraction(user, {found}, None).click()
    await settle()


async def type_into(user: User, marker: str, text: str) -> UserInteraction[ui.element]:
    typed = user.find(marker=marker).type(text)
    await settle()
    return typed


def speed(user: User) -> str:
    (transport,) = user.find(kind=Transport).elements
    shown: str = transport.props["speed"]
    return shown


def nodes(user: User) -> list[dict[str, Any]]:
    (tree,) = user.find(kind=ui.tree, marker="parts").elements
    listed: list[dict[str, Any]] = tree.props["nodes"]
    return listed


def with_monster(web_copy: Path, reference_data: Path) -> None:
    (web_copy / "Monster").mkdir(exist_ok=True)
    shutil.copyfile(reference_data / MONSTER, web_copy / MONSTER)


async def test_the_studio_opens_on_a_folder_of_the_library(viewer: User) -> None:
    await viewer.open("/")

    assert texts(viewer, "mt-plate") == ["Sword01", "waterring"]
    await viewer.should_see("1 mesh, 1 action")
    await viewer.should_see("Pick a model from the library.")


async def test_another_folder_is_chosen_from_the_index(viewer: User) -> None:
    await viewer.open("/")

    await press(viewer, "mt-index-row", "player")

    assert texts(viewer, "mt-plate") == ["HelmMale01", "Helper03", "MaskHelmMale09", "Player"]
    assert chosen(viewer, "mt-index-row") == ["player"]


async def test_a_name_is_found_in_every_folder(viewer: User) -> None:
    await viewer.open("/")

    await type_into(viewer, "find-model", "ring")

    assert texts(viewer, "mt-plate") == ["waterring"]
    await type_into(viewer, "find-model", "zz")
    assert texts(viewer, "mt-plate") == []


async def test_a_plate_opens_its_model_in_the_vitrine(viewer: User) -> None:
    await viewer.open("/")

    await press(viewer, "mt-plate", "Sword01")

    await viewer.should_see(SWORD_FACTS)
    await viewer.should_see("Sword01.bmd")
    assert chosen(viewer, "mt-plate") == ["Sword01"]


async def test_a_model_from_another_folder_brings_its_folder(viewer: User) -> None:
    await viewer.open(f"/?file={SWORD}")

    await type_into(viewer, "finder-input", "Helm")
    await press(viewer, "mt-palette-item", "HelmMale01")

    await viewer.should_see(HELM_SWITCH)
    assert chosen(viewer, "mt-plate") == ["HelmMale01"]


async def test_the_finder_runs_the_first_result_on_enter(viewer: User) -> None:
    await viewer.open("/")

    (await type_into(viewer, "finder-input", "waterr")).trigger("keydown.enter")
    await settle()

    await viewer.should_see("2 meshes, 1 bone, 1 action and 78 triangles")


async def test_a_mesh_row_shows_its_card(viewer: User) -> None:
    await viewer.open(f"/?file={SWORD}")

    await press(viewer, "mt-mesh", "sword02.jpg")

    for text in ("Mesh 0", "Item/sword02.OZJ", "triangles", "68"):
        await viewer.should_see(text)
    assert chosen(viewer, "mt-mesh") == ["sword02.jpg"]


async def test_a_mesh_row_pressed_again_is_let_go(viewer: User) -> None:
    await viewer.open(f"/?file={SWORD}")

    await press(viewer, "mt-mesh", "sword02.jpg")
    await press(viewer, "mt-mesh", "sword02.jpg")

    assert chosen(viewer, "mt-mesh") == []
    await viewer.should_not_see("Item/sword02.OZJ")


async def test_the_model_tab_lists_its_parts(viewer: User, reference_data: Path) -> None:
    await viewer.open(f"/?file={SWORD}")

    await press(viewer, "mt-tab", "Model")

    parts = model_parts(view_of(reference_data, SWORD)).children
    assert [node["label"] for node in nodes(viewer)] == [part.label for part in parts]
    await viewer.should_see("version")


async def test_the_actions_play_at_the_clients_speed(viewer: User, web_copy: Path, reference_data: Path) -> None:
    with_monster(web_copy, reference_data)
    await viewer.open(f"/?file={MONSTER}")

    await viewer.should_see("Model 0x152 in the client, a monster")
    assert texts(viewer, "mt-segment") == ["Stop 1", "Stop 2", "Walk", "Attack 1", "Attack 2", "Shock", "Die"]
    assert speed(viewer) == "6.25 keys a second, as the client plays this monster"
    await press(viewer, "mt-segment", "Walk")
    assert (chosen(viewer, "mt-segment"), speed(viewer)) == (
        ["Walk"],
        "8.5 keys a second, as the client plays this monster",
    )


async def test_the_rate_scales_the_clients_speed(viewer: User, web_copy: Path, reference_data: Path) -> None:
    with_monster(web_copy, reference_data)
    await viewer.open(f"/?file={MONSTER}&action=6")

    await press(viewer, "mt-rate", "2\N{MULTIPLICATION SIGN}")

    assert chosen(viewer, "mt-segment") == ["Die"]
    assert speed(viewer) == "27.5 keys a second, 2 times the client plays this monster"
    await viewer.should_see("9 keys, 0.7 s, once")


async def test_a_model_the_client_does_not_name_plays_at_eight_keys(viewer: User) -> None:
    await viewer.open(f"/?file={SWORD}")

    assert speed(viewer) == "8 keys a second, the client's speed for this action is not known"


async def test_the_findings_tab_counts_and_lists_them(viewer: User) -> None:
    await viewer.open(f"/?file={SPIDER_WEB}")

    await press(viewer, "mt-tab", "Findings, 2")

    await viewer.should_see("OUT_OF_RANGE")


async def test_a_finding_leads_to_its_part(viewer: User) -> None:
    await viewer.open(f"/?file={SPIDER_WEB}")
    await press(viewer, "mt-tab", "Findings, 2")

    (finding, *_) = presses(viewer, "mt-finding")
    UserInteraction(viewer, {finding}, None).click()
    await settle()

    assert chosen(viewer, "mt-tab") == ["Parts"]
    assert chosen(viewer, "mt-mesh") == ["cr1_tree03.jpg"]


async def test_a_mesh_says_what_is_not_drawn(viewer: User) -> None:
    await viewer.open(f"/?file={SPIDER_WEB}")

    await press(viewer, "mt-mesh", "cr1_tree03.jpg")

    await viewer.should_see("10 triangles not drawn")


async def test_a_mesh_says_why_its_texture_is_not_shown(viewer: User) -> None:
    await viewer.open("/?file=Item/waterring.bmd")

    await press(viewer, "mt-mesh", "waterring.JPG")

    await viewer.should_see("not shown: No waterring.OZJ in Item")


async def test_a_part_of_the_player_offers_its_skeleton(viewer: User, create_user: Callable[[], User]) -> None:
    other = create_user()

    await viewer.open("/?file=player/HelmMale01.bmd")
    await other.open(f"/?file={SWORD}")

    await press(viewer, "mt-toggle", HELM_SWITCH)
    assert chosen(viewer, "mt-toggle") == [HELM_SWITCH]
    await viewer.should_see("2 meshes, 60 bones, 205 actions")
    await other.should_not_see(HELM_SWITCH)


async def test_a_file_that_fails_shows_its_message(viewer: User, web_copy: Path) -> None:
    shutil.copyfile(web_copy / SWORD, web_copy.parent / "outside.bmd")

    await viewer.open("/?file=../outside.bmd")

    await viewer.should_see("This model cannot be opened.")
    await viewer.should_see("The served folder has no file")
    assert presses(viewer, "mt-tab") == []


async def test_two_pages_keep_their_own_model(viewer: User, create_user: Callable[[], User]) -> None:
    other = create_user()
    await viewer.open(f"/?file={SWORD}")
    await other.open(f"/?file={SPIDER_WEB}")

    await press(other, "mt-mesh", "cr1_tree03.jpg")

    await other.should_see("10 triangles not drawn")
    await viewer.should_see(SWORD_FACTS)
    await viewer.should_not_see("not drawn")


def test_the_address_leaves_out_what_is_usual() -> None:
    assert address(SWORD) == "/?file=Item%2FSword01.bmd"
    assert address(MONSTER, "donor", 2) == "/?file=Monster%2FMonster01.bmd&on=donor&action=2"
