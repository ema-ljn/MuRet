from typing import ClassVar

from rich.text import Text
from textual import events
from textual.binding import Binding, BindingType
from textual.widgets import DataTable

from mubmd.core.checks.text_checks import PRINTF_PATTERN
from mubmd.core.documents.item_table import ITEM_NAME_SIZE, ITEM_SECTION_SIZE
from mubmd.core.documents.text_table import TEXT_RECORD_SIZE
from mubmd.core.json_types import JsonValue

TEXT_WIDTH_FLOOR = 20
FIXED_COLUMNS_WIDTH = 6 + 5 + 3 * 2 + 2 + 1 + 2
SECTION_COLUMN_WIDTH = 7


class RecordTableView(DataTable[Text]):
    BINDINGS: ClassVar[list[BindingType]] = [Binding("enter", "select_cursor", "edit")]

    def __init__(self) -> None:
        super().__init__(cursor_type="row")
        self._values: list[JsonValue] = []
        self._original: list[JsonValue] = []
        self._encoding = "cp1252"
        self._item_names = False
        self._accent = "yellow"
        self._mark = "red"
        self.text_width = TEXT_WIDTH_FLOOR

    def on_mount(self) -> None:
        self._add_columns()

    def _add_columns(self) -> None:
        self.add_column(Text("#", justify="right"), key="index", width=6)
        if self._item_names:
            self.add_column(Text("section", justify="right"), key="section", width=SECTION_COLUMN_WIDTH)
        self.add_column("name" if self._item_names else "text", key="value", width=self.text_width)
        self.add_column(Text("bytes", justify="right"), key="bytes", width=5)

    def _text_width(self, available: int) -> int:
        fixed_width = FIXED_COLUMNS_WIDTH + (SECTION_COLUMN_WIDTH + 2 if self._item_names else 0)
        return max(TEXT_WIDTH_FLOOR, available - fixed_width)

    def on_resize(self, event: events.Resize) -> None:
        width = self._text_width(event.size.width)
        if width == self.text_width:
            return
        self.text_width = width
        cursor = self.cursor_row
        keys = [int(str(row.key.value)) for row in self.ordered_rows]
        self.clear(columns=True)
        self._add_columns()
        for index in keys:
            self._add_record(index, self._values[index])
        if keys:
            self.move_cursor(row=cursor)

    def _cells(self, index: int, value: JsonValue) -> tuple[Text, Text, Text]:
        changed = value != self._original[index]
        mark = Text(f"{index}{'●' if changed else ' '}", style=f"bold {self._mark}" if changed else "", justify="right")
        if isinstance(value, str):
            shown = value if len(value) <= self.text_width else value[: self.text_width - 1] + "…"
            text = Text(shown)
            if not self._item_names:
                text.highlight_regex(PRINTF_PATTERN, style=f"bold {self._accent}")
            size = len(value.encode(self._encoding))
        else:
            text = Text("⟨hex name⟩" if self._item_names else "⟨hex record⟩", style="dim italic")
            size = ITEM_NAME_SIZE if self._item_names else TEXT_RECORD_SIZE
        return mark, text, Text(str(size), justify="right")

    def _add_record(self, index: int, value: JsonValue) -> None:
        mark, text, size = self._cells(index, value)
        if self._item_names:
            section = Text(str(index // ITEM_SECTION_SIZE), justify="right")
            self.add_row(mark, section, text, size, key=str(index))
        else:
            self.add_row(mark, text, size, key=str(index))

    def set_colors(self, accent: str, mark: str) -> None:
        self._accent, self._mark = accent, mark

    def show(
        self, values: list[JsonValue], original: list[JsonValue], encoding: str, query: str, *, item_names: bool
    ) -> None:
        self._values, self._original, self._encoding = list(values), list(original), encoding
        layout_changed = self._item_names != item_names
        self._item_names = item_names
        self.clear(columns=layout_changed)
        if layout_changed:
            self.text_width = self._text_width(self.size.width)
            self._add_columns()
        wanted = query.casefold()
        for index, value in enumerate(values):
            if not wanted or (isinstance(value, str) and wanted in value.casefold()):
                self._add_record(index, value)

    def update_rows(self, values: list[JsonValue], original: list[JsonValue]) -> None:
        for index, value in enumerate(values):
            if value == self._values[index] and original[index] == self._original[index]:
                continue
            self._values[index], self._original[index] = value, original[index]
            key = str(index)
            if key in self.rows:
                for column, cell in zip(("index", "value", "bytes"), self._cells(index, value), strict=True):
                    self.update_cell(key, column, cell)

    def value_at(self, index: int) -> JsonValue:
        return self._values[index]

    def index_at_cursor(self) -> int | None:
        if not self.row_count:
            return None
        key = self.coordinate_to_cell_key(self.cursor_coordinate).row_key.value
        return None if key is None else int(key)
