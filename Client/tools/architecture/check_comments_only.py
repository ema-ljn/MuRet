"""Checks that a comments PR changed only comments.

For each .h, .cpp, .hpp and .inc file modified between two commits it strips the comments (// and /* */, leaving
string, character and raw string literals alone) and compares the code tokens. It also checks that every main.exe
address (0x and 6 to 8 hex digits) a comment cited in the base is still cited in a comment of the same file. Research
notes (Client/docs/research/**/*.md) may be added or changed, since long comments move there; any other change fails.
Exits with 1 and lists each case.

    python3 -I Client/tools/architecture/check_comments_only.py --base origin/refactor/client --head HEAD
"""
import argparse
import bisect
import difflib
import re
import subprocess
import sys

SOURCES = ('.h', '.cpp', '.hpp', '.inc')
RESEARCH_NOTES = re.compile(r'^Client/docs/research/.+\.md$')
ADDRESS = re.compile(r'\b0[xX]([0-9a-fA-F]{6,8})\b')
IDENTIFIER = re.compile(r'[A-Za-z_]\w*')
NUMBER = re.compile(r"\.?\d(?:[eEpP][+-]|'(?=\w)|[\w.])*")
LITERAL_PREFIXES = {'u8', 'u', 'U', 'L', 'R', 'u8R', 'uR', 'UR', 'LR'}


def git(repo: str, *args: str) -> str:
    return subprocess.run(['git', '-C', repo, *args], capture_output=True, check=True).stdout.decode(
        'utf-8', 'replace')


def _quoted_end(text: str, start: int) -> int:
    """The end of the string or character literal whose opening quote is at start (or of its line, if unclosed)."""
    quote, index = text[start], start + 1
    while index < len(text) and text[index] not in (quote, '\n'):
        index += 2 if text[index] == '\\' else 1
    return min(index + 1, len(text))


def _raw_end(text: str, quote: int) -> int:
    open_paren = text.find('(', quote)
    if open_paren < 0:
        return len(text)
    closing = text.find(')' + text[quote + 1:open_paren] + '"', open_paren)
    return len(text) if closing < 0 else closing + open_paren - quote + 1


def scan(text: str) -> tuple[list[tuple[str, int]], list[tuple[str, int]]]:
    """The code tokens and the comments of a C++ source, each with its starting offset."""
    tokens, comments, index = [], [], 0
    while index < len(text):
        char = text[index]
        if char.isspace():
            index += 1
        elif text.startswith('//', index):
            end = text.find('\n', index)
            end = len(text) if end < 0 else end
            comments.append((text[index:end], index))
            index = end
        elif text.startswith('/*', index):
            end = text.find('*/', index + 2)
            end = len(text) if end < 0 else end + 2
            comments.append((text[index:end], index))
            index = end
        elif char in '"\'':
            end = _quoted_end(text, index)
            tokens.append((text[index:end], index))
            index = end
        elif identifier := IDENTIFIER.match(text, index):
            end = identifier.end()
            if identifier.group() in LITERAL_PREFIXES and end < len(text) and text[end] in '"\'':
                raw = identifier.group().endswith('R') and text[end] == '"'
                end = _raw_end(text, end) if raw else _quoted_end(text, end)
            tokens.append((text[index:end], index))
            index = end
        elif number := NUMBER.match(text, index):
            tokens.append((number.group(), index))
            index = number.end()
        else:
            tokens.append((char, index))
            index += 1
    return tokens, comments


def cited(comments: list[tuple[str, int]]) -> set[str]:
    return {f'0x{match.lower()}' for comment, _ in comments for match in ADDRESS.findall(comment)}


class Source:
    def __init__(self, text: str):
        self.lines = text.splitlines()
        self.starts = [0] + [index + 1 for index, char in enumerate(text) if char == '\n']
        self.tokens, self.comments = scan(text)

    def line_of(self, offset: int) -> int:
        return bisect.bisect_right(self.starts, offset)

    def line_text(self, offset: int) -> str:
        number = self.line_of(offset)
        return f'{number}: {self.lines[number - 1].strip()}' if number <= len(self.lines) else f'{number}:'


def code_changes(before: Source, after: Source) -> list[str]:
    old = [token for token, _ in before.tokens]
    new = [token for token, _ in after.tokens]
    changes = []
    for tag, old_start, old_end, new_start, new_end in difflib.SequenceMatcher(None, old, new,
                                                                               autojunk=False).get_opcodes():
        if tag == 'equal':
            continue
        if new_start < new_end:
            changes.append('+ ' + after.line_text(after.tokens[new_start][1]))
        if old_start < old_end:
            changes.append('- ' + before.line_text(before.tokens[old_start][1]))
    return changes


def changed_files(repo: str, base: str, head: str) -> list[tuple[str, str]]:
    fields = git(repo, 'diff', '-z', '--name-status', '-M50%', base, head).split('\0')
    found, index = [], 0
    while index < len(fields) - 1:
        status = fields[index]
        width = 3 if status.startswith(('R', 'C')) else 2
        found.append((status, fields[index + width - 1]))
        index += width
    return found


def problems(repo: str, base: str, head: str) -> list[str]:
    found = []
    for status, path in changed_files(repo, base, head):
        if status in ('A', 'M') and RESEARCH_NOTES.match(path):
            continue
        if status != 'M' or not path.endswith(SOURCES):
            found.append(f'{path}: not a comments change ({status})')
            continue
        before = Source(git(repo, 'show', f'{base}:{path}'))
        after = Source(git(repo, 'show', f'{head}:{path}'))
        found += [f'{path}: code changed: {change}' for change in code_changes(before, after)]
        found += [f'{path}: no comment cites {address} any more'
                  for address in sorted(cited(before.comments) - cited(after.comments))]
    return found


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--repo', default='.')
    parser.add_argument('--base', required=True)
    parser.add_argument('--head', required=True)
    args = parser.parse_args(argv)
    found = problems(args.repo, args.base, args.head)
    for problem in found:
        print(problem)
    print(f'{len(found)} changes beyond comments' if found else 'only comments changed')
    return 1 if found else 0


if __name__ == '__main__':
    sys.exit(main())
