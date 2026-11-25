import re

from mubmd.core.documents.text_table import TEXT_RECORD_SIZE, TextTable, text_pointer
from mubmd.core.findings import Finding, Severity

_PRINTF = re.compile(r"%[-+ #0]*(?:\*|\d+)?(?:\.(?:\*|\d+))?(?:hh|h|ll|l|L|j|z|t|I64|I32|I)?[diouxXeEfFgGaAcspn%]")


def printf_specifiers(text: str) -> tuple[str, ...]:
    return tuple(match for match in _PRINTF.findall(text) if not match.endswith("%"))


def check_text_table(table: TextTable, original: TextTable | None) -> tuple[Finding, ...]:
    findings: list[Finding] = []
    for index, record in enumerate(table.records):
        pointer = text_pointer(index)
        text = table.text(index)
        if b"\0" not in record:
            findings.append(
                Finding(
                    "TEXT_FILLS_RECORD",
                    Severity.WARNING,
                    pointer,
                    f"Text {index} fills all {TEXT_RECORD_SIZE} bytes with no 0 after it: "
                    "the client reads it on into the next record",
                )
            )
        if text is None:
            findings.append(
                Finding(
                    "TEXT_NOT_DECODABLE", Severity.INFO, pointer, f"Text {index} is not {table.encoding}: kept as hex"
                )
            )
        elif original is not None and original.records[index] != record:
            before = original.text(index)
            if before is not None and printf_specifiers(before) != printf_specifiers(text):
                findings.append(
                    Finding(
                        "PRINTF_CHANGED",
                        Severity.WARNING,
                        pointer,
                        f"Text {index} changed its printf specifiers from {printf_specifiers(before)} "
                        f"to {printf_specifiers(text)}: the client may pass it to sprintf",
                    )
                )
    return tuple(findings)
