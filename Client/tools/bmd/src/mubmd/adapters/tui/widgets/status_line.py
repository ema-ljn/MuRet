from textual.widgets import Static


class StatusLine(Static):
    def __init__(self) -> None:
        super().__init__("")
        self.text = ""

    def show(self, text: str, *, error: bool = False) -> None:
        self.text = text
        self.set_class(error, "-error")
        self.update(text)
