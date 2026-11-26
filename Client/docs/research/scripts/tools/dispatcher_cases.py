#!/usr/bin/env python3
"""dispatcher_cases.py: the cases of the dispatcher 0x4e2b10, and the check of the client's opcode lists against them.

  python3 -I Client/docs/research/scripts/tools/dispatcher_cases.py generate \
      --tables <research>/client-research/analysis/mainexe/evidence/dispatch-tables.json \
      --out Client/docs/research/coverage/dispatcher-cases.tsv
  python3 -I Client/docs/research/scripts/tools/dispatcher_cases.py check \
      --cases Client/docs/research/coverage/dispatcher-cases.tsv \
      --lists Client/src/game/Protocol/Opcodes.h Client/src/game/Protocol/Session/Opcodes.h

Three levels, each a byte map into a jump table (paso3-protocolo.md, section 2): the opcode (0x00 to 0xF4, map1 and
jt1), the byte after an F1 (pkt[3], map2 and jt2) and the result of an F1 01 (pkt[4], map3 and jt3). A code whose slot
jumps to its level's default, the target of the level's JA, has no row.

`check` reads the `Network::Opcode` constants of each list: every level 1 code is in one of them, no list names a code
twice, and a code the dispatcher lacks is marked `// client only` on its line.

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
    args = parser.parse_args(argv)
    if args.command == 'generate':
        cases = build_cases(json.loads(args.tables.read_text(encoding='utf-8')))
        write_tsv(cases, args.out)
        print(f'{len(cases)} cases')
        return 0
    problems = check_lists(head_codes_of(args.cases.read_text(encoding='utf-8')),
                           {str(path): path.read_text(encoding='utf-8') for path in args.lists})
    for problem in problems:
        print(problem)
    print(f'{len(problems)} problems')
    return 1 if problems else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
