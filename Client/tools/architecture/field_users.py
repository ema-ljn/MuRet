"""Lists each data member of a struct and the modules whose files use it, for moving a struct's fields to the module
that owns them (AR3.2 Task 7).

A use is `<name>.<field>` or `<name>-><field>` where `<name>` is `Runtime()` (any `X()` given with --accessor) or a
name some file declares with the struct's type (`WorldRuntime& runtime`, a member `WorldRuntime* m_runtime`) or binds
to the accessor (`auto& runtime = world.Runtime();`). A field of another struct with the same name, reached through
a name never declared that way, is not a use. A file that defines the struct's member functions (`WorldRuntime::F(`)
also uses each field it names bare. A file's module comes from the module map, as in check_layers.py; tests are left
out.

    python3 -I Client/tools/architecture/field_users.py --src Client/src --modules Client/modules.tsv \\
        --map <module-map.tsv> --header game/Kernel/WorldRuntime.h --struct WorldRuntime [--accessor Runtime] [--single]
"""
import argparse
import pathlib
import re
import sys

# Isolated script execution omits the script directory from the import path.
if sys.flags.isolated:
    sys.path.append(str(pathlib.Path(__file__).resolve().parent))

import check_layers
from check_rounded_math import SOURCE_SUFFIXES, code_of

MEMBER = re.compile(r'(\w+)\s*(?:\{[^;{}]*\}|=[^;]*)?;$')


def struct_fields(header: str, name: str) -> list[str]:
    """The data members declared at the top level of `struct name { … };`, in order."""
    code = code_of(header)
    head = re.search(rf'^struct {re.escape(name)}\s*\{{', code, re.MULTILINE)
    if head is None:
        raise ValueError(f'struct {name} not found')
    depth, statement, fields = 1, '', []
    for char in code[head.end():]:
        if char == '{':
            depth += 1
        elif char == '}':
            depth -= 1
            if depth == 0:
                break
        if depth == 1 and char not in '{}':
            statement += char
            if char == ';':
                found = _member(statement)
                if found:
                    fields.append(found)
                statement = ''
        elif depth == 1 and char == '}':
            statement += '{}'
    return fields


def _member(statement: str) -> str | None:
    text = ' '.join(statement.split())
    if not text or text.startswith(('using ', 'friend ', 'static ', 'static_assert', 'template', 'enum ', 'typedef ')):
        return None
    initializer = text.find(' = ')
    if (initializer < 0 or initializer > text.find('(')) and re.search(
            r'\)\s*(?:const\s*)?(?:noexcept\s*)?(?:override\s*)?(?:=\s*(?:default|delete|0)\s*)?;$', text):
        return None
    found = MEMBER.search(text)
    return found.group(1) if found else None


def names_of(code: str, struct: str, accessor: str) -> set[str]:
    """The variables a file declares with the struct's type or binds to the accessor's result."""
    declared = re.findall(rf'\b{re.escape(struct)}\s*(?:const\s*)?[&*]+\s*(?:const\s*)?(\w+)', code)
    bound = re.findall(rf'\b(\w+)\s*=\s*[\w.\->()\[\]]*\b{re.escape(accessor)}\(\)\s*;', code)
    return set(declared) | set(bound)


def uses(code: str, fields: list[str], names: set[str], accessor: str, struct: str) -> set[str]:
    """The fields the code reaches through one of the names or the accessor, or bare in the struct's own functions."""
    if not fields:
        return set()
    alternatives = '|'.join(map(re.escape, fields))
    owners = '|'.join([re.escape(accessor) + r'\(\)'] + [re.escape(n) for n in sorted(names)])
    found = set(re.findall(rf'(?<!\w)(?:{owners})\s*(?:\.|->)\s*({alternatives})\b', code))
    if re.search(rf'\b{re.escape(struct)}::\w+\s*\(', code):
        found |= set(re.findall(rf'(?<![\w.>:])({alternatives})\b(?!\s*\()', code))
    return found


def field_modules(src: pathlib.Path, module_map: check_layers.ModuleMap, fields: list[str], struct: str,
                  accessor: str) -> dict[str, dict[str, set[str]]]:
    """For each field, the modules that use it and, for each module, its files."""
    codes = {path.relative_to(src).as_posix(): code_of(path.read_text(encoding='utf-8', errors='replace'))
             for path in sorted(src.rglob('*')) if path.suffix in SOURCE_SUFFIXES}
    names = set().union(*(names_of(code, struct, accessor) for code in codes.values()))
    result: dict[str, dict[str, set[str]]] = {field: {} for field in fields}
    for relative, code in codes.items():
        module = check_layers.module_of(relative, module_map) or '?'
        for field in uses(code, fields, names, accessor, struct):
            result[field].setdefault(module, set()).add(relative)
    return result


def _parse_args(argv: list[str] | None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    parser.add_argument('--src', type=pathlib.Path, required=True)
    parser.add_argument('--modules', type=pathlib.Path, required=True)
    parser.add_argument('--map', type=pathlib.Path, required=True)
    parser.add_argument('--header', required=True, help='the header that declares the struct, relative to --src')
    parser.add_argument('--struct', required=True)
    parser.add_argument('--accessor', default='Runtime')
    parser.add_argument('--single', action='store_true', help='only the fields that a single module uses')
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = _parse_args(argv)
    modules = check_layers.load_modules(args.modules)
    module_map = check_layers.load_module_map(args.map, modules)
    header = (args.src / args.header).read_text(encoding='utf-8', errors='replace')
    fields = struct_fields(header, args.struct)
    found = field_modules(args.src, module_map, fields, args.struct, args.accessor)
    print('field\tmodules\tfiles')
    for field in fields:
        users = found[field]
        if args.single and len(users) != 1:
            continue
        files = sorted(f for module_files in users.values() for f in module_files)
        print(f'{field}\t{" ".join(sorted(users))}\t{" ".join(files)}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
