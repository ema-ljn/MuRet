from nicegui.awaitable_response import AwaitableResponse
from nicegui.elements.scene import Object3D


class BmdModel(Object3D, component="bmd_model.js"):
    def __init__(self, file: str, on: str) -> None:
        super().__init__(file, on)

    def play(self, action: int) -> None:
        self.run_method("play", action)

    def pause(self) -> AwaitableResponse:
        return self.run_method("pause")

    def resume(self) -> None:
        self.run_method("resume")

    def seek(self, key: float) -> None:
        self.run_method("seek", key)

    def speed(self, keys_per_second: float) -> None:
        self.run_method("speed", keys_per_second)

    def skeleton(self, shown: bool) -> None:
        self.run_method("skeleton", shown)

    def additive(self, on: bool) -> None:
        self.run_method("additive", on)

    def highlight(self, mesh: int | None) -> None:
        self.run_method("highlight", mesh)

    def mark_bone(self, bone: int | None) -> None:
        self.run_method("mark_bone", bone)
