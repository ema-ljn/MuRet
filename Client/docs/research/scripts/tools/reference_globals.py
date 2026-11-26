"""The registry of the original's globals: each address of the .data that Client/src cites, with the data member that
holds it (its home), the home's module and the other modules citing it. build_client_reference.py writes it as
globals.tsv, uncut: a global some views copy has a long `home`. Standard library only.

An address is cited by a comment or a string; a number in the code is a value (a flag, a text line's key), and so is
a power of two (a flag's bit). A home is a data member of a struct or class in a header whose own comment, or the
comment block right above it, cites the address; a reference member only views it.
"""
import collections
import csv
import re

DATA_START = 0x6c6000
DATA_END = 0x6c6000 + 0x7698b64
COLUMNS = ('va', 'home', 'file', 'line', 'module', 'users', 'flag')
HEADERS = ('.h', '.hpp')

ADDRESS = re.compile(r'(?<![\w.])0[xX]0*([0-9a-fA-F]{6,7})\b')
RAW_PREFIX = re.compile(r'(?:^|[^\w])(?:u8|u|U|L)?R$')
RAW_OPEN = re.compile(r'"([^()\\\s]{0,16})\(')
SCOPE_HEAD = re.compile(r'^\s*(?:template\s*<.*>\s*)?(?:\[\[.*\]\]\s*)?(?:struct|class|union)\s+'
                        r'(?:alignas\s*\([^)]*\)\s*)?(\w+)[^(]*$', re.DOTALL)
ACCESS = re.compile(r'\s*(?:public|private|protected)\s*:\s*')
NOT_A_MEMBER = re.compile(r'\s*(?:using|typedef|friend|static_assert|template|namespace|return|enum)\b')
FORWARD = re.compile(r'\s*(?:struct|class|union|enum(?:\s+class)?)\s+\w+\s*', re.DOTALL)
CONSTANT = re.compile(r'\bconst(?:expr|eval|init)\b')


def _lines(text):
    """Each line of a C++ file as (code, cited, commented): the code without its strings and comments, the text of
    both for the addresses, and whether the line has a comment."""
    out, code, cited, commented = [], [], [], False
    state, i, n = None, 0, len(text)
    while i < n:
        c = text[i]
        if c == '\n':
            out.append((''.join(code), ''.join(cited), commented))
            state = None if state == '//' else state
            code, cited, commented = [], [], state == '/*'
            i += 1
        elif state in ('//', '/*'):
            if state == '/*' and text.startswith('*/', i):
                state, i = None, i + 2
            else:
                cited.append(c)
                i += 1
        elif state is not None:
            if text.startswith(state, i):
                code.append('""')
                i, state = i + len(state), None
            else:
                cited.append(c)
                i += 1
        elif text.startswith('//', i) or text.startswith('/*', i):
            state, i, commented = text[i:i + 2], i + 2, True
            cited.append(' ')
        elif c == '"' and RAW_PREFIX.search(''.join(code)) and RAW_OPEN.match(text, i):
            m = RAW_OPEN.match(text, i)
            state, i = ')' + m.group(1) + '"', m.end()
            cited.append(' ')
        elif c in '"\'' and not (c == "'" and _digit_separator(text, i)):
            j = i + 1
            while j < n and text[j] not in (c, '\n'):
                j += 2 if text[j] == '\\' else 1
            cited.append(' ' + text[i + 1:j] + ' ')
            code.append(c + c)
            i = j + 1 if j < n and text[j] == c else j
        else:
            code.append(c)
            i += 1
    out.append((''.join(code), ''.join(cited), commented))
    return out


def _digit_separator(text, i):
    hex_digits = '0123456789abcdefABCDEF'
    return 0 < i < len(text) - 1 and text[i - 1] in hex_digits and text[i + 1] in hex_digits


def _addresses(cited):
    vas = (int(m.group(1), 16) for m in ADDRESS.finditer(cited))
    return {va for va in vas if DATA_START <= va < DATA_END and va & (va - 1)}


def _without_templates(code):
    out, depth = [], 0
    for c in code:
        if c == '<':
            depth += 1
        elif c == '>' and depth:
            depth -= 1
        elif not depth:
            out.append(c)
    return ''.join(out)


def _is_function(head):
    return '(' in _without_templates(re.split(r'(?<![=!<>])=(?!=)', head)[0])


def _member_name(statement):
    """The name a struct's statement declares as a data member, or None for a function, a constant, a type or an
    alias."""
    s = re.sub(r'\[\[.*?\]\]', '', statement)
    if NOT_A_MEMBER.match(s) or FORWARD.fullmatch(s) or CONSTANT.search(s):
        return None
    if re.search(r'\bstatic\b', s) and re.search(r'\bconst\b', s):
        return None
    s = re.split(r'(?<![=!<>])=(?!=)', s)[0]
    s = _without_templates(s).split('{')[0]
    s = re.sub(r'\[[^\]]*\]', '', s)
    s = re.sub(r':\s*\d+\s*$', '', s)
    if '(' in s or '&' in s:
        return None
    s = s.split(',')[0]
    tokens = re.findall(r'[\w:]+', s)
    return tokens[-1] if len(tokens) >= 2 and re.fullmatch(r'[A-Za-z_]\w*', tokens[-1]) else None


def _declarations(lines):
    """(struct, member, first line, last line) of each data member declared in a struct or class, 1-based."""
    found = []
    scopes = []
    pending, first = '', None
    for number, (code, _, _) in enumerate(lines, 1):
        if code.lstrip().startswith('#'):
            continue
        for c in code:
            if c == '{':
                name = SCOPE_HEAD.match(pending)
                scopes.append((name.group(1) if name and not re.search(r'\benum\b', pending) else None,
                               pending, first))
                pending, first = '', None
            elif c == '}':
                if not scopes:
                    continue
                name, head, head_first = scopes.pop()
                if name is None and scopes and scopes[-1][0] is not None and head.strip() and not _is_function(head):
                    pending, first = head + '{}', head_first
                else:
                    pending, first = '', None
            elif c == ';':
                if scopes and scopes[-1][0] is not None and pending.strip():
                    member = _member_name(pending)
                    if member:
                        found.append((scopes[-1][0], member, first, number))
                pending, first = '', None
            else:
                if first is None and not c.isspace():
                    first = number
                pending += c
        if ACCESS.fullmatch(pending):
            pending, first = '', None
        else:
            pending += ' '
    return found


def _homes(lines):
    """Each cited address of a header's data members, with the members citing it: {va: [(Struct::member, line)]}."""
    homes = collections.defaultdict(list)
    for struct, member, first, last in _declarations(lines):
        cited = set()
        for number in range(first, last + 1):
            cited |= _addresses(lines[number - 1][1])
        above = first - 1
        while above >= 1 and not lines[above - 1][0].strip() and lines[above - 1][2]:
            cited |= _addresses(lines[above - 1][1])
            above -= 1
        for va in cited:
            homes[va].append((f'{struct}::{member}', last))
    return homes


def build_globals(sources, module_of):
    """The rows of globals.tsv, by address. sources maps each Client/src file's path to its text; module_of takes a
    path relative to Client/src and gives its module, or ''."""
    citing = collections.defaultdict(set)
    homes = collections.defaultdict(list)
    for path in sorted(sources):
        lines = _lines(sources[path])
        for _, cited, _ in lines:
            for va in _addresses(cited):
                citing[va].add(path)
        if path.endswith(HEADERS):
            for va, members in _homes(lines).items():
                homes[va] += [(path, home, line) for home, line in members]
    rows = []
    for va in sorted(citing):
        found = sorted(homes.get(va, []), key=lambda h: (h[0], h[2]))
        file, line = (found[0][0], str(found[0][2])) if found else ('', '')
        module = module_of(file.removeprefix('Client/src/')) if file else ''
        users = {module_of(p.removeprefix('Client/src/')) for p in citing[va] if p != file} - {'', module}
        flag = 'no-home' if not found else ('two-homes' if len(found) > 1 else '')
        rows.append({'va': f'0x{va:x}', 'home': ' '.join(h[1] for h in found), 'file': file, 'line': line,
                     'module': module, 'users': ' '.join(sorted(users)), 'flag': flag})
    return rows


def write(path, rows):
    with open(path, 'w', encoding='utf-8', newline='') as fh:
        writer = csv.writer(fh, delimiter='\t', lineterminator='\n')
        writer.writerow(COLUMNS)
        writer.writerows([row[c] for c in COLUMNS] for row in rows)
