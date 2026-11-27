from collections.abc import Callable
from pathlib import Path

import pytest

from mubmd.adapters.cli import commands
from mubmd.core.ports import FileStore
from tests.adapters.cli.conftest import Cli

ADDRESS = "http://127.0.0.1:8765"


def test_web_without_the_extra_says_how_to_install_it(
    cli: Cli, working_copy: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    def load_web() -> Callable[..., str]:
        raise ModuleNotFoundError("No module named 'nicegui'", name="nicegui")

    monkeypatch.setattr(commands, "load_web", load_web)

    run = cli("web", working_copy)

    assert (run.code, run.error["code"], run.error["suggestion"]) == (2, "USAGE", "pip install 'mubmd[web]'")


def test_web_on_a_path_that_does_not_exist(cli: Cli, tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    loaded: list[bool] = []
    monkeypatch.setattr(commands, "load_web", lambda: loaded.append(True))

    run = cli("web", tmp_path / "nope")

    assert (run.code, run.error["code"], loaded) == (20, "FILE_NOT_FOUND", [])


@pytest.mark.parametrize(
    ("flags", "port", "open_browser"),
    [((), 8765, True), (("--port", "0", "--no-browser"), 0, False)],
)
def test_web_prints_its_envelope_when_the_server_stops(
    cli: Cli,
    working_copy: Path,
    monkeypatch: pytest.MonkeyPatch,
    flags: tuple[str, ...],
    port: int,
    open_browser: bool,
) -> None:
    served: list[tuple[Path, int, bool]] = []

    def serve(store: FileStore, target: Path, *, port: int, open_browser: bool) -> str:
        served.append((target, port, open_browser))
        return ADDRESS

    monkeypatch.setattr(commands, "load_web", lambda: serve)

    run = cli("web", working_copy, *flags)

    assert (run.code, run.json["command"]) == (0, "web")
    assert run.json["data"] == {"path": str(working_copy), "url": ADDRESS}
    assert served == [(working_copy, port, open_browser)]


def test_a_port_out_of_range_is_a_usage_error(cli: Cli, working_copy: Path) -> None:
    run = cli("web", working_copy, "--port", "70000")

    assert (run.code, run.error["code"]) == (2, "USAGE")
