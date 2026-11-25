from dataclasses import dataclass


@dataclass(frozen=True, slots=True)
class RecordTable:
    records: tuple[bytes, ...]
    encoding: str
    stored_checksum: int | None
    trailing: bytes
