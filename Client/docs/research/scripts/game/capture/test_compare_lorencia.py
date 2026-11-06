"""Contrato offline del comparador; los datos sintéticos no prueban el original.

Ejecutar con MURET_COMPARE_LORENCIA apuntando al ejecutable compilado del comparador.
Los bits esperados caracterizan el puerto anterior al refactor, no una captura del juego.
"""

import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest


SIGNATURE = 'match camera=00635ae0 inverse=005fcc30'
FIELDS = {
    'angles': 'c2420000,00000000,00000000',
    'distance': '447a0000',
    'zoom': '00000000',
    'level': '0000',
    'fixedHeight': '44960000',
    'farPlane': '44fa0000',
    'targetDistance': '447a0000',
    'heroPosition': '00000000,00000000,00000000',
    'shake': '00000000',
    'worldTime': '00000000',
    'effectTarget': '00000000,00000000,00000000',
    'map': '00000000',
    'scene': '00000005',
    'position': '00000000,c47a0000,44548000',
    'fieldOfView': '420c0000',
    'nearPlane': '41a00000',
}
EXPECTED_BITS = {
    'camera.position.0': '00000000',
    'camera.angles.0': 'c2420000',
    'camera.position.1': 'c47a0000',
    'camera.angles.1': '00000000',
    'camera.position.2': '44548000',
    'camera.angles.2': '00000000',
    'camera.fov': '420c0000',
    'camera.near': '41a00000',
    'camera.far': '44fa0000',
    'state.yaw': '00000000',
    'state.distance': '447a0000',
    'state.zoom': '00000000',
    'state.level': '00000000',
    'state.fixedHeight': '44960000',
    'state.farPlane': '44fa0000',
    'state.targetDistance': '447a0000',
}


def make_capture(overrides=None, missing=(), newline='\n'):
    fields = {**FIELDS, **(overrides or {})}
    lines = [SIGNATURE]
    lines.extend(f'{key}={value}' for key, value in fields.items() if key not in missing)
    return newline.join(lines) + newline


@unittest.skipUnless(os.environ.get('MURET_COMPARE_LORENCIA'),
                     'requiere el ejecutable MURET_COMPARE_LORENCIA')
class CompareLorenciaTest(unittest.TestCase):
    def setUp(self):
        self.binary = str(Path(os.environ['MURET_COMPARE_LORENCIA']).resolve())
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.directory = Path(self.temporary.name)
        self.capture = self.directory / 'capture.txt'
        self.output = self.directory / 'comparison.json'

    def run_capture(self, text=None, output=None):
        self.capture.write_bytes((text if text is not None else make_capture()).encode('utf-8'))
        result = subprocess.run(
            [self.binary, str(self.capture), str(output or self.output)],
            capture_output=True, text=True, check=False,
        )
        return result

    def assert_error(self, text, message):
        result = self.run_capture(text)
        self.assertEqual(result.returncode, 1)
        self.assertEqual(result.stdout, '')
        self.assertEqual(result.stderr, f'error: {message}\n')

    def test_matching_capture_preserves_all_sixteen_ordered_bit_checks(self):
        result = self.run_capture()
        self.assertEqual(result.returncode, 0)
        self.assertEqual(result.stderr, '')
        self.assertEqual(result.stdout, 'matches=true cameraChecks=9 seededStateChecks=7\n')
        text = self.output.read_text()
        report = json.loads(text)
        self.assertEqual(report['implementation'], 'libClientLib.a: StepGameCamera')
        self.assertTrue(report['matches'])
        self.assertEqual(list(report['checks']), list(EXPECTED_BITS))
        for name, bits in EXPECTED_BITS.items():
            with self.subTest(name=name):
                self.assertEqual(report['checks'][name], {
                    'observed': bits, 'computed': bits, 'matches': True,
                })
                self.assertIn(f'"{name}": {{"observed": "{bits}", "computed": "{bits}", '
                              '"matches": true}', text)
        self.assertEqual(len(report['limits']), 4)
        self.assertIn('Sin atributo observado', report['limits'][2])

    def test_crlf_unknown_fields_and_numeric_spellings_remain_accepted(self):
        result = self.run_capture(make_capture({
            'map': ' \t+0x0', 'scene': '+0X5', 'zoom': '-0',
            'worldTime': 'ffffffff', 'unknownField': 'unparsed=value',
            'angles': 'c2420000,00000000,00000000,',
        }, newline='\r\n'))
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertTrue(json.loads(self.output.read_text())['matches'])

    def test_observed_mismatch_produces_report_and_unsuccessful_exit(self):
        result = self.run_capture(make_capture({'position': '3f800000,c47a0000,44548000'}))
        self.assertEqual(result.returncode, 1)
        self.assertEqual(result.stderr, '')
        self.assertEqual(result.stdout, 'matches=false cameraChecks=9 seededStateChecks=7\n')
        report = json.loads(self.output.read_text())
        self.assertFalse(report['matches'])
        self.assertEqual(report['checks']['camera.position.0'], {
            'observed': '3f800000', 'computed': '00000000', 'matches': False,
        })

    def test_visual_attribute_precedes_legacy_attribute_and_uses_low_byte(self):
        result = self.run_capture(make_capture({
            'visualCellAttribute': '0100', 'cellAttribute': 'invalid',
        }))
        self.assertEqual(result.returncode, 0, result.stderr)
        result = self.run_capture(make_capture({'cellAttribute': '0040'}))
        self.assertEqual(result.returncode, 1)
        self.assertEqual(result.stderr, '')
        report = json.loads(self.output.read_text())
        self.assertEqual(report['checks']['state.fixedHeight']['computed'], '44962000')
        self.assertEqual(report['checks']['camera.position.2']['computed'], '45003000')

    def test_empty_signature_duplicate_and_malformed_lines_are_rejected(self):
        cases = (
            ('', 'captura vacía'),
            (make_capture().replace(SIGNATURE, 'match camera=bad'),
             'la captura no confirmó ambas firmas'),
            (make_capture() + 'map=00000000\n', 'campo inválido o duplicado'),
            (make_capture() + 'broken\n', 'campo inválido o duplicado'),
        )
        for text, message in cases:
            with self.subTest(message=message):
                self.assert_error(text, message)

    def test_every_required_field_is_checked(self):
        for key in FIELDS:
            with self.subTest(key=key):
                self.assert_error(make_capture(missing=(key,)), 'map::at')

    def test_invalid_and_out_of_range_words_are_rejected(self):
        cases = (
            ('', 'stoul'), ('xyz', 'stoul'),
            ('1 ', 'bits inválidos'), ('100000000', 'bits inválidos'),
            ('ffffffffffffffffffffffff', 'stoul'), ('1\0', 'bits inválidos'),
        )
        for value, message in cases:
            with self.subTest(value=value):
                self.assert_error(make_capture({'distance': value}), message)
        self.assert_error(make_capture({'level': '10000'}), 'nivel fuera de WORD')
        self.assert_error(make_capture({'cellAttribute': '10000'}), 'atributo fuera de WORD')

    def test_vector_component_counts_are_checked(self):
        for key in ('angles', 'heroPosition', 'effectTarget', 'position'):
            with self.subTest(key=key):
                self.assert_error(make_capture({key: '00000000,00000000'}), 'vector incompleto')
                self.assert_error(make_capture({key: '0,0,0,0'}), 'vector con componentes de más')
                self.assert_error(make_capture({key: '0,,0'}), 'stoul')

    def test_other_maps_and_scenes_are_rejected(self):
        for key, value in (('map', '1'), ('scene', '4')):
            with self.subTest(key=key):
                self.assert_error(make_capture({key: value}), 'no es una captura de Lorencia en juego')

    def test_wrong_map_is_rejected_before_reading_scene(self):
        self.assert_error(make_capture({'map': '1'}, missing=('scene',)),
                          'no es una captura de Lorencia en juego')
        self.assert_error(make_capture({'map': '1', 'scene': 'invalid'}),
                          'no es una captura de Lorencia en juego')

    def test_late_invalid_fields_preserve_the_partial_json(self):
        for key, completed_checks in (('fieldOfView', 6), ('nearPlane', 7)):
            for missing in (True, False):
                with self.subTest(key=key, missing=missing):
                    text = (make_capture(missing=(key,)) if missing
                            else make_capture({key: 'invalid'}))
                    self.assert_error(text, 'map::at' if missing else 'stoul')
                    expected = ('{\n  "implementation": "libClientLib.a: StepGameCamera",\n'
                                '  "checks": {\n')
                    checks = list(EXPECTED_BITS.items())[:completed_checks]
                    expected += ',\n'.join(
                        f'    "{name}": {{"observed": "{bits}", "computed": "{bits}", '
                        '"matches": true}' for name, bits in checks
                    )
                    self.assertEqual(self.output.read_bytes(), expected.encode('utf-8'))

    def test_cli_and_file_errors_are_reported(self):
        result = subprocess.run([self.binary], capture_output=True, text=True, check=False)
        self.assertEqual(result.returncode, 1)
        self.assertEqual(result.stderr, 'error: uso: compare_lorencia CAPTURA JSON\n')
        result = subprocess.run([self.binary, str(self.capture), str(self.output)],
                                capture_output=True, text=True, check=False)
        self.assertEqual(result.returncode, 1)
        self.assertEqual(result.stderr, 'error: no se pudo abrir la captura\n')
        result = self.run_capture(output=self.directory / 'missing' / 'output.json')
        self.assertEqual(result.returncode, 1)
        self.assertEqual(result.stderr, 'error: no se pudo abrir el JSON\n')

    @unittest.skipUnless(Path('/dev/full').exists(), 'requiere /dev/full para provocar fallo de escritura')
    def test_write_failure_is_reported_after_flush(self):
        result = self.run_capture(output=Path('/dev/full'))
        self.assertEqual(result.returncode, 1)
        self.assertEqual(result.stdout, '')
        self.assertEqual(result.stderr, 'error: fallo al escribir el JSON\n')


if __name__ == '__main__':
    unittest.main()
