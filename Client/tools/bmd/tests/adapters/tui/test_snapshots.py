from collections.abc import Callable
from pathlib import Path
from typing import Any

import pytest
from textual.pilot import Pilot

from mubmd.adapters.storage import LocalFileStore
from mubmd.adapters.tui.app import MubmdApp

type SnapCompare = Callable[..., bool]


@pytest.mark.parametrize("theme", ["lorencia", "lorencia-light"])
def test_the_main_screen(
    snap_compare: SnapCompare, working_copy: Path, monkeypatch: pytest.MonkeyPatch, theme: str
) -> None:
    monkeypatch.setenv("HOME", str(working_copy.parent))
    app = MubmdApp(LocalFileStore(), working_copy)

    async def show_text(pilot: Pilot[Any]) -> None:
        app.theme = theme
        await app.open_path(working_copy / "Local" / "Text.bmd")
        await pilot.pause()
        app.select_index(500)
        await pilot.pause()

    assert snap_compare(app, terminal_size=(100, 30), run_before=show_text)


def test_a_record_table(snap_compare: SnapCompare, working_copy: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setenv("HOME", str(working_copy.parent))
    app = MubmdApp(LocalFileStore(), working_copy)

    async def show_dialog(pilot: Pilot[Any]) -> None:
        await app.open_path(working_copy / "Local" / "Dialog.bmd")
        await pilot.pause()
        app.select_index(53)
        await pilot.pause()

    assert snap_compare(app, terminal_size=(120, 40), run_before=show_dialog)


def test_a_model(snap_compare: SnapCompare, model_copy: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setenv("HOME", str(model_copy.parent))
    app = MubmdApp(LocalFileStore(), model_copy)

    async def show_bone(pilot: Pilot[Any]) -> None:
        await app.open_path(model_copy / "player" / "HelmMale01.bmd")
        await pilot.pause()
        app.select_part("/bones/3")
        await pilot.pause()

    assert snap_compare(app, terminal_size=(120, 40), run_before=show_bone)
