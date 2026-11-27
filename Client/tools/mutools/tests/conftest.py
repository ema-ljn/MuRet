import shutil
from pathlib import Path

import pytest

pytest_plugins = ["nicegui.testing.user_plugin"]

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
    "Local/slide.bmd",
    "Local/slidetest.bmd",
    "Local/movereq.bmd",
    "Local/movereqtest.bmd",
    "Local/TKOUREQ.bmd",
    "Local/MonsterSkill.bmd",
    "Local/MonsterSkilltest.bmd",
    "Local/Dialog.bmd",
    "Local/Dialogtest.bmd",
    "Local/quest.bmd",
    "Local/questtest.bmd",
)
MODEL_FILES: tuple[str, ...] = (
    "Object3/Object25.bmd",
    "object35/Object70.bmd",
    "Object12/Shine01.bmd",
    "skill/WaveForce.bmd",
    "skill/m_waves.bmd",
    "Item/Sword01.bmd",
    "Item/waterring.bmd",
    "player/Helper03.bmd",
    "player/HelmMale01.bmd",
    "object35/Object36.bmd",
    "NPC/BloodCastle01.bmd",
    "player/MaskHelmMale09.bmd",
)
TEXTURE_FILES: tuple[str, ...] = (
    "object35/crroof.OZJ",
    "Object12/light2_R.OZJ",
    "skill/GroundEff.OZJ",
    "skill/combo3.OZJ",
    "Item/sword02.OZJ",
    "Item/etcring.OZJ",
    "Item/unicon.OZJ",
    "Item/unicon01.OZT",
    "player/head_02.OZJ",
    "player/hide.OZJ",
    "object35/cr1_branch06.OZT",
    "object35/cr1_tree03.OZJ",
    "NPC/ng01.OZJ",
    "NPC/skin_barbarian_01.OZJ",
    "NPC/lnn01.OZT",
    "NPC/ju01_R.OZJ",
    "player/level_man02.OZJ",
    "player/hair_R.OZJ",
    "player/head_10.OZJ",
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


@pytest.fixture
def model_copy(working_copy: Path, reference_data: Path) -> Path:
    for name in MODEL_FILES:
        target = working_copy / name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(reference_data / name, target)
    for name in TEXTURE_FILES:
        (working_copy / name).touch()
    return working_copy


@pytest.fixture(scope="session")
def text_bytes(reference_data: Path) -> bytes:
    return (reference_data / "Local" / "Text.bmd").read_bytes()
