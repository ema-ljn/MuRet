from collections.abc import Callable, Sequence
from dataclasses import dataclass
from functools import partial
from pathlib import PurePosixPath
from typing import Any, Final, Protocol

from nicegui import ui
from nicegui.events import GenericEventArguments

from mutools.adapters.web.api import OWN
from mutools.adapters.web.elements import Viewport
from mutools.adapters.web.models import Models
from mutools.adapters.web.opened import OpenedModel
from mutools.adapters.web.widgets import Press, words
from mutools.app.model_parts import counted
from mutools.app.pose_edits import HELD_BONE, copy_action, copy_key, move_bone, remove_key, turn_bone
from mutools.app.session import EditSession
from mutools.core.errors import MutoolsError
from mutools.core.formats.action_file import is_dummy

TURN: Final = "turn"
MOVE: Final = "move"
TOOLS: Final = ((TURN, "Turn"), (MOVE, "Move"))
EDITOR_MARK: Final = "editor"
PICK_A_BONE: Final = "Pick a bone in the inspector's list of bones to turn or move it at the key under the playhead."
NO_ACTIONS: Final = "This model has no actions to edit."
HELD_NOTE: Final = (
    "This action holds its first bone at the x and y of key 0, as the client does: a move of it at another key shows "
    "only in z."
)
KEY_TIMEOUT_SECONDS: Final = 3.0

type _Edit = Callable[[EditSession], object]


class Edited(Protocol):
    def __call__(self, action: int | None, *, replay: bool = True) -> object: ...


@dataclass(frozen=True, slots=True)
class Answer:
    text: str = ""
    bad: bool = False
    warnings: tuple[str, ...] = ()


def _number(value: object) -> int | None:
    return value if type(value) is int else None


def _able(press: Press, able: bool) -> None:
    if not able:
        press.props("disabled")


def _numbers(value: object) -> Sequence[float] | None:
    if not isinstance(value, list) or not all(type(number) in (int, float) for number in value):
        return None
    return value


class Editor:
    def __init__(
        self,
        models: Models,
        viewport: Viewport,
        edited: Edited,
        play: Callable[[int], Any],
        open_file: Callable[[str], Any],
    ) -> None:
        self._models = models
        self._viewport = viewport
        self._edited = edited
        self._play = play
        self._open_file = open_file
        self._opened: OpenedModel | None = None
        self._action = 0
        self._bone: int | None = None
        self._tool = TURN
        self._answer = Answer()
        viewport.on("posed", self._posed)

    def show(self, opened: OpenedModel | None, action: int, bone: int | None) -> None:
        if opened is None or self._opened is None or opened.file != self._opened.file:
            self._answer = Answer()
        self._opened, self._action, self._bone = opened, action, bone
        self._viewport.edit(self._editable)
        self._viewport.pose_bone(self._posed_bone, self._tool)
        self.build.refresh()

    @property
    def _editable(self) -> bool:
        return self._opened is not None and self._opened.on == OWN and bool(self._opened.actions)

    @property
    def _session(self) -> EditSession | None:
        return None if self._opened is None else self._models.session(self._opened.file)

    def _dummy(self, bone: int) -> bool:
        bones = self._opened.own.get("bones") if self._opened is not None else None
        found = bones[bone] if isinstance(bones, list) and 0 <= bone < len(bones) else None
        return not isinstance(found, dict) or is_dummy(found)

    @property
    def _posed_bone(self) -> int | None:
        bone = self._bone
        return bone if self._editable and bone is not None and not self._dummy(bone) else None

    @ui.refreshable_method
    def build(self) -> None:
        with ui.element("section").classes("mt-editor").props('aria-label="Edit"').mark(EDITOR_MARK):
            with ui.element("div").classes("mt-editor-head"):
                words("Edit", "mt-section-title")
                self._where()
                self._history()
            if self._editable:
                self._tools()
            self._notes()

    def _where(self) -> None:
        opened = self._opened
        if opened is None:
            return
        if opened.on != OWN:
            words(f"These are the actions of {PurePosixPath(opened.skeleton).name}.", "mt-quiet")
            return
        if not opened.actions:
            words(NO_ACTIONS, "mt-quiet")
            return
        bone = self._bone
        if bone is None or bone >= len(opened.scene.bones):
            words(PICK_A_BONE, "mt-quiet")
        elif self._dummy(bone):
            words(f"Bone {bone} is a dummy bone, which has no pose.", "mt-quiet")
        else:
            words(f"Bone {bone}, {opened.scene.bones[bone].name}, at the key under the playhead.", "mt-quiet")

    def _history(self) -> None:
        session = self._session
        if session is None:
            return
        with ui.element("div").classes("mt-editor-save"):
            _able(Press("Undo", label="Undo, Ctrl Z", on_click=self.undo).classes("mt-edit-press"), session.can_undo)
            _able(Press("Redo", label="Redo, Ctrl Y", on_click=self.redo).classes("mt-edit-press"), session.can_redo)
            text = f"Save, {counted(session.edits, 'edit')}" if session.dirty else "Nothing to save"
            _able(Press(text, label="Save the model, Ctrl S", on_click=self.save).classes("mt-save"), session.dirty)

    def _tools(self) -> None:
        with ui.element("div").classes("mt-editor-tools"):
            with ui.element("div").classes("mt-tools").props('role="group" aria-label="Tool"'):
                for tool, label in TOOLS:
                    press = Press(label, on_click=partial(self._choose_tool, tool)).classes("mt-tool")
                    press.chosen(tool == self._tool)
            Press("Copy key", label="Copy the key under the playhead after it", on_click=self._copy_key).classes(
                "mt-edit-press"
            )
            Press("Remove key", label="Remove the key under the playhead", on_click=self._remove_key).classes(
                "mt-edit-press"
            )
            Press("Copy action", label="Copy this action after the last one", on_click=self._copy_action).classes(
                "mt-edit-press"
            )

    def _notes(self) -> None:
        opened = self._opened
        if opened is not None and opened.on != OWN:
            with ui.element("div").classes("mt-editor-note"):
                words("To edit them, open that file.", "mt-small")
                Press(f"Open {PurePosixPath(opened.skeleton).name}", on_click=self._open_skeleton).classes(
                    "mt-edit-press"
                )
        held = opened is not None and self._editable and opened.actions[self._action].lock
        if held and self._posed_bone == HELD_BONE:
            words(HELD_NOTE, "mt-small mt-editor-note")
        answer = self._answer
        if answer.text:
            with ui.element("div").classes("mt-editor-answer").props('role="status"'):
                words(answer.text, "mt-bad" if answer.bad else "")
                for warning in answer.warnings:
                    words(warning, "mt-small mt-warn")

    def _open_skeleton(self) -> None:
        if self._opened is not None:
            self._open_file(self._opened.skeleton)

    def _choose_tool(self, tool: str) -> None:
        self._tool = tool
        self._viewport.pose_bone(self._posed_bone, tool)
        self.build.refresh()

    def _say(self, answer: Answer) -> None:
        self._answer = answer
        self.build.refresh()

    def _apply(self, edit: _Edit, action: int | None, done: str) -> object:
        session = self._session
        if session is None or not self._editable:
            return None
        try:
            result = edit(session)
        except MutoolsError as error:
            self._viewport.reposed(action, self._opened.scene.actions if self._opened else ())
            self._say(Answer(error.message, bad=True))
            return None
        if result is False:
            self._say(Answer("That leaves the file as it was."))
            return result
        self._answer = Answer(done)
        self._edited(action)
        return result

    def _posed(self, event: GenericEventArguments) -> None:
        args = event.args if isinstance(event.args, dict) else {}
        action, key, bone = _number(args.get("action")), _number(args.get("key")), _number(args.get("bone"))
        quaternion, position = _numbers(args.get("quaternion")), _numbers(args.get("position"))
        if action is None or key is None or bone is None:
            return
        if quaternion is not None:
            turned = f"Turned bone {bone} at key {key}."
            self._apply(lambda session: turn_bone(session, action, bone, key, quaternion), action, turned)
        elif position is not None:
            moved = f"Moved bone {bone} at key {key}."
            self._apply(lambda session: move_bone(session, action, bone, key, position), action, moved)

    async def _key(self) -> int | None:
        try:
            key = await self._viewport.pause()
        except TimeoutError:
            key = None
        if _number(key) is None:
            self._say(Answer("The page did not say which key is under the playhead.", bad=True))
        return _number(key)

    async def _copy_key(self) -> None:
        action, key = self._action, await self._key()
        if key is not None:
            self._apply(lambda session: copy_key(session, action, key), action, f"Copied key {key} after it.")

    async def _remove_key(self) -> None:
        action, key = self._action, await self._key()
        if key is not None:
            self._apply(lambda session: remove_key(session, action, key), action, f"Removed key {key}.")

    def _copy_action(self) -> None:
        action = self._action
        copied = self._apply(lambda session: copy_action(session, action), None, f"Copied action {action}.")
        if isinstance(copied, int):
            self._answer = Answer(f"Copied action {action} as action {copied}.")
            self._play(copied)

    def undo(self) -> None:
        session = self._session
        if session is not None and self._editable and session.undo():
            self._answer = Answer("Undid the last edit.")
            self._edited(None)

    def redo(self) -> None:
        session = self._session
        if session is not None and self._editable and session.redo():
            self._answer = Answer("Did the edit again.")
            self._edited(None)

    def save(self) -> None:
        opened = self._opened
        if opened is None or self._session is None or not self._session.dirty:
            return
        try:
            result = self._models.save(opened.file)
        except MutoolsError as error:
            suggestion = f" Try: {error.suggestion}" if error.suggestion else ""
            self._say(Answer(f"{error.message}.{suggestion}", bad=True))
            return
        warnings = tuple(f"{finding.code} {finding.pointer}: {finding.message}" for finding in result.warnings)
        name = PurePosixPath(opened.file).name
        self._answer = Answer(f"Saved {name}: {counted(len(result.changes), 'value')} changed.", warnings=warnings)
        self._edited(None, replay=False)
