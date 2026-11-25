from collections.abc import Iterable
from pathlib import Path

from rich.style import Style
from rich.text import Text
from textual.message import Message
from textual.widgets import Tree
from textual.widgets.tree import TreeNode


def display_order(folder: Path, files: Iterable[Path]) -> list[Path]:
    def key(path: Path) -> list[tuple[int, str]]:
        *folders, name = path.relative_to(folder).parts
        return [*((0, part.casefold()) for part in folders), (1, name.casefold())]

    return sorted(files, key=key)


class BmdTree(Tree[Path]):
    ICON_NODE = "▸ "
    ICON_NODE_EXPANDED = "▾ "

    class FileSelected(Message):
        def __init__(self, path: Path) -> None:
            super().__init__()
            self.path = path

    def __init__(self, folder: Path, files: Iterable[Path]) -> None:
        super().__init__(folder.name, data=folder)
        self.unsaved: set[Path] = set()
        self.opened: Path | None = None
        self.nodes: dict[Path, TreeNode[Path]] = {folder: self.root}
        for file in display_order(folder, files):
            self.nodes[file] = self._folder(file.parent).add_leaf(file.name, data=file)
        self.root.expand()

    def _folder(self, path: Path) -> TreeNode[Path]:
        if path not in self.nodes:
            self.nodes[path] = self._folder(path.parent).add(path.name, data=path)
        return self.nodes[path]

    def render_label(self, node: TreeNode[Path], base_style: Style, style: Style) -> Text:
        if node.allow_expand:
            return super().render_label(node, base_style, style)
        label = Text(str(node.label), style=style)
        text = Text.assemble(("◆ " if node.data == self.opened else "  ", base_style), label)
        if node.data in self.unsaved:
            text.append(" ●", style=f"bold {self.app.current_theme.error or 'red'}")
        return text

    def on_tree_node_selected(self, event: Tree.NodeSelected[Path]) -> None:
        event.stop()
        if not event.node.allow_expand and event.node.data is not None:
            self.post_message(self.FileSelected(event.node.data))

    def _repaint(self, paths: set[Path]) -> None:
        for path in paths:
            if path in self.nodes:
                self.nodes[path].refresh()

    def reveal(self, path: Path) -> None:
        stale = {path} | ({self.opened} if self.opened is not None else set())
        self.opened = path
        node = self.nodes.get(path)
        while node is not None and node.parent is not None:
            node = node.parent
            node.expand()
        self._repaint(stale)

    def mark_unsaved(self, paths: set[Path]) -> None:
        stale = paths ^ self.unsaved
        self.unsaved = paths
        self._repaint(stale)
