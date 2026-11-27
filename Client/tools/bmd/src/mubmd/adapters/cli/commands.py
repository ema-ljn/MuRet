import shlex
import sys
from collections.abc import Callable, Iterator
from contextlib import contextmanager
from dataclasses import dataclass
from pathlib import Path
from typing import Annotated, Any, Final

import typer

from mubmd import __version__
from mubmd.adapters.cli.envelope import to_json_value
from mubmd.adapters.cli.exit_codes import EXIT_FINDINGS, EXIT_OK
from mubmd.adapters.storage import LocalFileStore
from mubmd.app.documents import EditResult, parse_json
from mubmd.app.edits import apply_operations, build, set_value
from mubmd.app.folders import (
    DiffResult,
    DumpResult,
    RoundtripResult,
    ValidateResult,
    diff,
    dump,
    roundtrip,
    validate,
)
from mubmd.app.paging import DEFAULT_LIMIT, Page, page
from mubmd.app.queries import (
    FormatInfo,
    GetResult,
    InfoResult,
    Match,
    SchemaResult,
    find,
    format_schema,
    get,
    info,
    list_formats,
)
from mubmd.app.workspace import WorkspaceResult, workspace_init
from mubmd.core.errors import ErrorCode, MubmdError
from mubmd.core.findings import Finding, Severity
from mubmd.core.json_types import JsonObject
from mubmd.core.patch import Change, outlined, parse_patch

STORE: Final = LocalFileStore()
DEFAULT_WEB_PORT: Final = 8765
HIGHEST_PORT: Final = 65535
REFERENCE_DATA: Final = Path("Client") / "assets" / "Data"


@dataclass(frozen=True, slots=True)
class FormatList:
    formats: tuple[FormatInfo, ...]


@dataclass(frozen=True, slots=True)
class EditData:
    path: Path
    changed: bool
    dry_run: bool
    changes: tuple[Change, ...]
    total: int
    next_offset: int | None


@dataclass(frozen=True, slots=True)
class VersionData:
    version: str


@dataclass(frozen=True, slots=True)
class TuiData:
    path: Path


@dataclass(frozen=True, slots=True)
class WebData:
    path: Path
    url: str


@dataclass(frozen=True, slots=True)
class BriefData:
    brief: str


@dataclass(frozen=True, slots=True)
class Outcome:
    command: str
    data: object
    data_type: Any
    warnings: tuple[Finding, ...] = ()
    exit_code: int = EXIT_OK
    fields: tuple[str, ...] | None = None
    item_type: Any = None
    lines: tuple[JsonObject, ...] = ()


OUTPUT_TYPES: Final[dict[str, Any]] = {
    "formats": FormatList,
    "schema": SchemaResult,
    "workspace init": WorkspaceResult,
    "info": InfoResult,
    "find": Page[Match],
    "get": GetResult,
    "set": EditData,
    "apply": EditData,
    "validate": ValidateResult,
    "diff": DiffResult,
    "dump": DumpResult,
    "build": EditData,
    "roundtrip": RoundtripResult,
    "tui": TuiData,
    "web": WebData,
}

EXAMPLES: Final = {
    "formats": "mubmd formats",
    "schema": "mubmd schema text",
    "workspace init": "mubmd workspace init ~/mu/Data",
    "info": "mubmd info ~/mu/Data/Local/Text.bmd",
    "find": "mubmd find ~/mu/Data/Local/Text.bmd usuario --limit 10",
    "get": "mubmd get ~/mu/Data/Local/Text.bmd /texts --offset 500 --limit 2",
    "set": "mubmd set ~/mu/Data/Local/Text.bmd /texts/500 'Te fuiste del juego' --dry-run",
    "apply": 'mubmd apply ~/mu/Data/Local/Text.bmd \'[{"op": "test", "path": "/texts/100", "value": "Zen"}, '
    '{"op": "replace", "path": "/texts/100", "value": "Zen!"}]\'',
    "validate": "mubmd validate ~/mu/Data",
    "diff": "mubmd diff Client/assets/Data/Local/Text.bmd ~/mu/Data/Local/Text.bmd",
    "dump": "mubmd dump ~/mu/Data/Local/Text.bmd --output text.json",
    "build": "mubmd build text.json --output ~/mu/Data/Local/Text.bmd --dry-run",
    "roundtrip": "mubmd roundtrip ~/mu/Data",
    "tui": "mubmd tui ~/mu/Data",
    "web": "mubmd web ~/mu/Data",
}

FileArgument = Annotated[Path, typer.Argument(help="A .bmd file", show_default=False)]
PointerArgument = Annotated[
    str,
    typer.Argument(
        help="A JSON Pointer into the file's view, such as /texts/500 in a table or /meshes/0/texture_name in a model"
    ),
]
EncodingOption = Annotated[
    str | None, typer.Option("--encoding", help="Read texts in this encoding instead of the catalog's")
]
DryRunOption = Annotated[bool, typer.Option("--dry-run", help="Report what would change and write nothing")]
LimitOption = Annotated[
    int, typer.Option("--limit", min=1, help="Items per page, or the length lists are cut to; there is no maximum")
]
OffsetOption = Annotated[int, typer.Option("--offset", min=0, help="Items to skip")]
FieldsOption = Annotated[
    str | None,
    typer.Option("--fields", help="Comma-separated keys to keep in each item, or in the result when it is one object"),
]
NdjsonOption = Annotated[bool, typer.Option("--ndjson", help="One JSON line per finding, then the envelope")]
OutputOption = Annotated[Path, typer.Option("--output", help="The file to write", show_default=False)]

app = typer.Typer(add_completion=False, context_settings={"help_option_names": []})
workspace_app = typer.Typer(context_settings={"help_option_names": []})
app.add_typer(workspace_app, name="workspace", help="Working copies of a reference Data folder")


@contextmanager
def about(file: Path) -> Iterator[None]:
    try:
        yield
    except MubmdError as error:
        quoted = shlex.quote(str(file))
        if error.suggestion is None and error.code is ErrorCode.POINTER_NOT_FOUND:
            error.suggestion = f"mubmd get {quoted} '' --limit 3"
        elif error.suggestion is not None:
            error.suggestion = error.suggestion.replace("<file>", quoted)
        raise


def split_fields(fields: str | None) -> tuple[str, ...] | None:
    return None if fields is None else tuple(name.strip() for name in fields.split(",") if name.strip())


def expanded(path: Path) -> Path:
    try:
        return path.expanduser()
    except RuntimeError:
        return path


def has_terminal() -> bool:
    return sys.stdin.isatty() and sys.stdout.isatty()


def edit_outcome(command: str, result: EditResult, *, limit: int = DEFAULT_LIMIT, offset: int = 0) -> Outcome:
    shown = page(list(result.changes), limit, offset)
    changes = tuple(map(outlined, shown.items))
    data = EditData(result.path, result.changed, result.dry_run, changes, shown.total, shown.next_offset)
    return Outcome(command, data, EditData, warnings=result.warnings)


@app.command(
    "formats",
    help="List the formats this version reads, their files and whether the client reads them; matches says whether "
    "a file is known by its name (a table) or by its content (a model)",
)
def formats_command() -> Outcome:
    return Outcome("formats", FormatList(list_formats()), FormatList)


@app.command("schema", help="Describe a format's fields: JSON Pointer, type, limit and research reference")
def schema_command(
    format_name: Annotated[
        str, typer.Argument(metavar="FORMAT", help="A format name from mubmd formats, such as text or model")
    ],
) -> Outcome:
    return Outcome("schema", format_schema(format_name), SchemaResult)


def find_reference_data(start: Path) -> Path:
    for folder in (start, *start.parents):
        if (folder / REFERENCE_DATA).is_dir():
            return folder / REFERENCE_DATA
    raise MubmdError(
        ErrorCode.USAGE,
        "No Client/assets/Data found above the current folder",
        suggestion="mubmd workspace init <dest> --from <reference Data folder>",
    )


@workspace_app.command(
    "init",
    help="Copy the reference Data folder to a working copy and check it against its .sha256. A working copy that "
    "is already there is kept as it is, and `edited` counts its files that differ from the reference",
)
def workspace_init_command(
    dest: Annotated[Path, typer.Argument(help="The working copy to create", show_default=False)],
    source: Annotated[
        Path | None, typer.Option("--from", help="The reference Data folder (default: Client/assets/Data above here)")
    ] = None,
    yes: Annotated[
        bool,
        typer.Option("--yes", help="Copy the reference over a folder that is not empty; files edited there are lost"),
    ] = False,
    dry_run: DryRunOption = False,
) -> Outcome:
    origin = expanded(source) if source is not None else find_reference_data(Path.cwd())
    result = workspace_init(STORE, origin, expanded(dest), yes=yes, dry_run=dry_run)
    return Outcome("workspace init", result, WorkspaceResult)


@app.command(
    "info",
    help="Format, encoding, cipher, a table's record counts or a model's parts, and number of findings of one file",
)
def info_command(file: FileArgument, encoding: EncodingOption = None, fields: FieldsOption = None) -> Outcome:
    with about(file):
        result = info(STORE, expanded(file), encoding=encoding)
    return Outcome("info", result, InfoResult, fields=split_fields(fields), item_type=InfoResult)


@app.command("find", help="Texts that contain the query, ignoring case, or match it with --regex")
def find_command(
    file: FileArgument,
    query: Annotated[str, typer.Argument(help="Text to look for")],
    regex: Annotated[
        bool,
        typer.Option(
            "--regex", help="Take the query as a Python regular expression, case-sensitive; (?i) ignores case"
        ),
    ] = False,
    field: Annotated[
        str | None,
        typer.Option("--field", help="Search only this field of a table's records, such as name", show_default=False),
    ] = None,
    limit: LimitOption = DEFAULT_LIMIT,
    offset: OffsetOption = 0,
    fields: FieldsOption = None,
    encoding: EncodingOption = None,
) -> Outcome:
    with about(file):
        result = find(
            STORE, expanded(file), query, regex=regex, field=field, limit=limit, offset=offset, encoding=encoding
        )
    return Outcome("find", result, Page[Match], fields=split_fields(fields), item_type=Match)


@app.command("get", help="One value or subtree of a file's view; lists are cut to --limit items from --offset")
def get_command(
    file: FileArgument,
    pointer: PointerArgument,
    limit: LimitOption = DEFAULT_LIMIT,
    offset: OffsetOption = 0,
    encoding: EncodingOption = None,
) -> Outcome:
    with about(file):
        result = get(STORE, expanded(file), pointer, limit=limit, offset=offset, encoding=encoding)
    return Outcome("get", result, GetResult)


@app.command(
    "set",
    help="Change one value and save the file, verified before it is replaced; the result is what was written. "
    "A value for a string is taken as is, others as JSON. total counts the changes and changes holds the first 50",
)
def set_command(
    file: FileArgument,
    pointer: PointerArgument,
    value: Annotated[str, typer.Argument(help="The new value; put -- before a value that starts with -")],
    dry_run: DryRunOption = False,
    encoding: EncodingOption = None,
) -> Outcome:
    with about(file):
        result = set_value(STORE, expanded(file), pointer, value, dry_run=dry_run, encoding=encoding)
    return edit_outcome("set", result)


def read_patch(patch: str) -> bytes:
    if patch.lstrip().startswith(("[", "{")):
        return patch.encode("utf-8")
    if patch == "-":
        return sys.stdin.buffer.read()
    file = expanded(Path(patch))
    if not file.is_file():
        raise MubmdError(
            ErrorCode.USAGE,
            "The patch is not a JSON array ([...]), not - for stdin, and not a file that exists",
            suggestion="mubmd apply --help",
        )
    return STORE.read(file)


@app.command(
    "apply",
    help="Apply a JSON Patch, all or none: a list of {op: test or replace, path: <pointer>, value}. A test that "
    "does not hold stops everything with TEST_FAILED, so test the values you last read before replacing them. "
    "total counts the changes and changes holds the first 50",
)
def apply_command(
    file: FileArgument,
    patch: Annotated[str, typer.Argument(help="A JSON Patch file, - for stdin, or the patch itself ([...])")],
    dry_run: DryRunOption = False,
    encoding: EncodingOption = None,
) -> Outcome:
    with about(file):
        ops = parse_patch(parse_json(read_patch(patch), "the patch"))
        result = apply_operations(STORE, expanded(file), ops, dry_run=dry_run, encoding=encoding)
    return edit_outcome("apply", result)


def finding_line(path: Path, finding: Finding) -> JsonObject:
    value = to_json_value(finding, Finding)
    assert isinstance(value, dict)
    return {"path": str(path), **value}


def path_line(path: str) -> JsonObject:
    return {"path": path}


def is_finding(finding: Finding) -> bool:
    return finding.severity in (Severity.ERROR, Severity.WARNING)


@app.command("validate", help="Findings of a file, or of every .bmd in a folder (only the files that have any)")
def validate_command(
    path: Annotated[Path, typer.Argument(help="A .bmd file or a folder", show_default=False)],
    ndjson: NdjsonOption = False,
    encoding: EncodingOption = None,
) -> Outcome:
    with about(path):
        result = validate(STORE, expanded(path), encoding=encoding)
    findings = [finding for entry in result.files for finding in entry.findings]
    exit_code = EXIT_FINDINGS if any(is_finding(finding) for finding in findings) else EXIT_OK
    if not ndjson:
        return Outcome("validate", result, ValidateResult, exit_code=exit_code)
    lines = tuple(finding_line(entry.path, finding) for entry in result.files for finding in entry.findings)
    streamed = ValidateResult((), result.checked, result.skipped)
    return Outcome("validate", streamed, ValidateResult, exit_code=exit_code, lines=lines)


@app.command(
    "diff",
    help="Field by field differences between two files, or a file and a JSON view; total counts them all "
    "and changes holds --limit of them",
)
def diff_command(
    a: Annotated[Path, typer.Argument(help="A .bmd file or a .json view", show_default=False)],
    b: Annotated[Path, typer.Argument(help="A .bmd file or a .json view", show_default=False)],
    limit: LimitOption = DEFAULT_LIMIT,
    offset: OffsetOption = 0,
    encoding: EncodingOption = None,
) -> Outcome:
    result = diff(STORE, expanded(a), expanded(b), limit=limit, offset=offset, encoding=encoding)
    return Outcome("diff", result, DiffResult, exit_code=EXIT_FINDINGS if result.total else EXIT_OK)


@app.command("dump", help="Write a file's whole view as JSON to --output; returns only its path and size")
def dump_command(
    file: FileArgument, output: OutputOption, dry_run: DryRunOption = False, encoding: EncodingOption = None
) -> Outcome:
    with about(file):
        result = dump(STORE, expanded(file), expanded(output), dry_run=dry_run, encoding=encoding)
    return Outcome("dump", result, DumpResult)


@app.command(
    "build",
    help="Write a .bmd from a JSON view: a table's view to the file name the catalog knows, a model's view to any .bmd "
    "name. total counts the changes and changes holds --limit of them",
)
def build_command(
    source: Annotated[Path, typer.Argument(help="A JSON view written by mubmd dump", show_default=False)],
    output: OutputOption,
    dry_run: DryRunOption = False,
    limit: LimitOption = DEFAULT_LIMIT,
    offset: OffsetOption = 0,
) -> Outcome:
    result = build(STORE, expanded(source), expanded(output), dry_run=dry_run)
    return edit_outcome("build", result, limit=limit, offset=offset)


@app.command("roundtrip", help="Decode and encode every .bmd of a folder and report any file that changes")
def roundtrip_command(
    folder: Annotated[Path, typer.Argument(help="A Data folder", show_default=False)], ndjson: NdjsonOption = False
) -> Outcome:
    result = roundtrip(STORE, expanded(folder))
    exit_code = EXIT_FINDINGS if result.differing else EXIT_OK
    lines = tuple(path_line(path) for path in result.differing) if ndjson else ()
    return Outcome("roundtrip", result, RoundtripResult, exit_code=exit_code, lines=lines)


@app.command("tui", help="Open the console UI, for people, on a Data folder or a .bmd file")
def tui_command(
    path: Annotated[Path, typer.Argument(help="A Data folder or a .bmd file (default: the current folder)")] = Path(),
) -> Outcome:
    from mubmd.adapters.tui.app import MubmdApp  # noqa: PLC0415  Textual takes 200 ms to import; only tui pays it.

    target = expanded(path)
    if not has_terminal():
        raise MubmdError(
            ErrorCode.USAGE,
            "mubmd tui is the console UI for people and needs a terminal; the other commands need none",
            suggestion="mubmd --brief",
        )
    if not target.exists():
        raise MubmdError(ErrorCode.FILE_NOT_FOUND, f"{target} does not exist")
    ui = MubmdApp(STORE, target)
    ui.run()
    if ui.return_code:
        raise MubmdError(ErrorCode.INTERNAL_ERROR, "The console UI stopped with an error; its report is above")
    return Outcome("tui", TuiData(target), TuiData)


def load_web() -> Callable[..., str]:
    from mubmd.adapters.web.server import serve  # noqa: PLC0415  NiceGUI is an extra and slow to import; only web pays it.

    return serve


@app.command(
    "web",
    help="Serve the web viewer of models, for people, on 127.0.0.1; it writes nothing and runs until Ctrl+C",
)
def web_command(
    path: Annotated[
        Path, typer.Argument(help="A Data folder or a model .bmd file (default: the current folder)")
    ] = Path(),
    port: Annotated[
        int, typer.Option("--port", min=0, max=HIGHEST_PORT, help="The port on 127.0.0.1; 0 takes a free one")
    ] = DEFAULT_WEB_PORT,
    no_browser: Annotated[bool, typer.Option("--no-browser", help="Do not open the page in the browser")] = False,
) -> Outcome:
    target = expanded(path)
    if not target.exists():
        raise MubmdError(ErrorCode.FILE_NOT_FOUND, f"{target} does not exist")
    try:
        serve = load_web()
    except ModuleNotFoundError as error:
        if (error.name or "").startswith("mubmd"):
            raise
        raise MubmdError(
            ErrorCode.USAGE,
            f"mubmd web needs the web extra, and {error.name} is not installed",
            suggestion="pip install 'mubmd[web]'",
        ) from error
    return Outcome("web", WebData(target, serve(STORE, target, port=port, open_browser=not no_browser)), WebData)


def version_outcome() -> Outcome:
    return Outcome("version", VersionData(__version__), VersionData)
