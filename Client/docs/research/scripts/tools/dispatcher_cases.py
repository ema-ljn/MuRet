#!/usr/bin/env python3
"""dispatcher_cases.py: the cases of the dispatcher 0x4e2b10, and the check of the client's opcode lists against them.

  python3 -I Client/docs/research/scripts/tools/dispatcher_cases.py generate \
      --tables <research>/client-research/analysis/mainexe/evidence/dispatch-tables.json \
      --out Client/docs/research/coverage/dispatcher-cases.tsv
  python3 -I Client/docs/research/scripts/tools/dispatcher_cases.py check \
      --cases Client/docs/research/coverage/dispatcher-cases.tsv \
      --lists Client/src/game/Protocol/Opcodes.h Client/src/game/Protocol/Session/Opcodes.h \
      --tables Client/src/game/Session/GameServerOpcodes.cpp Client/src/game/Session/ConnectServerOpcodes.cpp

Three levels, each a byte map into a jump table (paso3-protocolo.md, section 2): the opcode (0x00 to 0xF4, map1 and
jt1), the byte after an F1 (pkt[3], map2 and jt2) and the result of an F1 01 (pkt[4], map3 and jt3). A code whose slot
jumps to its level's default, the target of the level's JA, has no row.

`check` reads the `Network::Opcode` constants of each list: every level 1 code is in one of them, no list names a code
twice, and a code the dispatcher lacks is marked `// client only` on its line. With `--tables`, it reads the `Row(...)`
of the session's opcode tables too: every level 1 case and every F1 case has a row that names its handler's address,
and every code the client only writes has a `SentOnly` row.

Standard library only. Run with python3 -I.
"""
import argparse
import csv
import json
import pathlib
import re
import sys

COLUMNS = ('level', 'code', 'parent', 'handler')

# The JA after each level's bound (0x4e2b24, 0x4e2b44, 0x4e2b7a).
LEVELS = (
    (1, 'map1', 'jt1', '', 0x4E5B4A),
    (2, 'map2', 'jt2', '0xf1', 0x4E5B4A),
    (3, 'map3', 'jt3', '0xf1 0x01', 0x4E2D66),
)

OPCODE = re.compile(r'^\s*inline constexpr Network::Opcode (?P<name>\w+)\{0x(?P<code>[0-9a-fA-F]{2})\};(?P<rest>.*)$')
SUBCODE = re.compile(r'^\s*inline constexpr Network::Subcode (?P<name>\w+)\{0x(?P<code>[0-9a-fA-F]{2})\};')
NAMESPACE = re.compile(r'^namespace (?P<name>\w+)$')
ROW = re.compile(r'\bRow\(\s*(?P<opcode>[\w:]+),\s*(?P<subcode>std::nullopt|Network::Subcode\{0x[0-9a-fA-F]{2}\}|[\w:]+),'
                 r'\s*(?P<address>0x[0-9a-fA-F]+|0),\s*"(?P<name>[^"]*)",\s*(?P<action>[^,()<]+?)\s*[,)<]')
RAW_SUBCODE = re.compile(r'Network::Subcode\{0x(?P<code>[0-9a-fA-F]{2})\}')
F1 = 0xF1
CLIENT_ONLY = '// client only'


def build_cases(tables: dict) -> list[dict]:
    cases = []
    for level, map_key, jump_key, parent, default in LEVELS:
        byte_map, jumps = tables[map_key], tables[jump_key]
        for code, slot in enumerate(byte_map):
            handler = jumps[slot]
            if handler != default:
                cases.append({'level': str(level), 'code': f'0x{code:02x}', 'parent': parent,
                              'handler': f'0x{handler:x}'})
    return cases


def write_tsv(cases: list[dict], out: pathlib.Path) -> None:
    lines = ['\t'.join(COLUMNS)] + ['\t'.join(case[column] for column in COLUMNS) for case in cases]
    out.write_text('\n'.join(lines) + '\n', encoding='utf-8')


def read_opcodes(text: str) -> list[tuple[str, int, bool]]:
    opcodes = []
    for line in text.splitlines():
        match = OPCODE.match(line)
        if match:
            opcodes.append((match['name'], int(match['code'], 16), CLIENT_ONLY in match['rest']))
    return opcodes


def check_lists(head_codes: set[int], lists: dict[str, str]) -> list[str]:
    problems, named = [], set()
    for list_name, text in lists.items():
        seen = {}
        for name, code, client_only in read_opcodes(text):
            if code in seen:
                problems.append(f'{list_name}: {name} and {seen[code]} both name 0x{code:02x}')
            seen[code] = name
            if code in head_codes and client_only:
                problems.append(f'{list_name}: {name} (0x{code:02x}) is dispatched, not client only')
            if code not in head_codes and not client_only:
                problems.append(f'{list_name}: {name} (0x{code:02x}) has no case in 0x4e2b10; mark it {CLIENT_ONLY}')
        named |= set(seen)
    problems += [f'0x{code:02x} has a case in 0x4e2b10 and no name' for code in sorted(head_codes - named)]
    return problems


def read_names(lists: dict[str, str]) -> tuple[dict[str, tuple[int, bool]], dict[tuple[str | None, str], int]]:
    """The head codes by name, with whether the client only writes them, and the subcodes by namespace and name."""
    heads, subcodes = {}, {}
    for text in lists.values():
        namespace = None
        for line in text.splitlines():
            if match := NAMESPACE.match(line):
                namespace = match['name']
            elif line.startswith('} // namespace') or line == '}':
                namespace = None
            elif match := OPCODE.match(line):
                heads[match['name']] = (int(match['code'], 16), CLIENT_ONLY in match['rest'])
            elif match := SUBCODE.match(line):
                subcodes[(namespace, match['name'])] = int(match['code'], 16)
    return heads, subcodes


def read_rows(text: str, heads: dict[str, tuple[int, bool]], subcodes: dict[tuple[str | None, str], int]) -> list[dict]:
    rows = []
    for match in ROW.finditer(text):
        action = match['action']
        row = {'opcode': None, 'subcode': None, 'address': int(match['address'], 16), 'name': match['name'],
               'action': action.removesuffix('{}') if action in ('Unported{}', 'SentOnly{}') else 'handler'}
        opcode = match['opcode'].rsplit('::', 1)[-1]
        subcode = match['subcode']
        if opcode not in heads:
            row['unknown'] = opcode
        else:
            row['opcode'] = heads[opcode][0]
        if raw := RAW_SUBCODE.fullmatch(subcode):
            row['subcode'] = int(raw['code'], 16)
        elif subcode != 'std::nullopt':
            parts = subcode.split('::')
            key = (parts[-2], parts[-1]) if len(parts) > 1 and (parts[-2], parts[-1]) in subcodes else (None, parts[-1])
            if key not in subcodes:
                row['unknown'] = subcode
            else:
                row['subcode'] = subcodes[key]
        rows.append(row)
    return rows


def check_tables(cases_tsv: str, lists: dict[str, str], tables: dict[str, str]) -> list[str]:
    cases = list(csv.DictReader(cases_tsv.splitlines(), delimiter='\t'))
    level_one = {int(case['code'], 16): int(case['handler'], 16) for case in cases if case['level'] == '1'}
    of_f1 = {int(case['code'], 16): int(case['handler'], 16) for case in cases if case['level'] == '2'}
    heads, subcodes = read_names(lists)
    problems, rows = [], []
    for table_name, text in tables.items():
        for row in read_rows(text, heads, subcodes):
            if 'unknown' in row:
                problems.append(f'{table_name}: {row["unknown"]} is not in the lists')
                continue
            rows.append(row)
            if row['action'] == 'SentOnly':
                continue
            case = of_f1.get(row['subcode']) if row['opcode'] == F1 and row['subcode'] is not None else None
            case = case if case is not None else level_one.get(row['opcode'])
            if case is None:
                problems.append(f'{table_name}: {row["name"]} has no case in 0x4e2b10 and is not SentOnly')
            elif row['address'] != case:
                problems.append(f'{table_name}: {row["name"]} names 0x{row["address"]:x}, its case is 0x{case:x}')
    handled = [row for row in rows if row['action'] != 'SentOnly']
    for code in sorted(level_one):
        if not any(row['opcode'] == code for row in handled):
            problems.append(f'0x{code:02x} has no row')
    for code in sorted(of_f1):
        if not any(row['opcode'] == F1 and row['subcode'] == code for row in handled):
            problems.append(f'0x{F1:02x} 0x{code:02x} has no row')
    for name, (code, client_only) in heads.items():
        if client_only and not any(row['opcode'] == code and row['action'] == 'SentOnly' for row in rows):
            problems.append(f'{name} (0x{code:02x}) has no SentOnly row')
    return problems


def head_codes_of(cases_tsv: str) -> set[int]:
    rows = csv.DictReader(cases_tsv.splitlines(), delimiter='\t')
    return {int(row['code'], 16) for row in rows if row['level'] == '1'}


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    commands = parser.add_subparsers(dest='command', required=True)
    generate = commands.add_parser('generate')
    generate.add_argument('--tables', type=pathlib.Path, required=True)
    generate.add_argument('--out', type=pathlib.Path, required=True)
    check = commands.add_parser('check')
    check.add_argument('--cases', type=pathlib.Path, required=True)
    check.add_argument('--lists', type=pathlib.Path, nargs='+', required=True)
    check.add_argument('--tables', type=pathlib.Path, nargs='*', default=[])
    args = parser.parse_args(argv)
    if args.command == 'generate':
        cases = build_cases(json.loads(args.tables.read_text(encoding='utf-8')))
        write_tsv(cases, args.out)
        print(f'{len(cases)} cases')
        return 0
    cases = args.cases.read_text(encoding='utf-8')
    lists = {str(path): path.read_text(encoding='utf-8') for path in args.lists}
    problems = check_lists(head_codes_of(cases), lists)
    if args.tables:
        problems += check_tables(cases, lists, {str(path): path.read_text(encoding='utf-8') for path in args.tables})
    for problem in problems:
        print(problem)
    print(f'{len(problems)} problems')
    return 1 if problems else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
