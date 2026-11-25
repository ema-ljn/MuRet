"""Moves the files of some client modules to their folder under Client/src, as module-map.tsv says.

    python3 -I Client/tools/architecture/move.py --modules game/Guild[,game/Items] [--root engine]
    python3 -I Client/tools/architecture/move.py --record <squash hash>

A move runs git mv on every file whose target is one of the modules (or is under the root) to
Client/src/<target>/<name>. It rewrites the #include lines naming a moved file whose path inside its root changed
(Scenes/X.h -> game/Guild/X.h is included as "Guild/X.h"; Core/X.h -> engine/Core/X.h keeps "Core/X.h") in the
client's sources, its tests and the labs, the moved paths in CMakeLists.txt, *.cmake and the workflows, the file
column of the map, of the layer baseline and of the named-values baseline, and a home of named-values.tsv that names
a moved header, and adds each move to moves.tsv without its commit. It lists the lines of those build files that
still name a folder the move emptied, to fix by hand. After the squash, --record adds it to .git-blame-ignore-revs
and to the rows of moves.tsv.
"""
import argparse
import pathlib
import re
import subprocess
import sys

SRC = 'Client/src'
MAP = 'Client/docs/research/coverage/module-map.tsv'
BASELINE = 'Client/tools/architecture/layers-baseline.tsv'
NAMED_VALUES = 'Client/tools/architecture/named-values.tsv'
NAMED_VALUES_BASELINE = 'Client/tools/architecture/named-values-baseline.tsv'
MOVES = 'Client/docs/research/coverage/referencia/entradas/moves.tsv'
IGNORE_REVS = '.git-blame-ignore-revs'
SOURCES = ('.h', '.cpp', '.hpp', '.inc')
INCLUDERS = (f'{SRC}/*', 'Client/tests/*', 'labs/*')
BUILD_FILES = ('CMakeLists.txt', '*/CMakeLists.txt', '*.cmake', '.github/workflows/*.yml', '.github/workflows/*.yaml')
INCLUDE = re.compile(r'^(\s*#\s*include\s+")([^"]+)(")', re.MULTILINE)


class Repo:
    def __init__(self, path: str):
        self.path = pathlib.Path(path)

    def git(self, *args: str) -> str:
        return subprocess.run(['git', '-C', str(self.path), *args], capture_output=True, text=True,
                              check=True).stdout

    def tracked(self, *patterns: str) -> list[str]:
        return [name for name in self.git('ls-files', '--', *patterns).splitlines() if name]

    def read(self, relative: str) -> str:
        with open(self.path / relative, encoding='utf-8', newline='') as handle:
            return handle.read()

    def write(self, relative: str, text: str) -> None:
        with open(self.path / relative, 'w', encoding='utf-8', newline='') as handle:
            handle.write(text)


def read_tsv(repo: Repo, relative: str) -> tuple[list[str], list[dict[str, str]]]:
    lines = repo.read(relative).splitlines()
    columns = lines[0].split('\t')
    return columns, [dict(zip(columns, line.split('\t'))) for line in lines[1:] if line]


def write_tsv(repo: Repo, relative: str, columns: list[str], rows: list[dict[str, str]]) -> None:
    lines = ['\t'.join(columns)] + ['\t'.join(row.get(column, '') for column in columns) for row in rows]
    repo.write(relative, '\n'.join(lines) + '\n')


def include_text(key: str, roots: set[str]) -> str:
    """How a file under Client/src is included: relative to its root's folder once it is in one."""
    first, _, rest = key.partition('/')
    return rest if first in roots and rest else key


def selected(target: str, modules: list[str], root: str | None) -> bool:
    return any(target == module or target.startswith(module + '/') for module in modules) or (
        root is not None and target.split('/')[0] == root)


def plan(rows: list[dict[str, str]], modules: list[str], root: str | None) -> tuple[list[tuple[str, str]], list[str]]:
    """The (old, new) paths under Client/src of the files to move, and the requested modules no row names."""
    unknown = [m for m in modules if not any(selected(r['target'], [m], None) for r in rows)]
    if root is not None and not any(selected(r['target'], [], root) for r in rows):
        unknown.append(root)
    moves = [(r['file'], f"{r['target']}/{r['file'].rsplit('/', 1)[-1]}") for r in rows
             if selected(r['target'], modules, root)]
    return [(old, new) for old, new in moves if old != new], unknown


def rewrite_includes(repo: Repo, renamed: dict[str, str]) -> int:
    count = 0

    def replace(match: re.Match) -> str:
        nonlocal count
        if match.group(2) not in renamed:
            return match.group(0)
        count += 1
        return match.group(1) + renamed[match.group(2)] + match.group(3)

    for name in repo.tracked(*INCLUDERS):
        if name.endswith(SOURCES):
            text = repo.read(name)
            new = INCLUDE.sub(replace, text)
            if new != text:
                repo.write(name, new)
    return count


def rewrite_build_paths(repo: Repo, moves: list[tuple[str, str]], emptied: list[str]) -> None:
    patterns = [(re.compile(rf'(?<![\w.-])src/{re.escape(old)}(?![\w./-])'), f'src/{new}') for old, new in moves]
    folders = [(folder, re.compile(rf'(?<![\w.-])src/{re.escape(folder)}/')) for folder in emptied]
    for name in repo.tracked(*BUILD_FILES):
        lines = repo.read(name).split('\n')
        changed = False
        for number, line in enumerate(lines, 1):
            new = line
            for pattern, replacement in patterns:
                new = pattern.sub(replacement, new)
            if new != line:
                print(f'{name}:{number}: {line.strip()}  ->  {new.strip()}')
                lines[number - 1], changed = new, True
            for folder, pattern in folders:
                if pattern.search(new):
                    print(f'{name}:{number}: names src/{folder}/, which the move emptied; check it by hand: '
                          f'{new.strip()}')
        if changed:
            repo.write(name, '\n'.join(lines))


def emptied_folders(repo: Repo, moves: list[tuple[str, str]]) -> list[str]:
    left = {name.removeprefix(f'{SRC}/') for name in repo.tracked(f'{SRC}/*')}
    folders = {str(parent) for old, _ in moves for parent in pathlib.PurePosixPath(old).parents if str(parent) != '.'}
    return sorted(f for f in folders if not any(name.startswith(f + '/') for name in left))


def run_move(repo: Repo, modules: list[str], root: str | None) -> int:
    columns, rows = read_tsv(repo, MAP)
    moves, unknown = plan(rows, modules, root)
    if unknown:
        print(f'no row of {MAP} has the target {", ".join(unknown)}')
        return 1
    targets = [new for _, new in moves]
    clashes = sorted({new for new in targets if targets.count(new) > 1 or (repo.path / SRC / new).exists()})
    if clashes:
        print(f'these paths would be taken twice or already exist: {", ".join(clashes)}')
        return 1
    new_roots = sorted({new.split('/')[0] for _, new in moves
                        if '/' in new and not (repo.path / SRC / new.split('/')[0]).exists()})
    for old, new in moves:
        (repo.path / SRC / new).parent.mkdir(parents=True, exist_ok=True)
        repo.git('mv', f'{SRC}/{old}', f'{SRC}/{new}')

    roots = {row['target'].split('/')[0] for row in rows}
    renamed = {include_text(old, roots): include_text(new, roots) for old, new in moves
               if include_text(old, roots) != include_text(new, roots)}
    includes = rewrite_includes(repo, renamed)
    emptied = emptied_folders(repo, moves)
    rewrite_build_paths(repo, moves, emptied)

    new_of = dict(moves)
    for row in rows:
        if row['file'] in new_of:
            row['file'] = new_of[row['file']]
            row['current_folder'] = row['file'].rsplit('/', 1)[0] if '/' in row['file'] else ''
    write_tsv(repo, MAP, columns, rows)
    baseline_columns, baseline = read_tsv(repo, BASELINE)
    for row in baseline:
        row['file'] = new_of.get(row['file'], row['file'])
        row['include'] = new_of.get(row['include'], row['include'])
    write_tsv(repo, BASELINE, baseline_columns, baseline)
    space_columns, spaces = read_tsv(repo, NAMED_VALUES)
    for row in spaces:
        row['home'] = new_of.get(row['home'], row['home'])
    write_tsv(repo, NAMED_VALUES, space_columns, spaces)
    named_columns, named = read_tsv(repo, NAMED_VALUES_BASELINE)
    for row in named:
        row['file'] = new_of.get(row['file'], row['file'])
    named.sort(key=lambda row: tuple(row[column] for column in named_columns))
    write_tsv(repo, NAMED_VALUES_BASELINE, named_columns, named)
    move_columns, recorded = read_tsv(repo, MOVES)
    recorded += [{'commit': '', 'from': f'{SRC}/{old}', 'to': f'{SRC}/{new}'} for old, new in moves]
    write_tsv(repo, MOVES, move_columns, recorded)

    print(f'moved {len(moves)} files; rewrote {includes} include lines; emptied {len(emptied)} folders')
    for root in new_roots:
        print(f'{SRC}/{root} is new: add it as an include directory in Client/CMakeLists.txt, or its moved includes '
              f'("Guild/X.h") do not resolve')
    print('next: build, check_pure_move.py --base <base> --head HEAD and check_layers.py; '
          'after the squash, move.py --record <hash>')
    return 0


def run_record(repo: Repo, commit: str) -> int:
    full = repo.git('rev-parse', '--verify', f'{commit}^{{commit}}').strip()
    subject = repo.git('log', '-1', '--format=%s', full).strip()
    ignored = repo.read(IGNORE_REVS)
    if full not in ignored.split('\n'):
        repo.write(IGNORE_REVS, ignored.rstrip('\n') + f'\n\n# {subject}\n{full}\n')
    columns, rows = read_tsv(repo, MOVES)
    filled = sum(1 for row in rows if not row['commit'])
    for row in rows:
        row['commit'] = row['commit'] or full
    write_tsv(repo, MOVES, columns, rows)
    print(f'recorded {full[:12]} ({subject}) in {IGNORE_REVS} and in {filled} rows of moves.tsv')
    return 0


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--repo', default='.')
    parser.add_argument('--modules', default='', help='comma-separated modules, such as game/Guild,game/Items')
    parser.add_argument('--root', help='move every module of this root (engine, game or app)')
    parser.add_argument('--record', metavar='HASH', help="the move's squash, once it is merged")
    args = parser.parse_args(argv)
    repo = Repo(args.repo)
    if args.record:
        return run_record(repo, args.record)
    modules = [m.strip() for m in args.modules.split(',') if m.strip()]
    if not modules and not args.root:
        parser.error('give --modules, --root or --record')
    return run_move(repo, modules, args.root)


if __name__ == '__main__':
    sys.exit(main())
