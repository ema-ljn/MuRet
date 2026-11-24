"""Checks that the client's #include graph respects the module layers of Client/modules.tsv.

A file's module comes from its row in the module map or, for a file the map does not have yet, from the longest
matching glob in folders_today. An include may stay inside its module or go to a module that the includer's may_use
names, directly or as *layer<N. Violations listed in the baseline do not fail the check; new ones do. A root given
with --clean-root may have none: its violations fail even when the baseline lists them, and are never written to it.

    python3 -I Client/tools/architecture/check_layers.py --src Client/src --modules Client/modules.tsv \\
        --map <module-map.tsv> --baseline Client/tools/architecture/layers-baseline.tsv [--clean-root engine] [--write-baseline]
"""
import argparse
import csv
import fnmatch
import pathlib
import posixpath
import re
import sys
from dataclasses import dataclass

INCLUDE = re.compile(r'^\s*#\s*include\s*(?:"([^"]+)"|<([^>]+)>)', re.MULTILINE)
SOURCE_SUFFIXES = ('.h', '.cpp')
LAYER_BOUND = re.compile(r'\*layer<(\d+)')
BASELINE_COLUMNS = ('file', 'include', 'from_module', 'to_module')


@dataclass(frozen=True)
class Module:
    name: str
    layer: int
    folders_today: tuple[str, ...]
    may_use: tuple[str, ...]

    def may_include(self, other: 'Module') -> bool:
        if other.name == self.name or other.name in self.may_use:
            return True
        bounds = (LAYER_BOUND.fullmatch(entry) for entry in self.may_use)
        return any(other.layer < int(bound.group(1)) for bound in bounds if bound)


@dataclass(frozen=True)
class ModuleMap:
    targets: dict[str, str]
    modules: dict[str, Module]


def _read_tsv(path: pathlib.Path) -> list[dict[str, str]]:
    with open(path, encoding='utf-8', newline='') as handle:
        return list(csv.DictReader(handle, delimiter='\t', quoting=csv.QUOTE_NONE))


def _split_list(cell: str) -> tuple[str, ...]:
    return tuple(item.strip() for item in cell.split(';') if item.strip())


def modules_from_rows(rows: list[dict[str, str]]) -> dict[str, Module]:
    return {row['module']: Module(row['module'], int(row['layer']), _split_list(row['folders_today']),
                                  _split_list(row['may_use']))
            for row in rows}


def load_modules(path: pathlib.Path) -> dict[str, Module]:
    return modules_from_rows(_read_tsv(path))


def module_map_from_rows(rows: list[dict[str, str]], modules: dict[str, Module]) -> ModuleMap:
    return ModuleMap({row['file']: row['target'] for row in rows}, modules)


def load_module_map(path: pathlib.Path, modules: dict[str, Module]) -> ModuleMap:
    return module_map_from_rows(_read_tsv(path), modules)


def _resolve(quoted: str, angled: str, folder: str, src: pathlib.Path, roots: tuple[str, ...]) -> str | None:
    """Where the compiler finds the include: a quoted one first next to its includer, then src, then each root."""
    include = quoted or angled
    nearby = (posixpath.join(folder, include),) if quoted else ()
    for candidate in (*nearby, include, *(f'{root}/{include}' for root in roots)):
        normal = posixpath.normpath(candidate)
        if not normal.startswith(('../', '/')) and (src / normal).is_file():
            return normal
    return None


def include_graph(src: pathlib.Path, roots: tuple[str, ...] = ()) -> dict[str, set[str]]:
    """Each file's includes that resolve to a file under src, as paths under src, however they are spelled."""
    graph = {}
    for file in sorted(src.rglob('*')):
        if file.suffix not in SOURCE_SUFFIXES or not file.is_file():
            continue
        text = file.read_text(encoding='utf-8', errors='replace')
        path = file.relative_to(src).as_posix()
        folder = posixpath.dirname(path)
        resolved = (_resolve(quoted, angled, folder, src, roots) for quoted, angled in INCLUDE.findall(text))
        graph[path] = {include for include in resolved if include}
    return graph


def roots_of(modules: dict[str, Module]) -> tuple[str, ...]:
    return tuple(sorted({name.split('/')[0] for name in modules}))


def _module_owning(target: str, modules: dict[str, Module]) -> str | None:
    owners = [name for name in modules if target == name or target.startswith(name + '/')]
    return max(owners, key=len, default=None)


def _module_by_folder(path: str, modules: dict[str, Module]) -> str | None:
    matches = [(len(glob), name) for name, module in modules.items() for glob in module.folders_today
               if fnmatch.fnmatchcase(path, glob)]
    return max(matches, default=(0, None))[1]


def module_of(path: str, module_map: ModuleMap) -> str | None:
    target = module_map.targets.get(path)
    if target is not None:
        return _module_owning(target, module_map.modules)
    return _module_by_folder(path, module_map.modules)


def unmapped(graph: dict[str, set[str]], module_map: ModuleMap) -> list[str]:
    files = set(graph).union(*graph.values())
    return sorted(file for file in files if module_of(file, module_map) is None)


def violations(graph: dict[str, set[str]], module_map: ModuleMap) -> list[tuple[str, str, str, str]]:
    found = []
    for file in sorted(graph):
        source = module_of(file, module_map)
        for include in sorted(graph[file]):
            target = module_of(include, module_map)
            if source and target and not module_map.modules[source].may_include(module_map.modules[target]):
                found.append((file, include, source, target))
    return found


def _module_edges(graph: dict[str, set[str]], module_map: ModuleMap) -> dict[str, set[str]]:
    edges = {}
    for file, includes in graph.items():
        source = module_of(file, module_map)
        for include in includes:
            target = module_of(include, module_map)
            if source and target and target != source:
                edges.setdefault(source, set()).add(target)
                edges.setdefault(target, set())
    return edges


def _reachable(start: str, edges: dict[str, set[str]]) -> set[str]:
    seen, pending = set(), [start]
    while pending:
        for target in edges[pending.pop()]:
            if target not in seen:
                seen.add(target)
                pending.append(target)
    return seen


def cycles(graph: dict[str, set[str]], module_map: ModuleMap) -> list[list[str]]:
    edges = _module_edges(graph, module_map)
    reach = {name: _reachable(name, edges) for name in edges}
    components = {tuple(sorted({name} | {other for other in reach[name] if name in reach[other]})) for name in reach}
    return sorted(list(component) for component in components if len(component) > 1)


def load_baseline(path: pathlib.Path) -> set[tuple[str, str, str, str]]:
    if not path.exists():
        return set()
    return {tuple(row[column] for column in BASELINE_COLUMNS) for row in _read_tsv(path)}


def write_baseline(path: pathlib.Path, found: list[tuple[str, str, str, str]]) -> None:
    with open(path, 'w', encoding='utf-8', newline='') as handle:
        writer = csv.writer(handle, delimiter='\t', lineterminator='\n', quoting=csv.QUOTE_NONE)
        writer.writerow(BASELINE_COLUMNS)
        writer.writerows(found)


def clean_root_violations(found: list[tuple[str, str, str, str]],
                          roots: tuple[str, ...]) -> list[tuple[str, str, str, str]]:
    return [violation for violation in found if violation[2].split('/')[0] in roots]


def _print_violations(rows: list[tuple[str, str, str, str]]) -> None:
    for file, include, source, target in rows:
        print(f'  {file} -> {include} ({source} -> {target})')


def _parse_args(argv: list[str] | None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--src', type=pathlib.Path, required=True)
    parser.add_argument('--modules', type=pathlib.Path, required=True)
    parser.add_argument('--map', type=pathlib.Path, required=True)
    parser.add_argument('--baseline', type=pathlib.Path, required=True)
    parser.add_argument('--write-baseline', action='store_true')
    parser.add_argument('--clean-root', action='append', default=[], metavar='ROOT',
                        help='a root that may have no violation, not even in the baseline')
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = _parse_args(argv)
    modules = load_modules(args.modules)
    module_map = load_module_map(args.map, modules)
    roots = roots_of(modules)
    unknown = [root for root in args.clean_root if root not in roots]
    if unknown:
        print(f'unknown --clean-root {", ".join(map(repr, unknown))}; the roots are {", ".join(roots)}')
        return 1
    graph = include_graph(args.src, roots)

    missing = unmapped(graph, module_map)
    if missing:
        print(f'{len(missing)} files have no module (add a row to the map or a glob to folders_today):')
        for file in missing:
            print(f'  {file}')
        return 1

    found = violations(graph, module_map)
    unclean = clean_root_violations(found, tuple(args.clean_root))
    if unclean:
        print(f'{len(unclean)} violations from a clean root ({", ".join(args.clean_root)}), baseline or not:')
        _print_violations(unclean)
        return 1
    if args.write_baseline:
        write_baseline(args.baseline, found)
        print(f'wrote {len(found)} violations to {args.baseline}')
        return 0

    for component in cycles(graph, module_map):
        print(f'cycle of {len(component)} modules: {", ".join(component)}')
    known = load_baseline(args.baseline)
    new = [violation for violation in found if violation not in known]
    stale = sorted(known.difference(found))
    if stale:
        print(f'{len(stale)} baseline rows no longer happen; rerun with --write-baseline to drop them:')
        _print_violations(stale)
    print(f'{len(found)} layer violations: {len(found) - len(new)} in the baseline, {len(new)} new')
    _print_violations(new)
    return 1 if new else 0


if __name__ == '__main__':
    sys.exit(main())
