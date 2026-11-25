from textual.widgets import Input

from mubmd.app.session import Measure


class RecordEditor(Input):
    def __init__(self) -> None:
        super().__init__(placeholder="select a record and press enter", disabled=True)
        self.pointer: str | None = None
        self.loaded = ""

    def load(self, pointer: str, index: int, value: str) -> None:
        self.pointer = pointer
        self.loaded = value
        self.disabled = False
        self.border_title = f"record {index}"
        self.value = value

    def revert(self) -> None:
        self.value = self.loaded

    @property
    def pending(self) -> bool:
        return self.pointer is not None and self.value != self.loaded

    def show_measure(self, measure: Measure) -> None:
        self.set_class(measure.error is not None, "-invalid")
        if measure.error is None:
            self.border_subtitle = f"{measure.size}/{measure.limit} bytes"
        else:
            self.border_subtitle = f"✗ {measure.error.code}"
