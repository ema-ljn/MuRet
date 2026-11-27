import json
from enum import StrEnum
from itertools import islice
from typing import Final

from mutools.core.findings import Finding
from mutools.core.json_types import JsonObject, JsonValue
from mutools.core.leaves import is_blank, is_leaf, leaf_text, leaves
from mutools.core.pointer import format_pointer, parse_pointer
from mutools.core.ports import FieldInfo

LABEL_VALUES: Final = 8
ROW_POINTER_TOKENS: Final = len(("records", "N"))


class RowFilter(StrEnum):
    ALL = "all"
    USED = "used"
    CHANGED = "changed"
    FINDINGS = "findings"

    def following(self) -> "RowFilter":
        filters = list(RowFilter)
        return filters[(filters.index(self) + 1) % len(filters)]


def rows_key(view: JsonObject) -> str:
    return "texts" if "texts" in view else "records"


def rows_of(view: JsonObject) -> list[JsonValue]:
    rows = view.get(rows_key(view))
    return rows if isinstance(rows, list) else []


def row_pointer(view: JsonObject, index: int) -> str:
    return f"/{rows_key(view)}/{index}"


def label_title(fields: tuple[FieldInfo, ...]) -> str:
    for info in fields:
        if info.type == "text":
            return next(token for token in reversed(parse_pointer(info.pointer)) if not token.startswith("<"))
    return "values"


def _is_number(value: JsonValue) -> bool:
    return isinstance(value, int | float) and not isinstance(value, bool)


def edit_text(value: JsonValue) -> str | None:
    text = leaf_text(value)
    if text is None and _is_number(value):
        return json.dumps(value)
    return text


def row_label(value: JsonValue) -> str:
    if is_leaf(value):
        return leaf_text(value) or ""
    for _, leaf in leaves(value):
        text = leaf_text(leaf)
        if text:
            return text
    if is_blank(value):
        return ""
    numbers = (f"{format_pointer(tokens)[1:]} {leaf}" for tokens, leaf in leaves(value) if _is_number(leaf))
    return " · ".join(islice(numbers, LABEL_VALUES))


def row_text(value: JsonValue) -> str:
    texts = (leaf_text(leaf) for _, leaf in leaves(value))
    return "\n".join(text for text in texts if text).casefold()


def row_matches(value: JsonValue, query: str) -> bool:
    return query.casefold() in row_text(value)


def _row_of(pointer: str) -> str | None:
    tokens = parse_pointer(pointer)
    return None if len(tokens) < ROW_POINTER_TOKENS else format_pointer(tokens[:ROW_POINTER_TOKENS])


def findings_by_row(findings: tuple[Finding, ...]) -> dict[str, tuple[Finding, ...]]:
    rows: dict[str, tuple[Finding, ...]] = {}
    for finding in findings:
        row = _row_of(finding.pointer)
        if row is not None:
            rows[row] = (*rows.get(row, ()), finding)
    return rows


def row_shown(
    pointer: str, value: JsonValue, original: JsonValue, findings: tuple[Finding, ...], which: RowFilter
) -> bool:
    match which:
        case RowFilter.USED:
            return not is_blank(value)
        case RowFilter.CHANGED:
            return value != original
        case RowFilter.FINDINGS:
            return any(_row_of(finding.pointer) == pointer for finding in findings)
        case _:
            return True
