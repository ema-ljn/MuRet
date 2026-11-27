from typing import Final

from mutools.core.errors import ErrorCode

EXIT_OK: Final = 0
EXIT_FINDINGS: Final = 1
EXIT_USAGE: Final = 2
EXIT_NOT_FOUND: Final = 20
EXIT_PRECONDITION: Final = 30
EXIT_INTERNAL: Final = 70

EXIT_CODE_MEANINGS: Final = {
    EXIT_OK: "done",
    EXIT_FINDINGS: "done, with findings (validate) or differences (diff, roundtrip)",
    EXIT_USAGE: "bad usage, or a value that does not fit its field",
    EXIT_NOT_FOUND: "a file, format or pointer that does not exist",
    EXIT_PRECONDITION: "a precondition failed: read-only reference, corrupt file, file changed since it was read, "
    "a JSON Patch test that did not hold, write-verify, a folder that is not empty, a disk error",
    EXIT_INTERNAL: "a bug in the tool: report the command",
}

_USAGE: Final = {ErrorCode.USAGE, ErrorCode.INVALID_VALUE, ErrorCode.TEXT_TOO_LONG, ErrorCode.NOT_ENCODABLE}
_NOT_FOUND: Final = {ErrorCode.FILE_NOT_FOUND, ErrorCode.UNKNOWN_FORMAT, ErrorCode.POINTER_NOT_FOUND}


def exit_code_for(code: ErrorCode) -> int:
    if code in _USAGE:
        return EXIT_USAGE
    if code in _NOT_FOUND:
        return EXIT_NOT_FOUND
    if code is ErrorCode.INTERNAL_ERROR:
        return EXIT_INTERNAL
    return EXIT_PRECONDITION
