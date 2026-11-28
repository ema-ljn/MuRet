import shlex
import sys
from collections.abc import Callable, Iterator
from contextlib import contextmanager
from dataclasses import dataclass
from pathlib import Path
from typing import Annotated, Any, Final

import typer

from mutools import __version__
from mutools.adapters.cli.envelope import to_json_value
from mutools.adapters.cli.exit_codes import EXIT_FINDINGS, EXIT_OK
from mutools.adapters.storage import LocalFileStore
from mutools.app.actions import ActionEdit, ActionExport, export_action, import_action, remove_action
from mutools.app.documents import EditResult, parse_json
from mutools.app.edits import apply_operations, build, set_value
from mutools.app.folders import (
    DiffResult,
    DumpResult,
    RoundtripResult,
    ValidateResult,
    diff,
    dump,
    roundtrip,
    validate,
)
from mutools.app.paging import DEFAULT_LIMIT, Page, page
from mutools.app.queries import (
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
from mutools.app.workspace import WorkspaceResult, workspace_init
from mutools.core.errors import ErrorCode, MutoolsError
from mutools.core.findings import Finding, Severity
from mutools.core.json_types import JsonObject
from mutools.core.patch import Change, outlined, parse_patch

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
class ActionData:
    path: Path
    action: int
    actions: int
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
    edit: bool


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
    "action export": ActionExport,
    "action import": ActionData,
    "action remove": ActionData,
    "roundtrip": RoundtripResult,
    "tui": TuiData,
    "web": WebData,
}

EXAMPLES: Final = {
    "formats": "mutools formats",
    "schema": "mutools schema text",
    "workspace init": "mutools workspace init ~/mu/Data",
    "info": "mutools info ~/mu/Data/Local/Text.bmd",
    "find": "mutools find ~/mu/Data/Local/Text.bmd usuario --limit 10",
    "get": "mutools get ~/mu/Data/Local/Text.bmd /texts --offset 500 --limit 2",
    "set": "mutools set ~/mu/Data/Local/Text.bmd /texts/500 'Te fuiste del juego' --dry-run",
    "apply": 'mutools apply ~/mu/Data/Local/Text.bmd \'[{"op": "test", "path": "/texts/100", "value": "Zen"}, '
    '{"op": "replace", "path": "/texts/100", "value": "Zen!"}]\'',
    "validate": "mutools validate ~/mu/Data",
    "diff": "mutools diff Client/assets/Data/Local/Text.bmd ~/mu/Data/Local/Text.bmd",
    "dump": "mutools dump ~/mu/Data/Local/Text.bmd --output text.json",
    "build": "mutools build text.json --output ~/mu/Data/Local/Text.bmd --dry-run",
    "action export": "mutools action export ~/mu/Data/player/Player.bmd 194 --output action.json",
    "action import": "mutools action import ~/mu/Data/player/Player.bmd action.json --dry-run",
    "action remove": "mutools action remove ~/mu/Data/player/Player.bmd 205",
    "roundtrip": "mutools roundtrip ~/mu/Data",
    "tui": "mutools tui ~/mu/Data",
    "web": "mutools web ~/mu/Data",
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
ModelArgument = Annotated[Path, typer.Argument(help="A model .bmd file", show_default=False)]
ActionArgument = Annotated[int, typer.Argument(min=0, help="An action's number, from 0, as in /actions/N")]

app = typer.Typer(add_completion=False, context_settings={"help_option_names": []})
workspace_app = typer.Typer(context_settings={"help_option_names": []})
app.add_typer(workspace_app, name="workspace", help="Working copies of a reference Data folder")
action_app = typer.Typer(context_settings={"help_option_names": []})
app.add_typer(action_app, name="action", help="A model's actions (its animations) as files of their own")


@contextmanager
def about(file: Path) -> Iterator[None]:
    try:
        yield
    except MutoolsError as error:
        quoted = shlex.quote(str(file))
        if error.suggestion is None and error.code is ErrorCode.POINTER_NOT_FOUND:
            error.suggestion = f"mutools get {quoted} '' --limit 3"
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
        str, typer.Argument(metavar="FORMAT", help="A format name from mutools formats, such as text or model")
    ],
) -> Outcome:
    return Outcome("schema", format_schema(format_name), SchemaResult)


def find_reference_data(start: Path) -> Path:
    for folder in (start, *start.parents):
        if (folder / REFERENCE_DATA).is_dir():
            return folder / REFERENCE_DATA
    raise MutoolsError(
        ErrorCode.USAGE,
        "No Client/assets/Data found above the current folder",
        suggestion="mutools workspace init <dest> --from <reference Data folder>",
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
        raise MutoolsError(
            ErrorCode.USAGE,
            "The patch is not a JSON array ([...]), not - for stdin, and not a file that exists",
            suggestion="mutools apply --help",
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
    source: Annotated[Path, typer.Argument(help="A JSON view written by mutools dump", show_default=False)],
    output: OutputOption,
    dry_run: DryRunOption = False,
    limit: LimitOption = DEFAULT_LIMIT,
    offset: OffsetOption = 0,
) -> Outcome:
    result = build(STORE, expanded(source), expanded(output), dry_run=dry_run)
    return edit_outcome("build", result, limit=limit, offset=offset)


@action_app.command(
    "export",
    help="Write one action of a model to --output as an action file: its keys, its lock and every bone's position "
    "and rotation in each key, with the bones' names and parents; returns only its path and size",
)
def action_export_command(
    file: ModelArgument, action: ActionArgument, output: OutputOption, dry_run: DryRunOption = False
) -> Outcome:
    with about(file):
        result = export_action(STORE, expanded(file), action, expanded(output), dry_run=dry_run)
    return Outcome("action export", result, ActionExport)


def action_outcome(command: str, edit: ActionEdit, limit: int, offset: int) -> Outcome:
    result = edit.result
    shown = page(list(result.changes), limit, offset)
    changes = tuple(map(outlined, shown.items))
    data = ActionData(
        result.path, edit.action, edit.actions, result.changed, result.dry_run, changes, shown.total, shown.next_offset
    )
    return Outcome(command, data, ActionData, warnings=result.warnings)


@action_app.command(
    "import",
    help="Put the action of an action file into a model with the same skeleton (the same bones, names and parents): "
    "after its last action, or over action --at. action is its number and actions how many the model has now; "
    "total counts the changes and changes holds --limit of them",
)
def action_import_command(
    file: ModelArgument,
    source: Annotated[
        Path,
        typer.Argument(
            metavar="ACTION_FILE", help="An action file written by mutools action export", show_default=False
        ),
    ],
    at: Annotated[
        int | None,
        typer.Option("--at", min=0, help="Put it over this action instead of after the last one", show_default=False),
    ] = None,
    dry_run: DryRunOption = False,
    limit: LimitOption = DEFAULT_LIMIT,
    offset: OffsetOption = 0,
) -> Outcome:
    with about(file):
        edit = import_action(STORE, expanded(file), expanded(source), at=at, dry_run=dry_run)
    return action_outcome("action import", edit, limit, offset)


@action_app.command(
    "remove",
    help="Take an action out of a model and out of every bone. Any action but the last needs --yes, because every "
    "later one moves down one number. total counts the changes and changes holds --limit of them",
)
def action_remove_command(
    file: ModelArgument,
    action: ActionArgument,
    yes: Annotated[
        bool, typer.Option("--yes", help="Remove an action that is not the last; every later one moves down one number")
    ] = False,
    dry_run: DryRunOption = False,
    limit: LimitOption = DEFAULT_LIMIT,
    offset: OffsetOption = 0,
) -> Outcome:
    with about(file):
        edit = remove_action(STORE, expanded(file), action, yes=yes, dry_run=dry_run)
    return action_outcome("action remove", edit, limit, offset)


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
    from mutools.adapters.tui.app import MutoolsApp  # noqa: PLC0415  Textual takes 200 ms to import; only tui pays it.

    target = expanded(path)
    if not has_terminal():
        raise MutoolsError(
            ErrorCode.USAGE,
            "mutools tui is the console UI for people and needs a terminal; the other commands need none",
            suggestion="mutools --brief",
        )
    if not target.exists():
        raise MutoolsError(ErrorCode.FILE_NOT_FOUND, f"{target} does not exist")
    ui = MutoolsApp(STORE, target)
    ui.run()
    if ui.return_code:
        raise MutoolsError(ErrorCode.INTERNAL_ERROR, "The console UI stopped with an error; its report is above")
    return Outcome("tui", TuiData(target), TuiData)


def load_web() -> Callable[..., str]:
    from mutools.adapters.web.server import serve  # noqa: PLC0415  NiceGUI is an extra and slow to import; only web pays it.

    return serve


@app.command(
    "web",
    help=(
        "Serve the web viewer of models, for people, on 127.0.0.1, until Ctrl+C; it writes nothing unless --edit lets "
        "the page change a working copy's actions and save them"
    ),
)
def web_command(
    path: Annotated[
        Path, typer.Argument(help="A Data folder or a model .bmd file (default: the current folder)")
    ] = Path(),
    port: Annotated[
        int, typer.Option("--port", min=0, max=HIGHEST_PORT, help="The port on 127.0.0.1; 0 takes a free one")
    ] = DEFAULT_WEB_PORT,
    no_browser: Annotated[bool, typer.Option("--no-browser", help="Do not open the page in the browser")] = False,
    edit: Annotated[
        bool, typer.Option("--edit", help="Let the page edit the models' actions and save them, in a working copy")
    ] = False,
) -> Outcome:
    target = expanded(path)
    if not target.exists():
        raise MutoolsError(ErrorCode.FILE_NOT_FOUND, f"{target} does not exist")
    if edit and STORE.is_reference(target):
        raise MutoolsError(
            ErrorCode.REFERENCE_READ_ONLY,
            f"{target} is inside the reference folder, which is never written; edit a working copy",
            suggestion="mutools workspace init ~/mu/Data",
        )
    try:
        serve = load_web()
    except ModuleNotFoundError as error:
        if (error.name or "").startswith("mutools"):
            raise
        raise MutoolsError(
            ErrorCode.USAGE,
            f"mutools web needs the web extra, and {error.name} is not installed",
            suggestion="pip install 'mutools[web]'",
        ) from error
    url = serve(STORE, target, port=port, open_browser=not no_browser, edit=edit)
    return Outcome("web", WebData(target, url, edit), WebData)


def version_outcome() -> Outcome:
    return Outcome("version", VersionData(__version__), VersionData)
