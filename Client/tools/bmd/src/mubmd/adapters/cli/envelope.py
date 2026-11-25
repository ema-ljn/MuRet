import json
from functools import cache
from typing import Any, Final

from pydantic import TypeAdapter

from mubmd.core.errors import MubmdError
from mubmd.core.json_types import JsonObject, JsonValue

SCHEMA_VERSION: Final = 1


@cache
def adapter(value_type: Any) -> TypeAdapter[Any]:
    return TypeAdapter(value_type)


def to_json_value(value: object, value_type: Any) -> JsonValue:
    result: JsonValue = adapter(value_type).dump_python(value, mode="json")
    return result


def output_schema(value_type: Any) -> JsonObject:
    schema: JsonObject = adapter(value_type).json_schema(mode="serialization")
    return schema


def success(command: str, data: JsonValue, warnings: list[JsonValue]) -> JsonObject:
    return {"ok": True, "schema": SCHEMA_VERSION, "command": command, "data": data, "warnings": warnings}


def failure(error: MubmdError, received: JsonObject) -> JsonObject:
    return {
        "ok": False,
        "schema": SCHEMA_VERSION,
        "error": {
            "code": error.code.value,
            "message": error.message,
            "suggestion": error.suggestion,
            "input": received,
            "pointer": error.pointer,
        },
    }


def to_line(value: JsonValue) -> str:
    line = json.dumps(value, ensure_ascii=False, separators=(",", ":"))
    return line.encode("utf-8", "backslashreplace").decode("utf-8")
