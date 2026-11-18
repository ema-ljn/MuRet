"""Laboratorio didáctico Q-C184/Q-C192: prepara y analiza; nunca ejecuta el original.

Python 3.10+ en el host. Los comandos generados se usan sólo en la VM, con WinDbg x86.
La captura pasiva usa el s31mem del lab fijado por el runbook CL9.
"""

import argparse
import hashlib
import json
from pathlib import Path, PureWindowsPath
import re
import struct
import sys
import uuid

from recover_cl9_evidence import EvidenceError, outside_checkouts


EXE_SHA256 = 'c32bbd5f16ca400c7cfc71fa5f4cd573c57f146dd19a6da52fdd1007abaca26a'
EXE_SIZE = 3058308
IMAGE_BASE = 0x400000
ITEM_TABLE_POINTER = 0x7758550
OWN_POINTER = 0x7681eb4
EQUIPMENT_OFFSET = 0xb6c
ITEM_SIZE = 0x48
POINTER_MASK = 0xffffffff
MAX_LOG_SIZE = 64 * 1024 * 1024
GUARD_CONTAINER = 0x57630c8
GUARD_HASH = 0x448310
MAX_GUARD_CAPACITY = 0x100000
GUARD_FIELDS = ('gvalues', 'gkeys', 'gcap', 'gmult', 'gslot', 'gkey', 'shadow', 'ghash', 'gprobes')
POINTS = {
    'bonus': [(0x40dc8c, 'on-read.wds'), (0x550d10, 'on-begin.wds'),
              (0x5513a3, 'on-end.wds'), (0x5513b1, 'on-end.wds')],
    'wear': [(0x550adb, 'on-wear-before.wds'), (0x550ae0, 'on-wear-factor.wds'),
             (0x550b00, 'on-wear-reduced.wds'), (0x550b1e, 'on-wear-sum.wds')],
}
SIGNATURES = {
    'bonus': [('sig_item', 0x40dc50, 16), ('sig_stats', 0x550d10, 16),
              ('sig_return_a', 0x5513a3, 1), ('sig_return_b', 0x5513b1, 1),
              ('sig_guard_hash', GUARD_HASH, 0x30)],
    'wear': [('sig_wear_before', 0x550adb, 8), ('sig_wear_factor', 0x550ae0, 8),
             ('sig_wear_reduced', 0x550b00, 8), ('sig_wear_sum', 0x550b1e, 8)],
}
FP_COMMANDS = '.echo QC184_FP_BEGIN; rF; .echo QC184_FP_END'


def read_va_bytes(image, address, size):
    """Mapea una VA del PE32 a bytes de archivo; no lo carga como módulo."""
    if len(image) < 0x40 or image[:2] != b'MZ' or size <= 0:
        raise ValueError('PE o longitud inválidos')
    pe = struct.unpack_from('<I', image, 0x3c)[0]
    if pe + 24 > len(image) or image[pe:pe+4] != b'PE\0\0':
        raise ValueError('falta el encabezado PE')
    machine, sections = struct.unpack_from('<HH', image, pe + 4)
    optional_size = struct.unpack_from('<H', image, pe + 20)[0]
    optional = pe + 24
    table = optional + optional_size
    if machine != 0x14c or optional_size < 32 or not 1 <= sections <= 96:
        raise ValueError('se requiere PE32 i386')
    if table + sections * 40 > len(image) or struct.unpack_from('<H', image, optional)[0] != 0x10b:
        raise ValueError('tabla de secciones PE32 inválida')
    base = struct.unpack_from('<I', image, optional + 28)[0]
    if base != IMAGE_BASE:
        raise ValueError('ImageBase distinta de 0x00400000')
    rva = address - base
    matches = []
    for index in range(sections):
        _, start, raw_size, raw = struct.unpack_from('<IIII', image, table + index * 40 + 8)
        if start <= rva and rva + size <= start + raw_size and raw + rva - start + size <= len(image):
            matches.append(image[raw+rva-start:raw+rva-start+size])
    if len(matches) != 1:
        raise ValueError(f'VA 0x{address:08x} sin una región única respaldada por archivo')
    return matches[0]


def _guest_path(value):
    if len(value) > 160 or not re.fullmatch(r'[A-Za-z]:\\[A-Za-z0-9_.\\-]+', value):
        raise ValueError('usar ruta guest absoluta ASCII, sin espacios ni comandos')
    path = PureWindowsPath(value)
    if len(path.parts) < 2 or '..' in path.parts:
        raise ValueError('la ruta guest debe ser una carpeta nueva, sin ..')
    return path


def _outside_git(path):
    absolute = Path(path).absolute()
    try:
        outside_checkouts(absolute)
    except EvidenceError as error:
        raise ValueError(f'ruta de evidencia inválida: {error}') from error
    return absolute.resolve()


def _record(event, fields):
    format_text = 'QC184_' + event + ' ' + ' '.join(f'{name}=%08x' for name in fields) + r'\n'
    return f'.printf "{format_text}", ' + ', '.join(fields.values())


def _handler(condition, command):
    return f'.if ({condition}) {{ {command}; {FP_COMMANDS}; }}\ngc\n'


def _guard_commands(own):
    # 550ed8..550f7b: own has 0xf08 data bytes; the counter is shadow[0xf08].
    # 448310 hashes the four key bytes with the multiplier at container+0x1c.
    # Only debugger pseudo-registers change; the target's hash method is never called.
    commands = [f'r @$t0 = {own}',
                f'r @$t1 = poi(0x{GUARD_CONTAINER+4:x})',
                f'r @$t2 = poi(0x{GUARD_CONTAINER+8:x})',
                f'r @$t3 = dwo(0x{GUARD_CONTAINER+12:x})',
                f'r @$t4 = dwo(0x{GUARD_CONTAINER+28:x})',
                *[f'r @$t{i} = 0' for i in range(5, 10)],
                f'r @$t10 = poi(poi(0x{GUARD_CONTAINER:x})+0xc)']
    checks = (f'(@$t10 == 0x{GUARD_HASH:x}) & (@$t3 > 0) & '
              f'(@$t3 <= 0x{MAX_GUARD_CAPACITY:x}) & (@$t1 != 0) & (@$t2 != 0)')
    lookup = [f'r @$t5 = ((@$t5*@$t4)+((@$t0>>0x{shift:x})&0xff))&0xffffffff'
              for shift in (0, 8, 16, 24)]
    lookup += ['r @$t5 = @$t5 mod @$t3',
               '.for (r @$t9 = 0; @$t9 < @$t3; r @$t9 = @$t9+1) { '
               'r @$t7 = dwo(@$t2+4*@$t5); '
               '.if (@$t7 == @$t0) { r @$t6 = poi(@$t1+4*@$t5); .break; } '
               '.if (@$t7 == 0) { .break; } '
               'r @$t5 = (@$t5+1) mod @$t3; }',
               '.if (@$t6 != 0) { r @$t8 = by(@$t6+0xf08); }']
    commands.append(f'.if ({checks}) {{ {"; ".join(lookup)}; }}')
    return '; '.join(commands) + '; '


def _guard_fields():
    registers = dict(zip(GUARD_FIELDS, (1, 2, 3, 4, 5, 7, 6, 10, 9)))
    return dict(depth='@$t8', **{name: f'@$t{index}' for name, index in registers.items()})


def _stat_fields(own, caller):
    fields = {'tid': '@$tid', 'own': own, 'caller': caller, 'pc': '@eip', 'frame': '@esp',
              **_guard_fields()}
    for offset in (0x2a, 0x2c, 0x2e, 0x30, 0x32, 0x34, 0x36, 0x62, 0x64, 0x66):
        fields[f'w{offset:x}'] = f'wo(({own})+0x{offset:x})'
    return fields


def _bonus_handlers():
    fields = {'tid': '@$tid', 'own': f'poi(0x{OWN_POINTER:x})', 'item': 'poi(@ebp+8)',
              'table': '@edx', 'address': '@edx-0x10', 'index': '@eax',
              'id': 'wo(poi(@ebp+8))', 'dur': 'by(poi(@ebp+8)+0x1a)',
              'flags': 'by(poi(@ebp+8)+0x1c)', 'value': 'wo(poi(@ebp+0xc))',
              'kind': '@ecx&0xff', 'mem': 'by(@edx-0x10)', 'caller': 'poi(@ebp+4)',
              **_guard_fields()}
    read = (_guard_commands(f'poi(0x{OWN_POINTER:x})') + _record('READ', fields)
            + '; .echo QC184_PREFIX_BEGIN; '
            'db @edx-0x40 L40; .echo QC184_PREFIX_END')
    begin = _guard_commands('@ecx') + _record('BEGIN', _stat_fields('@ecx', 'poi(@esp)'))
    end = (_guard_commands(f'poi(0x{OWN_POINTER:x})')
           + _record('END', _stat_fields(f'poi(0x{OWN_POINTER:x})', 'poi(@esp)')))
    return {'on-read.wds': _handler('wo(poi(@ebp+8)) == 0xffff', read),
            'on-begin.wds': begin + '; ' + FP_COMMANDS + '\ngc\n',
            'on-end.wds': end + '; ' + FP_COMMANDS + '\ngc\n'}


def _wear_handlers():
    item = '(@esi+0xb6c+0x48*poi(@ebp-4))'
    common = {'tid': '@$tid', 'own': '@esi', 'frame': '@ebp',
              'slot': 'poi(@ebp-4)', 'id': f'wo({item})'}
    before = dict(common, value='wo(@ebp-0xc)', base='by(@esp+4)', dur='by(@esp)',
                  table=f'poi(0x{ITEM_TABLE_POINTER:x})',
                  address=f'poi(0x{ITEM_TABLE_POINTER:x})-0x27',
                  mem_base=f'by(poi(0x{ITEM_TABLE_POINTER:x})-0x27)')
    before.update(value_raw='poi(@ebp-0xc)', arg1='poi(@esp)', arg2='poi(@esp+4)',
                  arg3='poi(@esp+8)', arg4='poi(@esp+0xc)', arg5='poi(@esp+0x10)')
    reduced = dict(common, reduced='@ebx', low='@ebx&0xffff')
    summed = {'tid': '@$tid', 'own': '@esi', 'frame': '@ebp',
              'sum': 'poi(@ebp-8)', 'w62': 'wo(@esi+0x62)'}
    return {'on-wear-before.wds': _handler(f'wo({item}) == 0xffff', _record('WEAR_BEFORE', before)),
            'on-wear-factor.wds': _handler(f'wo({item}) == 0xffff', _record('WEAR_FACTOR', common)),
            'on-wear-reduced.wds': _handler(f'wo({item}) == 0xffff', _record('WEAR_REDUCED', reduced)),
            'on-wear-sum.wds': _record('WEAR_SUM', summed) + '; ' + FP_COMMANDS + '\ngc\n'}


def _signature_conditions(fingerprints):
    conditions = []
    for entry in fingerprints.values():
        data = bytes.fromhex(entry['hex'])
        for offset in range(0, len(data), 4):
            chunk = data[offset:offset+4]
            if len(chunk) == 4:
                value = int.from_bytes(chunk, 'little')
                conditions.append(f'dwo(0x{entry["address"]+offset:08x}) == 0x{value:08x}')
            else:
                conditions.extend(f'by(0x{entry["address"]+offset+i:08x}) == 0x{byte:02x}'
                                  for i, byte in enumerate(chunk))
    return conditions


def render_commands(manifest, guest_directory):
    guest = _guest_path(guest_directory)
    mode = manifest['mode']
    if mode not in POINTS:
        raise ValueError('modo desconocido')
    handlers = _bonus_handlers() if mode == 'bonus' else _wear_handlers()
    checks = ['@$ptrsize == 4', '@$t19 == 0',
              *_signature_conditions(manifest['fingerprints'])]
    # MASM uses bitwise AND on the 0/1 comparison results; && is C++ syntax.
    profile = ' & '.join(f'({check})' for check in checks)
    install = ['.echo QC184_PROFILE_OK']
    for index, (address, name) in enumerate(POINTS[mode]):
        # CommandString interpreta escapes C: duplicar las barras del nombre guest.
        handler = str(guest / name).replace('\\', '\\\\')
        install.append(f'ba{index} e 1 0x{address:08x} "$$><{handler}"')
    install.extend(('bl', '.echo QC184_REVIEW_FOUR_HARDWARE_POINTS_THEN_RUN_arm.wds'))
    start = (f'.logopen /t {guest / "native.log"}\n.expr /s masm\nn 16\nversion\n.time\nlm a 0x00400000\n'
             f'.echo QC184_SESSION id={manifest["session"]} mode={mode} schema=1\n'
             '.printf "QC184_PROCESS pid=%08x\\n", @$tpid\n'
             # An absent $bpN is an error, so inspect bl before referring to owned IDs.
             'r @$t19 = 0\n.foreach (qc184point { bl }) { r @$t19 = 1; }\n'
             f'.if ({profile}) {{ {"; ".join(install)}; }} '
             '.else { .echo QC184_BLOCKED_PROFILE_OR_BREAKPOINT_IDS; }\n')
    ownership = ' & '.join(f'(@$bp{i} == 0x{address:08x})'
                            for i, (address, _) in enumerate(POINTS[mode]))
    stop = (f'.if ({ownership}) {{ bc 0; bc 1; bc 2; bc 3; .echo QC184_COMPLETE; '
            '.logclose; .detach; } .else { .echo QC184_STOP_BLOCKED_CHECK_OWNERSHIP; }\n')
    arm = (f'.if ({ownership}) {{ .echo QC184_ARMED_OK; g; }} '
           '.else { .echo QC184_BLOCKED_HARDWARE_POINTS; }\n')
    return dict(handlers, **{'start.wds': start, 'arm.wds': arm, 'stop.wds': stop})


def _passive_spec(fingerprints):
    lines = ['# s31mem x86 del lab9f83; suma DWORD modulo 2^32 para los offsets negativos.']
    lines += [f'{name} 0x{entry["address"]:08x} 0x{len(bytes.fromhex(entry["hex"])):x}'
              for name, entry in fingerprints.items()]
    lines += ['table_before 0x07758550 0x4',
              'selector *0x07758550+0xfffffff0 0x1',
              'wear_base *0x07758550+0xffffffd9 0x1',
              'prefix *0x07758550+0xffffffc0 0x40',
              'table_after 0x07758550 0x4']
    return '\n'.join(lines) + '\n'


def prepare(executable, output, guest_directory, mode):
    guest = _guest_path(guest_directory)
    if mode not in POINTS:
        raise ValueError('modo desconocido')
    executable = Path(executable)
    if executable.stat().st_size != EXE_SIZE:
        raise ValueError('el archivo no tiene el tamaño del cliente c32bbd5f')
    image = executable.read_bytes()
    if hashlib.sha256(image).hexdigest() != EXE_SHA256:
        raise ValueError('SHA-256 distinto del cliente c32bbd5f')
    fingerprints = {name: {'address': address, 'hex': read_va_bytes(image, address, size).hex()}
                    for name, address, size in SIGNATURES[mode]}
    manifest = {'schema': 1, 'session': uuid.uuid4().hex, 'mode': mode, 'exe_sha256': EXE_SHA256,
                'exe_size': EXE_SIZE, 'image_base': IMAGE_BASE, 'guest_directory': str(guest),
                'fingerprints': fingerprints, 'native_fidelity_verified': False, 'files': {}}
    files = render_commands(manifest, str(guest))
    files['passive-spec.txt'] = _passive_spec(fingerprints)
    output = _outside_git(output)
    output.mkdir(parents=True, exist_ok=False)
    for name, text in files.items():
        data = text.replace('\n', '\r\n').encode('ascii')
        (output / name).write_bytes(data)
        manifest['files'][name] = hashlib.sha256(data).hexdigest()
    (output / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
    return manifest


def _report(manifest):
    errors = []
    if manifest.get('schema') != 1 or manifest.get('mode') not in POINTS:
        errors.append('perfil de manifest desconocido')
    if manifest.get('exe_sha256') != EXE_SHA256:
        errors.append('el manifest no identifica c32bbd5f')
    return {'schema': 1, 'session': manifest.get('session'), 'mode': manifest.get('mode'),
            'status': 'INCOMPLETE', 'errors': errors, 'warnings': [],
            'native_fidelity_verified': False, 'rng_count_observed': False}


def _guard_errors(row):
    if not set((*GUARD_FIELDS, 'depth', 'own')).issubset(row):
        return ['falta procedencia del contador de la sombra']
    capacity = row['gcap']
    if not 0 < capacity <= MAX_GUARD_CAPACITY:
        return ['capacidad del guardia inválida o fuera del límite de captura']
    hashed = 0
    for shift in (0, 8, 16, 24):
        hashed = (hashed * row['gmult'] + (row['own'] >> shift & 255)) & POINTER_MASK
    valid = (row['gvalues'] != 0 and row['gkeys'] != 0 and row['shadow'] != 0
             and row['gkey'] == row['own'] and row['ghash'] == GUARD_HASH
             and row['gprobes'] < capacity and row['gslot'] < capacity
             and row['gslot'] == (hashed + row['gprobes']) % capacity
             and 0 < row['depth'] <= 255)
    return [] if valid else ['la sombra no acredita acceso al registro observado']


def _check_read(row):
    required = {'tid', 'own', 'item', 'table', 'address', 'index', 'id', 'dur',
                'flags', 'value', 'kind', 'mem', 'caller', 'depth'}
    if not required.issubset(row):
        return ['READ incompleto']
    errors = _guard_errors(row)
    checks = ((row['id'] == 0xffff, 'ID distinto de ffff'),
              (row['address'] == (row['table'] - 16) & POINTER_MASK, 'dirección distinta de table-16'),
              (row['index'] == 0xffffffeb, 'EAX no contiene 21*(-1) en 32 bits'),
              (row['value'] == (row['flags'] >> 2) & 3, 'valor distinto de flags[3:2]'),
              (row['kind'] == row['mem'] and row['kind'] <= 255, 'selector y memoria difieren'),
              (0 < row['dur'] <= 255 and row['depth'] > 0, 'durabilidad/acceso no acreditados'),
              (row['caller'] in (0x40e038, 0x550d72), 'caller desconocido'))
    errors.extend(message for passed, message in checks if not passed)
    slot, remainder = divmod(row['item'] - row['own'] - EQUIPMENT_OFFSET, ITEM_SIZE)
    if remainder or not 0 <= slot < 12:
        errors.append('el ítem no corresponde a los doce lugares del registro observado')
    row['slot'] = slot
    row['predicted_contribution'] = 5 * row['value'] if 1 <= row['kind'] <= 4 else 0
    return errors


def _fp_fields(text):
    fields = {'raw': text}
    for output, pattern in (('cw', r'\b(?:fpcw|fcw|cw)\s*=\s*([0-9a-f]+)'),
                            ('sw', r'\b(?:fpsw|fsw|sw)\s*=\s*([0-9a-f]+)')):
        match = re.search(pattern, text, re.I)
        if match:
            fields[output] = int(match[1], 16)
    return fields


def _prefix_fields(lines):
    data = bytearray()
    start = None
    for line in lines:
        parts = re.split(r'\s{2,}', line.strip(), maxsplit=2)
        if len(parts) < 2 or not re.fullmatch(r'[0-9a-fA-F`]+', parts[0]):
            raise ValueError('línea de prefijo sin dirección')
        address = int(parts[0].replace('`', ''), 16)
        tokens = parts[1].replace('-', ' ').split()
        if len(tokens) != 16 or any(not re.fullmatch(r'[0-9a-fA-F]{2}', token) for token in tokens):
            raise ValueError('prefijo ilegible o distinto de db x86 de 16 bytes por línea')
        if start is None:
            start = address
        if address != start + len(data):
            raise ValueError('prefijo discontinuo')
        data.extend(int(token, 16) for token in tokens)
    if len(data) != 64:
        raise ValueError('prefijo distinto de 64 bytes')
    return {'address': start, 'hex': data.hex()}


def _parse_block(line, block, events, report):
    if line in ('QC184_FP_BEGIN', 'QC184_PREFIX_BEGIN'):
        if block is not None or not events:
            report['errors'].append('bloque anidado o sin registro anterior')
        return [line, []], True
    if line in ('QC184_FP_END', 'QC184_PREFIX_END'):
        expected = line.replace('_END', '_BEGIN')
        if block is None or block[0] != expected or not events:
            report['errors'].append('fin de bloque sin comienzo correspondiente')
            return None, True
        try:
            if line == 'QC184_FP_END':
                events[-1]['fp'] = _fp_fields('\n'.join(block[1]))
            else:
                events[-1]['prefix'] = _prefix_fields(block[1])
        except ValueError as error:
            report['errors'].append(str(error))
        return None, True
    if block is not None:
        block[1].append(line)
        return block, True
    return block, False


def _parse_events(text, report):
    events, block = [], None
    tags = 'READ|BEGIN|END|WEAR_BEFORE|WEAR_FACTOR|WEAR_REDUCED|WEAR_SUM'
    for number, raw in enumerate(text.splitlines(), 1):
        line = raw.strip()
        block, consumed = _parse_block(line, block, events, report)
        if consumed:
            continue
        if match := re.fullmatch(rf'QC184_({tags}) (.+)', line):
            try:
                pairs = [pair.split('=', 1) for pair in match[2].split()]
                fields = dict(pairs)
                if len(fields) != len(pairs):
                    raise ValueError('campos duplicados')
                row = {key: int(value, 16) for key, value in fields.items()}
                if any(not 0 <= value <= POINTER_MASK for value in row.values()):
                    raise ValueError('campo fuera de 32 bits')
                events.append(dict(row, event=match[1]))
            except ValueError:
                report['errors'].append(f'registro mal formado en línea {number}')
        elif 'QC184_BLOCKED_' in line or 'QC184_STOP_BLOCKED' in line:
            report['errors'].append('el debugger informó un bloqueo de perfil o de limpieza')
        elif re.match(r'(Memory access error|Syntax error|Bad register|Unable to|Couldn.t resolve|'
                      r'Numeric expression missing|Operand error)', line, re.I):
            report['errors'].append(f'error del debugger en línea {number}')
    if block is not None:
        report['errors'].append('bloque FPU/prefijo truncado')
    return events


def _require(row, names, report):
    if not set(names).issubset(row):
        report['errors'].append(f'{row["event"]}: faltan campos obligatorios')
        return False
    fp = row.get('fp', {})
    if 'cw' not in fp or 'sw' not in fp:
        report['errors'].append(f'{row["event"]}: falta CW/SW del contexto FPU')
    elif fp['cw'] > 0xffff or fp['sw'] > 0xffff:
        report['errors'].append('CW/SW fuera de 16 bits')
    else:
        fp['exceptions_masked'] = fp['cw'] & 0x3f == 0x3f
    return True


def _check_prefix(row, report):
    prefix = row.get('prefix')
    if prefix is None or prefix['address'] != row['table'] - 64:
        report['errors'].append('READ sin el prefijo correspondiente')
        return
    if bytes.fromhex(prefix['hex'])[48] != row['mem']:
        report['errors'].append('el prefijo y el selector cambiaron entre lecturas')


def _bonus_flow(events, report):
    stacks, calls = {}, []
    words = tuple(f'w{x:x}' for x in (0x2a, 0x2c, 0x2e, 0x30, 0x32, 0x34, 0x36, 0x62, 0x64, 0x66))
    for index, row in enumerate(events):
        if row['event'] not in ('BEGIN', 'READ', 'END'):
            report['errors'].append('registro de otro modo en la captura bonus')
            continue
        required = ('tid', 'own') if row['event'] == 'READ' else (
            'tid', 'own', 'caller', 'pc', 'frame', 'depth', *GUARD_FIELDS, *words)
        if not _require(row, required, report):
            continue
        stack = stacks.setdefault(row['tid'], [])
        if row['event'] != 'READ':
            report['errors'].extend(_guard_errors(row))
        if row['event'] == 'BEGIN':
            if row['pc'] != 0x550d10 or row['depth'] == 0:
                report['errors'].append('BEGIN fuera de la entrada esperada o con registro cerrado')
            stack.append({'begin': index, 'row': row, 'reads': []})
        elif row['event'] == 'READ':
            errors = _check_read(row)
            report['errors'].extend(errors)
            if not errors:
                _check_prefix(row, report)
            if not stack or stack[-1]['row']['own'] != row['own']:
                report['errors'].append('READ fuera del cálculo de stats observado')
            else:
                stack[-1]['reads'].append(index)
        elif not stack:
            report['errors'].append('END sin BEGIN en el hilo')
        else:
            begin = stack.pop()
            if any(row[key] != begin['row'][key] for key in ('own', 'caller', 'frame')):
                report['errors'].append('BEGIN/END no corresponden al mismo frame/registro/caller')
            if row['pc'] not in (0x5513a3, 0x5513b1) or row['depth'] == 0:
                report['errors'].append('END fuera del retorno esperado o con registro cerrado')
            calls.append({'begin_event': begin['begin'], 'read_events': begin['reads'], 'end_event': index})
    if any(stacks.values()):
        report['errors'].append('hay cálculos de stats sin retorno observado')
    report['stats_calls'] = calls


def _wear_denominator(row):
    level = row['arg3'] >> 3 & 15
    denominator = (row['arg2'] & 255) + min(level, 4) + 2 * max(level - 4, 0)
    ancient = row['arg5'] if row['arg5'] < 0x80000000 else row['arg5'] - 0x100000000
    remainder = abs(ancient) % 4 * (-1 if ancient < 0 else 1)
    if remainder in (1, 2):
        denominator += 20
    elif row['arg4'] & 0x3f:
        denominator += 15
    return denominator


def _wear_before(row, report):
    required = ('value', 'value_raw', 'arg1', 'arg2', 'arg3', 'arg4', 'arg5',
                'base', 'dur', 'table', 'address', 'mem_base')
    if not _require(row, required, report):
        return False
    if (row['value_raw'] & 0xffff) != row['value'] or row['value'] != 0:
        report['errors'].append('54e680 no dejó AX=0 para el lugar vacío')
    if row['base'] != (row['arg2'] & 255) or row['dur'] != (row['arg1'] & 255) or row['dur'] == 0:
        report['errors'].append('argumentos crudos no corresponden a los bytes semánticos')
    if row['address'] != row['table'] - 39 or row['base'] != row['mem_base']:
        report['errors'].append('argumento de durabilidad base distinto de table-39')
    if row['slot'] == 7 and (row['arg4'] != 0 or row['arg5'] != 0):
        report['errors'].append('las alas no pasaron los dos ceros documentados')
    row['predicted_max_durability'] = _wear_denominator(row)
    return True


def _wear_flow(events, report):
    pending, calls = {}, []
    for index, row in enumerate(events):
        if row['event'] == 'WEAR_SUM':
            if not _require(row, ('tid', 'own', 'frame', 'sum', 'w62'), report):
                continue
            keys = [key for key in pending if key[:3] == (row['tid'], row['own'], row['frame'])]
            for key in keys:
                entry = pending.pop(key)
                if entry['stage'] != 3:
                    report['errors'].append('suma final antes de completar BEFORE/FACTOR/REDUCED')
                else:
                    calls.append(dict(entry, sum_event=index))
            continue
        if row['event'] not in ('WEAR_BEFORE', 'WEAR_FACTOR', 'WEAR_REDUCED') or not _require(
                row, ('tid', 'own', 'frame', 'slot', 'id'), report):
            report['errors'].append('registro wear incompleto o de otro modo')
            continue
        key = (row['tid'], row['own'], row['frame'], row['slot'])
        if row['id'] != 0xffff or not 1 <= row['slot'] <= 7:
            report['errors'].append('ranura/ID distintos de la rama vacía 1..7')
        if row['event'] == 'WEAR_BEFORE':
            if key in pending or not _wear_before(row, report):
                report['errors'].append('BEFORE duplicado o sin argumentos completos')
            pending[key] = {'stage': 1, 'before_event': index, 'value_raw': row.get('value_raw')}
        elif row['event'] == 'WEAR_FACTOR':
            if key not in pending or pending[key]['stage'] != 1:
                report['errors'].append('FACTOR sin BEFORE correspondiente')
                continue
            if not re.search(r'\bst0\s*=', row.get('fp', {}).get('raw', ''), re.I):
                report['errors'].append('FACTOR sin retorno ST0 observado')
            pending[key].update(stage=2, factor_event=index)
        elif key not in pending or pending[key]['stage'] != 2:
            report['errors'].append('REDUCED sin BEFORE/FACTOR correspondiente')
        elif _require(row, ('reduced', 'low'), report):
            if row['reduced'] != pending[key]['value_raw'] or row['low'] != 0 or row['reduced'] & 0xffff:
                report['errors'].append('reducción distinta del residuo alto con WORD bajo cero')
            pending[key].update(stage=3, reduced_event=index)
    if pending:
        report['errors'].append('hay ranuras wear sin completar hasta la publicación de word62')
    if not calls:
        report['errors'].append('ningún cálculo wear completo de una ranura vacía')
    report['wear_calls'] = calls


def analyze_trace(text, manifest):
    report = _report(manifest)
    headers = re.findall(r'^QC184_SESSION id=(\S+) mode=(\S+) schema=1\s*$', text, re.M)
    if headers != [(manifest.get('session'), manifest.get('mode'))]:
        report['errors'].append('sesión/modo ausente, duplicado o distinto del bundle')
    lines = {line.strip() for line in text.splitlines()}
    if not {'QC184_PROFILE_OK', 'QC184_ARMED_OK', 'QC184_COMPLETE'}.issubset(lines):
        report['errors'].append('falta perfil, armado o cierre completo de la captura')
    pids = re.findall(r'^QC184_PROCESS pid=([0-9a-fA-F]+)\s*$', text, re.M)
    if len(pids) != 1 or not 0 < int(pids[0], 16) <= POINTER_MASK:
        report['errors'].append('falta un PID único del proceso capturado')
    else:
        report['pid'] = int(pids[0], 16)
    events = _parse_events(text, report)
    report['events'] = events
    report['reads'] = [row for row in events if row['event'] == 'READ']
    if manifest.get('mode') == 'bonus':
        _bonus_flow(events, report)
        if not report['reads']:
            report['errors'].append('no se observó la rama solicitada; no cuenta como aprobada')
    else:
        _wear_flow(events, report)
    report['warnings'].append('CAPTURED acredita consistencia de captura; interpretar ST0 y comparar MuRet por separado')
    if not report['errors']:
        report['status'] = 'CAPTURED'
    return report


def analyze_passive(directory, manifest):
    """Valida s31mem: exit0 o bytes cero nunca sustituyen paginas/ilegibles/ok."""
    report = _report(manifest)
    directory = Path(directory)
    log = (directory / 's31mem.txt').read_text(encoding='latin-1')
    entries = {}
    pattern = r'^(\w+) ([0-9a-fA-F]+) ([0-9a-fA-F]+) paginas=(\d+) ilegibles=(\d+) ok$'
    for name, address, size, pages, bad in re.findall(pattern, log, re.M):
        entries[name] = (int(address, 16), int(size, 16), int(pages), int(bad))
    if 'ERROR' in log or not log.rstrip().endswith('fin') or not re.match(r'pid [1-9][0-9]*\s', log):
        report['errors'].append('log pasivo con error, PID ausente o sin fin')
    data = {}
    sizes = dict(table_before=4, selector=1, wear_base=1, prefix=64, table_after=4)
    sizes.update({name: len(bytes.fromhex(item['hex'])) for name, item in manifest['fingerprints'].items()})
    for name, size in sizes.items():
        entry = entries.get(name)
        path = directory / f'{name}.bin'
        if entry is None or entry[1] != size or entry[2] <= 0 or entry[3] != 0 or not path.is_file():
            report['errors'].append(f'{name}: lectura incompleta o ilegible')
            continue
        data[name] = path.read_bytes()
        if len(data[name]) != size:
            report['errors'].append(f'{name}: tamaño de archivo distinto del log')
    if report['errors']:
        return report
    return _finish_passive(report, data, entries, manifest)


def _finish_passive(report, data, entries, manifest):
    before = int.from_bytes(data['table_before'], 'little')
    after = int.from_bytes(data['table_after'], 'little')
    if before != after or before < 64:
        report['errors'].append('puntero de tabla nulo o cambió durante la muestra')
    for name, offset in (('selector', -16), ('wear_base', -39), ('prefix', -64)):
        if entries[name][0] != (before + offset) & POINTER_MASK:
            report['errors'].append(f'{name}: dirección resuelta inesperada')
    for name in ('table_before', 'table_after'):
        if entries[name][0] != ITEM_TABLE_POINTER:
            report['errors'].append(f'{name}: global incorrecto')
    for name, expected in manifest['fingerprints'].items():
        if data[name].hex() != expected['hex'] or entries[name][0] != expected['address']:
            report['errors'].append(f'{name}: firma cargada distinta')
    if data['prefix'][48] != data['selector'][0] or data['prefix'][25] != data['wear_base'][0]:
        report['errors'].append('los bytes cambiaron entre las lecturas del prefijo')
    report.update(table=before, selector=data['selector'][0], wear_base=data['wear_base'][0])
    report['files'] = {name: hashlib.sha256(value).hexdigest() for name, value in data.items()}
    report['warnings'].append('lecturas secuenciales, no snapshot atómico; verificar PID/EXE del guest')
    if not report['errors']:
        report['status'] = 'CAPTURED'
    return report


def _load_manifest(bundle):
    bundle = Path(bundle)
    manifest = json.loads((bundle / 'manifest.json').read_text(encoding='utf-8'))
    for name, expected in manifest.get('files', {}).items():
        if Path(name).name != name or hashlib.sha256((bundle / name).read_bytes()).hexdigest() != expected:
            raise ValueError(f'archivo del bundle distinto: {name}')
    return manifest


def _write_report(path, report):
    path = _outside_git(path)
    with path.open('x', encoding='utf-8') as output:
        json.dump(report, output, indent=2)
        output.write('\n')
    print(f'{report["status"]}: {path}')
    return 0 if report['status'] == 'CAPTURED' else 3


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='command', required=True)
    generate = sub.add_parser('prepare', help='leer el EXE como datos y generar un bundle nuevo')
    generate.add_argument('--main-exe', type=Path, required=True)
    generate.add_argument('--output', type=Path, required=True)
    generate.add_argument('--guest-directory', required=True)
    generate.add_argument('--mode', choices=POINTS, default='bonus')
    for name in ('analyze-debug', 'analyze-passive'):
        analyze = sub.add_parser(name)
        analyze.add_argument('--bundle', type=Path, required=True)
        analyze.add_argument('--input', type=Path, required=True)
        analyze.add_argument('--output', type=Path, required=True)
    args = parser.parse_args(argv)
    try:
        if args.command == 'prepare':
            prepare(args.main_exe, args.output, args.guest_directory, args.mode)
            print(f'Bundle preparado; original no ejecutado: {args.output}')
            return 0
        manifest = _load_manifest(args.bundle)
        if args.command == 'analyze-passive':
            report = analyze_passive(args.input, manifest)
        else:
            if args.input.stat().st_size > MAX_LOG_SIZE:
                raise ValueError('log mayor que 64 MiB; usar una sesión acotada')
            raw = args.input.read_bytes()
            text = raw.decode('utf-16') if raw.startswith((b'\xff\xfe', b'\xfe\xff')) else raw.decode('latin-1')
            report = analyze_trace(text, manifest)
            report['log_sha256'] = hashlib.sha256(raw).hexdigest()
        return _write_report(args.output, report)
    except (OSError, ValueError, KeyError, struct.error) as error:
        print(f'Error: {error}', file=sys.stderr)
        return 1


if __name__ == '__main__':
    raise SystemExit(main())
