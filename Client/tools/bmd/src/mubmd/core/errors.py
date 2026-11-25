from enum import StrEnum


class ErrorCode(StrEnum):
    USAGE = "USAGE"
    FILE_NOT_FOUND = "FILE_NOT_FOUND"
    UNKNOWN_FORMAT = "UNKNOWN_FORMAT"
    POINTER_NOT_FOUND = "POINTER_NOT_FOUND"
    INVALID_VALUE = "INVALID_VALUE"
    TEXT_TOO_LONG = "TEXT_TOO_LONG"
    NOT_ENCODABLE = "NOT_ENCODABLE"
    CORRUPT_FILE = "CORRUPT_FILE"
    REFERENCE_READ_ONLY = "REFERENCE_READ_ONLY"
    STALE_FILE = "STALE_FILE"
    TEST_FAILED = "TEST_FAILED"
    WRITE_VERIFY_FAILED = "WRITE_VERIFY_FAILED"
    NOT_EMPTY = "NOT_EMPTY"
    IO_ERROR = "IO_ERROR"
    INTERNAL_ERROR = "INTERNAL_ERROR"


class MubmdError(Exception):
    def __init__(
        self, code: ErrorCode, message: str, *, suggestion: str | None = None, pointer: str | None = None
    ) -> None:
        super().__init__(message)
        self.code = code
        self.message = message
        self.suggestion = suggestion
        self.pointer = pointer
