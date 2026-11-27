from collections.abc import Iterator
from pathlib import Path
from typing import Any

from mubmd.core.catalog import Identified
from mubmd.core.documents.model import Model
from mubmd.core.findings import Finding, Severity
from mubmd.core.formats.model_textures import texture_text, texture_use
from mubmd.core.ports import FileStore


class TextureFiles:
    def __init__(self, store: FileStore) -> None:
        self._store = store
        self._names: dict[Path, frozenset[str]] = {}
        self._beside: dict[Path, tuple[Path, ...]] = {}

    def _names_in(self, folder: Path) -> frozenset[str]:
        if folder not in self._names:
            self._names[folder] = frozenset(path.name.casefold() for path in self._store.files_in(folder))
        return self._names[folder]

    def _folders_beside(self, folder: Path) -> tuple[Path, ...]:
        if folder not in self._beside:
            others = self._store.folders_in(folder.parent)
            self._beside[folder] = tuple(other for other in others if other != folder)
        return self._beside[folder]

    def place(self, model: Path, file: str) -> str | None:
        wanted = file.casefold()
        folder = model.absolute().parent
        if wanted in self._names_in(folder):
            return ""
        return next((other.name for other in self._folders_beside(folder) if wanted in self._names_in(other)), None)


def _texture_findings(textures: TextureFiles, path: Path, model: Model, original: Model | None) -> Iterator[Finding]:
    folder = path.absolute().parent.name
    for index, mesh in enumerate(model.meshes):
        name = texture_text(mesh.texture_name, model.encoding)
        if name is None:
            continue
        file = texture_use(name).file
        place = textures.place(path, file)
        pointer = f"/meshes/{index}/texture_name"
        if place is None:
            message = f"No {file} in {folder} or in the folders beside it"
            yield Finding("TEXTURE_NOT_FOUND", Severity.WARNING, pointer, message)
        elif place and original is not None:
            before = original.meshes[index].texture_name if index < len(original.meshes) else None
            if mesh.texture_name != before:
                message = f"{folder} has no {file}; the folder {place} beside it does"
                yield Finding("TEXTURE_ELSEWHERE", Severity.WARNING, pointer, message)


def findings_of(
    store: FileStore,
    path: Path,
    identified: Identified,
    document: Any,
    original: Any | None,
    textures: TextureFiles | None = None,
) -> tuple[Finding, ...]:
    findings: tuple[Finding, ...] = identified.format.codec.check(document, original)
    if not isinstance(document, Model):
        return findings
    files = textures or TextureFiles(store)
    return (*findings, *_texture_findings(files, path, document, original))
