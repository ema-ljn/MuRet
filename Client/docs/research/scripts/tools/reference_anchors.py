#!/usr/bin/env python3
"""reference_anchors.py: stable anchors for the cuts classified by hand in referencia/entradas/cortes-vivos.tsv.

A hand row names its cut by file and line, which a move or an edit above it breaks. Its anchor is what the cut says:
its addresses, the cut's name, the MuRet symbol around it and a hash of its text. build_client_reference.py finds
every cut site of the commit it measures and gives each row the site with the same anchor, wherever it is now.

Writing the anchor column from the rows' file and line at a commit (once, or after classifying new rows by hand):

  python3 -I Client/docs/research/scripts/tools/reference_anchors.py --anchor-entries --ref HEAD

Standard library only. Run with python3 -I.
"""
import argparse
import bisect
import csv
import difflib
import hashlib
import os
import re
import sys

ADDR = re.compile(r'0[xX](?:00)?([4-6][0-9a-fA-F]{5})\b')
# An address without 0x at the start of a string ("4ecc30: ..."), as some files write it.
BARE = re.compile(r'"\s*([4-6][0-9a-f]{5})(?=[:/ (])')
CUT_NAMES = r'cut|Cut|CutAt|CutWhenFading|ScatterAndCut|CloseCut|OpenCut|CutTheDialog|Cut[A-Z][A-Za-z]*'
NAME = re.compile(r'\b(' + CUT_NAMES + r'|Unexpected)\s*(<[^;(){}]*?>)?\s*\(')
NOT_A_TYPE = {'return', 'co_return', 'else', 'case', 'throw', 'and', 'or', 'not', 'co_await', 'co_yield', 'do'}
LINE_NUMBER = re.compile(r'(\.(?:cpp|h|inc|hpp)):\d+')
JOINED_STRINGS = re.compile(r'"\s+"')
CPP = ('.h', '.cpp', '.inc', '.hpp')
ENTRIES = 'Client/docs/research/coverage/referencia/entradas'


def mask(text):
    """(code, in_string): the text with its comments blanked, and whether each position is inside a literal."""
    n = len(text)
    code = list(text)
    in_string = bytearray(n)
    i = 0
    while i < n:
        c = text[i]
        if text.startswith('//', i):
            j = text.find('\n', i)
            j = n if j < 0 else j
            code[i:j] = ' ' * (j - i)
            i = j
        elif text.startswith('/*', i):
            j = text.find('*/', i + 2)
            j = n if j < 0 else j + 2
            code[i:j] = [ch if ch == '\n' else ' ' for ch in text[i:j]]
            i = j
        elif text.startswith('R"', i) and (i == 0 or not (text[i - 1].isalnum() or text[i - 1] == '_')):
            m = re.match(r'R"([^(\s]*)\(', text[i:])
            if not m:
                i += 1
                continue
            end = text.find(')' + m.group(1) + '"', i)
            end = n if end < 0 else end + len(m.group(1)) + 2
            in_string[i:end] = b'\1' * (end - i)
            i = end
        elif c == '"' or (c == "'" and not (i > 0 and text[i - 1].isalnum())):
            j = i + 1
            while j < n and text[j] != c and text[j] != '\n':
                j += 2 if text[j] == '\\' else 1
            end = min(j + 1, n)
            in_string[i:end] = b'\1' * (end - i)
            i = end
        else:
            i += 1
    return ''.join(code), in_string


def normalize(text):
    """The text without comments, with whitespace collapsed, adjacent literals joined and no source line numbers."""
    code, _ = mask(text)
    code = ' '.join(code.split())
    return LINE_NUMBER.sub(r'\1', JOINED_STRINGS.sub('', code))


def anchor_of(text, vas, name, symbol):
    digest = hashlib.sha1(normalize(text).encode('utf-8')).hexdigest()[:12]
    return '|'.join((','.join(f'0x{va:x}' for va in sorted(set(vas))), name, symbol, digest))


def _close_paren(code, in_string, i):
    depth = 0
    for j in range(i, len(code)):
        if in_string[j]:
            continue
        if code[j] == '(':
            depth += 1
        elif code[j] == ')':
            depth -= 1
            if depth == 0:
                return j
    return len(code) - 1


def _previous_token(code, i):
    j = i - 1
    while j >= 0 and code[j] in ' \t\r\n':
        j -= 1
    if j < 0:
        return ''
    if code[j] == ':' and j > 0 and code[j - 1] == ':':
        j -= 2
        while j >= 0 and (code[j].isalnum() or code[j] == '_'):
            j -= 1
        return _previous_token(code, j + 1)
    if code[j].isalnum() or code[j] == '_':
        k = j
        while k >= 0 and (code[k].isalnum() or code[k] == '_'):
            k -= 1
        return code[k + 1:j + 1]
    if code[j] == '>':
        return '->' if j > 0 and code[j - 1] == '-' else '>'
    return code[j]


def _is_definition(code, i, end):
    token = _previous_token(code, i)
    if token in ('.', '->'):
        return False
    if token and (token[0].isalpha() or token[0] == '_') and token not in NOT_A_TYPE:
        return True
    if token in ('>', '*', '&'):
        return True
    return code[end + 1:end + 40].lstrip().startswith(('{', 'const {', 'noexcept'))


def cut_sites(path, text, line_symbols):
    """Every call of a cut (Unexpected, Cut, CutAt…) that is not a definition, one per call even across lines."""
    code, in_string = mask(text)
    starts = [0] + [m.end() for m in re.finditer('\n', text)]
    sites = []
    covered_until = -1
    for m in NAME.finditer(code):
        i = m.start(1)
        if in_string[i] or i <= covered_until:
            continue
        open_paren = m.end() - 1
        end = _close_paren(code, in_string, open_paren)
        if _is_definition(code, i, end):
            continue
        line = bisect.bisect_right(starts, i)
        arguments = code[open_paren + 1:end]
        vas = sorted({int(h, 16) for h in ADDR.findall(arguments) + BARE.findall(arguments)})
        statement = code[starts[line - 1]:end + 1]
        symbol = line_symbols[line - 1] if line - 1 < len(line_symbols) else ''
        sites.append(dict(file=path, line=line, end=bisect.bisect_right(starts, end), name=m.group(1), vas=vas,
                          text=normalize(statement), symbol=symbol,
                          anchor=anchor_of(statement, vas, m.group(1), symbol)))
        covered_until = end
    return sites


def resolve(anchor, current_cuts, text=''):
    """The current cut with this anchor; else the one with the same addresses, name and symbol whose text is closest
    to `text` (resolved_by = similar); else None."""
    if not anchor:
        return None
    for cut in current_cuts:
        if cut['anchor'] == anchor:
            return dict(cut, resolved_by='anchor')
    same_place = anchor.rpartition('|')[0] + '|'
    similar = [cut for cut in current_cuts if cut['anchor'].startswith(same_place)]
    if not similar:
        return None
    wanted = normalize(text)
    best = max(similar, key=lambda cut: difflib.SequenceMatcher(None, wanted, cut['text']).ratio())
    return dict(best, resolved_by='similar')


def resolve_entries(entries, sites):
    """The hand rows with today's file and lines. Each site goes to one row, in order, so equal cuts keep theirs."""
    pool = list(sites)
    rows = []
    for row in entries:
        found = resolve(row.get('anchor', ''), pool, row.get('mensaje', ''))
        if found is None:
            rows.append(dict(row, resolved_by='unresolved'))
            continue
        taken = next(k for k, site in enumerate(pool) if (site['file'], site['line']) == (found['file'], found['line'])
                     and site['anchor'] == found['anchor'])
        del pool[taken]
        rows.append(dict(row, archivo=found['file'], línea=str(found['line']), fin=str(found['end']),
                         resolved_by=found['resolved_by']))
    return rows


def anchor_entries(rows, sites):
    """The rows with the anchor of the cut at their file and line (empty if no cut starts there)."""
    by_line = {}
    for site in sites:
        by_line.setdefault((site['file'], site['line']), []).append(site)
    out = []
    for row in rows:
        here = by_line.get((row['archivo'], int(row['línea'] or 0)), [])
        site = next((s for s in here if s['name'] == row.get('nombre')), here[0] if here else None)
        out.append(dict(row, anchor=site['anchor'] if site else ''))
    return out


def read_table(path):
    with open(path, encoding='utf-8', newline='') as fh:
        reader = csv.DictReader(fh, delimiter='\t')
        return list(reader.fieldnames), list(reader)


def write_table(path, columns, rows):
    with open(path, 'w', encoding='utf-8', newline='') as fh:
        writer = csv.writer(fh, delimiter='\t', lineterminator='\n', quoting=csv.QUOTE_MINIMAL)
        writer.writerow(columns)
        for row in rows:
            writer.writerow([row.get(c, '') for c in columns])


def main():
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    from build_client_reference import cat_files, git
    from client_reference_symbols import symbols

    ap = argparse.ArgumentParser(description='Writes the anchor column of the hand-classified cuts.')
    ap.add_argument('--anchor-entries', action='store_true', required=True)
    ap.add_argument('--muret', default='.')
    ap.add_argument('--ref', default='HEAD')
    ap.add_argument('--entradas', default=ENTRIES)
    a = ap.parse_args()
    paths = [f for f in git(a.muret, 'ls-tree', '-r', '--name-only', a.ref, '--', 'Client/src').split('\n')
             if f.endswith(CPP) and not f.endswith('Input/Key.h')]
    sites = []
    for path, text in cat_files(a.muret, a.ref, paths).items():
        sites += cut_sites(path.removeprefix('Client/src/'), text, symbols(text.split('\n')))
    table = os.path.join(a.entradas, 'cortes-vivos.tsv')
    columns, rows = read_table(table)
    rows = anchor_entries(rows, sites)
    write_table(table, columns + ['anchor'] * ('anchor' not in columns), rows)
    print(f'{len(sites)} cut sites; {sum(1 for r in rows if r["anchor"])} of {len(rows)} rows anchored', file=sys.stderr)


if __name__ == '__main__':
    main()
