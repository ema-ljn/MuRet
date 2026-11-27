from collections.abc import Iterator
from pathlib import Path
from typing import Any

from mutools.core.catalog import Identified
from mutools.core.documents.model import Model
from mutools.core.findings import Finding, Severity
from mutools.core.formats.model_textures import texture_text, texture_use
from mutools.core.ports import FileStore


class TextureFiles:
    def __init__(self, store: FileStore) -> None:
        self._store = store
        self._files: dict[Path, dict[str, Path]] = {}
        self._beside: dict[Path, tuple[Path, ...]] = {}

    def _files_in(self, folder: Path) -> dict[str, Path]:
        if folder not in self._files:
            self._files[folder] = {path.name.casefold(): path for path in self._store.files_in(folder)}
        return self._files[folder]

    def _folders_beside(self, folder: Path) -> tuple[Path, ...]:
        if folder not in self._beside:
            others = self._store.folders_in(folder.parent)
            self._beside[folder] = tuple(other for other in others if other != folder)
        return self._beside[folder]

    def _folders_from(self, folder: Path) -> Iterator[Path]:
        yield folder
        yield from self._folders_beside(folder)

    def find(self, model: Path, file: str) -> Path | None:
        wanted = file.casefold()
        found = (self._files_in(folder).get(wanted) for folder in self._folders_from(model.absolute().parent))
        return next((path for path in found if path is not None), None)

    def place(self, model: Path, file: str) -> str | None:
        found = self.find(model, file)
        if found is None:
            return None
        return "" if found.parent == model.absolute().parent else found.parent.name


def texture_not_found(model: Path, file: str) -> str:
    return f"No {file} in {model.absolute().parent.name} or in the folders beside it"


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
            yield Finding("TEXTURE_NOT_FOUND", Severity.WARNING, pointer, texture_not_found(path, file))
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
