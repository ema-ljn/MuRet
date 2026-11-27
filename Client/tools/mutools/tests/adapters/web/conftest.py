import os
import re
import shutil
import signal
import subprocess
import sys
from collections.abc import Iterator
from dataclasses import dataclass, field
from pathlib import Path

import pytest
from nicegui.testing import User
from playwright.sync_api import Browser, Page, sync_playwright

from mutools.adapters.storage import LocalFileStore
from mutools.adapters.web.server import build
from mutools.app.checks import TextureFiles
from mutools.core.formats.model_textures import texture_use
from mutools.core.leaves import leaf_text
from tests.models import view_of

REAL_FILES = ("Item/sword02.OZJ", "Item/unicon.OZJ", "Item/unicon01.OZT", "player/Player.bmd")
BROWSER_MODELS = (
    "Item/Sword01.bmd",
    "Item/waterring.bmd",
    "Monster/Monster01.bmd",
    "object35/Object36.bmd",
    "Object3/Object25.bmd",
    "player/HelmMale01.bmd",
    "player/Helper03.bmd",
    "player/Player.bmd",
)
MUTOOLS = Path(sys.executable).parent / "mutools"
ADDRESS = re.compile(r"http://127\.0\.0\.1:\d+")
STOP_SECONDS = 20
REFUSED = 400


@dataclass
class Collected:
    console_errors: list[str] = field(default_factory=list)
    page_errors: list[str] = field(default_factory=list)
    answers: list[tuple[int, str]] = field(default_factory=list)

    @property
    def nothing(self) -> bool:
        return not (self.console_errors or self.page_errors or self.answers)


@pytest.fixture
def web_copy(model_copy: Path, reference_data: Path) -> Path:
    for name in REAL_FILES:
        shutil.copyfile(reference_data / name, model_copy / name)
    return model_copy


@pytest.fixture
def viewer(user: User, web_copy: Path) -> User:
    build(LocalFileStore(), web_copy)
    return user


def _texture_files(reference_data: Path, name: str) -> Iterator[Path]:
    textures = TextureFiles(LocalFileStore())
    meshes = view_of(reference_data, name)["meshes"]
    assert isinstance(meshes, list)
    for mesh in meshes:
        text = leaf_text(mesh["texture_name"]) if isinstance(mesh, dict) else None
        found = None if text is None else textures.find(reference_data / name, texture_use(text).file)
        if found is not None:
            yield found


def start_web(folder: Path) -> tuple[subprocess.Popen[str], str]:
    command = [str(MUTOOLS), "web", str(folder), "--port", "0", "--no-browser"]
    # NiceGUI takes a process that has pytest's variable for one of its own screen tests and asks for their port.
    outside_pytest = {name: value for name, value in os.environ.items() if name != "PYTEST_CURRENT_TEST"}
    process = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, env=outside_pytest)
    assert process.stderr is not None
    line = process.stderr.readline()
    address = ADDRESS.search(line)
    if address is None:
        process.kill()
        raise AssertionError(f"mutools web did not start: {line}{process.stderr.read()}")
    return process, address.group()


def stop_web(process: subprocess.Popen[str]) -> tuple[int, str]:
    process.send_signal(signal.SIGINT)
    out, _ = process.communicate(timeout=STOP_SECONDS)
    return process.returncode, out


@pytest.fixture(scope="session")
def served_folder(tmp_path_factory: pytest.TempPathFactory, reference_data: Path) -> Path:
    folder = tmp_path_factory.mktemp("served") / "Data"
    for name in BROWSER_MODELS:
        for source in (reference_data / name, *_texture_files(reference_data, name)):
            target = folder / source.relative_to(reference_data)
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(source, target)
    return folder


@pytest.fixture(scope="session")
def served(served_folder: Path) -> Iterator[str]:
    process, address = start_web(served_folder)
    yield address
    stop_web(process)


@pytest.fixture(scope="session")
def chromium() -> Iterator[Browser]:
    with sync_playwright() as playwright:
        browser = playwright.chromium.launch(
            executable_path=os.environ.get("MUTOOLS_CHROMIUM") or None, args=["--enable-unsafe-swiftshader"]
        )
        yield browser
        browser.close()


@pytest.fixture
def collected() -> Collected:
    return Collected()


@pytest.fixture
def page(chromium: Browser, collected: Collected) -> Iterator[Page]:
    page = chromium.new_page(viewport={"width": 1500, "height": 900})
    page.on(
        "console", lambda message: collected.console_errors.append(message.text) if message.type == "error" else None
    )
    page.on("pageerror", lambda error: collected.page_errors.append(str(error)))
    page.on(
        "response",
        lambda response: (
            collected.answers.append((response.status, response.url)) if response.status >= REFUSED else None
        ),
    )
    yield page
    page.close()
