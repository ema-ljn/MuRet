from typing import ClassVar

from rich.text import Text
from textual import events
from textual.binding import Binding, BindingType
from textual.widgets import DataTable

from mubmd.core.checks.text_checks import PRINTF_PATTERN
from mubmd.core.documents.text_table import TEXT_RECORD_SIZE
from mubmd.core.json_types import JsonValue

TEXT_WIDTH_FLOOR = 20
FIXED_COLUMNS_WIDTH = 6 + 5 + 3 * 2 + 2 + 1 + 2


class TextTableView(DataTable[Text]):
    BINDINGS: ClassVar[list[BindingType]] = [Binding("enter", "select_cursor", "edit")]

    def __init__(self) -> None:
        super().__init__(cursor_type="row")
        self._texts: list[JsonValue] = []
        self._original: list[JsonValue] = []
        self._encoding = "cp1252"
        self._accent = "yellow"
        self._mark = "red"
        self.text_width = TEXT_WIDTH_FLOOR

    def on_mount(self) -> None:
        self._add_columns()

    def _add_columns(self) -> None:
        self.add_column(Text("#", justify="right"), key="index", width=6)
        self.add_column("text", key="text", width=self.text_width)
        self.add_column(Text("bytes", justify="right"), key="bytes", width=5)

    def on_resize(self, event: events.Resize) -> None:
        width = max(TEXT_WIDTH_FLOOR, event.size.width - FIXED_COLUMNS_WIDTH)
        if width == self.text_width:
            return
        self.text_width = width
        cursor = self.cursor_row
        keys = [int(str(row.key.value)) for row in self.ordered_rows]
        self.clear(columns=True)
        self._add_columns()
        for index in keys:
            self.add_row(*self._cells(index, self._texts[index]), key=str(index))
        if keys:
            self.move_cursor(row=cursor)

    def _cells(self, index: int, value: JsonValue) -> tuple[Text, Text, Text]:
        changed = value != self._original[index]
        mark = Text(f"{index}{'●' if changed else ' '}", style=f"bold {self._mark}" if changed else "", justify="right")
        if isinstance(value, str):
            shown = value if len(value) <= self.text_width else value[: self.text_width - 1] + "…"
            text = Text(shown)
            text.highlight_regex(PRINTF_PATTERN, style=f"bold {self._accent}")
            size = len(value.encode(self._encoding))
        else:
            text = Text("⟨hex record⟩", style="dim italic")
            size = TEXT_RECORD_SIZE
        return mark, text, Text(str(size), justify="right")

    def set_colors(self, accent: str, mark: str) -> None:
        self._accent, self._mark = accent, mark

    def show(self, texts: list[JsonValue], original: list[JsonValue], encoding: str, query: str = "") -> None:
        self._texts, self._original, self._encoding = list(texts), list(original), encoding
        self.clear()
        wanted = query.casefold()
        for index, value in enumerate(texts):
            if not wanted or (isinstance(value, str) and wanted in value.casefold()):
                self.add_row(*self._cells(index, value), key=str(index))

    def update_rows(self, texts: list[JsonValue]) -> None:
        for index, value in enumerate(texts):
            if value == self._texts[index]:
                continue
            self._texts[index] = value
            key = str(index)
            if key in self.rows:
                for column, cell in zip(("index", "text", "bytes"), self._cells(index, value), strict=True):
                    self.update_cell(key, column, cell)

    def text_at(self, index: int) -> JsonValue:
        return self._texts[index]

    def index_at_cursor(self) -> int | None:
        if not self.row_count:
            return None
        key = self.coordinate_to_cell_key(self.cursor_coordinate).row_key.value
        return None if key is None else int(key)
