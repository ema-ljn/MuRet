"""Check that named client values have one home per space.

Recognizes scalar literal initializers of constants and explicit enumerators, including the client's
std::bit_cast float constants. It does not evaluate C++ expressions. Spaces match the name or declared
type by full regex. An optional ;values=<min>..<max> limits a match to integer literals in that range.
A home is a logical module or a header path relative to the source root. Watch rows track duplicate
definitions without failing on new ones. Declaration matching around template parameters is lexical.

    python3 -I Client/tools/architecture/check_named_values.py --src Client/src --modules Client/modules.tsv \\
        --map <module-map.tsv> --spaces <named-values.tsv> --baseline <named-values-baseline.tsv>
"""
import argparse
import bisect
import csv
import pathlib
import re
import sys
from collections import defaultdict
from dataclasses import dataclass
from decimal import Decimal, localcontext

# Isolated script execution omits the script directory from the import path.
if sys.flags.isolated:
    sys.path.append(str(pathlib.Path(__file__).resolve().parent))

import check_layers
from check_rounded_math import SOURCE_SUFFIXES, code_of

SPACE_COLUMNS = ('space', 'home', 'match', 'policy', 'note')
BASELINE_COLUMNS = ('space', 'file', 'name', 'value')
POLICIES = ('home-only', 'single-definition', 'watch')
IDENTIFIER = r'[A-Za-z_]\w*'
TYPE = rf'(?:(?:signed|unsigned|short|long|const|volatile)\s+)*(?:::)?{IDENTIFIER}(?:\s*::\s*{IDENTIFIER})*'
CONSTANT = re.compile(rf'\b(?:constexpr|const)\s+(?P<type>{TYPE})\s+(?P<name>{IDENTIFIER})\s*(?=[=({{])')
ENUM = re.compile(rf'\benum\s+(?:(?:class|struct)\s+)?(?P<type>{IDENTIFIER})?(?:\s*:\s*{TYPE})?\s*{{')
DECLARATOR = re.compile(rf'\s*(?P<name>{IDENTIFIER})\s*(?P<initializer>[=({{].*)', re.DOTALL)
TYPE_SEPARATORS = re.compile(r'\s*::\s*')
PUNCTUATION = re.compile(r'[(){}\[\],;]')
CLOSERS = {'(': ')', '[': ']', '{': '}'}
DECIMAL_DIGITS = r"[0-9](?:'?[0-9])*"
HEX_DIGITS = r"[0-9a-fA-F](?:'?[0-9a-fA-F])*"
INTEGER = re.compile(rf"(?P<sign>[+-]?)\s*(?P<digits>0[xX]{HEX_DIGITS}|0[bB][01](?:'?[01])*"
                     rf"|0(?:'?[0-7])*|[1-9](?:'?[0-9])*)(?:[uU](?:ll|LL|[lLzZ])?|(?:ll|LL|[lLzZ])[uU]?)?")
DECIMAL_LITERAL = re.compile(
    rf'(?P<sign>[+-]?)\s*(?P<digits>(?:{DECIMAL_DIGITS}\.(?:{DECIMAL_DIGITS})?|\.{DECIMAL_DIGITS})'
    rf'(?:[eE][+-]?{DECIMAL_DIGITS})?|{DECIMAL_DIGITS}[eE][+-]?{DECIMAL_DIGITS})(?P<suffix>[fFlL]?)'
)
BIT_CAST = re.compile(r'\bstd\s*::\s*bit_cast\s*<\s*(?P<type>float|double)\s*>\s*\((?P<argument>.*)\)', re.DOTALL)
CAST_DOUBLE = re.compile(r'static_cast\s*<\s*double\s*>\s*\((?P<argument>.*)\)', re.DOTALL)
BIT_ARGUMENT = re.compile(r'(?:std\s*::\s*)?uint(?:32|64)_t\s*{(?P<bits>.*)}', re.DOTALL)
TYPED_BASELINE = re.compile(r'(f32|f64|f64-from-f32):(.+)')


@dataclass(frozen=True)
class Value:
    spelling: str
    integer: int | None


@dataclass(frozen=True)
class Space:
    name: str
    home: str
    root: str | None
    field: str
    pattern: re.Pattern[str]
    policy: str
    bounds: tuple[int, int] | None

    def matches(self, definition: 'Definition') -> bool:
        text = definition.name if self.field == 'name' else definition.type
        if not self.pattern.fullmatch(text):
            return False
        integer = definition.value.integer
        return self.bounds is None or (integer is not None and self.bounds[0] <= integer <= self.bounds[1])


@dataclass(frozen=True)
class Definition:
    file: str
    line: int
    name: str
    type: str
    value: Value
    module: str | None


@dataclass(frozen=True)
class Finding:
    space: Space
    definition: Definition
    reason: str

    @property
    def row(self) -> tuple[str, str, str, str]:
        return self.space.name, self.definition.file, self.definition.name, self.definition.value.spelling

    def report(self) -> None:
        space, file, name, value = self.row
        print(f'{file}:{self.definition.line}: {space} {value} ({name}): {self.reason}')


def integer_of(text: str) -> int | None:
    match = INTEGER.fullmatch(text)
    if not match:
        return None
    digits = match['digits'].replace("'", '')
    base = 0 if digits.lower().startswith(('0x', '0b')) else 8 if digits.startswith('0') else 10
    return int(match['sign'] + digits, base)


def decimal_value(text: str, declared_type: str) -> Value | None:
    match = DECIMAL_LITERAL.fullmatch(text)
    if not match:
        return None
    digits = match['digits'].replace("'", '')
    number = Decimal(match['sign'] + digits)
    with localcontext() as context:
        context.prec = len(digits)
        spelling = str(number.normalize()).lower()
    if not any(mark in spelling for mark in '.e'):
        spelling += '.0'
    suffix = match['suffix'].lower()
    spelling += suffix
    if declared_type == 'double' and suffix == 'f':
        spelling = f'f64-from-f32:{spelling}'
    elif declared_type == 'float' and suffix != 'f':
        spelling = f'f32:{spelling}'
    return Value(spelling, None)


def bit_value(text: str, declared_type: str) -> Value | None:
    match = BIT_CAST.fullmatch(text)
    if not match:
        return None
    argument = match['argument'].strip()
    braced = BIT_ARGUMENT.fullmatch(argument)
    bits = integer_of(braced['bits'].strip() if braced else argument)
    width = 32 if match['type'] == 'float' else 64
    if bits is None or not 0 <= bits < (1 << width):
        return None
    representation = 'f64-from-f32' if width == 32 and declared_type == 'double' else f'f{width}'
    return Value(f'{representation}:{hex(bits)}', None)


def literal_value(text: str, declared_type: str = '') -> Value | None:
    text = text.removeprefix('=').strip()
    if text.startswith('{') and text.endswith('}'):
        text = text[1:-1].strip()
    while text.startswith('(') and text.endswith(')'):
        text = text[1:-1].strip()
    integer = integer_of(text)
    if integer is not None:
        return Value(hex(integer), integer)
    cast = CAST_DOUBLE.fullmatch(text)
    if cast:
        text = cast['argument'].strip()
        declared_type = 'double'
    return decimal_value(text, declared_type) or bit_value(text, declared_type)


def declarator_ranges(code: str, start: int, terminator: str) -> list[tuple[int, int]]:
    """Split a constant statement or enumeration at top-level commas."""
    closers = []
    ranges = []
    for token in PUNCTUATION.finditer(code, start):
        char = token[0]
        if not closers and char == terminator:
            return ranges + [(start, token.start())]
        if not closers and char in ')]};':
            return []
        if not closers and char == ',':
            ranges.append((start, token.start()))
            start = token.end()
        elif char in CLOSERS:
            closers.append(CLOSERS[char])
        elif closers and char == closers[-1]:
            closers.pop()
    return []


def definitions_in(text: str, file: str, module: str | None):
    code = code_of(text)
    line_breaks = [match.start() for match in re.finditer('\n', code)]
    for pattern, start_group, terminator in ((CONSTANT, 'name', ';'), (ENUM, None, '}')):
        for statement in pattern.finditer(code):
            type_name = statement['type'] or ''
            type_name = TYPE_SEPARATORS.sub('::', ' '.join(type_name.split()))
            start = statement.start(start_group) if start_group else statement.end()
            for first, last in declarator_ranges(code, start, terminator):
                entry = DECLARATOR.fullmatch(code, first, last)
                if not entry or (pattern is ENUM and not entry['initializer'].startswith('=')):
                    continue
                value = literal_value(entry['initializer'], type_name)
                if value is not None:
                    line = bisect.bisect_left(line_breaks, entry.start('name')) + 1
                    yield Definition(file, line, entry['name'], type_name, value, module)


def tsv_rows(path: pathlib.Path, columns: tuple[str, ...]):
    with path.open(encoding='utf-8', newline='') as file:
        reader = csv.DictReader(file, delimiter='\t', quoting=csv.QUOTE_NONE)
        if tuple(reader.fieldnames or ()) != columns:
            raise ValueError(f'{path} must have the columns {", ".join(columns)}')
        for row in reader:
            if None in row or any(value is None for value in row.values()):
                raise ValueError(f'{path}:{reader.line_num} has the wrong number of columns')
            yield row


def value_bounds(text: str, name: str) -> tuple[int, int] | None:
    if not text:
        return None
    endpoints = text.split('..')
    if len(endpoints) == 2:
        lo, hi = (integer_of(endpoint.strip()) for endpoint in endpoints)
        if lo is not None and hi is not None and lo <= hi:
            return lo, hi
    raise ValueError(f'{name} has an invalid values range: {text!r}')


def home_root(home: str, module_map: check_layers.ModuleMap, roots: tuple[str, ...]) -> str | None:
    first = home.split('/')[0]
    if first in roots:
        return first
    module = check_layers.module_of(home, module_map)
    if module:
        return module.split('/')[0]
    owners = {module.split('/')[0] for root in roots
              if (module := check_layers.module_of(f'{root}/{home}', module_map))}
    if not owners:
        for module in module_map.modules:
            root, _, folder = module.partition('/')
            if folder and home.startswith(folder + '/'):
                owners.add(root)
    return next(iter(owners)) if len(owners) == 1 else None


def load_spaces(path: pathlib.Path, module_map: check_layers.ModuleMap, roots: tuple[str, ...]) -> list[Space]:
    spaces = {}
    for row in tsv_rows(path, SPACE_COLUMNS):
        name, home, match, policy, _ = (row[column].strip() for column in SPACE_COLUMNS)
        if not name or name in spaces or not home or policy not in POLICIES:
            raise ValueError(f'{path}: invalid or duplicate space {name!r}, home {home!r}, policy {policy!r}')
        rule, _, bound = match.partition(';values=')
        field, _, pattern = rule.partition('=')
        if field not in ('name', 'type') or not pattern or (rule != match and not bound):
            raise ValueError(f'{path}: invalid match for {name}: {match!r}')
        spaces[name] = Space(name, home, home_root(home, module_map, roots), field, re.compile(pattern), policy,
                             value_bounds(bound, name))
    return list(spaces.values())


def defines_in_home(definition: Definition, space: Space, module_map: check_layers.ModuleMap) -> bool:
    home, file = space.home, definition.file
    if home in module_map.modules:
        return bool(definition.module and (definition.module == home or definition.module.startswith(home + '/')))
    target = module_map.targets.get(file)
    if file == home or target == home:
        return True
    root = space.root
    return bool(root and (file == f'{root}/{home}' or home == f'{root}/{file}'))


def definitions_by_value(src: pathlib.Path, spaces: list[Space], module_map: check_layers.ModuleMap):
    groups = defaultdict(list)
    for path in sorted(src.rglob('*')):
        if path.suffix not in SOURCE_SUFFIXES or not path.is_file():
            continue
        file = path.relative_to(src).as_posix()
        module = check_layers.module_of(file, module_map)
        text = path.read_text(encoding='utf-8', errors='replace')
        for definition in definitions_in(text, file, module):
            for space in spaces:
                if space.matches(definition):
                    groups[space, definition.value.spelling].append(definition)
    return groups


def policy_findings(space: Space, definitions: list[Definition], module_map: check_layers.ModuleMap):
    homes = [definition for definition in definitions if defines_in_home(definition, space, module_map)]
    if space.policy == 'home-only' or homes:
        reason = (f'define this value in {space.home}' if space.policy == 'home-only'
                  else f'this value is already defined in {space.home}')
        yield from (Finding(space, definition, reason) for definition in definitions if definition not in homes)
        definitions = homes if space.policy != 'home-only' else []
    if len({definition.file for definition in definitions}) > 1:
        reason = f'defined by literal in multiple files; give this value one home in {space.home}'
        yield from (Finding(space, definition, reason) for definition in definitions)


def clean_root_findings(groups, clean_roots: list[str]):
    for (space, _), definitions in groups.items():
        if space.root != 'game':
            continue
        for definition in definitions:
            if definition.module is None:
                continue
            root = definition.module.split('/')[0]
            if root in clean_roots and root != space.root:
                yield Finding(space, definition, f'{root} names a game value from {space.home}')


def by_row(findings):
    rows = {}
    for finding in findings:
        rows.setdefault(finding.row, finding)
    return rows


def load_baseline(path: pathlib.Path) -> set[tuple[str, str, str, str]]:
    if not path.exists():
        return set()
    baseline = set()
    for row in tsv_rows(path, BASELINE_COLUMNS):
        space, file, name, spelling = (row[column] for column in BASELINE_COLUMNS)
        typed = TYPED_BASELINE.fullmatch(spelling)
        value = literal_value(typed[2] if typed else spelling)
        if value is None:
            raise ValueError(f'{path}: invalid baseline value {spelling!r}')
        spelling = f'{typed[1]}:{value.spelling}' if typed else value.spelling
        baseline.add((space, file, name, spelling))
    return baseline


def write_baseline(path: pathlib.Path, rows) -> None:
    with path.open('w', encoding='utf-8', newline='') as file:
        writer = csv.writer(file, delimiter='\t', lineterminator='\n', quoting=csv.QUOTE_NONE)
        writer.writerow(BASELINE_COLUMNS)
        writer.writerows(sorted(rows))


def compare_baseline(path: pathlib.Path, found) -> int:
    known = load_baseline(path)
    new = sorted(found.keys() - known)
    stale = sorted(known - found.keys())
    if stale:
        print(f'{len(stale)} baseline rows no longer happen; remove them from {path.name}:')
        for row in stale:
            print('\t'.join(row))
    blocking = sum(found[row].space.policy != 'watch' for row in new)
    watched = len(new) - blocking
    watch_summary = f', {watched} new watched' if watched else ''
    print(f'{len(found)} named values: {len(found) - len(new)} in the baseline, {blocking} new{watch_summary}')
    for row in new:
        found[row].report()
    return int(bool(blocking or stale))


def parse_args(argv: list[str] | None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--src', type=pathlib.Path, required=True)
    for name in ('modules', 'map', 'spaces', 'baseline'):
        parser.add_argument(f'--{name}', type=pathlib.Path, required=True)
    parser.add_argument('--write-baseline', action='store_true')
    parser.add_argument('--clean-root', action='append', default=[], metavar='ROOT',
                        help='a root that may not define game values, even with a baseline row')
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    try:
        if not args.src.is_dir():
            raise ValueError(f'no source directory at {args.src}')
        modules = check_layers.load_modules(args.modules)
        roots = check_layers.roots_of(modules)
        unknown = [root for root in args.clean_root if root not in roots]
        if unknown:
            raise ValueError(f'unknown --clean-root {", ".join(map(repr, unknown))}; the roots are {", ".join(roots)}')
        module_map = check_layers.load_module_map(args.map, modules)
        spaces = load_spaces(args.spaces, module_map, roots)
        groups = definitions_by_value(args.src, spaces, module_map)
        if args.clean_root:
            unclean = by_row(clean_root_findings(groups, args.clean_root))
            if unclean:
                for row in sorted(unclean):
                    unclean[row].report()
                return 1

        found = by_row(finding for (space, _), definitions in groups.items()
                       for finding in policy_findings(space, definitions, module_map))
        if args.write_baseline:
            write_baseline(args.baseline, found)
            print(f'wrote {len(found)} named values to {args.baseline}')
            return 0
        return compare_baseline(args.baseline, found)
    except (OSError, ValueError, KeyError, re.error) as error:
        print(error)
        return 1


if __name__ == '__main__':
    sys.exit(main())
