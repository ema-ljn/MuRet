from collections.abc import Callable
from typing import Any, Final

from nicegui import ui
from nicegui.elements.mixins.text_element import TextElement

ON: Final = "mt-on"
ICONS: Final = {
    "lock": '<path d="M8 11V8a4 4 0 0 1 8 0v3"/><rect x="5" y="11" width="14" height="10" rx="2"/>',
    "search": '<circle cx="11" cy="11" r="7"/><path d="m20 20-3.5-3.5"/>',
    "down": '<path d="m6 9 6 6 6-6"/>',
    "frame": '<path d="M4 9V4h5M20 9V4h-5M4 15v5h5M20 15v5h-5"/><circle cx="12" cy="12" r="2.5"/>',
    "picture": '<path d="M4 8h3l2-3h6l2 3h3v11H4z"/><circle cx="12" cy="13" r="3.5"/>',
    "open": '<path d="m9 6 6 6-6 6"/>',
    "previous": '<path d="m15 6-6 6 6 6"/>',
    "next": '<path d="m9 6 6 6-6 6"/>',
}
AXES: Final = (
    '<svg width="54" height="54" viewBox="0 0 56 56" role="img" aria-label="Axes: x, y, and z pointing up">'
    '<circle cx="28" cy="28" r="26" fill="rgba(22,23,26,0.45)" stroke="rgba(228,227,221,0.18)"/>'
    '<path d="M28 28 L46 36" stroke="#E68A7E" stroke-width="2" stroke-linecap="round"/>'
    '<path d="M28 28 L14 38" stroke="#8CC79B" stroke-width="2" stroke-linecap="round"/>'
    '<path d="M28 28 L28 9" stroke="#9AA6FF" stroke-width="2" stroke-linecap="round"/>'
    '<text x="48" y="44" font-size="9" fill="#E68A7E">x</text><text x="7" y="45" font-size="9" fill="#8CC79B">y</text>'
    '<text x="31" y="11" font-size="9" fill="#9AA6FF">z</text></svg>'
)


class Press(TextElement):
    def __init__(self, text: str = "", *, on_click: Callable[[], Any] | None = None, label: str | None = None) -> None:
        super().__init__(tag="button", text=text)
        self._props["type"] = "button"
        self._classes.append("mt-press")
        if label is not None:
            self._props["aria-label"] = label
        if on_click is not None:
            self.on("click", on_click)

    def chosen(self, on: bool) -> "Press":
        self.classes(add=ON) if on else self.classes(remove=ON)
        self._props["aria-pressed"] = "true" if on else "false"
        self.update()
        return self


def words(text: str, classes: str = "") -> ui.label:
    label = ui.label(text).classes(f"mt-words {classes}".strip())
    label.tag = "span"
    return label


def icon(name: str, size: int = 14, classes: str = "") -> ui.html:
    svg = (
        f'<svg width="{size}" height="{size}" viewBox="0 0 24 24" fill="none" stroke="currentColor" '
        f'stroke-width="2" aria-hidden="true">{ICONS[name]}</svg>'
    )
    return ui.html(svg, sanitize=False, tag="span").classes(f"mt-icon {classes}".strip())
