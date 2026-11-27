from dataclasses import dataclass
from pathlib import Path

from mubmd.app.checks import TextureFiles, texture_not_found
from mubmd.core.errors import ErrorCode, MubmdError
from mubmd.core.formats.model_textures import texture_use
from mubmd.core.formats.texture_images import RgbaImage, read_ozj, read_ozt
from mubmd.core.json_types import JsonObject
from mubmd.core.leaves import leaf_text
from mubmd.core.pointer import resolve
from mubmd.core.ports import FileStore


@dataclass(frozen=True, slots=True)
class MeshTexture:
    file: Path
    content: bytes | RgbaImage


def _texture_name(view: JsonObject, mesh: int) -> str:
    pointer = f"/meshes/{mesh}"
    part = resolve(view, pointer)
    name = leaf_text(part.get("texture_name")) if isinstance(part, dict) else None
    if name is None:
        raise MubmdError(ErrorCode.FILE_NOT_FOUND, f"The texture name at {pointer} is not text", pointer=pointer)
    return name


def mesh_texture(store: FileStore, model: Path, view: JsonObject, mesh: int) -> MeshTexture:
    use = texture_use(_texture_name(view, mesh))
    file = TextureFiles(store).find(model, use.file)
    if file is None:
        raise MubmdError(ErrorCode.FILE_NOT_FOUND, texture_not_found(model, use.file))
    data = store.read(file)
    return MeshTexture(file, read_ozt(data) if use.loader == "OZT" else read_ozj(data))
