"""Capturas sintéticas unitarias: no son evidencia del cliente original."""

import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

from analyze_cl7_series import analyze, negatives, parse, report


SAMPLE_COUNT = 2250
METADATA = {
    'program_exit': 0,
    'numerical_samples': SAMPLE_COUNT,
    'numerical_end': True,
    'numerical_error': False,
}


SAMPLE_FIELDS = {
    'map': '00000000',
    'scene': '00000005',
    'worldTime': '00000000',
    'angles': '00000000,00000000,00000000',
    'position': '00000000,00000000,00000000',
    'zoom': '00000000',
    'distance': '00000000',
    'targetDistance': '00000000',
    'farPlane': '00000000',
    'nearPlane': '00000000',
    'fieldOfView': '00000000',
    'fixedHeight': '00000000',
    'level': '0000',
    'effectTarget': '00000000,00000000,00000000',
    'shake': '00000000',
    'renderSize': '00000000,00000000',
    'tourState': '00000000',
    'cullingLevel': '00000000',
    'mouseUpdate': '00000000',
    'mouseUpdateMax': '00000000',
    'pubLayer': '00000000',
    'storm': '00000000',
    'heroPresent': '1',
    'heroPointer': '00000001',
    'heroPosition': '00000000,00000000,00000000',
    'heroAngles': '00000000,00000000,00000000',
    'heroAction': '01',
    'heroSafe': '00',
    'heroMovement': '00',
    'heroMovementType': '00',
    'heroPath': ','.join(['00', '00', '05'] + ['00'] * 30),
    'visualCellPresent': '0',
    'entitiesSampled': '0',
    'boundaryStable': '1',
}


def make_sample(sample_id, overrides):
    ticks = f'{sample_id * 40:08x}'
    sample = {
        **SAMPLE_FIELDS,
        'ticksBefore': ticks,
        'ticksAfter': ticks,
        'ticksEntitiesAfter': ticks,
    }
    sample.update(overrides)
    sample['mapAfter'] = sample['map']
    sample['sceneAfter'] = sample['scene']
    sample['heroPointerAfter'] = sample['heroPointer']
    if sample['heroPresent'] == '1':
        sample['heroPositionAfter'] = sample['heroPosition']
    else:
        for key in (
            'heroPosition', 'heroAngles', 'heroAction', 'heroSafe',
            'heroMovement', 'heroMovementType', 'heroPath',
        ):
            del sample[key]
    return sample


def make_capture(overrides=None):
    overrides = overrides or {}
    lines = [
        'camera_series 1',
        'samplesPlanned=000008ca',
        'rateHz=00000019',
        'sequence=non_atomic',
    ]
    for sample_id in range(SAMPLE_COUNT):
        lines.append(f'sample {sample_id}')
        sample = make_sample(sample_id, overrides.get(sample_id, {}))
        lines.extend(f'{key}={value}' for key, value in sample.items())
        lines.append('end_sample')
    lines.extend(('samplesCompleted=000008ca', 'end', ''))
    return '\n'.join(lines)


def analyze_capture(overrides=None):
    _, samples = parse(make_capture(overrides), METADATA)
    result = analyze(samples, METADATA)
    return result, report(result)


class SeriesAnalysisTest(unittest.TestCase):
    def test_cli_reads_and_writes_utf8_under_an_ascii_locale(self):
        environment = {
            **os.environ,
            'LC_ALL': 'C',
            'LANG': 'C',
            'PYTHONUTF8': '0',
            'PYTHONCOERCECLOCALE': '0',
        }
        script = Path(__file__).with_name('analyze_cl7_series.py')
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            capture = directory / 'capture.txt'
            metadata = directory / 'metadata.json'
            output = directory / 'analysis.json'
            summary = directory / 'report.md'
            capture.write_text(make_capture().replace(
                'sequence=non_atomic', 'sequence=non_atomic\nnote=cámara'
            ), encoding='utf-8')
            for note in ('camera', 'cámara'):
                with self.subTest(metadata_note=note):
                    metadata.write_text(json.dumps(
                        {**METADATA, 'note': note}, ensure_ascii=False
                    ), encoding='utf-8')
                    result = subprocess.run([
                        sys.executable, str(script), str(capture),
                        '--metadata', str(metadata), '--json', str(output),
                        '--report', str(summary),
                    ], env=environment, capture_output=True, check=False)
                    self.assertEqual(result.returncode, 0, result.stderr)
                    self.assertEqual(result.stderr, b'')
                    self.assertTrue(json.loads(result.stdout)['valid'])
                    analysis = json.loads(output.read_bytes().decode('utf-8'))
                    self.assertEqual(analysis['validation']['samples'], SAMPLE_COUNT)
                    self.assertEqual(analysis['source']['sha256'], hashlib.sha256(
                        capture.read_bytes()
                    ).hexdigest())
                    report_bytes = summary.read_bytes()
                    self.assertIn('→'.encode('utf-8'), report_bytes)
                    self.assertIn('Acciones(hex→muestras)', report_bytes.decode('utf-8'))

    def test_stationary_capture_does_not_claim_a_walk(self):
        result, text = analyze_capture()
        self.assertEqual(result['hero']['position_changes_observed'], 0)
        self.assertIn('No se observo desplazamiento', text)

    def test_presence_gap_at_same_position_does_not_count_as_motion(self):
        result, text = analyze_capture({
            1124: {'heroPresent': '0', 'heroPointer': '00000000'},
        })
        self.assertEqual(result['hero']['present_samples'], 2249)
        self.assertEqual(result['hero']['unique_positions'], 1)
        self.assertEqual(result['hero']['position_changes_observed'], 0)
        self.assertIsNone(result['hero']['first_changed_sample'])
        self.assertIsNone(result['hero']['last_changed_sample'])
        self.assertIn('No se observo desplazamiento', text)
        self.assertNotIn('Se observo desplazamiento', text)

    def test_appearance_and_disappearance_do_not_count_as_motion(self):
        for missing_id in (0, 2249):
            with self.subTest(missing_id=missing_id):
                result, text = analyze_capture({
                    missing_id: {
                        'heroPresent': '0', 'heroPointer': '00000000',
                    },
                })
                self.assertEqual(
                    result['hero']['position_changes_observed'], 0
                )
                self.assertNotIn('Se observo desplazamiento', text)

    def test_different_positions_across_a_gap_do_not_prove_continuity(self):
        overrides = {
            sample_id: {'heroPresent': '0', 'heroPointer': '00000000'}
            for sample_id in range(1, 2249)
        }
        overrides[2249] = {'heroPosition': '3f800000,00000000,00000000'}
        result, text = analyze_capture(overrides)
        hero = result['hero']
        self.assertEqual(hero['present_samples'], 2)
        self.assertEqual(hero['position_changes_observed'], 0)
        self.assertEqual(hero['unique_positions'], 2)
        self.assertEqual(
            hero['bounds_xyz'], [[0.0, 1.0], [0.0, 0.0], [0.0, 0.0]]
        )
        self.assertEqual(hero['net_displacement_xyz'], [1.0, 0.0, 0.0])
        self.assertIn('continuidad', text)
        self.assertNotIn('Se observo desplazamiento', text)
        self.assertNotIn('No se observo desplazamiento', text)
        self.assertNotIn('sin traslado observado', text)

    def test_new_identity_map_or_scene_does_not_count_as_motion(self):
        changes = (
            {'heroPointer': '00000002'},
            {'map': '00000001'},
            {'scene': '00000004'},
        )
        for change in changes:
            with self.subTest(change=change):
                overrides = {
                    sample_id: {
                        **change,
                        'heroPosition': '3f800000,00000000,00000000',
                    }
                    for sample_id in range(1125, SAMPLE_COUNT)
                }
                result, text = analyze_capture(overrides)
                self.assertEqual(
                    result['hero']['position_changes_observed'], 0
                )
                self.assertIn('continuidad', text)
                self.assertNotIn('Se observo desplazamiento', text)

    def test_numeric_signed_zero_is_not_displacement(self):
        result, text = analyze_capture({
            1125: {'heroPosition': '80000000,00000000,00000000'},
        })
        self.assertEqual(result['hero']['unique_positions'], 1)
        self.assertEqual(result['hero']['position_changes_observed'], 0)
        self.assertIn('No se observo desplazamiento', text)

    def test_comparable_changed_positions_are_reported_as_motion(self):
        overrides = {
            sample_id: {'heroPosition': '3f800000,00000000,00000000'}
            for sample_id in range(1125, SAMPLE_COUNT)
        }
        result, text = analyze_capture(overrides)
        hero = result['hero']
        self.assertEqual(hero['position_changes_observed'], 1)
        self.assertEqual(hero['first_changed_sample'], 1125)
        self.assertEqual(hero['last_changed_sample'], 1125)
        self.assertEqual(hero['unique_positions'], 2)
        self.assertIn('Se observo desplazamiento', text)
        self.assertNotIn('sin traslado observado', text)
        self.assertIn('cambios observados:1', text)

    def test_absent_hero_does_not_imply_a_stationary_hero(self):
        result, text = analyze_capture({
            sample_id: {'heroPresent': '0', 'heroPointer': '00000000'}
            for sample_id in range(SAMPLE_COUNT)
        })
        hero = result['hero']
        self.assertEqual(hero['present_samples'], 0)
        self.assertEqual(hero['unique_positions'], 0)
        self.assertEqual(hero['position_changes_observed'], 0)
        self.assertNotIn('bounds_xyz', hero)
        self.assertNotIn('net_displacement_xyz', hero)
        self.assertIn('sin observaciones del heroe', text)
        self.assertNotIn('No se observo desplazamiento', text)
        self.assertNotIn('sin traslado observado', text)

    def test_all_five_integrity_negatives_are_rejected(self):
        result = negatives(make_capture(), METADATA)
        self.assertEqual(set(result), {
            'truncate_in_block', 'missing_end', 'error_marker_after_footer',
            'incorrect_count', 'metadata_exit_nonzero',
        })
        self.assertTrue(all(check['rejected'] for check in result.values()))


if __name__ == '__main__':
    unittest.main()
