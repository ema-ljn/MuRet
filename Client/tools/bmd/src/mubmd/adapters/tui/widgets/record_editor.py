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

    def show_value(self, value: str) -> None:
        self.loaded = value
        self.value = value

    def revert(self) -> None:
        self.value = self.loaded

    def reset(self) -> None:
        self.pointer = None
        self.clear()
        self.loaded = ""
        self.disabled = True
        self.border_title = None
        self.border_subtitle = None
        self.remove_class("-invalid")

    @property
    def pending(self) -> bool:
        return self.pointer is not None and self.value != self.loaded

    def show_measure(self, measure: Measure) -> None:
        self.set_class(measure.error is not None, "-invalid")
        if measure.error is not None:
            self.border_subtitle = f"✗ {measure.error.code}"
            return
        codes = "".join(f" · ⚠ {warning.code}" for warning in measure.warnings)
        self.border_subtitle = f"{measure.size}/{measure.limit} bytes{codes}"
