from textual.widgets import Static


class TitleBar(Static):
    def __init__(self) -> None:
        super().__init__("◆ mubmd")
        self.text = "◆ mubmd"

    def show(self, folder: str, read_only: bool, unsaved: int) -> None:
        mode = "read-only" if read_only else "working copy"
        if unsaved:
            mode += f" · {unsaved} unsaved"
        self.text = f"◆ mubmd ─ {folder} ─ {mode}"
        self.update(self.text)
