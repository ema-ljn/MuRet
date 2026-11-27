from typing import Final

from nicegui import ui
from nicegui.awaitable_response import AwaitableResponse

STUDIO: Final = {"mutools-studio": "studio"}


class Viewport(ui.element, component="viewport.js", esm=STUDIO):
    def open(self, file: str, on: str) -> None:
        self.run_method("open", file, on)

    def play(self, action: int, *, repeats: bool, start: bool = True) -> None:
        self.run_method("play", action, repeats, start)

    def pause(self) -> AwaitableResponse:
        return self.run_method("pause")

    def seek(self, key: float) -> None:
        self.run_method("seek", key)

    def speed(self, keys_per_second: float) -> None:
        self.run_method("speed", keys_per_second)

    def mode(self, view: str) -> None:
        self.run_method("mode", view)

    def skeleton(self, shown: bool) -> None:
        self.run_method("skeleton", shown)

    def additive(self, on: bool) -> None:
        self.run_method("additive", on)

    def highlight(self, mesh: int | None) -> None:
        self.run_method("highlight", mesh)

    def mark_bone(self, bone: int | None) -> None:
        self.run_method("mark_bone", bone)

    def toggle(self) -> None:
        self.run_method("command", {"name": "toggle"})

    def frame(self) -> None:
        self.run_method("frame")

    def picture(self, name: str) -> None:
        self.run_method("picture", name)


class Transport(ui.element, component="transport.js", esm=STUDIO):
    def __init__(self, speed: str) -> None:
        super().__init__()
        self._props["speed"] = speed


class RenderState(ui.element, component="render_state.js", esm=STUDIO):
    def __init__(self, variant: str) -> None:
        super().__init__()
        self._props["variant"] = variant


class Thumbnail(ui.element, component="thumbnail.js", esm=STUDIO):
    def __init__(self, file: str, stamp: str) -> None:
        super().__init__()
        self._props["file"] = file
        self._props["stamp"] = stamp


class Shortcuts(ui.element, component="shortcuts.js", esm=STUDIO):
    pass
