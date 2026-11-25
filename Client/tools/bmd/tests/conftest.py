import shutil
from pathlib import Path

import pytest

TEXT_FILES = ("Local/Text.bmd", "Local/Texttest.bmd", "Local/TKOU.bmd")


@pytest.fixture(scope="session")
def reference_data() -> Path:
    return Path(__file__).resolve().parents[3] / "assets" / "Data"


@pytest.fixture
def working_copy(tmp_path: Path, reference_data: Path) -> Path:
    data = tmp_path / "Data"
    for name in TEXT_FILES:
        target = data / name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(reference_data / name, target)
    return data


@pytest.fixture(scope="session")
def text_bytes(reference_data: Path) -> bytes:
    return (reference_data / "Local" / "Text.bmd").read_bytes()
