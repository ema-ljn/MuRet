import shutil
from pathlib import Path

import pytest
from nicegui.testing import User

SWORD = "Item/Sword01.bmd"
HELM = "player/HelmMale01.bmd"
PLAYER = "player/Player.bmd"
ROUTES = ("/api/scene", "/api/poses", "/api/texture")
NEAR = 0.01


async def test_the_scene_of_a_model(viewer: User) -> None:
    response = await viewer.http_client.get("/api/scene", params={"file": SWORD})

    scene = response.json()
    assert (response.status_code, response.headers["Cache-Control"]) == (200, "no-store")
    assert (scene["file"], scene["skeleton"], scene["donor"]) == (SWORD, SWORD, None)
    assert scene["bones"] == [{"name": "Box01", "parent": -1}]
    assert scene["actions"] == [{"keys": 1, "lock": False}]
    assert [len(mesh["bones"]) for mesh in scene["meshes"]] == [204]
    assert scene["bounds"]["low"] == pytest.approx([-1.673, -64.725, -9.254], abs=NEAR)
    assert scene["bounds"]["high"] == pytest.approx([1.771, 10.318, 8.245], abs=NEAR)


async def test_the_scene_on_the_donor(viewer: User) -> None:
    own = (await viewer.http_client.get("/api/scene", params={"file": HELM})).json()
    borrowed = (await viewer.http_client.get("/api/scene", params={"file": HELM, "on": "donor"})).json()
    refused = await viewer.http_client.get("/api/scene", params={"file": "player/Helper03.bmd", "on": "donor"})

    assert (own["skeleton"], own["donor"], len(own["bones"])) == (HELM, PLAYER, 56)
    assert (borrowed["skeleton"], len(borrowed["bones"]), len(borrowed["actions"])) == (PLAYER, 60, 205)
    assert refused.status_code == 400


async def test_the_poses_of_an_action(viewer: User) -> None:
    response = await viewer.http_client.get("/api/poses", params={"file": SWORD, "action": 0})
    missing = await viewer.http_client.get("/api/poses", params={"file": SWORD, "action": 1})

    poses = response.json()
    assert (poses["keys"], len(poses["bones"])) == (1, 1)
    assert (len(poses["bones"][0]["positions"]), len(poses["bones"][0]["quaternions"])) == (3, 4)
    assert (missing.status_code, missing.json()["code"]) == (404, "POINTER_NOT_FOUND")


async def test_a_jpeg_texture(viewer: User) -> None:
    response = await viewer.http_client.get("/api/texture", params={"file": SWORD, "mesh": 0})

    assert (response.headers["Content-Type"], len(response.content)) == ("image/jpeg", 6292)


async def test_a_tga_texture(viewer: User) -> None:
    response = await viewer.http_client.get("/api/texture", params={"file": "player/Helper03.bmd", "mesh": 1})

    assert (response.headers["Content-Type"], len(response.content)) == ("application/octet-stream", 4096)
    assert (response.headers["X-Width"], response.headers["X-Height"]) == ("32", "32")


async def test_a_texture_that_is_not_there(viewer: User) -> None:
    response = await viewer.http_client.get("/api/texture", params={"file": "Item/waterring.bmd", "mesh": 1})

    assert (response.status_code, response.json()["code"]) == (404, "FILE_NOT_FOUND")


@pytest.mark.parametrize(
    "file", ["../outside.bmd", "<the reference's sword>", "Item/../../outside.bmd", "Item/Sword01.bmd\x00", ""]
)
async def test_a_file_outside_the_folder_is_not_found(
    viewer: User, web_copy: Path, reference_data: Path, file: str
) -> None:
    shutil.copyfile(web_copy / SWORD, web_copy.parent / "outside.bmd")
    asked = str(reference_data / SWORD) if file.startswith("<") else file

    for route in ROUTES:
        response = await viewer.http_client.get(route, params={"file": asked})

        assert (route, response.status_code, response.json()["code"]) == (route, 404, "FILE_NOT_FOUND")


async def test_a_table_is_not_a_scene(viewer: User) -> None:
    response = await viewer.http_client.get("/api/scene", params={"file": "Local/Text.bmd"})

    assert (response.status_code, response.json()["code"]) == (404, "UNKNOWN_FORMAT")


async def test_a_changed_file_is_read_again(viewer: User, web_copy: Path) -> None:
    before = (await viewer.http_client.get("/api/scene", params={"file": SWORD})).json()
    shutil.copyfile(web_copy / HELM, web_copy / SWORD)

    after = (await viewer.http_client.get("/api/scene", params={"file": SWORD})).json()

    assert (len(before["bones"]), len(after["bones"])) == (1, 56)
