from pathlib import Path
from typing import Any

from mubmd.core.documents.model import Model
from mubmd.core.formats.model_format import ModelFormat
from mubmd.core.formats.model_view import model_view
from mubmd.core.json_types import JsonObject, JsonValue
from mubmd.core.pointer import resolve
from mubmd.core.ports import CatalogFile

CODEC = ModelFormat()


def model_file(name: str) -> CatalogFile:
    return CatalogFile(Path(name).name, None, read_by_client=False)


def model_of(reference_data: Path, name: str) -> Model:
    return CODEC.decode((reference_data / name).read_bytes(), model_file(name))


def view_of(reference_data: Path, name: str) -> JsonObject:
    return model_view(model_of(reference_data, name))


def at(view: JsonValue, pointer: str) -> Any:
    return resolve(view, pointer)


def put(view: JsonValue, pointer: str, value: JsonValue) -> None:
    parent, _, last = pointer.rpartition("/")
    container = at(view, parent)
    container[int(last) if isinstance(container, list) else last] = value
