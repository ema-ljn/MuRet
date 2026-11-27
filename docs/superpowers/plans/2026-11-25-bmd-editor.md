# BMD editor Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or
> superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** `mubmd`, a Python tool under `Client/tools/bmd/` that reads, checks and edits MU's `.bmd` files byte-exact,
through a CLI built for agents, a Textual console UI and a project skill. This plan details stages 1 (design and
skills) and 2 (texts end to end); stages 3 to 5 get their own plan when they start.

**Architecture:** Ports and adapters. `core/` (documents, formats, views, checks, JSON Pointer and Patch) imports only
the standard library; `app/` holds the use cases; `adapters/` holds the file system, the Typer CLI and the Textual UI.
Every format goes bytes → document → view (a JSON value); edits are JSON Patch operations on the view; the view goes
back to a document and to bytes, and a write is verified by decoding what is about to be written.

**Tech Stack:** Python 3.12+ (CI 3.13), Textual 8.2.8, Typer 0.29.2, Rich 15.0.0, pydantic 2.14.4 (output JSON Schemas
only); dev: pytest 8.4.2, pytest-asyncio 1.5.0, pytest-textual-snapshot 1.1.0, syrupy 4.8.0, jsonschema 4.26.0,
ruff 0.17.6, mypy 2.4.1. These pins were checked together with `pip check` on 2026-11-25 (pytest-textual-snapshot
caps pytest below 9).

**Spec:** [`docs/superpowers/specs/2026-11-25-bmd-editor-design.md`](../specs/2026-11-25-bmd-editor-design.md)

## Global Constraints

- Byte-exact: an unedited file decodes and encodes to identical bytes; every byte read is kept.
- `Client/assets/Data` is never written: a folder `D` with `D.sha256` beside it, after resolving symlinks, is read-only.
- `core/` imports nothing outside the standard library and nothing from `app/` or `adapters/`.
- CLI: one JSON object on stdout and nothing else; errors as one JSON object on stderr; nothing but `tui` prompts.
- Envelope: `{"ok": true, "schema": 1, "command": <name>, "data": {...}, "warnings": [...]}`; error:
  `{"ok": false, "schema": 1, "error": {"code", "message", "suggestion", "input", "pointer"}}`.
- Exit codes: 0 done; 1 done with findings or differences; 2 usage; 20 not found; 30 precondition.
- Addresses are JSON Pointers (RFC 6901); `apply` takes JSON Patch (RFC 6902) `test` and `replace` only.
- Text records: 300 bytes, `Text.bmd`/`Texttest.bmd` in `cp1252`, `TKOU.bmd` in `gb2312`; XOR3 key `FC CF AB`.
- English in code, docs, commits and PR text; comments only when strictly necessary (CODING_RULES §7, §9); Angular
  commits, scope `tools` (`feat(tools): ...`); PRs target `main`.
- Library rule (§10): JSON Pointer and the two JSON Patch operations are written by hand (about 60 lines; `jsonpointer`
  and `jsonpatch` were looked at and cost more than they save). MU's ciphers and formats are written by hand.

## Review Focus

1. A `Data` folder copied from Windows with other letter case (`local/TEXT.BMD`): the catalog matches names without
   case, as the client does. Test in Task 5.
2. A text with a character its file's encoding cannot hold (an emoji in `Text.bmd` or `TKOU.bmd`):
   `NOT_ENCODABLE` naming the pointer, and the file untouched. Test in Task 7.
3. The file changed on disk between reading and saving (the TUI open while an agent runs `set`): `STALE_FILE`, exit
   30, nothing overwritten. Tests in Tasks 6 and 9.
4. A symlink that points into the reference folder: still read-only. Test in Task 6.
5. A write that fails halfway (disk full, exception during the write): the original file intact and no temporary file
   left behind. Test in Task 6.

---

## Files

```text
Client/tools/bmd/
  pyproject.toml, README.md
  src/mubmd/__init__.py                       __version__
  src/mubmd/core/errors.py                    ErrorCode, MubmdError
  src/mubmd/core/json_types.py                JsonValue, JsonObject
  src/mubmd/core/pointer.py                   parse_pointer, format_pointer, resolve
  src/mubmd/core/patch.py                     PatchOp, parse_patch, apply_patch, Change, diff_views
  src/mubmd/core/findings.py                  Severity, Finding
  src/mubmd/core/ciphers/xor3.py              XOR3_KEY, xor3
  src/mubmd/core/documents/text_table.py      TextTable, TEXT_RECORD_COUNT, TEXT_RECORD_SIZE
  src/mubmd/core/formats/text_format.py       TextFormat (the FormatCodec for texts)
  src/mubmd/core/checks/text_checks.py        check_text_table, printf_specifiers
  src/mubmd/core/catalog.py                   CatalogFile, FormatSpec, Identified, identify, all_formats
  src/mubmd/core/ports.py                     FormatCodec, FileStore
  src/mubmd/app/documents.py                  OpenDocument, EditResult, open_document, save_view
  src/mubmd/app/queries.py                    info, find, get, list_formats, format_schema
  src/mubmd/app/edits.py                      set_value, apply_operations, build
  src/mubmd/app/folders.py                    validate, diff, dump, roundtrip
  src/mubmd/app/workspace.py                  workspace_init
  src/mubmd/adapters/storage.py               LocalFileStore
  src/mubmd/adapters/cli/main.py              main(argv) -> int, the Typer app
  src/mubmd/adapters/cli/envelope.py          success, failure, to_json
  src/mubmd/adapters/cli/exit_codes.py        EXIT_* and exit_code_for
  src/mubmd/adapters/cli/describe.py          describe
  src/mubmd/adapters/cli/human.py             Rich rendering for --human
  src/mubmd/adapters/tui/app.py               MubmdApp
  src/mubmd/adapters/tui/theme.py             LORENCIA, LORENCIA_LIGHT
  src/mubmd/adapters/tui/widgets/*.py         BmdTree, TextTableView, RecordEditor, DiffScreen, TitleBar
  tests/...                                   mirrors src/; tests/conftest.py has the fixtures below
.claude/skills/<7 vendored skills>/, .claude/skills/muret-bmd/SKILL.md
.github/workflows/bmd-tool.yml
```

Shared test fixtures in `tests/conftest.py` (Task 3 creates the file, later tasks add to it):

- `reference_data() -> Path`: `<repo>/Client/assets/Data`, found from the test file's location.
- `working_copy(tmp_path) -> Path`: a `Data` folder in `tmp_path` holding copies of `Local/Text.bmd`,
  `Local/Texttest.bmd` and `Local/TKOU.bmd` (not the 370 MB: no `Data.sha256` beside it, so it is writable).
- `text_bytes() -> bytes`: the bytes of the reference `Local/Text.bmd`.

## Stage 1: design and skills (PR 1)

### Task 1: Vendor the six Python skills of wshobson/agents

**Files:**
- Create: `.claude/skills/<name>/{SKILL.md,LICENSE,references/upstream-skill.md,references/<upstream references>}` for
  `python-design-patterns`, `python-code-style`, `python-project-structure`, `python-testing-patterns`,
  `python-type-safety`, `python-error-handling`
- Modify: `AGENTS.md` (the "skills del repo" row: add the Python skills)

- [ ] **Step 1:** `git clone --depth 1 --filter=blob:none https://github.com/wshobson/agents` into the scratchpad and
  record the commit (`git rev-parse HEAD`). Copy each `plugins/python-development/skills/<name>/SKILL.md` verbatim to
  `references/upstream-skill.md`, and its `references/*` verbatim; `LICENSE` is the repo's MIT license.
- [ ] **Step 2:** Write each `SKILL.md` as the MuRet layer, in the shape of `.claude/skills/clean-architecture/SKILL.md`:
  frontmatter (`name`, a `description` that says "Use when writing or reviewing Python in MuRet (the `mubmd` tool and
  the research scripts) ...", `license: MIT`, `metadata.upstream` with the commit URL); "What takes precedence"
  (Emanuel; byte-exact fidelity; `docs/CODING_RULES.md`; the bmd editor spec; this skill; `references/`); a table "The
  skill says | In MuRet" that maps its advice to the spec (frozen dataclasses, `src/` layout, pytest, ruff, mypy
  strict, errors as `MubmdError` with stable codes); a table "Where it does not apply" (for example web services,
  async, ORMs, background jobs, logging stacks; Google-style docstrings everywhere conflict with §9, so only public
  functions whose name and types do not say it all get one); "Origin and license" with the update procedure.
- [ ] **Step 3:** Check: `for d in .claude/skills/python-*; do head -5 $d/SKILL.md; ls $d/references; done` shows the
  six with frontmatter and upstream copies; `grep -c "^name:" .claude/skills/python-*/SKILL.md` gives 1 each.
- [ ] **Step 4: Commit** `docs(tools): vendor six python skills from wshobson/agents`

### Task 2: Vendor `ai-native-cli`

**Files:**
- Create: `.claude/skills/ai-native-cli/{SKILL.md,LICENSE,references/upstream-skill.md}`

- [ ] **Step 1:** Copy `skills/ai-native-cli/SKILL.md` of sickn33/agentic-awesome-skills (record the commit)
  verbatim to `references/upstream-skill.md`; `LICENSE` holds the repo's `LICENSE-CONTENT` (CC BY 4.0) with the
  attribution line the license asks for (title, source URL, license, "adapted").
- [ ] **Step 2:** Write `SKILL.md` as the MuRet layer: description "Use when designing, changing or reviewing a MuRet
  command-line tool that agents call (mubmd first) ..."; precedence as in Task 1; a table of which rules MuRet adopts
  and how (JSON by default with `--human`; the envelope; codes and the exit-code table 0/1/2/20/30 of the spec, which
  replaces the skill's 10/11 because there is no auth; JSON Pointer; JSON help; `--brief`; `--dry-run`; `--fields`;
  `--yes` only for `workspace init` into a non-empty folder); what it skips (`agent/` folder and `issue` system: the
  project skill and GitHub issues cover them; G2/G3 secret patterns: the tool takes no secrets; G8 shell
  metacharacters: arguments arrive as argv, never through a shell, and texts may contain `&` or `%`).
- [ ] **Step 3: Commit** `docs(tools): vendor the ai-native-cli skill`

Then open **PR 1**, `docs(tools): design the bmd editor and vendor its skills`, with the spec, this plan and Tasks 1
and 2; assign `ema-ljn` and request his review.

## Stage 2: texts end to end (PR 2)

### Task 3: The package, its tooling and its workflow

**Files:**
- Create: `Client/tools/bmd/pyproject.toml`, `Client/tools/bmd/README.md`, `src/mubmd/__init__.py`,
  `src/mubmd/{core,app,adapters}/__init__.py`, `tests/conftest.py`, `tests/test_layers.py`,
  `.github/workflows/bmd-tool.yml`
- Modify: `.gitignore` (`.mypy_cache/`, `.pytest_cache/`, `.ruff_cache/`, `*.egg-info/`)

**Interfaces:**
- Produces: the installable package `mubmd` (`pip install -e "Client/tools/bmd[dev]"`), the console script
  `mubmd = "mubmd.adapters.cli.main:run"`, and the fixtures listed in "Files".

- [ ] **Step 1:** `pyproject.toml`: build backend `hatchling`; `[project]` with the runtime pins above;
  `[project.optional-dependencies] dev` with the dev pins; `[tool.pytest.ini_options]` `asyncio_mode = "auto"`,
  `testpaths = ["tests"]`; `[tool.mypy]` `strict = true`, `python_version = "3.12"`; `[tool.ruff]` `line-length = 120`,
  `target-version = "py312"`, `lint.select = ["E", "F", "W", "I", "B", "UP", "SIM", "RUF", "N", "PL"]`.
- [ ] **Step 2: Write the failing test** `tests/test_layers.py::test_core_imports_only_the_standard_library` and
  `::test_app_does_not_import_adapters`: parse every module under `src/mubmd/core` and `src/mubmd/app` with `ast`;
  for `core`, every imported top-level name is in `sys.stdlib_module_names` or is `mubmd.core.*`; for `app`, nothing
  under `mubmd.adapters` and nothing from `typer`, `textual`, `rich`, `click`, `pydantic`.
- [ ] **Step 3:** Run `pytest tests/test_layers.py -v`: PASS with the empty packages (the test must also fail when a
  stray `import typer` is added to `core/__init__.py`; check by hand once and remove it).
- [ ] **Step 4:** `bmd-tool.yml`: on `push` to `main` and `pull_request`, paths `Client/tools/bmd/**`,
  `.claude/skills/muret-bmd/**`, `.github/workflows/bmd-tool.yml`; `ubuntu-24.04`; `actions/checkout@v7`,
  `actions/setup-python@v6` with `3.13` and pip cache; working directory `Client/tools/bmd`; steps
  `pip install -e ".[dev]"`, `ruff check .`, `ruff format --check .`, `mypy src tests`, `pytest -v`.
- [ ] **Step 5:** Run locally the four checks of the workflow: all pass.
- [ ] **Step 6: Commit** `build(tools): start the mubmd package and its workflow`

### Task 4: Errors, JSON Pointer, JSON Patch and XOR3

**Files:**
- Create: `core/errors.py`, `core/json_types.py`, `core/pointer.py`, `core/patch.py`, `core/findings.py`,
  `core/ciphers/xor3.py` and their tests under `tests/core/`

**Interfaces:**
- Produces:
  - `class ErrorCode(StrEnum)`: `USAGE`, `FILE_NOT_FOUND`, `UNKNOWN_FORMAT`, `POINTER_NOT_FOUND`, `INVALID_VALUE`,
    `TEXT_TOO_LONG`, `NOT_ENCODABLE`, `CORRUPT_FILE`, `REFERENCE_READ_ONLY`, `STALE_FILE`, `TEST_FAILED`,
    `WRITE_VERIFY_FAILED`, `NOT_EMPTY` (value = name).
  - `class MubmdError(Exception)`: `__init__(self, code: ErrorCode, message: str, *, suggestion: str | None = None,
    pointer: str | None = None)` with those as attributes (a plain class: a frozen dataclass cannot be raised).
  - `type JsonValue = None | bool | int | float | str | list[JsonValue] | dict[str, JsonValue]`;
    `type JsonObject = dict[str, JsonValue]`.
  - `parse_pointer(text: str) -> tuple[str, ...]`, `format_pointer(tokens: Sequence[str | int]) -> str`,
    `resolve(value: JsonValue, pointer: str) -> JsonValue`.
  - `@dataclass(frozen=True, slots=True) class PatchOp: op: Literal["test", "replace"]; path: str; value: JsonValue`;
    `parse_patch(raw: JsonValue) -> tuple[PatchOp, ...]`; `apply_patch(view: JsonObject, ops: Sequence[PatchOp]) ->
    JsonObject` (returns a new object, never mutates `view`).
  - `@dataclass(frozen=True, slots=True) class Change: pointer: str; old: JsonValue; new: JsonValue`;
    `diff_views(old: JsonValue, new: JsonValue) -> tuple[Change, ...]` (leaf by leaf, in document order).
  - `class Severity(StrEnum)`: `ERROR = "error"`, `WARNING = "warning"`, `INFO = "info"`;
    `@dataclass(frozen=True, slots=True) class Finding: code: str; severity: Severity; pointer: str; message: str`.
  - `XOR3_KEY: Final = bytes((0xFC, 0xCF, 0xAB))`; `xor3(data: bytes) -> bytes`.

- [ ] **Step 1: Write the failing tests:**
  - `test_pointer.py`: `parse_pointer("") == ()`; `parse_pointer("/texts/500") == ("texts", "500")`;
    `parse_pointer("/a~1b/c~0d") == ("a/b", "c~d")`; `format_pointer(["a/b", 3]) == "/a~1b/3"`;
    `parse_pointer("texts")` raises `MubmdError` with `ErrorCode.POINTER_NOT_FOUND`; `resolve` on a list rejects
    `"-"`, `"007"`, `"-1"` and an index past the end with `POINTER_NOT_FOUND` and the pointer in `.pointer`.
  - `test_patch.py`: `test` then `replace` on `/texts/1` applies; a `test` whose value differs raises `TEST_FAILED`
    and the input view is unchanged; `{"op": "add", ...}` raises `INVALID_VALUE`; `replace` of a missing pointer raises
    `POINTER_NOT_FOUND`; `diff_views({"t": ["a", "b"]}, {"t": ["a", "c"]}) == (Change("/t/1", "b", "c"),)`.
  - `test_xor3.py`: `xor3(bytes(6)) == bytes.fromhex("fccfabfccfab")`; `xor3(xor3(b)) == b` for 1,000 random bytes;
    record 500 of the reference `Text.bmd`, after `xor3`, starts with `"El usuario salió del juego".encode("cp1252")`
    followed by `b"\0"`.
- [ ] **Step 2:** Run `pytest tests/core -v`: FAIL (modules missing).
- [ ] **Step 3:** Implement the signatures above. `xor3` builds the key stream with `itertools.cycle` or a repeated
  key and `bytes(a ^ b for ...)`; it is the hot path for 32 tables, so keep it one pass.
- [ ] **Step 4:** Run `pytest tests/core -v`: PASS. Run `ruff check . && mypy src tests`: clean.
- [ ] **Step 5: Commit** `feat(tools): add errors, json pointer and patch, and xor3`

### Task 5: The text table, its format, its view, its checks and the catalog

**Files:**
- Create: `core/documents/text_table.py`, `core/formats/text_format.py`, `core/checks/text_checks.py`,
  `core/catalog.py`, `core/ports.py`, tests under `tests/core/`

**Interfaces:**
- Consumes: Task 4.
- Produces:
  - `TEXT_RECORD_COUNT: Final = 2048`, `TEXT_RECORD_SIZE: Final = 300`.
  - `@dataclass(frozen=True, slots=True) class TextTable: records: tuple[bytes, ...]; encoding: str; trailing: bytes =
    b""` (records are plain, already un-XORed, 300 bytes each); `TextTable.text(index: int) -> str | None` (up to the
    first `0`; `None` when that does not decode or bytes after the `0` are not zero); `TextTable.with_text(index: int,
    text: str) -> TextTable` (zero-filled record; `NOT_ENCODABLE`, `TEXT_TOO_LONG` above 300 bytes,
    `POINTER_NOT_FOUND` outside 0..2047).
  - `class FormatCodec[D](Protocol)`: `name: str`; `decode(self, data: bytes, file: CatalogFile) -> D`;
    `encode(self, document: D) -> bytes`; `to_view(self, document: D) -> JsonObject`; `from_view(self, view:
    JsonObject) -> D`; `check(self, document: D, original: D | None) -> tuple[Finding, ...]`;
    `describe_fields(self) -> tuple[FieldInfo, ...]`.
  - `@dataclass(frozen=True, slots=True) class FieldInfo: pointer: str; type: str; limit: str; reference: str`.
  - `TextFormat` implements `FormatCodec[TextTable]`; its view is
    `{"format_version": 1, "format": "text", "encoding": <str>, "texts": [<str | {"hex": <600 hex digits>}> × 2048]}`
    plus `"trailing": <hex>` only when the file has bytes past 614,400.
  - `@dataclass(frozen=True, slots=True) class CatalogFile: name: str; encoding: str | None; read_by_client: bool`;
    `@dataclass(frozen=True, slots=True) class FormatSpec: name: str; size: int; files: tuple[CatalogFile, ...];
    codec: FormatCodec[Any]`; `@dataclass(frozen=True, slots=True) class Identified: format: FormatSpec; file:
    CatalogFile`; `identify(file_name: str, data: bytes) -> Identified`; `all_formats() -> tuple[FormatSpec, ...]`;
    `format_by_name(name: str) -> FormatSpec`.
  - `printf_specifiers(text: str) -> tuple[str, ...]` (C `printf` conversions, `%%` excluded; `"% d puntos"` gives
    `("% d",)`); `check_text_table(table: TextTable, original: TextTable | None) -> tuple[Finding, ...]` with codes
    `TEXT_FILLS_RECORD` (warning, pointer `/texts/<i>`), `TEXT_NOT_DECODABLE` (info) and `PRINTF_CHANGED` (warning,
    only against `original`).

- [ ] **Step 1: Write the failing tests:**
  - `test_text_format.py::test_round_trip_is_byte_exact` for `Text.bmd`, `Texttest.bmd` and `TKOU.bmd` of the
    reference data: `encode(decode(data)) == data`, and `encode(from_view(to_view(decode(data)))) == data`.
  - `::test_reads_the_real_texts`: text 500 of `Text.bmd` is `"El usuario salió del juego"`, text 100 is `"Zen"`;
    text 500 of `TKOU.bmd` is `"对方中断连接了"`; 1,645 non-empty texts in `Text.bmd`.
  - `::test_record_7_fills_its_300_bytes`: `len(table.text(7).encode("cp1252")) == 300` and
    `check_text_table` reports exactly one `TEXT_FILLS_RECORD`, at `/texts/7` (Q-C07, as the C++ golden test
    `GoldenTextTable.LoadsTheRealFile`).
  - `::test_a_short_file_is_corrupt`: 614,399 bytes raise `CORRUPT_FILE`; 614,401 bytes keep one trailing byte and
    round-trip.
  - `test_text_table.py`: `with_text(500, "€")` encodes `0x80` in cp1252; `with_text(500, "😀")` raises
    `NOT_ENCODABLE` with pointer `/texts/500`; 301 bytes raise `TEXT_TOO_LONG` and the message says `"301 bytes, the
    record holds 300"`; the original table is unchanged after any edit.
  - `test_text_checks.py`: `printf_specifiers("%s chance de sucesso : %d%%") == ("%s", "%d")`;
    `printf_specifiers("% d puntos de Exp ganados") == ("% d",)`; changing `"%d"` to `"%s"` gives `PRINTF_CHANGED`.
  - `test_catalog.py`: `identify("Text.bmd", data)`, `identify("TEXT.BMD", data)` and `identify("text.bmd", data)`
    give format `text`, encoding `cp1252`, `read_by_client=True`; `identify("TKOU.bmd", ...)` gives `gb2312` and
    `False`; `identify("Item.bmd", ...)` raises `UNKNOWN_FORMAT` until stage 3 adds it.
- [ ] **Step 2:** Run `pytest tests/core -v`: the new tests FAIL.
- [ ] **Step 3:** Implement. Decode: `xor3` over the first 614,400 bytes, split in 300-byte records, keep the rest as
  `trailing`. A record goes to the view as text only when `text()` is not `None`; otherwise as `{"hex": ...}`.
  `from_view` rebuilds each record: a string is encoded and zero-filled (same limits as `with_text`), a `{"hex"}` is
  taken as is (it must be 600 hex digits: `INVALID_VALUE` otherwise). The `describe_fields` row for texts is
  `FieldInfo("/texts/<0..2047>", "string", "300 bytes in the file's encoding", "Client/docs/research/05-formatos-de-datos.md#tablas-bmd")`.
- [ ] **Step 4:** Run `pytest tests/core -v`: PASS; `ruff check . && mypy src tests`: clean.
- [ ] **Step 5: Commit** `feat(tools): read and write text tables byte-exact`

### Task 6: The file system adapter

**Files:**
- Create: `core/ports.py` (add `FileStore`), `adapters/storage.py`, `tests/adapters/test_storage.py`

**Interfaces:**
- Produces: `class FileStore(Protocol)`: `read(self, path: Path) -> bytes`; `sha256(self, path: Path) -> str`;
  `is_reference(self, path: Path) -> bool`; `write(self, path: Path, data: bytes, *, expected_sha256: str | None) ->
  None`; `list_bmd(self, folder: Path) -> tuple[Path, ...]` (sorted, case-insensitive `.bmd`). `LocalFileStore`
  implements it. `write` raises `REFERENCE_READ_ONLY` (suggestion `mubmd workspace init <dest>`), `STALE_FILE` when
  the current SHA-256 differs from `expected_sha256` (`None` means a new file), and `FILE_NOT_FOUND` for a missing
  folder; it writes `<name>.<pid>.tmp` beside the target, `fsync`s, renames the old file to `<name>.bak` only if no
  `.bak` exists yet, and `os.replace`s.

- [ ] **Step 1: Write the failing tests** in `test_storage.py`:
  - `test_reference_folder_is_read_only`: a `tmp_path/Data` with `tmp_path/Data.sha256` beside it; `write` into
    `Data/Local/Text.bmd` raises `REFERENCE_READ_ONLY`; `is_reference` is true for the folder and for any file in it.
  - `test_a_symlink_into_the_reference_is_read_only` (Review Focus 4).
  - `test_write_keeps_the_first_backup`: two writes leave `.bak` with the original bytes.
  - `test_a_stale_file_is_not_overwritten` (Review Focus 3): change the file after reading its hash; `write` raises
    `STALE_FILE` and the file keeps the other writer's bytes.
  - `test_a_failed_write_leaves_the_original` (Review Focus 5): patch `os.replace` to raise `OSError`; the original
    bytes stay and no `*.tmp` remains.
- [ ] **Step 2:** Run: FAIL. **Step 3:** Implement. **Step 4:** Run: PASS, ruff and mypy clean.
- [ ] **Step 5: Commit** `feat(tools): write files atomically and never into the reference data`

### Task 7: The use cases

**Files:**
- Create: `app/documents.py`, `app/queries.py`, `app/edits.py`, `app/folders.py`, `app/workspace.py`, tests under
  `tests/app/` with `tests/app/fake_store.py` (an in-memory `FileStore`)

**Interfaces:**
- Consumes: Tasks 4 to 6.
- Produces (all results are frozen, slotted dataclasses; `Page[T]` is `items: tuple[T, ...]; total: int;
  next_offset: int | None`):
  - `@dataclass class OpenDocument: path: Path; identified: Identified; sha256: str; document: Any; view: JsonObject;
    read_only: bool`; `open_document(store: FileStore, path: Path) -> OpenDocument`.
  - `@dataclass class EditResult: path: Path; changed: bool; dry_run: bool; changes: tuple[Change, ...]; warnings:
    tuple[Finding, ...]`; `save_view(store: FileStore, opened: OpenDocument, view: JsonObject, *, dry_run: bool) ->
    EditResult` (no change → `changed=False` and no write; else `from_view`, `encode`, write-verify by decoding the
    new bytes and comparing views (`WRITE_VERIFY_FAILED`), then `store.write(..., expected_sha256=opened.sha256)`).
  - `info(store, path) -> InfoResult(path, format, encoding, read_by_client, read_only, size, records, used_records,
    findings: int)`.
  - `find(store, path, query: str, *, regex: bool = False, limit: int = 50, offset: int = 0) -> Page[Match]`;
    `Match(index: int, pointer: str, text: str)`; case-insensitive substring unless `regex`.
  - `get(store, path, pointer: str, *, limit: int = 50) -> GetResult(pointer, value: JsonValue, truncated: bool)`
    (lists longer than `limit` are cut).
  - `set_value(store, path, pointer, raw: str, *, dry_run: bool) -> EditResult` (the target's current type decides
    the parsing: a string target takes `raw` as is; anything else parses `raw` as JSON, `INVALID_VALUE` on failure).
  - `apply_operations(store, path, ops: Sequence[PatchOp], *, dry_run: bool) -> EditResult`.
  - `validate(store, path: Path) -> ValidateResult(files: tuple[FileFindings, ...])`, `FileFindings(path,
    findings)`; a folder walks `list_bmd` and reports files of unknown format as one `UNKNOWN_FORMAT` info each.
  - `diff(store, a: Path, b: Path) -> DiffResult(changes)` (`b` may be a `.json` view).
  - `dump(store, path, output: Path) -> DumpResult(output, size)`; `build(store, source: Path, output: Path, *,
    dry_run: bool) -> EditResult` (the JSON's `format` picks the codec; `format_version` other than 1 is
    `INVALID_VALUE`).
  - `roundtrip(store, folder) -> RoundtripResult(checked, identical, differing: tuple[str, ...], skipped: int)`.
  - `workspace_init(store, source: Path, dest: Path, *, yes: bool) -> WorkspaceResult(dest, files, changed)`: copies
    the tree and checks every line of `source.parent / f"{source.name}.sha256"`; a `dest` that already checks is
    `changed=False`; a non-empty `dest` that does not check needs `yes` (`NOT_EMPTY`).
  - `list_formats() -> tuple[FormatInfo, ...]`; `format_schema(name) -> SchemaResult(format, fields)`.

- [ ] **Step 1: Write the failing tests**, one per behavior, against `FakeStore` holding the reference `Text.bmd`:
  `set_value` of `/texts/500` writes and a second identical call returns `changed=False` with one write in total;
  `dry_run` writes nothing and returns one `Change`; `set_value` on the reference path raises `REFERENCE_READ_ONLY`;
  an emoji raises `NOT_ENCODABLE` and nothing is written (Review Focus 2); `apply_operations` with a failing `test`
  writes nothing; changing `"%d"` in text 577 returns a `PRINTF_CHANGED` warning and still writes; `find("usuario")`
  pages with `total`, `next_offset`; `get("/texts", limit=3)` is truncated; `roundtrip` over `working_copy` gives
  `differing == ()`; `workspace_init` twice gives `changed=False` the second time; `dump` then `build` gives the same
  bytes.
- [ ] **Step 2:** Run `pytest tests/app -v`: FAIL. **Step 3:** Implement. **Step 4:** Run: PASS; ruff, mypy clean.
- [ ] **Step 5: Commit** `feat(tools): add the use cases of the bmd editor`

### Task 8: The CLI

**Files:**
- Create: `adapters/cli/main.py`, `envelope.py`, `exit_codes.py`, `describe.py`, `human.py`, tests under
  `tests/adapters/cli/`

**Interfaces:**
- Consumes: Task 7.
- Produces: `main(argv: Sequence[str] | None = None) -> int` (never raises, never calls `sys.exit`) and
  `run() -> None` (`sys.exit(main())`, the console script); `EXIT_OK = 0`, `EXIT_FINDINGS = 1`, `EXIT_USAGE = 2`,
  `EXIT_NOT_FOUND = 20`, `EXIT_PRECONDITION = 30`; `exit_code_for(code: ErrorCode) -> int` (`USAGE`,
  `INVALID_VALUE`, `TEXT_TOO_LONG`, `NOT_ENCODABLE` → 2; `FILE_NOT_FOUND`, `UNKNOWN_FORMAT`, `POINTER_NOT_FOUND` →
  20; the rest → 30); `describe(command: str | None) -> JsonObject`.

The commands and options are the spec's list. Global options on the callback: `--human`, `--brief`, `--version`,
`--help`. Per command where it applies: `--limit`, `--offset`, `--fields`, `--dry-run`, `--regex`, `--output`,
`--ndjson`, `--yes`, `--from` (`workspace init`; default: the first `Client/assets/Data` found walking up from the
current folder, else `USAGE` with the suggestion `--from <path>`).

- [ ] **Step 1: Write the failing tests** with `main(argv)` and `capsys`:
  - `test_output.py`: every command's stdout is exactly one line of JSON with `ok`, `schema == 1`, `command`, `data`,
    `warnings`; stderr is empty on success; each command's `data` validates against its schema from `describe` with
    `jsonschema`.
  - `test_errors.py`: one test per error code reachable from the CLI, checking stderr JSON (`code`, `message`,
    `suggestion` where the spec gives one, `input`, `pointer`), empty stdout and the exit code; a missing argument and
    an unknown flag give `USAGE` and 2, not Click's text.
  - `test_help.py`: `main([])` and `main(["--help"])` print JSON with `commands` (each with `params` carrying `name`,
    `type`, `required`, `default`, `choices`, plus `example`, `output_schema`), `exit_codes` and `error_codes` equal
    to the members of `ErrorCode`; `main(["set", "--help"])` describes only `set`; `--brief` prints the same paragraph
    as the first paragraph of `.claude/skills/muret-bmd/SKILL.md` (this test lands in Task 10 when the skill exists);
    `--human --help` prints Rich text.
  - `test_paging.py`: `find` with `--limit 2 --offset 2`, `--fields index,pointer` keeps only those keys.
- [ ] **Step 2:** Run: FAIL. **Step 3:** Implement: the Typer app runs with `standalone_mode=False` inside `main`,
  which catches `click.UsageError` and `MubmdError` and writes the error envelope; output dataclasses go to JSON with
  `pydantic.TypeAdapter(type(data)).dump_python(data, mode="json")`, and their schemas come from the same adapter's
  `json_schema()`; the help is generated by walking the Click command tree (`typer.main.get_command(app)`), with
  `help_option_names` replaced by the JSON `--help`.
- [ ] **Step 4:** Run `pytest tests/adapters/cli -v`: PASS; ruff, mypy clean.
- [ ] **Step 5: Commit** `feat(tools): add the agent-first cli`

### Task 9: The console UI

**Files:**
- Create: `adapters/tui/app.py`, `adapters/tui/theme.py`, `adapters/tui/mubmd.tcss`,
  `adapters/tui/widgets/{title_bar,bmd_tree,text_table_view,record_editor,diff_screen}.py`, tests under
  `tests/adapters/tui/` with `snapshot_app.py` and `__snapshots__/`

**Interfaces:**
- Consumes: Task 7 (the TUI calls only use cases).
- Produces: `MubmdApp(App[None])` with `__init__(self, store: FileStore, root: Path)`; `LORENCIA: Final[Theme]` and
  `LORENCIA_LIGHT: Final[Theme]` (`textual.theme.Theme`; dark: background near black, primary MU's old gold,
  error deep red, foreground parchment; exact values are the builder's); the `tui` CLI command opens it.

Bindings: `/` search, `enter` edit the selected record, `escape` cancel, `ctrl+z` undo, `ctrl+y` redo, `ctrl+s` save
(opens the diff), `d` diff, `t` toggle light/dark, `q` quit; `ctrl+p` is Textual's palette with an entry per action.

- [ ] **Step 1: Write the failing tests** with `app.run_test()` and `Pilot` on `working_copy`:
  - opening shows the tree with only `.bmd` files and selecting `Local/Text.bmd` fills the table with 2,048 rows and
    the title `text · XOR3 · 1645/2048 used`;
  - `/` + `usuario` filters the rows; selecting row 500 and `enter` opens the editor with the text and `26/300 bytes`;
  - typing an emoji marks the editor invalid and `ctrl+s` does not save;
  - editing, `ctrl+z`, `ctrl+y` restore the texts in order;
  - `ctrl+s` opens the diff with one change and confirming writes the file (bytes differ only in record 500);
  - the file changed on disk after opening: confirming shows `STALE_FILE` and writes nothing (Review Focus 3);
  - on the reference folder, the title says `read-only` and `ctrl+s` offers the working copy.
  - `test_snapshots.py`: `snap_compare` of the main screen with `Text.bmd` selected, in `lorencia` and
    `lorencia-light`, at 100 × 30.
- [ ] **Step 2:** Run: FAIL. **Step 3:** Implement the layout of the spec's sketch. **Step 4:** Run: PASS (create the
  snapshots with `--snapshot-update` only after looking at the SVG report); ruff, mypy clean.
- [ ] **Step 5: Commit** `feat(tools): add the textual console ui with the lorencia theme`

### Task 10: The `muret-bmd` skill

**Files:**
- Create: `.claude/skills/muret-bmd/SKILL.md`, `Client/tools/bmd/tests/skill/test_recipes.py`
- Modify: `AGENTS.md` ("Dónde está cada cosa": one row, "editing `.bmd` files (texts first): the `muret-bmd` skill"),
  `Client/tools/bmd/README.md`

- [ ] **Step 1 (baseline, before writing the skill):** following `superpowers:writing-skills`, give a fresh subagent
  with only the installed tool three jobs on a working copy: change text 500; translate texts 500 and 501 in one call
  guarded by their current values; find every text with `%d` and report their pointers. Record where it went wrong
  or wasted calls.
- [ ] **Step 2:** Write `SKILL.md`: frontmatter `name: muret-bmd`, a `description` starting "Use when opening,
  inspecting, translating or editing a MU `.bmd` file (Text.bmd and, as stages land, items, skills, gates, dialogs,
  quests, models) ..."; the brief paragraph; the workflow; recipes, each a fenced `bash` block preceded by
  `<!-- recipe: <name> -->` and using `$WORK` for the working copy; the error codes with the way out; the rules from
  the spec's AI friendliness section; what the baseline got wrong.
- [ ] **Step 3: Write the failing test** `test_recipes.py::test_every_recipe_runs`: parse the recipes, copy the
  `working_copy` fixture, run each line with `subprocess.run(shlex.split(line), env={..., "WORK": ...})` (no shell),
  and assert exit 0 and `"ok": true`; `::test_brief_matches_the_skill` (from Task 8).
- [ ] **Step 4:** Run: PASS. **Step 5 (check):** a fresh subagent with the skill does the three baseline jobs; each
  ends in at most three calls without reading `src/`. Fix the skill until it does.
- [ ] **Step 6: Commit** `docs(tools): add the muret-bmd skill with executable recipes`

### Task 11: Finish stage 2

- [ ] **Step 1:** Run the workflow's checks from `Client/tools/bmd`: `ruff check .`, `ruff format --check .`,
  `mypy src tests`, `pytest -v`: all pass.
- [ ] **Step 2:** `mubmd roundtrip ../../assets/Data` reports `differing: []` with `skipped` equal to the `.bmd` files
  that are not texts.
- [ ] **Step 3:** Fill `README.md`: install, the commands, the JSON format, the TUI keys, the stages still to come.
- [ ] **Step 4:** Open **PR 2**, `feat(tools): edit text tables with mubmd`, assign `ema-ljn`, request his review,
  subscribe to its activity.

## Stages 3 to 5 (their own plans)

- **Stage 3, every table:** a `RecordTableFormat` driven by a schema per table (fields that cover each record with no
  gap, `raw_0xNN` where the research has no name), the per-record and whole-file XOR3 variants, the login XOR3 of
  `credit.bmd`, the counts of `movereq` and `MonsterSkill`, and the checksum (port of `TableChecksum`) recomputed only
  when records change; the TUI's record table view; the skill's recipes for items, skills and gates. Its plan is
  [`2026-11-26-bmd-editor-tables.md`](2026-11-26-bmd-editor-tables.md).
  - [x] Item tables: schema, byte-exact roundtrip, checksum on edit, CLI name translation, and TUI name editing.
  - [x] Every other table, the console UI for any table and the skill's recipes.
- **Stage 4, models:** the lossless model codec (K16, every raw byte), its view (bytes as hex, floats as numbers or
  `{"f32"}`), validation, `info` summaries that never print geometry, and the TUI's model tree. Its plan is
  [`2026-11-27-bmd-editor-models.md`](2026-11-27-bmd-editor-models.md).
- **Stage 5, the preview:** the braille wireframe of a model, posed with the first key of its first action.
