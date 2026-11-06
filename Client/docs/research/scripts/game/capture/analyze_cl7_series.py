#!/usr/bin/env python3
"""Análisis nativo pasivo CL7: valida captura completa, sin emulación ni VM."""

import argparse
import hashlib
import json
import math
import re
import statistics
import struct
from collections import Counter
from pathlib import Path


EXPECTED = 2250
GLOBAL = {
    'map': (1, 8),
    'scene': (1, 8),
    'worldTime': (1, 8),
    'angles': (3, 8),
    'position': (3, 8),
    'zoom': (1, 8),
    'distance': (1, 8),
    'targetDistance': (1, 8),
    'farPlane': (1, 8),
    'nearPlane': (1, 8),
    'fieldOfView': (1, 8),
    'fixedHeight': (1, 8),
    'level': (1, 4),
    'effectTarget': (3, 8),
    'shake': (1, 8),
    'renderSize': (2, 8),
    'tourState': (1, 8),
    'cullingLevel': (1, 8),
    'mouseUpdate': (1, 8),
    'mouseUpdateMax': (1, 8),
    'pubLayer': (1, 8),
    'storm': (1, 8),
    'ticksBefore': (1, 8),
    'ticksAfter': (1, 8),
    'heroPointer': (1, 8),
    'mapAfter': (1, 8),
    'sceneAfter': (1, 8),
    'heroPointerAfter': (1, 8),
    'ticksEntitiesAfter': (1, 8),
}
HERO = {
    'heroPosition': (3, 8),
    'heroAngles': (3, 8),
    'heroAction': (1, 2),
    'heroSafe': (1, 2),
    'heroMovement': (1, 2),
    'heroMovementType': (1, 2),
    'heroPath': (33, 2),
}
VISUAL_CELL = {
    'visualCell': (2, 8),
    'visualCellAttribute': (1, 4),
    'visualCellLayer1': (1, 2),
}
ENTITIES = {
    'birdAlive': (5, 2),
    'birdType': (5, 4),
    'fishAlive': (3, 2),
    'fishType': (3, 4),
}
CAMERA_STATE_FIELDS = (
    'angles', 'position', 'zoom', 'distance', 'targetDistance', 'farPlane',
    'nearPlane', 'fieldOfView', 'fixedHeight', 'level', 'effectTarget',
    'shake',
)
LIMITS = [
    'Lecturas RPM secuenciales, no atomicas, aun con boundaryStable=1.',
    'GetTickCount es grueso: un intervalo registrado0ms '
    'no demuestra duracion nula.',
    'La cadencia del lector no mide FPS del cliente '
    'ni cantidad exacta de pasos logicos.',
    'La celda visual no es la celda logica protegida; '
    'el path puede persistir al quedar quieto.',
    'core_print_entities_ms incluye impresion '
    'y lecturas adicionales de entidades.',
    'Conteos observados solo describen esos5slots '
    'de aves y3de peces en esta serie.',
    'No se ejecuto emulacion ni se infirio comportamiento no observado.',
]


def words(sample, key, shape=None):
    if key not in sample:
        raise ValueError('missing ' + key)
    values = sample[key].split(',')
    if shape:
        count, width = shape
        pattern = '[0-9a-f]{%d}' % width
        if len(values) != count or any(
            not re.fullmatch(pattern, value) for value in values
        ):
            raise ValueError('bad width/count ' + key)
    return [int(value, 16) for value in values]


def floats(sample, key):
    values = [
        struct.unpack('<f', struct.pack('<I', value))[0]
        for value in words(sample, key)
    ]
    if not all(math.isfinite(value) for value in values):
        raise ValueError('nonfinite ' + key)
    return values


def check_fields(sample, fields):
    for key, shape in fields.items():
        words(sample, key, shape)


def boundary_stable(sample):
    unchanged = all(
        sample[before] == sample[after]
        for before, after in (
            ('map', 'mapAfter'), ('scene', 'sceneAfter'),
            ('heroPointer', 'heroPointerAfter'),
        )
    )
    if (sample['heroPresent'] == '1'
            and sample['heroPointer'] == sample['heroPointerAfter']):
        words(sample, 'heroPositionAfter', (3, 8))
        unchanged = unchanged and (
            sample['heroPosition'] == sample['heroPositionAfter']
        )
    return unchanged


def check_sample(sample):
    check_fields(sample, GLOBAL)
    for key in (
        'heroPresent', 'visualCellPresent', 'entitiesSampled',
        'boundaryStable',
    ):
        if sample.get(key) not in ('0', '1'):
            raise ValueError('bad flag ' + key)
    if bool(words(sample, 'heroPointer')[0]) != (sample['heroPresent'] == '1'):
        raise ValueError('pointer/presence disagreement')
    if sample['heroPresent'] == '1':
        check_fields(sample, HERO)
    if sample['visualCellPresent'] == '1':
        check_fields(sample, VISUAL_CELL)
        if any(value > 255 for value in words(sample, 'visualCell')):
            raise ValueError('cell out of bounds')
    if sample['entitiesSampled'] == '1':
        check_fields(sample, ENTITIES)
    if boundary_stable(sample) != (sample['boundaryStable'] == '1'):
        raise ValueError('boundary flag disagreement')


def check_metadata(metadata):
    if (type(metadata.get('program_exit')) is not int
            or metadata['program_exit'] != 0):
        raise ValueError('metadata exit not zero')
    if (metadata.get('numerical_samples') != EXPECTED
            or metadata.get('numerical_end') is not True
            or metadata.get('numerical_error') is not False):
        raise ValueError('metadata not complete/error-free')


def parse_blocks(lines):
    header = {}
    samples = []
    current = None
    completed = False
    ended = False
    for line in lines:
        if ended:
            raise ValueError('trailing content')
        if line.startswith('sample '):
            if (current is not None or completed
                    or line != 'sample %d' % len(samples)):
                raise ValueError('bad sample sequence')
            current = {'id': len(samples)}
        elif line == 'end_sample':
            if current is None:
                raise ValueError('unexpected end_sample')
            check_sample(current)
            samples.append(current)
            current = None
        elif line == 'end':
            if (current is not None or not completed
                    or len(samples) != EXPECTED):
                raise ValueError('premature end')
            ended = True
        elif line.startswith('samplesCompleted='):
            if (current is not None or completed or len(samples) != EXPECTED
                    or line != 'samplesCompleted=000008ca'):
                raise ValueError('bad count/footer')
            completed = True
        elif '=' in line:
            key, value = line.split('=', 1)
            target = current if current is not None else header
            if key in target or (current is None and samples):
                raise ValueError('duplicate/misplaced field ' + key)
            target[key] = value
        elif (current is None and not samples
                and line.startswith((
                    'field ', 'heroField ', 'derived ', 'entity ',
                ))):
            continue
        else:
            raise ValueError('unknown schema line')
    if not ended or current is not None or len(samples) != EXPECTED:
        raise ValueError('incomplete capture')
    return header, samples


def parse(text, metadata):
    check_metadata(metadata)
    lines = text.splitlines()
    if not lines or lines[0] != 'camera_series 1' or not text.endswith('\n'):
        raise ValueError('bad header/truncated ending')
    if any(re.search(r'\berror\b', line, re.I) for line in lines):
        raise ValueError('error marker')
    header, samples = parse_blocks(lines[1:])
    if (header.get('samplesPlanned') != '000008ca'
            or header.get('rateHz') != '00000019'
            or header.get('sequence') != 'non_atomic'):
        raise ValueError('unexpected plan/rate')
    return header, samples


def dist(values):
    values = sorted(values)
    if not values:
        return {'count': 0}
    return {
        'count': len(values),
        'min': values[0],
        'median': statistics.median(values),
        'mean': statistics.mean(values),
        'p95': values[math.ceil(len(values) * .95) - 1],
        'max': values[-1],
    }


def sorted_counts(values):
    return dict(sorted(Counter(values).items()))


def hist(samples, key):
    return sorted_counts(sample[key] for sample in samples if key in sample)


def delta(before, after):
    return (after - before) & 0xffffffff


def endpoint(sample):
    result = {
        'id': sample['id'],
        'ticks': words(sample, 'ticksBefore')[0],
        'map': words(sample, 'map')[0],
        'scene': words(sample, 'scene')[0],
    }
    if sample['heroPresent'] == '1':
        result.update(
            position=floats(sample, 'heroPosition'),
            angles=floats(sample, 'heroAngles'),
            action=sample['heroAction'],
            movement=sample['heroMovement'],
            path_state=words(sample, 'heroPath')[:3],
        )
    if sample['visualCellPresent'] == '1':
        result['visual_cell'] = words(sample, 'visualCell')
    return result


def same_hero_context(previous, current):
    return (
        previous['heroPresent'] == current['heroPresent'] == '1'
        and words(previous, 'heroPointer')[0] != 0
        and all(previous[key] == current[key]
                for key in ('heroPointer', 'map', 'scene'))
    )


def hero_position_changes(samples):
    return [
        current['id'] for previous, current in zip(samples, samples[1:])
        if same_hero_context(previous, current)
        and floats(previous, 'heroPosition') != floats(current, 'heroPosition')
    ]


def timing_statistics(samples):
    ticks = [words(sample, 'ticksBefore')[0] for sample in samples]
    gaps = [delta(before, after) for before, after in zip(ticks, ticks[1:])]
    span = sum(gaps)
    return {
        'first_to_last_start_ms': span,
        'observer_rate_hz': (len(samples) - 1) * 1000 / span if span else None,
        'sample_start_gaps_ms': dist(gaps),
        'core_read_ms': dist([
            delta(words(sample, 'ticksBefore')[0],
                  words(sample, 'ticksAfter')[0])
            for sample in samples
        ]),
        'core_print_entities_ms': dist([
            delta(words(sample, 'ticksBefore')[0],
                  words(sample, 'ticksEntitiesAfter')[0])
            for sample in samples
        ]),
    }


def hero_statistics(samples):
    present = [sample for sample in samples if sample['heroPresent'] == '1']
    positions = [floats(sample, 'heroPosition') for sample in present]
    changes = hero_position_changes(samples)
    result = {
        'present_samples': len(present),
        'position_changes_observed': len(changes),
        'first_changed_sample': changes[0] if changes else None,
        'last_changed_sample': changes[-1] if changes else None,
        'unique_positions': len({tuple(position) for position in positions}),
        'unique_visual_cells': len({
            sample['visualCell'] for sample in samples
            if sample['visualCellPresent'] == '1'
        }),
        'action_counts': hist(present, 'heroAction'),
        'movement_counts': hist(present, 'heroMovement'),
        'movement_type_counts': hist(present, 'heroMovementType'),
        'safe_counts': hist(present, 'heroSafe'),
        'path_num_counts': sorted_counts(
            str(words(sample, 'heroPath')[2]) for sample in present
        ),
        'path_current_counts': sorted_counts(
            str(words(sample, 'heroPath')[0]) for sample in present
        ),
        'path_subpoint_counts': sorted_counts(
            str(words(sample, 'heroPath')[1]) for sample in present
        ),
    }
    if positions:
        # Resumen bruto de posiciones; no acredita continuidad entre segmentos.
        result['bounds_xyz'] = [
            [min(position[axis] for position in positions),
             max(position[axis] for position in positions)]
            for axis in range(3)
        ]
        result['net_displacement_xyz'] = [
            positions[-1][axis] - positions[0][axis] for axis in range(3)
        ]
    return result


def entity_statistics(samples):
    observed = [
        sample for sample in samples if sample['entitiesSampled'] == '1'
    ]
    return {
        'observations': len(observed),
        'sample_ids': [sample['id'] for sample in observed],
        'bird_alive_counts': sorted_counts(
            str(sum(value != 0 for value in words(sample, 'birdAlive')))
            for sample in observed
        ),
        'fish_alive_counts': sorted_counts(
            str(sum(value != 0 for value in words(sample, 'fishAlive')))
            for sample in observed
        ),
        'bird_types': hist(observed, 'birdType'),
        'fish_types': hist(observed, 'fishType'),
    }


def analyze(samples, metadata):
    unstable_ids = [
        sample['id'] for sample in samples if sample['boundaryStable'] == '0'
    ]
    return {
        'validation': {
            'complete': True,
            'program_exit': 0,
            'samples': len(samples),
            'end_sample_blocks': len(samples),
            'footer_end': True,
            'error_markers': False,
        },
        'timing': timing_statistics(samples),
        'boundary_unstable': {'count': len(unstable_ids), 'ids': unstable_ids},
        'start': endpoint(samples[0]),
        'end': endpoint(samples[-1]),
        'maps': hist(samples, 'map'),
        'scenes': hist(samples, 'scene'),
        'unique_raw_camera_states': {
            key: len({sample[key] for sample in samples})
            for key in CAMERA_STATE_FIELDS
        },
        'hero': hero_statistics(samples),
        'entities': entity_statistics(samples),
        'screenshot_frames_in_metadata': len(metadata.get('frames', [])),
        'limits': list(LIMITS),
    }


def negatives(text, metadata):
    tests = {
        'truncate_in_block': text[:text.index('end_sample',
                                             text.index('sample 1124\n'))],
        'missing_end': text[:text.rfind('end\n')],
        'error_marker_after_footer': text + 'error field=ReadProcessMemory\n',
        'incorrect_count': text.replace('samplesCompleted=000008ca',
                                        'samplesCompleted=000008c9'),
    }
    result = {}
    for name, broken in tests.items():
        try:
            parse(broken, metadata)
        except ValueError as error:
            result[name] = {'rejected': True, 'reason': str(error)}
        else:
            raise AssertionError('negative test accepted ' + name)
    bad = dict(metadata, program_exit=1)
    try:
        parse(text, bad)
    except ValueError as error:
        result['metadata_exit_nonzero'] = {
            'rejected': True, 'reason': str(error),
        }
    else:
        raise AssertionError('nonzero exit accepted')
    return result


def motion_description(hero):
    if hero['unique_positions'] == 0:
        return (
            'sin observaciones del heroe',
            'No hay posiciones observadas del heroe '
            'para evaluar su desplazamiento.',
        )
    if hero['position_changes_observed'] > 0:
        return (
            'con movimiento observado',
            'Se observo desplazamiento del heroe. '
            'Las muestras no equivalen a pasos o frames consecutivos.',
        )
    if hero['unique_positions'] > 1:
        return (
            'con continuidad del heroe no acreditada',
            'La continuidad del heroe entre posiciones distintas '
            'no esta acreditada. Estos datos no permiten atribuirlas '
            'a desplazamiento del mismo heroe.',
        )
    return (
        'sin traslado observado del heroe',
        'No se observo desplazamiento del heroe en esta serie. '
        'Estos datos no acreditan una vuelta caminando.',
    )


def report_observations(result):
    hero = result['hero']
    entities = result['entities']
    return [
        'Inicio: `' + json.dumps(result['start'], ensure_ascii=False) + '`.',
        'Final: `' + json.dumps(result['end'], ensure_ascii=False) + '`.',
        f"Posiciones distintas:{hero['unique_positions']}; "
        f"cambios observados:{hero['position_changes_observed']}; "
        f"celdas visuales distintas:{hero['unique_visual_cells']}.",
        'Acciones(hex→muestras): `' + json.dumps(hero['action_counts']) + '`.',
        'Movement(hex→muestras): `' + json.dumps(hero['movement_counts'])
        + '`; PathNum(decimal→muestras): `'
        + json.dumps(hero['path_num_counts']) + '`.',
        f"Observaciones de entidades:{entities['observations']}; "
        'aves vivas(cantidad→observaciones):'
        f"{entities['bird_alive_counts']}; "
        f"peces:{entities['fish_alive_counts']}.",
        'Frames screenshot declarados por metadata:'
        f"{result['screenshot_frames_in_metadata']}; "
        'este analisis no los interpreta.',
    ]


def report(result):
    timing = result['timing']
    title, description = motion_description(result['hero'])
    return '\n'.join([
        '# Serie CL7: captura completa, ' + title, '',
        description, '',
        'Captura completa:2250 bloques cerrados, footer end, '
        'sin error y metadata program_exit=0.',
        'Intervalo entre primera y ultima lectura:'
        f"{timing['first_to_last_start_ms']}ms; cadencia observada del lector:"
        f"{timing['observer_rate_hz']:.6f}Hz. No son FPS del cliente.",
        f"Intervalo RPM core(ms):{timing['core_read_ms']}. "
        'Muestras con frontera cambiante:'
        f"{result['boundary_unstable']['count']}.", '',
        *report_observations(result), '',
        'Pruebas negativas: truncacion dentro de bloque, falta de end, '
        'error despues del footer, '
        'count incorrecto y exit no cero rechazados.', '',
        *['- ' + limit for limit in result['limits']], '',
    ])


def write_results(result, json_path, report_path):
    json_path.write_text(json.dumps(
        result, indent=2, ensure_ascii=False, allow_nan=False
    ) + '\n', encoding='utf-8')
    report_path.write_text(report(result), encoding='utf-8')
    print(json.dumps({
        'valid': True,
        'samples': result['validation']['samples'],
        'observer_rate_hz': result['timing']['observer_rate_hz'],
        'position_changes': result['hero']['position_changes_observed'],
        'unique_visual_cells': result['hero']['unique_visual_cells'],
        'boundary_unstable': result['boundary_unstable']['count'],
        'json': str(json_path),
    }, ensure_ascii=False))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('series', type=Path)
    parser.add_argument('--metadata', type=Path, required=True)
    parser.add_argument('--json', type=Path, required=True)
    parser.add_argument('--report', type=Path, required=True)
    args = parser.parse_args()
    if args.series.name == 'progress.txt':
        parser.error('progress.txt is never a final capture')
    raw = args.series.read_bytes()
    text = raw.decode('utf-8')
    metadata = json.loads(args.metadata.read_text(encoding='utf-8'))
    try:
        _, samples = parse(text, metadata)
        result = analyze(samples, metadata)
        # Los negativos usan LF tras validar primero el archivo final original.
        result['negative_tests'] = negatives(
            '\n'.join(text.splitlines()) + '\n', metadata
        )
    except (ValueError, KeyError) as error:
        parser.error(str(error))
    result['source'] = {
        'file': str(args.series),
        'sha256': hashlib.sha256(raw).hexdigest(),
        'metadata': str(args.metadata),
        'metadata_sha256': hashlib.sha256(
            args.metadata.read_bytes()
        ).hexdigest(),
    }
    write_results(result, args.json, args.report)


if __name__ == '__main__':
    main()
