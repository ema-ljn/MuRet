from pathlib import Path

import pytest

from tests.app.fake_store import FakeStore

WORK = Path("/work/Data")
TEXT = WORK / "Local" / "Text.bmd"
REFERENCE = Path("/ref/Data")
REFERENCE_TEXT = REFERENCE / "Local" / "Text.bmd"


@pytest.fixture
def store(text_bytes: bytes) -> FakeStore:
    return FakeStore({TEXT: text_bytes, REFERENCE_TEXT: text_bytes}, reference=REFERENCE)
