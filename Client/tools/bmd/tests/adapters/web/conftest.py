import shutil
from pathlib import Path

import pytest
from nicegui import app
from nicegui.testing import User

from mubmd.adapters.storage import LocalFileStore
from mubmd.adapters.web.api import add_api
from mubmd.adapters.web.models import Models

REAL_FILES = ("Item/sword02.OZJ", "Item/unicon.OZJ", "Item/unicon01.OZT", "player/Player.bmd")


@pytest.fixture
def web_copy(model_copy: Path, reference_data: Path) -> Path:
    for name in REAL_FILES:
        shutil.copyfile(reference_data / name, model_copy / name)
    return model_copy


@pytest.fixture
def viewer(user: User, web_copy: Path) -> User:
    add_api(app, Models(LocalFileStore(), web_copy))
    return user
