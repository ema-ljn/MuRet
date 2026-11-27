from collections.abc import Callable
from functools import partial
from typing import Any, Final

from nicegui import ui

from mutools.adapters.web.elements import Transport
from mutools.adapters.web.opened import DEFAULT_KEYS_PER_SECOND, OpenedModel, ShownAction
from mutools.adapters.web.widgets import Press, icon, words
from mutools.app.model_parts import counted

RATES: Final = ((0.5, "Half speed"), (1.0, "Client speed"), (2.0, "Double speed"))
TIMES: Final = "\N{MULTIPLICATION SIGN}"
RATE_TEXT: Final = {0.5: f"\N{VULGAR FRACTION ONE HALF}{TIMES}", 1.0: f"1{TIMES}", 2.0: f"2{TIMES}"}
TIMELINE_MARK: Final = "timeline"


def _detail(action: ShownAction) -> str:
    once = "" if action.repeats else ", once"
    return f"{counted(action.keys, 'key')}, {action.seconds:.1f} s{once}"


def _summary(opened: OpenedModel) -> str:
    total = sum(action.seconds for action in opened.actions)
    if all(action.client_keys_per_second is not None for action in opened.actions):
        return f"End to end at the client's speed, {total:.1f} seconds in all"
    speed = f"{DEFAULT_KEYS_PER_SECOND:g} keys a second"
    if any(action.client_keys_per_second is not None for action in opened.actions):
        return f"End to end, {total:.1f} seconds in all; the client sets no speed for some, which play at {speed}"
    return f"End to end at {speed}, {total:.1f} seconds in all: the client's speed is not known"


class Timeline:
    def __init__(self, on_action: Callable[[int], Any], on_rate: Callable[[float], Any]) -> None:
        self._on_action = on_action
        self._on_rate = on_rate

    @ui.refreshable_method
    def build(self, opened: OpenedModel | None, action: int, rate: float) -> None:
        with ui.element("section").classes("mt-actions").props('aria-label="Actions"').mark(TIMELINE_MARK):
            with ui.element("div").classes("mt-actions-head"):
                words("Actions", "mt-section-title")
                if opened is None:
                    return
                if not opened.actions:
                    words("This model has no actions.", "mt-quiet")
                    return
                words(_summary(opened), "mt-quiet")
                with ui.element("div").classes("mt-rates").props('role="group" aria-label="Speed"'):
                    for value, label in RATES:
                        Press(RATE_TEXT[value], label=label, on_click=partial(self._on_rate, value)).classes(
                            "mt-rate"
                        ).chosen(value == rate)
            with ui.element("div").classes("mt-strip"):
                for shown in opened.actions:
                    with Press(shown.name, on_click=partial(self._on_action, shown.index)) as segment:
                        segment.classes("mt-segment").style(f"flex-grow: {shown.seconds:.3f}")
                        if shown.lock:
                            icon("lock", 11, "mt-segment-lock").props('role="img" aria-label="locked"')
                        words(_detail(shown), "mt-segment-detail")
                    segment.chosen(shown.index == action)
            Transport(opened.speed_line(opened.actions[action], rate))
