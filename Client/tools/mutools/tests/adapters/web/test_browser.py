import json
import socket
from pathlib import Path
from typing import Any

import pytest
from playwright.sync_api import Page, expect

from mutools.adapters.web.library import PAGE_SIZE
from tests.adapters.web.conftest import MANY_FOLDER, MANY_PLATES, Collected, start_web, stop_web

pytestmark = pytest.mark.browser

MONSTER = "Monster/Monster01.bmd"
READY = "window.mutoolsViewer && window.mutoolsViewer.ready"
READY_MS = 30_000
NEAR = 0.01
PLAYING_FPS = 30
TALLEST_PAGE = 1300
COUNT_CONTEXTS = """
window.webglCanvases = new Set();
const getContext = HTMLCanvasElement.prototype.getContext;
HTMLCanvasElement.prototype.getContext = function (kind, ...rest) {
  if (String(kind).startsWith("webgl")) window.webglCanvases.add(this);
  return getContext.call(this, kind, ...rest);
};
"""


def state(page: Page) -> dict[str, Any]:
    viewer: dict[str, Any] = page.evaluate("window.mutoolsViewer")
    return viewer


def opened(page: Page, served: str, file: str) -> dict[str, Any]:
    page.goto(f"{served}/?file={file}")
    page.wait_for_function(READY, timeout=READY_MS)
    return state(page)


def frames_in_a_second(page: Page) -> int:
    first: int = page.evaluate("window.mutoolsStudio.frames()")
    page.wait_for_timeout(1000)
    last: int = page.evaluate("window.mutoolsStudio.frames()")
    return last - first


def play(page: Page, name: str, action: int) -> None:
    page.locator(".mt-segment", has_text=name).click()
    page.wait_for_function(f"window.mutoolsViewer.action === {action} && window.mutoolsViewer.playing")


def test_a_model_is_drawn_with_its_textures(page: Page, served: str, collected: Collected) -> None:
    viewer = opened(page, served, MONSTER)

    assert (viewer["meshes"], viewer["bones"], viewer["textures"]) == (14, 45, {"shown": 14, "flat": 0})
    assert collected.nothing, collected


def test_the_browser_poses_the_model_where_python_does(page: Page, served: str) -> None:
    bounds = opened(page, served, MONSTER)["bounds"]

    assert bounds["low"] == pytest.approx([-79.39, -112.313, -1.25], abs=NEAR)
    assert bounds["high"] == pytest.approx([84.139, 51.869, 214.797], abs=NEAR)


def test_nothing_is_drawn_while_the_model_is_still(page: Page, served: str) -> None:
    opened(page, served, MONSTER)
    page.wait_for_timeout(500)

    assert frames_in_a_second(page) == 0


def test_an_action_draws_at_most_thirty_frames_a_second(page: Page, served: str) -> None:
    opened(page, served, MONSTER)
    play(page, "Stop 2", 1)

    assert 0 < frames_in_a_second(page) <= PLAYING_FPS + 1


def test_a_tga_texture_is_drawn(page: Page, served: str) -> None:
    viewer = opened(page, served, "player/Helper03.bmd")

    assert viewer["textures"] == {"shown": 2, "flat": 0}


def test_a_missing_texture_leaves_the_mesh_flat(page: Page, served: str, collected: Collected) -> None:
    viewer = opened(page, served, "Item/waterring.bmd")

    assert viewer["textures"] == {"shown": 1, "flat": 1}
    assert {(status, url.rpartition("/api/")[2]) for status, url in collected.answers} == {
        (404, "texture?file=Item%2Fwaterring.bmd&mesh=1")
    }
    assert collected.page_errors == []


def test_an_action_plays_and_pauses(page: Page, served: str) -> None:
    opened(page, served, MONSTER)

    play(page, "Stop 2", 1)
    started = state(page)
    page.wait_for_function(f"window.mutoolsViewer.key > {started['key']}")
    page.locator(".mt-play").click()
    page.wait_for_function("!window.mutoolsViewer.playing")
    stopped = state(page)["key"]
    page.wait_for_timeout(500)

    assert (started["action"], started["keys"]) == (1, 30)
    assert state(page)["key"] == stopped
    expect(page.get_by_role("slider", name="Key")).to_have_attribute("aria-valuenow", str(stopped))


def test_an_action_loops_as_the_clients_does(page: Page, served: str) -> None:
    opened(page, served, MONSTER)

    play(page, "Stop 2", 1)
    unlocked = state(page)["duration"]
    play(page, "Walk", 2)

    assert (unlocked, state(page)["duration"]) == (30, 6)


def test_dying_plays_once_and_stays_on_its_last_key(page: Page, served: str) -> None:
    opened(page, served, MONSTER)

    play(page, "Die", 6)
    page.wait_for_function("!window.mutoolsViewer.playing")

    assert (state(page)["key"], state(page)["duration"]) == (8, 8)


def test_space_plays_and_pauses(page: Page, served: str) -> None:
    opened(page, served, MONSTER)

    page.keyboard.press("Space")
    page.wait_for_function("window.mutoolsViewer.playing")
    page.keyboard.press("Space")

    page.wait_for_function("!window.mutoolsViewer.playing")


def test_the_finder_opens_a_model_without_a_reload(page: Page, served: str) -> None:
    opened(page, served, MONSTER)
    page.evaluate("window.sameDocument = true")

    page.keyboard.press("Control+k")
    page.get_by_role("textbox", name="Find a model or run a command").fill("Sword")
    page.keyboard.press("Enter")
    page.wait_for_function(f"window.mutoolsViewer.file === 'Item/Sword01.bmd' && {READY}", timeout=READY_MS)

    assert page.evaluate("window.sameDocument") is True
    assert page.url.endswith("/?file=Item%2FSword01.bmd")


def test_a_big_folder_keeps_the_page_its_height(page: Page, served: str) -> None:
    opened(page, served, f"{MANY_FOLDER}/Sword00.bmd")
    page.evaluate("window.sameDocument = true")

    expect(page.locator(".mt-plate")).to_have_count(PAGE_SIZE)
    assert page.evaluate("document.documentElement.scrollHeight") < TALLEST_PAGE
    page.get_by_role("button", name="Next page").click()

    expect(page.locator(".mt-plate")).to_have_count(MANY_PLATES - PAGE_SIZE)
    assert page.evaluate("window.sameDocument") is True


def test_the_plates_share_one_webgl_context(page: Page, served: str) -> None:
    page.add_init_script(COUNT_CONTEXTS)
    opened(page, served, "player/Player.bmd")

    plates = page.locator(".mt-plate-image")
    expect(plates).to_have_count(3, timeout=READY_MS)

    assert page.evaluate("window.webglCanvases.size") == 2


def test_a_part_moves_with_the_players_actions(page: Page, served: str) -> None:
    opened(page, served, "player/HelmMale01.bmd")

    page.get_by_role("button", name="On Player.bmd's skeleton").click()
    page.wait_for_url("**on=donor**")
    page.wait_for_function(f"window.mutoolsViewer.skeleton === 'player/Player.bmd' && {READY}", timeout=READY_MS)

    viewer = state(page)
    assert (viewer["skeleton"], viewer["bones"]) == ("player/Player.bmd", 60)
    assert viewer["bounds"]["low"] == pytest.approx([-13.141, -16.272, 160.149], abs=NEAR)
    assert viewer["bounds"]["high"] == pytest.approx([25.936, 34.578, 192.624], abs=NEAR)


@pytest.mark.parametrize(
    ("file", "meshes", "bones", "corners", "skeleton_shown"),
    [
        ("object35/Object36.bmd", 2, 7, 336, "false"),
        ("player/Player.bmd", 0, 60, 0, "true"),
        ("Object3/Object25.bmd", 0, 0, 0, "true"),
    ],
)
def test_odd_models_load_without_errors(
    page: Page, served: str, collected: Collected, file: str, meshes: int, bones: int, corners: int, skeleton_shown: str
) -> None:
    viewer = opened(page, served, file)

    assert (viewer["meshes"], viewer["bones"], viewer["corners"]) == (meshes, bones, corners)
    expect(page.get_by_role("button", name="Bones", exact=True)).to_have_attribute("aria-pressed", skeleton_shown)
    assert collected.nothing, collected


def test_the_port_answers_when_the_ready_line_is_printed(served_folder: Path) -> None:
    process, address = start_web(served_folder)
    host, _, port = address.removeprefix("http://").partition(":")

    with socket.socket() as probe:
        refused = probe.connect_ex((host, int(port)))
    stop_web(process)

    assert refused == 0


def test_the_server_stops_with_its_envelope(served_folder: Path) -> None:
    process, address = start_web(served_folder)

    code, out = stop_web(process)

    envelope = json.loads(out)
    assert (code, len(out.splitlines())) == (0, 1)
    assert (envelope["command"], envelope["data"]) == ("web", {"path": str(served_folder), "url": address})
