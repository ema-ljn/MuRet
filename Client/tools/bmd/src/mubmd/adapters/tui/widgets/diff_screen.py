from typing import ClassVar

from rich.text import Text
from textual.app import ComposeResult
from textual.binding import Binding, BindingType
from textual.containers import VerticalScroll
from textual.screen import ModalScreen
from textual.widgets import Static

from mubmd.core.json_types import JsonValue
from mubmd.core.patch import Change


def _shown(value: JsonValue) -> str:
    return value if isinstance(value, str) else str(value)


class DiffScreen(ModalScreen[bool]):
    BINDINGS: ClassVar[list[BindingType]] = [
        Binding("y", "confirm", "write"),
        Binding("enter", "confirm", "write", show=False),
        Binding("escape", "cancel", "cancel"),
        Binding("n", "cancel", "cancel", show=False),
    ]

    def __init__(self, changes: tuple[Change, ...], target: str, *, confirm: bool) -> None:
        super().__init__()
        self.changes = changes
        self.target = target
        self.confirm = confirm

    def compose(self) -> ComposeResult:
        theme = self.app.current_theme
        body = Text()
        for change in self.changes:
            body.append(f"{change.pointer}\n", style="bold")
            body.append(f"  - {_shown(change.old)}\n", style=theme.error or "red")
            body.append(f"  + {_shown(change.new)}\n", style=theme.success or "green")
        hint = "y write · escape cancel" if self.confirm else "escape close"
        body.append(f"\n{hint}", style="dim")
        with VerticalScroll(id="diff") as frame:
            count = len(self.changes)
            frame.border_title = f"{'save ' if self.confirm else ''}{count} change{'s' if count != 1 else ''}"
            frame.border_subtitle = f"→ {self.target}"
            yield Static(body)

    def action_confirm(self) -> None:
        self.dismiss(self.confirm)

    def action_cancel(self) -> None:
        self.dismiss(False)
