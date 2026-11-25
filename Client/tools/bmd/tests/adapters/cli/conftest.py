import hashlib
import json
import shutil
from collections.abc import Callable
from dataclasses import dataclass
from pathlib import Path
from typing import Any

import pytest

from mubmd.adapters.cli.main import main


@dataclass(frozen=True)
class Run:
    code: int
    out: str
    err: str

    @property
    def json(self) -> Any:
        return json.loads(self.out)

    @property
    def error(self) -> Any:
        return json.loads(self.err)["error"]


type Cli = Callable[..., Run]


@pytest.fixture
def cli(capsys: pytest.CaptureFixture[str]) -> Cli:
    def run(*argv: str | Path) -> Run:
        code = main([str(arg) for arg in argv])
        captured = capsys.readouterr()
        return Run(code, captured.out, captured.err)

    return run


@pytest.fixture
def text(working_copy: Path) -> Path:
    return working_copy / "Local" / "Text.bmd"


@pytest.fixture
def reference(tmp_path: Path, reference_data: Path) -> Path:
    data = tmp_path / "ref" / "Data"
    (data / "Local").mkdir(parents=True)
    shutil.copyfile(reference_data / "Local" / "Text.bmd", data / "Local" / "Text.bmd")
    digest = hashlib.sha256((data / "Local" / "Text.bmd").read_bytes()).hexdigest()
    (tmp_path / "ref" / "Data.sha256").write_text(f"{digest}  ./Local/Text.bmd\n")
    return data
