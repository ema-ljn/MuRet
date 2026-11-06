"""Control unitario de las APIs; no ejecuta ni emula el cliente original.

Requiere Linux con g++/GNU ld para --wrap. MinGW y Windows XP se verifican por separado.
"""

import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest


DIRECTORY = Path(__file__).resolve().parent


@unittest.skipUnless(sys.platform.startswith('linux') and shutil.which('g++'), 'requiere Linux y g++/GNU ld')
class CaptureReadersTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.temporary.cleanup)
        cls.build = Path(cls.temporary.name)
        shutil.copyfile(DIRECTORY / 'test_capture_windows.h', cls.build / 'windows.h')
        cls.sources = Path(os.environ.get('MURET_CAPTURE_SOURCE_DIR', DIRECTORY))
        cls.binaries = {}
        for name in ('passive_camera', 'passive_cl7', 'read_fpu'):
            source = cls.sources / f'{name}.cpp'
            if not source.exists():
                source = cls.sources / f'{name}.c'
            binary = cls.build / name
            cls.compile(source, binary)
            cls.binaries[name] = binary
        series = cls.sources / 'passive_cl7.cpp'
        if not series.exists():
            series = cls.sources / 'passive_cl7.c'
        unit = cls.build / 'entities.cpp'
        unit.write_text('#define main CaptureProgramMain\n'
                        f'#include "{series}"\n'
                        '#undef main\n'
                        'int main() { return '
                        + ('Client::Capture::' if series.suffix == '.cpp' else '')
                        + 'ReadEntities(nullptr, "bird", 0, 6) ? 1 : 0; }\n',
                        encoding='utf-8')
        cls.compile(unit, cls.build / 'entities')

    @classmethod
    def compile(cls, source, binary):
        subprocess.run([
            'g++', '-x', 'c++', '-std=c++20', '-O2', '-Wall', '-Wextra',
            '-fno-exceptions', '-fno-rtti', '-I', str(cls.build),
            '-I', str(DIRECTORY), str(source), str(DIRECTORY / 'test_capture_api.cpp'),
            '-Wl,--wrap=fflush,--wrap=fclose,--wrap=printf,--wrap=puts', '-o', str(binary),
        ], check=True, capture_output=True, text=True, timeout=60)

    def run_reader(self, name, scenario='', args=None, full=False):
        trace = self.build / 'trace.txt'
        output = self.build / 'output.txt'
        if args is None:
            args = (['--output', '/dev/full' if full else str(output)] if name == 'read_fpu'
                    else ['42', '--output', '/dev/full' if full else str(output)])
        result = subprocess.run(
            [str(self.binaries[name]), *args], capture_output=True, text=True, check=False,
            env={**os.environ, 'MURET_CAPTURE_SCENARIO': scenario, 'MURET_CAPTURE_TRACE': str(trace)},
            timeout=10,
        )
        text = '' if full or '--output' not in args else output.read_text()
        return result, text, trace.read_text().splitlines()

    def test_camera_preserves_schema_order_width_and_non_atomic_clock(self):
        result, text, trace = self.run_reader('passive_camera')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(text, 'match camera=00635ae0 inverse=005fcc30\n'
                         'pid=0000002a\nticksBefore=00000064\nticksAfter=00000065\n'
                         'map=00000000\nscene=00000005\nworldTime=00000000\n'
                         'angles=00000000,00000000,00000000\nposition=00000000,00000000,00000000\n'
                         'zoom=00000000\ndistance=00000000\ntargetDistance=00000000\n'
                         'farPlane=00000000\nnearPlane=00000000\nfieldOfView=00000000\n'
                         'fixedHeight=00000000\nlevel=0000\neffectTarget=00000000,00000000,00000000\n'
                         'shake=00000000\nheroPointer=00000000\n')
        self.assertEqual(trace.count('close_process'), 1)

    def test_series_keeps_schedule_sample_count_entities_and_boundary(self):
        result, text, _ = self.run_reader('passive_cl7', 'hero')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertTrue(text.startswith('camera_series 1\nsequence=non_atomic\n'))
        self.assertIn('rateHz=00000019\nsamplesPlanned=000008ca\n', text)
        self.assertEqual(text.count('\nend_sample\n'), 2250)
        self.assertEqual(text.count('entitiesSampled=1\n'), 90)
        self.assertEqual(text.count('boundaryStable=1\n'), 2250)
        self.assertIn('visualCell=00000001,00000002\n', text)
        self.assertTrue(text.endswith('samplesCompleted=000008ca\nend\n'))

    def test_entities_reject_count_beyond_owned_buffers(self):
        result = subprocess.run([str(self.build / 'entities')], capture_output=True, text=True, check=False)
        self.assertEqual(result.returncode, 0, 'count=6 debe rechazarse antes de leer o imprimir entidades')
        self.assertNotIn('birdAlive=', result.stdout)

    def test_read_failures_preserve_partial_count_and_immediate_win32_error(self):
        for scenario, error in (('short_read', '0000012b'), ('read_error', '0000007b')):
            with self.subTest(scenario=scenario):
                result, text, trace = self.run_reader('passive_camera', scenario)
                self.assertEqual(result.returncode, 1)
                self.assertIn('requested=00000004 read=00000003 win32=' + error, text)
                self.assertEqual(trace.count('close_process'), 1)
                if scenario == 'read_error':
                    self.assertLess(trace.index('last_error'), trace.index('close_process'))

    def test_overflow_is_rejected_and_process_close_failure_changes_exit(self):
        for scenario, error in (('overflow', 'error field=heroPosition reason=address_overflow'),
                                ('close_process', 'error field=CloseHandle win32=00000006')):
            result, text, _ = self.run_reader('passive_camera', scenario)
            self.assertEqual(result.returncode, 1)
            self.assertIn(error, text)

    def test_pid_validation_and_cli_remain_compatible(self):
        for pid in ('', '0', '-1', '4294967296', '42x'):
            result, _, _ = self.run_reader('passive_camera', args=[pid])
            self.assertEqual(result.returncode, 1)
            self.assertEqual(result.stdout, 'error reason=invalid_pid expected=nonzero_decimal_dword\n')
        result, _, _ = self.run_reader('read_fpu', args=[])
        self.assertEqual(result.returncode, 2)

    def test_fpu_preserves_snapshot_and_one_owned_decrement(self):
        result, text, trace = self.run_reader('read_fpu')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(text, 'thread_snapshot 1\npid=0000002a\ntid=0000002b\n'
                         'eip=12345678\ncontrolWord=027f\npriorSuspendCount=3\nresumePreviousCount=4\n'
                         'ticksBefore=00000064\nticksAfter=00000065\nend\n')
        self.assertEqual(trace.count('resume'), 1)
        self.assertEqual(trace[trace.index('suspend'):trace.index('resume') + 1],
                         ['suspend', 'context', 'resume'])
        self.assertNotIn('abandoned_suspension', trace)

    def test_context_error_is_saved_before_resume_and_close(self):
        result, text, trace = self.run_reader('read_fpu', 'context')
        self.assertEqual(result.returncode, 1)
        self.assertEqual(text, 'error field=GetThreadContext win32=222\n')
        self.assertEqual(trace[trace.index('suspend'):trace.index('resume') + 1],
                         ['suspend', 'context', 'last_error', 'resume'])

    def test_resume_failure_keeps_handle_until_recovery_and_ignores_foreign_count(self):
        result, text, trace = self.run_reader('read_fpu', 'resume_once')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertTrue(text.startswith('recovery_pending pid=42 tid=43 prior=3 win32=333\nrecovery_completed\n'))
        self.assertEqual(trace.count('resume'), 2)
        self.assertGreater(trace.index('close_thread'), len(trace) - 1 - trace[::-1].index('resume'))
        self.assertNotIn('close_while_owed', trace)
        self.assertNotIn('io_before_first_resume', trace)
        self.assertNotIn('abandoned_suspension', trace)

    def test_recovery_finishes_even_when_output_fails(self):
        result, _, trace = self.run_reader('read_fpu', 'resume_once', full=True)
        self.assertEqual(result.returncode, 1)
        self.assertEqual(trace.count('resume'), 2)
        self.assertNotIn('close_while_owed', trace)
        self.assertNotIn('abandoned_suspension', trace)

    def test_target_exit_during_recovery_returns_four(self):
        result, text, trace = self.run_reader('read_fpu', 'target_exit')
        self.assertEqual(result.returncode, 4)
        self.assertIn('error field=target_exited_during_recovery\n', text)
        self.assertEqual(trace.count('resume'), 1)
        self.assertNotIn('close_while_owed', trace)

    def test_recovery_flush_failure_is_reported_after_owned_decrement(self):
        result, text, trace = self.run_reader('read_fpu', 'resume_flush_error')
        self.assertEqual(result.returncode, 1)
        self.assertIn('recovery_completed\n', text)
        self.assertEqual(trace.count('resume'), 2)
        self.assertNotIn('close_while_owed', trace)
        self.assertNotIn('abandoned_suspension', trace)

    def test_explicit_file_close_failure_is_observable(self):
        for name in self.binaries:
            with self.subTest(name=name):
                result, _, trace = self.run_reader(name, 'fclose')
                self.assertEqual(result.returncode, 1)
                self.assertIn('fclose', trace)


if __name__ == '__main__':
    unittest.main()
