"""Checks that a refactor move moved lines and changed nothing else.

For each file git sees as renamed between two commits (-M50%), the old and the new file must have the same lines, as a
multiset, once #include lines and blank lines are left out: a moved file may change its includes and nothing else.
A library's export macro (ENGINE_API, from cmake/AddLibrary.cmake) is left out too, so a file that moves out of a shared
library may drop it. Renamed CMakeLists.txt, *.cmake, *.md and *.tsv files may change. Exits with 1 and lists each
difference.

    python3 -I Client/tools/architecture/check_pure_move.py --base origin/refactor/client --head HEAD
"""
import argparse
import collections
import re
import subprocess
import sys

INCLUDE_LINE = re.compile(r'^\s*#\s*include\b')
EXPORT_MACRO = re.compile(r'\b[A-Z][A-Z0-9]*_API\b\s*')
MAY_CHANGE = ('CMakeLists.txt', '.cmake', '.md', '.tsv')


def git(repo: str, *args: str) -> str:
    return subprocess.run(['git', '-C', repo, *args], capture_output=True, check=True).stdout.decode(
        'utf-8', 'replace')


def renames(repo: str, base: str, head: str) -> list[tuple[str, str]]:
    fields = git(repo, 'diff', '-z', '--name-status', '-M50%', base, head).split('\0')
    found, index = [], 0
    while index < len(fields) - 1:
        status = fields[index]
        if status.startswith(('R', 'C')):
            if status.startswith('R'):
                found.append((fields[index + 1], fields[index + 2]))
            index += 3
        else:
            index += 2
    return found


def lines_of(text: str) -> collections.Counter:
    kept = (EXPORT_MACRO.sub('', line) for line in text.splitlines() if not INCLUDE_LINE.match(line))
    return collections.Counter(line for line in kept if line.strip())


def differences(repo: str, base: str, head: str) -> tuple[int, list[tuple[str, str, collections.Counter,
                                                                           collections.Counter]]]:
    moved = renames(repo, base, head)
    found = []
    for old, new in moved:
        if new.endswith(MAY_CHANGE):
            continue
        before = lines_of(git(repo, 'show', f'{base}:{old}'))
        after = lines_of(git(repo, 'show', f'{head}:{new}'))
        if before != after:
            found.append((old, new, before - after, after - before))
    return len(moved), found


def _print_lines(sign: str, lines: collections.Counter) -> None:
    for line, count in sorted(lines.items()):
        print(f'  {sign} {line.strip()}' + (f'  (x{count})' if count > 1 else ''))


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--repo', default='.')
    parser.add_argument('--base', required=True)
    parser.add_argument('--head', required=True)
    args = parser.parse_args(argv)
    moved, found = differences(args.repo, args.base, args.head)
    for old, new, gone, added in found:
        print(f'{old} -> {new}:')
        _print_lines('-', gone)
        _print_lines('+', added)
    if found:
        print(f'{len(found)} of {moved} moved files changed more than their includes')
        return 1
    print(f'{moved} moved files, none changed beyond their includes')
    return 0


if __name__ == '__main__':
    sys.exit(main())
