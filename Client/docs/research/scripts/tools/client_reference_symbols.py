"""client_reference_symbols.py: para cada línea de un archivo C++ de MuRet, el símbolo que la contiene (aproximado).

Dentro de una función o de una tabla, es el nombre de la función o de la tabla de más afuera (las lambdas cuentan como
parte de su función). Un comentario fuera de toda función toma el nombre de la siguiente definición o declaración, si
está a menos de 25 líneas. No es un parser: se equivoca con macros raras, pero alcanza para ubicar una cita.
"""
import re

KW = {'if', 'for', 'while', 'switch', 'return', 'sizeof', 'catch', 'static_assert', 'decltype', 'alignof', 'alignas',
      'defined', 'noexcept', 'requires', 'template', 'operator', 'assert', 'case', 'do', 'else', 'new', 'delete',
      'throw', 'co_return', 'co_await', 'static_cast', 'reinterpret_cast', 'const_cast', 'dynamic_cast',
      'std::format', 'TEST', 'TEST_F', 'TEST_P'}
DEF_RE = re.compile(r'(~?[A-Za-z_]\w*(?:::~?[A-Za-z_]\w*)*)\s*\(')
DATA_RE = re.compile(r'([A-Za-z_]\w*)\s*(?:\[[^\]]*\])*\s*(?:=\s*)?$')
ASSIGN_RE = re.compile(r'([A-Za-z_]\w*)\s*(?:\[[^\]]*\])*\s*(?:=|\{)')
NS_RE = re.compile(r'(?:^|\s)namespace\b|extern\s+"C"')
CLASS_RE = re.compile(r'(?:^|[\s>])(?:class|struct|union|enum(?:\s+class)?)\s+(?:alignas\([^)]*\)\s*)?([A-Za-z_]\w*)'
                      r'(?![^:{]*\()')
TEST_RE = re.compile(r'\b(TEST(?:_F|_P)?)\s*\(\s*(\w+)\s*,\s*(\w+)\s*\)')
STR_RE = re.compile(r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'')


def short(name):
    parts = name.split('::')
    return '::'.join(parts[-2:])


def name_of(sig, ends_with_brace):
    sig = sig.strip()
    t = TEST_RE.search(sig)
    if t:
        return f'{t.group(2)}.{t.group(3)}'
    for m in DEF_RE.finditer(sig):
        n = m.group(1)
        if n.split('::')[-1] not in KW and n not in KW and not n.startswith('std::'):
            return short(n)
    if ends_with_brace:
        m = DATA_RE.search(sig)
        if m:
            return m.group(1)
    m = ASSIGN_RE.search(sig)
    if m and m.group(1) not in KW:
        return m.group(1)
    if not ends_with_brace:
        m = DATA_RE.search(sig)
        if m and m.group(1) not in KW:
            return m.group(1)
    return ''


def symbols(lines):
    out = [''] * len(lines)
    stack = []  # (kind, name): ns, class, fn, blk
    sig = ''
    pending = []
    in_block_comment = False

    def resolve(name, i):
        nonlocal pending
        cls = next((n for k, n in reversed(stack) if k == 'class'), '')
        if name and cls and '::' not in name and '.' not in name:
            name = f'{cls}::{name}'
        for j in pending:
            if i - j <= 25 and name:
                out[j] = name
        pending = []
        return name

    for i, raw in enumerate(lines):
        code = raw
        if in_block_comment:
            end = code.find('*/')
            if end < 0:
                code = ''
            else:
                code = code[end + 2:]
                in_block_comment = False
        code = STR_RE.sub('""', code)
        c = code.find('//')
        if c >= 0:
            code = code[:c]
        while '/*' in code:
            s = code.find('/*')
            e = code.find('*/', s + 2)
            if e < 0:
                code = code[:s]
                in_block_comment = True
                break
            code = code[:s] + ' ' + code[e + 2:]
        if code.lstrip().startswith('#'):
            code = ''
        fn = next((n for k, n in stack if k == 'fn'), None)
        if fn is not None:
            out[i] = fn
        else:
            pending.append(i)
        for ch in code:
            transparent = all(k in ('ns', 'class') for k, _ in stack)
            if ch == '{':
                if transparent:
                    if NS_RE.search(sig):
                        stack.append(('ns', ''))
                        pending = []
                    else:
                        cm = CLASS_RE.search(sig)
                        if cm:
                            stack.append(('class', cm.group(1)))
                        else:
                            n = resolve(name_of(sig, True), i)
                            stack.append(('fn', n))
                            if not out[i]:
                                out[i] = n
                else:
                    stack.append(('blk', ''))
                sig = ''
            elif ch == '}':
                if stack:
                    stack.pop()
                sig = ''
            elif ch == ';':
                if transparent:
                    n = name_of(sig, False)
                    if n and not sig.strip().startswith(('using', 'return', 'friend')):
                        n = resolve(n, i)
                        if not out[i]:
                            out[i] = n
                sig = ''
            elif all(k in ('ns', 'class') for k, _ in stack):
                sig += ch
        if all(k in ('ns', 'class') for k, _ in stack):
            sig += ' '
        if not raw.strip():
            pending = []
        pending = [j for j in pending if i - j <= 25]
    return out
