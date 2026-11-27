from collections.abc import Iterable
from dataclasses import dataclass
from typing import ClassVar

from rich.text import Text
from textual import events
from textual.binding import Binding, BindingType
from textual.widgets import DataTable

from mutools.app.rows import row_label
from mutools.core.checks.text_checks import PRINTF_PATTERN
from mutools.core.json_types import JsonValue
from mutools.core.leaves import leaf_text

TEXT_WIDTH_FLOOR = 20
INDEX_WIDTH = 6
SECTION_WIDTH = 7
BYTES_WIDTH = 5
CELL_PADDING = 2
FRAME_WIDTH = 5


@dataclass(frozen=True, slots=True)
class RowLayout:
    title: str
    leaf_rows: bool
    section_size: int | None


class RecordTableView(DataTable[Text]):
    BINDINGS: ClassVar[list[BindingType]] = [Binding("enter", "select_cursor", "edit")]

    def __init__(self) -> None:
        super().__init__(cursor_type="row")
        self._values: list[JsonValue] = []
        self._original: list[JsonValue] = []
        self._encoding = "cp1252"
        self._layout = RowLayout("text", leaf_rows=True, section_size=None)
        self._accent = "yellow"
        self._mark = "red"
        self.text_width = TEXT_WIDTH_FLOOR

    def on_mount(self) -> None:
        self._add_columns()

    def _fixed_widths(self) -> list[int]:
        section = [SECTION_WIDTH] if self._layout.section_size else []
        size = [BYTES_WIDTH] if self._layout.leaf_rows else []
        return [INDEX_WIDTH, *section, *size]

    def _add_columns(self) -> None:
        self.add_column(Text("#", justify="right"), key="index", width=INDEX_WIDTH)
        if self._layout.section_size:
            self.add_column(Text("section", justify="right"), key="section", width=SECTION_WIDTH)
        self.add_column(self._layout.title, key="value", width=self.text_width)
        if self._layout.leaf_rows:
            self.add_column(Text("bytes", justify="right"), key="bytes", width=BYTES_WIDTH)

    def _text_width(self, available: int) -> int:
        fixed = self._fixed_widths()
        return max(TEXT_WIDTH_FLOOR, available - sum(fixed) - (len(fixed) + 1) * CELL_PADDING - FRAME_WIDTH)

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
            self._add_record(index)
        if keys:
            self.move_cursor(row=cursor)

    def _label(self, value: JsonValue) -> Text:
        if self._layout.leaf_rows and leaf_text(value) is None:
            return Text("⟨hex record⟩", style="dim italic")
        label = row_label(value)
        text = Text(label if len(label) <= self.text_width else label[: self.text_width - 1] + "…")
        if self._layout.leaf_rows:
            text.highlight_regex(PRINTF_PATTERN, style=f"bold {self._accent}")
        return text

    def _byte_size(self, value: JsonValue) -> int:
        text = leaf_text(value)
        if text is not None:
            return len(text.encode(self._encoding))
        return len(str(value.get("hex", ""))) // 2 if isinstance(value, dict) else 0

    def _cells(self, index: int) -> dict[str, Text]:
        value = self._values[index]
        changed = index >= len(self._original) or value != self._original[index]
        style = f"bold {self._mark}" if changed else ""
        cells = {"index": Text(f"{index}{'●' if changed else ' '}", style=style, justify="right")}
        if self._layout.section_size:
            cells["section"] = Text(str(index // self._layout.section_size), justify="right")
        cells["value"] = self._label(value)
        if self._layout.leaf_rows:
            cells["bytes"] = Text(str(self._byte_size(value)), justify="right")
        return cells

    def _add_record(self, index: int) -> None:
        self.add_row(*self._cells(index).values(), key=str(index))

    def set_colors(self, accent: str, mark: str) -> None:
        self._accent, self._mark = accent, mark

    def show(
        self, values: list[JsonValue], original: list[JsonValue], encoding: str, shown: Iterable[int], layout: RowLayout
    ) -> None:
        self._values, self._original, self._encoding = list(values), list(original), encoding
        layout_changed = layout != self._layout
        self._layout = layout
        self.clear(columns=layout_changed)
        if layout_changed:
            self.text_width = self._text_width(self.size.width)
            self._add_columns()
        for index in shown:
            self._add_record(index)

    def update_rows(self, values: list[JsonValue], original: list[JsonValue]) -> bool:
        if len(values) != len(self._values) or len(original) != len(self._original):
            return False
        for index, value in enumerate(values):
            if value == self._values[index] and original[index] == self._original[index]:
                continue
            self._values[index], self._original[index] = value, original[index]
            key = str(index)
            if key in self.rows:
                for column, cell in self._cells(index).items():
                    self.update_cell(key, column, cell)
        return True

    def value_at(self, index: int) -> JsonValue:
        return self._values[index]

    def index_at_cursor(self) -> int | None:
        if not self.row_count:
            return None
        key = self.coordinate_to_cell_key(self.cursor_coordinate).row_key.value
        return None if key is None else int(key)
