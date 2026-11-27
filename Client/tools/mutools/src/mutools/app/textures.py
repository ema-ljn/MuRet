from dataclasses import dataclass
from pathlib import Path

from mutools.app.checks import TextureFiles, texture_not_found
from mutools.core.errors import ErrorCode, MutoolsError
from mutools.core.formats.model_textures import texture_use
from mutools.core.formats.texture_images import RgbaImage, read_ozj, read_ozt
from mutools.core.json_types import JsonObject
from mutools.core.leaves import leaf_text
from mutools.core.pointer import resolve
from mutools.core.ports import FileStore


@dataclass(frozen=True, slots=True)
class MeshTexture:
    file: Path
    content: bytes | RgbaImage


def _texture_name(view: JsonObject, mesh: int) -> str:
    pointer = f"/meshes/{mesh}"
    part = resolve(view, pointer)
    name = leaf_text(part.get("texture_name")) if isinstance(part, dict) else None
    if name is None:
        raise MutoolsError(ErrorCode.FILE_NOT_FOUND, f"The texture name at {pointer} is not text", pointer=pointer)
    return name


def mesh_texture(store: FileStore, model: Path, view: JsonObject, mesh: int) -> MeshTexture:
    use = texture_use(_texture_name(view, mesh))
    file = TextureFiles(store).find(model, use.file)
    if file is None:
        raise MutoolsError(ErrorCode.FILE_NOT_FOUND, texture_not_found(model, use.file))
    data = store.read(file)
    return MeshTexture(file, read_ozt(data) if use.loader == "OZT" else read_ozj(data))
