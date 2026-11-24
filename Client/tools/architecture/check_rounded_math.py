"""Checks that the client takes no math from the C library or from glm's trigonometry.

IEEE 754 fixes +, -, *, / and the square root, so std::sqrt, std::fabs, std::floor and their kind give the same bits on
every system. Each C library rounds the sine, the cosine, the logarithm and the other functions listed below in its own
way, and glm calls them inside rotate, perspective, slerp, angleAxis and quat(vec3). The client takes them from
Render/Models/RoundedMath.h and RoundedTransforms.h (docs/adr/0027-correctly-rounded-math.md). The check goes by name:
it reads std::, glm:: and unqualified calls outside comments and literals. It cannot see a glm::quat built from a
variable that holds Euler angles, and it skips an unqualified log(, the name the client gives its log. A row of the
exceptions file allows one call in one file.

    python3 -I Client/tools/architecture/check_rounded_math.py --src Client/src Client/tests \\
        --exceptions Client/tools/architecture/rounded-math-exceptions.tsv
"""
import argparse
import bisect
import csv
import pathlib
import re
import sys

SOURCE_SUFFIXES = ('.h', '.cpp', '.hpp', '.inc')
EXCEPTION_COLUMNS = ('file', 'call', 'why')
C_LIBRARY = ('sin|cos|tan|asin|acos|atan|atan2|sinh|cosh|tanh|asinh|acosh|atanh|exp|exp2|expm1|log|log2|log10|log1p'
             '|pow|cbrt|hypot|erf|erfc|tgamma|lgamma')
GLM = (r'sin|cos|tan|asin|acos|atan|sinh|cosh|tanh|asinh|acosh|atanh|pow|exp|exp2|log|log2|rotate\w*|perspective\w*'
       r'|infinitePerspective\w*|tweakedInfinitePerspective|angleAxis|angle|slerp|eulerAngle\w*|pitch|yaw|roll'
       r'|yawPitchRoll|orientate\d?|orientedAngle')
NOT_READ = re.compile(r'''//[^\n]*|/\*.*?\*/|(?:u8|[uUL])?R"([^()\\\s]*)\(.*?\)\1"|(?:u8|[uUL])?"(?:\\.|[^"\\\n])*"'''
                      r"""|(?:u8|[uUL]|(?<!\w))'(?:\\.|[^'\\\n])*'""", re.DOTALL)
STD_NAME = re.compile(rf'\bstd\s*::\s*((?:{C_LIBRARY})[fl]?)\b')
GLM_NAME = re.compile(rf'\bglm\s*::\s*({GLM})\b')
GLM_EULER_QUAT = re.compile(r'\bglm\s*::\s*[df]?quat(?:\s+\w+)?\s*[({]\s*glm\s*::\s*(?:[df]?vec3|radians)\b')
UNQUALIFIED_CALL = re.compile(rf'(?<![\w.])((?:{C_LIBRARY})[fl]?)\s*\(')
LAST_WORD = re.compile(r'[A-Za-z_]\w*$')
OWN_NAMES = {'log'}
BEFORE_AN_EXPRESSION = {'return', 'co_return', 'co_yield', 'case', 'throw', 'else', 'do', 'and', 'or', 'not', 'xor'}
ADVICE = 'use Render/Models/RoundedMath.h or RoundedTransforms.h (docs/adr/0027-correctly-rounded-math.md)'


def code_of(text: str) -> str:
    """The source with its comments and literals blanked, so every offset and line stays where it was."""
    return NOT_READ.sub(lambda skipped: re.sub(r'[^\n]', ' ', skipped.group()), text)


def _is_c_call(code: str, start: int) -> bool:
    """Whether the name at start is the C function: not a member, another namespace's function or a declaration."""
    before = code[:start].rstrip()
    if before.endswith('::'):
        return not re.search(r'[\w>]$', before[:-2].rstrip())
    if before.endswith(('.', '->')):
        return False
    word = LAST_WORD.search(before)
    return word is None or word.group() in BEFORE_AN_EXPRESSION


def calls(text: str) -> list[tuple[int, str]]:
    """The line and the name of each call to math that the C library or glm decides."""
    code = code_of(text)
    found = [(match.start(), f'std::{match.group(1)}') for match in STD_NAME.finditer(code)]
    found += [(match.start(), f'glm::{match.group(1)}') for match in GLM_NAME.finditer(code)]
    found += [(match.start(), 'glm::quat(vec3)') for match in GLM_EULER_QUAT.finditer(code)]
    found += [(match.start(1), match.group(1)) for match in UNQUALIFIED_CALL.finditer(code)
              if match.group(1) not in OWN_NAMES and _is_c_call(code, match.start(1))]
    starts = [0] + [index + 1 for index, char in enumerate(code) if char == '\n']
    return sorted((bisect.bisect_right(starts, offset), name) for offset, name in found)


def calls_under(root: pathlib.Path, folders: list[str]) -> list[tuple[str, int, str]]:
    found = []
    for folder in folders:
        for path in sorted((root / folder).rglob('*')):
            if path.suffix in SOURCE_SUFFIXES and path.is_file():
                text = path.read_text(encoding='utf-8', errors='replace')
                found += [(path.relative_to(root).as_posix(), line, name) for line, name in calls(text)]
    return found


def load_exceptions(path: pathlib.Path) -> set[tuple[str, str]]:
    with path.open(encoding='utf-8', newline='') as file:
        return {(row['file'], row['call']) for row in csv.DictReader(file, delimiter='\t')}


def _parse_args(argv: list[str] | None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--root', type=pathlib.Path, default=pathlib.Path('.'))
    parser.add_argument('--src', nargs='+', required=True, help='folders to read, relative to the root')
    parser.add_argument('--exceptions', type=pathlib.Path, required=True,
                        help=f'a TSV with the columns {", ".join(EXCEPTION_COLUMNS)}')
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = _parse_args(argv)
    found = calls_under(args.root, args.src)
    allowed = load_exceptions(args.exceptions)
    new = [(file, line, name) for file, line, name in found if (file, name) not in allowed]
    stale = sorted(allowed.difference((file, name) for file, _, name in found))
    for file, line, name in new:
        print(f'{file}:{line}: {name}')
    if stale:
        print(f'{len(stale)} exception rows no longer happen; delete them from {args.exceptions.name}:')
        for file, name in stale:
            print(f'  {file}\t{name}')
    if new:
        print(f'{len(new)} calls to math that the C library or glm decides: {ADVICE}')
    elif not stale:
        print(f'no math from the C library or from glm (calls in the exceptions: {len(found)})')
    return 1 if new or stale else 0


if __name__ == '__main__':
    sys.exit(main())
