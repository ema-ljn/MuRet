from pathlib import Path

import pytest

from tests.app.fake_store import FakeStore
from tests.conftest import TABLE_FILES

WORK = Path("/work/Data")
TEXT = WORK / "Local" / "Text.bmd"
REFERENCE = Path("/ref/Data")
REFERENCE_TEXT = REFERENCE / "Local" / "Text.bmd"
GATE = WORK / "Gate.bmd"
SKILL = WORK / "Local" / "skill.bmd"
SLIDE = WORK / "Local" / "slide.bmd"
DIALOG = WORK / "Local" / "Dialog.bmd"
MOVE_REQUIREMENTS = WORK / "Local" / "movereq.bmd"
ITEM_SET_OPTIONS = WORK / "Local" / "itemsetoption.bmd"


@pytest.fixture
def store(text_bytes: bytes) -> FakeStore:
    return FakeStore({TEXT: text_bytes, REFERENCE_TEXT: text_bytes}, reference=REFERENCE)


@pytest.fixture
def tables(reference_data: Path) -> FakeStore:
    return FakeStore({WORK / name: (reference_data / name).read_bytes() for name in TABLE_FILES})
