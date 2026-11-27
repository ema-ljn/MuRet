from collections.abc import Sequence
from typing import ClassVar

from rich.text import Text
from textual import events
from textual.binding import Binding, BindingType
from textual.widgets import DataTable

from mutools.core.json_types import JsonValue
from mutools.core.leaves import leaf_text, leaves
from mutools.core.pointer import format_pointer

NAME_WIDTH_LIMIT = 28
VALUE_WIDTH_FLOOR = 20
MARK_WIDTH = 1
CELL_PADDING = 2
COLUMNS = 3
SCROLLBAR_WIDTH = 1
BYTES_SHOWN = 16
FACT_KEY = "fact "


def shown_value(value: JsonValue, width: int) -> Text:
    text = leaf_text(value)
    if text is None and isinstance(value, dict):
        digits = next((str(part) for part in value.values()), "")
        cut = digits if len(digits) <= 2 * BYTES_SHOWN else f"{digits[: 2 * BYTES_SHOWN]}…"
        return Text(f"⟨{next(iter(value), 'hex')} {cut}⟩", style="dim italic")
    shown = str(value) if text is None else text
    line = Text(shown if len(shown) <= width else shown[: width - 1] + "…")
    if text is not None and isinstance(value, dict):
        line.append(" ⟨+tail⟩", style="dim italic")
    return line


class FieldList(DataTable[Text]):
    BINDINGS: ClassVar[list[BindingType]] = [Binding("enter", "select_cursor", "edit")]

    def __init__(self) -> None:
        super().__init__(cursor_type="row", show_header=False)
        self.record_pointer: str | None = None
        self._record: JsonValue = None
        self._original: JsonValue = None
        self._names: list[str] = []
        self._facts: Sequence[tuple[str, str]] = ()
        self._mark = "red"
        self._value_width = VALUE_WIDTH_FLOOR

    def _cells(self, value: JsonValue, changed: bool) -> tuple[Text, Text]:
        mark = Text("●" if changed else " ", style=f"bold {self._mark}" if changed else "")
        return mark, shown_value(value, self._value_width)

    def show(
        self,
        pointer: str,
        title: str,
        record: JsonValue,
        original: JsonValue,
        mark: str,
        facts: Sequence[tuple[str, str]] = (),
    ) -> None:
        fields = [(format_pointer(tokens)[1:], value) for tokens, value in leaves(record)]
        before = {format_pointer(tokens)[1:]: value for tokens, value in leaves(original)}
        names = [*(name for name, _ in fields), *(f"{FACT_KEY}{label}" for label, _ in facts)]
        same_rows = pointer == self.record_pointer and names == self._names
        self.record_pointer, self._record, self._original, self._mark = pointer, record, original, mark
        self._facts = facts
        self.border_title = title
        labels = [*(name for name, _ in fields), *(label for label, _ in facts)]
        name_width = min(NAME_WIDTH_LIMIT, max(map(len, labels), default=1))
        fixed = name_width + MARK_WIDTH + COLUMNS * CELL_PADDING + SCROLLBAR_WIDTH
        value_width = max(VALUE_WIDTH_FLOOR, self.size.width - fixed)
        in_place = same_rows and value_width == self._value_width
        self._names, self._value_width = names, value_width
        rows = [
            (name, Text(name), *self._cells(value, name not in before or before[name] != value))
            for name, value in fields
        ]
        rows += [
            (f"{FACT_KEY}{label}", Text(label, style="dim"), Text(" "), Text(value, style="dim"))
            for label, value in facts
        ]
        if in_place:
            for key, _, changed, shown in rows:
                self.update_cell(key, "mark", changed)
                self.update_cell(key, "value", shown)
            return
        cursor = self.cursor_row if same_rows else 0
        self.clear(columns=True)
        self.add_column("field", key="field", width=name_width)
        self.add_column("", key="mark", width=MARK_WIDTH)
        self.add_column("value", key="value", width=value_width)
        for key, label, changed, shown in rows:
            self.add_row(label, changed, shown, key=key)
        if rows:
            self.move_cursor(row=min(cursor, len(rows) - 1))

    def empty(self) -> None:
        self.record_pointer, self._record, self._original, self._names = None, None, None, []
        self._facts = ()
        self.border_title = None
        self.clear(columns=True)

    def on_resize(self, event: events.Resize) -> None:
        if self.record_pointer is not None and self.border_title is not None:
            title = str(self.border_title)
            self.show(self.record_pointer, title, self._record, self._original, self._mark, self._facts)

    def pointer_of(self, name: str) -> str | None:
        if self.record_pointer is None or name.startswith(FACT_KEY):
            return None
        return f"{self.record_pointer}/{name}"
