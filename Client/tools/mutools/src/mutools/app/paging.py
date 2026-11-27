from dataclasses import dataclass
from typing import Final

from mutools.core.errors import ErrorCode, MutoolsError

DEFAULT_LIMIT: Final = 50


@dataclass(frozen=True, slots=True)
class Page[T]:
    items: tuple[T, ...]
    total: int
    next_offset: int | None


def page[T](items: list[T], limit: int, offset: int) -> Page[T]:
    if limit < 1 or offset < 0:
        raise MutoolsError(ErrorCode.USAGE, "--limit is at least 1 and --offset at least 0")
    end = offset + limit
    return Page(tuple(items[offset:end]), len(items), end if end < len(items) else None)
