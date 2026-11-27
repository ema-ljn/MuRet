import ast
import sys
from collections.abc import Iterator
from pathlib import Path

PACKAGE = Path(__file__).resolve().parents[1] / "src" / "mubmd"
UI_LIBRARIES = {"typer", "textual", "rich", "click", "pydantic"}
WEB_LIBRARIES = {"nicegui", "fastapi", "starlette", "uvicorn"}
OTHER_ADAPTERS = ("mubmd.adapters.cli", "mubmd.adapters.tui", "typer", "textual")


def imported_modules(layer: str) -> Iterator[tuple[Path, str]]:
    for path in sorted((PACKAGE / layer).rglob("*.py")):
        for node in ast.walk(ast.parse(path.read_text(encoding="utf-8"))):
            if isinstance(node, ast.Import):
                for alias in node.names:
                    yield path, alias.name
            elif isinstance(node, ast.ImportFrom) and node.level == 0 and node.module:
                yield path, node.module


def test_core_imports_only_the_standard_library() -> None:
    offenders = [
        f"{path.name}: {module}"
        for path, module in imported_modules("core")
        if module.split(".")[0] not in sys.stdlib_module_names and not module.startswith("mubmd.core")
    ]
    assert offenders == []


def test_app_does_not_import_adapters() -> None:
    offenders = [
        f"{path.name}: {module}"
        for path, module in imported_modules("app")
        if module.startswith("mubmd.adapters") or module.split(".")[0] in UI_LIBRARIES | WEB_LIBRARIES
    ]
    assert offenders == []


def test_only_the_web_adapter_imports_the_web_libraries() -> None:
    web = PACKAGE / "adapters" / "web"
    offenders = [
        f"{path.name}: {module}"
        for path, module in imported_modules("adapters")
        if web not in path.parents and module.split(".")[0] in WEB_LIBRARIES
    ]
    assert offenders == []


def test_the_web_adapter_does_not_import_the_other_adapters() -> None:
    offenders = [
        f"{path.name}: {module}"
        for path, module in imported_modules("adapters/web")
        if module.startswith(OTHER_ADAPTERS) or module.split(".")[0] in OTHER_ADAPTERS
    ]
    assert offenders == []
