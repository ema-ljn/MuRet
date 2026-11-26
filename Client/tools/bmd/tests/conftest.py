import shutil
from pathlib import Path

import pytest

TEXT_FILES = ("Local/Text.bmd", "Local/Texttest.bmd", "Local/TKOU.bmd")
TABLE_FILES: tuple[str, ...] = (
    *TEXT_FILES,
    "Local/item.bmd",
    "Local/itemtest.bmd",
    "Local/n380.bmd",
    "Local/skill.bmd",
    "Local/Skilltest.bmd",
    "Gate.bmd",
    "Gatetest.bmd",
    "Local/Filter.bmd",
    "Local/FilterName.bmd",
    "Local/itemsettype.bmd",
    "Local/itemsettypetest.bmd",
    "Local/itemsetoption.bmd",
    "Local/itemsetoptiontest.bmd",
    "Local/credit.bmd",
    "Local/credittest.bmd",
)


@pytest.fixture(scope="session")
def reference_data() -> Path:
    return Path(__file__).resolve().parents[3] / "assets" / "Data"


@pytest.fixture
def working_copy(tmp_path: Path, reference_data: Path) -> Path:
    data = tmp_path / "Data"
    for name in TABLE_FILES:
        target = data / name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(reference_data / name, target)
    return data


@pytest.fixture(scope="session")
def text_bytes(reference_data: Path) -> bytes:
    return (reference_data / "Local" / "Text.bmd").read_bytes()
