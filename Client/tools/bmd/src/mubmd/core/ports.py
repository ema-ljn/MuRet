from dataclasses import dataclass
from pathlib import Path
from typing import Protocol

from mubmd.core.findings import Finding
from mubmd.core.json_types import JsonObject


@dataclass(frozen=True, slots=True)
class CatalogFile:
    name: str
    encoding: str | None
    read_by_client: bool


@dataclass(frozen=True, slots=True)
class FieldInfo:
    pointer: str
    type: str
    limit: str
    reference: str


class FormatCodec[D](Protocol):
    name: str

    def decode(self, data: bytes, file: CatalogFile) -> D: ...

    def encode(self, document: D) -> bytes: ...

    def to_view(self, document: D) -> JsonObject: ...

    def from_view(self, view: JsonObject) -> D: ...

    def check(self, document: D, original: D | None) -> tuple[Finding, ...]: ...

    def describe_fields(self) -> tuple[FieldInfo, ...]: ...


class FileStore(Protocol):
    def read(self, path: Path) -> bytes: ...

    def sha256(self, path: Path) -> str: ...

    def exists(self, path: Path) -> bool: ...

    def is_reference(self, path: Path) -> bool: ...

    def write(self, path: Path, data: bytes, *, expected_sha256: str | None) -> None: ...

    def copy(self, source: Path, destination: Path) -> None: ...

    def list_bmd(self, folder: Path) -> tuple[Path, ...]: ...

    def list_files(self, folder: Path) -> tuple[Path, ...]: ...
