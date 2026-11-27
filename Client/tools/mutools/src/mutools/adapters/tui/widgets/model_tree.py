from collections.abc import Set
from pathlib import Path
from typing import ClassVar

from rich.text import Text
from textual import on
from textual.binding import Binding, BindingType
from textual.message import Message
from textual.widgets import Tree
from textual.widgets.tree import TreeNode

from mutools.app.model_parts import Part

GUIDE_DEPTH = 2
INDEX_GAP = "  "


def part_label(part: Part, changed: bool, mark: str) -> Text:
    if not changed:
        return Text(part.label)
    index, gap, rest = part.label.partition(INDEX_GAP)
    style = f"bold {mark}"
    if not gap:
        return Text.assemble(part.label, (" ●", style))
    return Text.assemble((f"{index}●", style), f" {rest}")


class ModelTree(Tree[str]):
    ICON_NODE = "▸ "
    ICON_NODE_EXPANDED = "▾ "
    BINDINGS: ClassVar[list[BindingType]] = [Binding("enter", "select_cursor", "edit")]

    class PartHighlighted(Message):
        def __init__(self, pointer: str) -> None:
            super().__init__()
            self.pointer = pointer

    class PartSelected(Message):
        def __init__(self, pointer: str) -> None:
            super().__init__()
            self.pointer = pointer

    def __init__(self) -> None:
        super().__init__("", data="")
        self.guide_depth = GUIDE_DEPTH
        self.auto_expand = False
        self.pointer = ""
        self.nodes: dict[str, TreeNode[str]] = {"": self.root}
        self._path: Path | None = None

    def _folded(self) -> set[str]:
        return {pointer for pointer, node in self.nodes.items() if node.allow_expand and not node.is_expanded}

    def show(self, path: Path, root: Part, changed: Set[str], mark: str) -> None:
        same_file = path == self._path
        folded = self._folded() if same_file else set()
        self._path = path
        self.reset(part_label(root, root.pointer in changed, mark), root.pointer)
        self.nodes = {root.pointer: self.root}
        pending = [(self.root, root)]
        while pending:
            node, part = pending.pop()
            for child in part.children:
                label = part_label(child, child.pointer in changed, mark)
                added = node.add(
                    label, data=child.pointer, expand=child.pointer not in folded, allow_expand=bool(child.children)
                )
                self.nodes[child.pointer] = added
                pending.append((added, child))
        if root.pointer not in folded:
            self.root.expand()
        self._move_to(self.pointer if same_file and self.pointer in self.nodes else root.pointer)

    def _move_to(self, pointer: str) -> None:
        node = self.nodes[pointer]
        ancestor = node.parent
        while ancestor is not None:
            if not ancestor.is_expanded:
                ancestor.expand()
            ancestor = ancestor.parent
        self.pointer = pointer
        self.get_node_at_line(0)
        self.move_cursor(node)

    def select(self, pointer: str) -> None:
        if pointer not in self.nodes:
            return
        moved = pointer != self.pointer
        self._move_to(pointer)
        if moved:
            self.post_message(self.PartHighlighted(pointer))

    def toggle(self, pointer: str) -> bool:
        node = self.nodes.get(pointer)
        if node is None or not node.allow_expand:
            return False
        node.toggle()
        return True

    @on(Tree.NodeHighlighted)
    def _part_highlighted(self, event: Tree.NodeHighlighted[str]) -> None:
        event.stop()
        pointer = event.node.data
        if pointer is not None and pointer != self.pointer and self.nodes.get(pointer) is event.node:
            self.pointer = pointer
            self.post_message(self.PartHighlighted(pointer))

    @on(Tree.NodeSelected)
    def _part_selected(self, event: Tree.NodeSelected[str]) -> None:
        event.stop()
        if event.node.data is not None and self.nodes.get(event.node.data) is event.node:
            self.post_message(self.PartSelected(event.node.data))
