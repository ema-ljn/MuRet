from textual.widgets import Static


class TitleBar(Static):
    def __init__(self) -> None:
        super().__init__("◆ mutools", markup=False)
        self.text = "◆ mutools"

    def show(self, folder: str, read_only: bool, unsaved: int) -> None:
        mode = "read-only" if read_only else "working copy"
        if unsaved:
            mode += f" · {unsaved} unsaved"
        self.text = f"◆ mutools ─ {folder} ─ {mode}"
        self.update(self.text)
