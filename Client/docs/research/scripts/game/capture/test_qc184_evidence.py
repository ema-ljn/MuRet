"""Fixtures sintéticos: no ejecutan el cliente ni un debugger."""

from pathlib import Path
import re
import struct
import tempfile
import unittest

import qc184_evidence as tool


class Qc184EvidenceTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)

    def function(self, name):
        function = getattr(tool, name, None)
        self.assertTrue(callable(function), f'falta implementar {name}')
        return function

    def manifest(self, mode='bonus'):
        return {'schema': 1, 'session': 'synthetic', 'mode': mode,
                'exe_sha256': 'c32bbd5f16ca400c7cfc71fa5f4cd573c57f146dd19a6da52fdd1007abaca26a',
                'fingerprints': {'sig_item': {'address': 0x40dc50, 'hex': '558bec'}}}

    def trace(self, body, mode='bonus', complete=True):
        if mode == 'bonus' and body:
            kind = int(re.search(r' mem=([0-9a-f]+)', body)[1], 16)
            prefix = bytearray(64)
            prefix[48] = kind
            dump = '\n'.join(f'{0xfc0+i:08x}  ' + ' '.join(f'{x:02x}' for x in prefix[i:i+16])
                             + '  ................' for i in range(0, 64, 16))
            body = (self.stats('BEGIN', 0x550d10) + self.fp() + body
                    + f'QC184_PREFIX_BEGIN\n{dump}\nQC184_PREFIX_END\n' + self.fp()
                    + self.stats('END', 0x5513a3) + self.fp())
        return (f'QC184_SESSION id=synthetic mode={mode} schema=1\n'
                'QC184_PROCESS pid=0000007b\nQC184_PROFILE_OK\nQC184_ARMED_OK\n'
                + body + ('QC184_COMPLETE\n' if complete else ''))

    def fp(self, sw=0):
        return (f'QC184_FP_BEGIN\nfpcw=027f fpsw={sw:04x} fptw=ffff\n'
                'st0=+0.00000000000000000000e+000\nQC184_FP_END\n')

    def stats(self, event, pc):
        return (f'QC184_{event} tid=12 own=2000 caller=512efa pc={pc:x} frame=100000 depth=1 '
                'w2a=0 w2c=0 w2e=0 w30=0 w32=0 w34=0 w36=0 w62=4b w64=0 w66=0 '
                + self.guard() + '\n')

    def read(self, kind=0xfe, value=0, flags=0xf3, address=0x1000-16):
        return (f'QC184_READ tid=00000012 own=00002000 item=00002b6c table=00001000 '
                f'address={address:08x} index=ffffffeb id=0000ffff dur=00000019 '
                f'flags={flags:08x} value={value:08x} kind={kind:08x} mem={kind:08x} '
                'caller=0040e038 depth=00000001 ' + self.guard() + '\n')

    def guard(self):
        return ('gvalues=8000 gkeys=9000 gcap=10 gmult=1 gslot=0 gkey=2000 '
                'shadow=a000 ghash=448310 gprobes=0')

    def test_positive_depth_without_matching_shadow_provenance_is_incomplete(self):
        valid = self.trace(self.read())
        for old, new in (('gkey=2000', 'gkey=2004'), ('shadow=a000', 'shadow=0'),
                         ('gcap=10', 'gcap=0'), ('gslot=0', 'gslot=10'),
                         ('ghash=448310', 'ghash=448314')):
            with self.subTest(field=old):
                report = self.function('analyze_trace')(valid.replace(old, new), self.manifest())
                self.assertNotEqual(report['status'], 'CAPTURED')

    def test_shadow_slot_uses_wrapped_hash_and_modulo_for_non_power_of_two_capacity(self):
        # 448310: bytes 04,03,02,01 with multiplier ffffffff yield fffffffe.
        # fffffffe % 7 == 2; one linear probe reaches slot 3.
        row = dict(own=0x01020304, gvalues=0x8000, gkeys=0x9000, gcap=7,
                   gmult=0xffffffff, gslot=3, gkey=0x01020304, shadow=0xa000,
                   ghash=0x448310, gprobes=1, depth=1)
        self.assertEqual(self.function('_guard_errors')(row), [])
        row['gslot'] = 2
        self.assertTrue(self.function('_guard_errors')(row))

    def test_bonus_records_the_counter_from_the_hash_shadow(self):
        commands = self.function('render_commands')(self.manifest(), r'C:\Capture\qc184-01')
        for name in ('on-read.wds', 'on-begin.wds', 'on-end.wds'):
            with self.subTest(script=name):
                self.assertIn('by(@$t6+0xf08)', commands[name])
                self.assertIn('shadow=%08x', commands[name])
                self.assertIn('gkey=%08x', commands[name])
                self.assertNotIn('depth=%08x\\n", by(', commands[name])

    def test_unknown_selector_is_observed_without_assuming_zero(self):
        report = self.function('analyze_trace')(self.trace(self.read()), self.manifest())
        self.assertEqual(report['reads'][0]['kind'], 0xfe)
        self.assertEqual(report['reads'][0]['predicted_contribution'], 0)
        self.assertFalse(report['native_fidelity_verified'])

    def test_nonzero_bonus_keeps_the_real_kind_and_predicted_contribution(self):
        report = self.function('analyze_trace')(
            self.trace(self.read(kind=3, value=2, flags=8)), self.manifest())
        self.assertEqual(report['reads'][0]['predicted_contribution'], 10)
        self.assertEqual(report['reads'][0]['kind'], 3)

    def test_wrong_address_and_bonus_value_are_not_accepted_as_observations(self):
        analyze = self.function('analyze_trace')
        for row in (self.read(address=0x1000), self.read(value=2)):
            with self.subTest(row=row):
                self.assertTrue(analyze(self.trace(row), self.manifest())['errors'])

    def test_masm_expression_errors_in_an_otherwise_complete_trace_are_rejected(self):
        for error in ('Numeric expression missing from expression', 'Operand error at division'):
            with self.subTest(error=error):
                text = self.trace(self.read()).replace('QC184_COMPLETE', error + '\nQC184_COMPLETE')
                report = self.function('analyze_trace')(text, self.manifest())
                self.assertNotEqual(report['status'], 'CAPTURED')

    def test_missing_completion_wrong_session_and_zero_reads_remain_incomplete(self):
        analyze = self.function('analyze_trace')
        for trace in (self.trace(self.read(), complete=False),
                      self.trace(self.read()).replace('id=synthetic', 'id=another'), self.trace('')):
            with self.subTest(trace=trace):
                self.assertNotEqual(analyze(trace, self.manifest())['status'], 'CAPTURED')

    def test_wear_captures_keep_high_ebx_and_fp_flags_as_observed(self):
        text = ('QC184_WEAR_BEFORE tid=12 own=2000 frame=100000 slot=3 id=ffff value=0 '
                'value_raw=02000000 base=0 dur=19 arg1=19 arg2=0 arg3=0 arg4=0 arg5=0 '
                'table=1000 address=fd9 mem_base=0\n' + self.fp()
                + 'QC184_WEAR_FACTOR tid=12 own=2000 frame=100000 slot=3 id=ffff\n' + self.fp(4)
                + 'QC184_WEAR_REDUCED tid=12 own=2000 frame=100000 slot=3 id=ffff '
                'reduced=02000000 low=0\n' + self.fp(4)
                + 'QC184_WEAR_SUM tid=12 own=2000 frame=100000 sum=02000000 w62=4b\n' + self.fp(4))
        report = self.function('analyze_trace')(self.trace(text, 'wear'), self.manifest('wear'))
        self.assertEqual(report['events'][1]['fp']['sw'], 4)
        self.assertEqual(report['events'][2]['reduced'], 0x02000000)
        self.assertEqual(report['events'][2]['low'], 0)
        self.assertFalse(report['native_fidelity_verified'])

    def test_isolated_read_and_reduced_rows_never_count_as_complete_captures(self):
        analyze = self.function('analyze_trace')
        for mode, body in (('bonus', self.read()), ('wear', 'QC184_WEAR_REDUCED id=ffff low=0\n')):
            text = (f'QC184_SESSION id=synthetic mode={mode} schema=1\nQC184_PROCESS pid=7b\n'
                    'QC184_PROFILE_OK\nQC184_ARMED_OK\n' + body + 'QC184_COMPLETE\n')
            self.assertNotEqual(analyze(text, self.manifest(mode))['status'], 'CAPTURED')

    def test_stats_frames_missing_fp_and_pid_are_rejected(self):
        analyze = self.function('analyze_trace')
        valid = self.trace(self.read())
        invalid = (valid.replace('pc=5513a3 frame=100000', 'pc=5513a3 frame=200000'),
                   valid.replace('QC184_PROCESS pid=0000007b\n', ''),
                   valid.replace('QC184_FP_BEGIN', 'FP_MISSING'),
                   valid.replace('QC184_PREFIX_END', 'PREFIX_MISSING'))
        for text in invalid:
            self.assertNotEqual(analyze(text, self.manifest())['status'], 'CAPTURED')

    def test_wear_commands_capture_all_five_raw_arguments(self):
        commands = self.function('render_commands')(self.manifest('wear'), r'C:\Capture\qc184-01')
        before = commands['on-wear-before.wds']
        for name, expression in (('arg1', 'poi(@esp)'), ('arg2', 'poi(@esp+4)'),
                                 ('arg3', 'poi(@esp+8)'), ('arg4', 'poi(@esp+0xc)'),
                                 ('arg5', 'poi(@esp+0x10)'), ('value_raw', 'poi(@ebp-0xc)')):
            self.assertIn(name + '=%08x', before)
            self.assertIn(expression, before)

    def test_base_zero_with_level_and_ancient_bonus_has_a_nonzero_denominator(self):
        text = ('QC184_WEAR_BEFORE tid=12 own=2000 frame=100000 slot=3 id=ffff value=0 '
                'value_raw=02000000 base=0 dur=19 arg1=19 arg2=0 arg3=8 arg4=0 arg5=2 '
                'table=1000 address=fd9 mem_base=0\n' + self.fp()
                + 'QC184_WEAR_FACTOR tid=12 own=2000 frame=100000 slot=3 id=ffff\n' + self.fp()
                + 'QC184_WEAR_REDUCED tid=12 own=2000 frame=100000 slot=3 id=ffff '
                'reduced=02000000 low=0\n' + self.fp()
                + 'QC184_WEAR_SUM tid=12 own=2000 frame=100000 sum=02000000 w62=4b\n' + self.fp())
        report = self.function('analyze_trace')(self.trace(text, 'wear'), self.manifest('wear'))
        self.assertFalse(report['errors'])
        self.assertEqual(report['events'][0]['predicted_max_durability'], 21)

    def test_wear_missing_before_factor_sum_or_argument_is_incomplete(self):
        analyze = self.function('analyze_trace')
        reduced = 'QC184_WEAR_REDUCED tid=12 own=2000 frame=100000 slot=3 id=ffff reduced=0 low=0\n'
        for body in (reduced, reduced.replace('reduced=0 ', ''), reduced.replace('tid=12 ', '')):
            self.assertNotEqual(analyze(self.trace(body, 'wear'), self.manifest('wear'))['status'], 'CAPTURED')

    def test_render_uses_four_hardware_points_and_only_clears_owned_ids(self):
        render = self.function('render_commands')
        for mode, addresses in (
                ('bonus', [0x40dc8c, 0x550d10, 0x5513a3, 0x5513b1]),
                ('wear', [0x550adb, 0x550ae0, 0x550b00, 0x550b1e])):
            manifest = self.manifest(mode)
            commands = render(manifest, r'C:\Capture\qc184-01')
            points = re.findall(r'ba([0-3]) e 1 0x([0-9a-f]+)', commands['start.wds'])
            self.assertEqual([int(address, 16) for _, address in points], addresses)
            self.assertNotIn('bc *', commands['stop.wds'])
            self.assertIn('.detach', commands['stop.wds'])
            self.assertNotIn('DebugActiveProcess', ''.join(commands.values()))

    def test_startup_uses_the_debugger_number_base_and_version_commands(self):
        # CDB 6.12 stops the generated script at .radix; .version is invalid too.
        # These are debugger commands, rather than extension or dot commands.
        for mode in ('bonus', 'wear'):
            with self.subTest(mode=mode):
                startup = self.function('render_commands')(
                    self.manifest(mode), r'C:\Capture\qc184-01')['start.wds'].splitlines()
                self.assertIn('n 16', startup)
                self.assertIn('version', startup)
                self.assertNotIn('.radix 16', startup)
                self.assertNotIn('.version', startup)

    def test_guards_combine_parenthesized_masm_predicates(self):
        # && produces "Numeric expression missing" with the selected MASM evaluator.
        for mode in ('bonus', 'wear'):
            commands = self.function('render_commands')(self.manifest(mode), r'C:\Capture\qc184-01')
            for name in ('start.wds', 'arm.wds', 'stop.wds'):
                with self.subTest(mode=mode, script=name):
                    self.assertNotIn('&&', commands[name])
                    self.assertIn(') & (', commands[name])
            self.assertIn('(@$ptrsize == 4) & (@$t19 == 0)', commands['start.wds'])
            self.assertIn('(@$bp0 == 0x', commands['stop.wds'])

    def test_startup_checks_the_breakpoint_list_without_reading_absent_pseudo_registers(self):
        # @$bp0 raises "Bad register" when ID 0 does not exist; it never reads as zero.
        for mode in ('bonus', 'wear'):
            startup = self.function('render_commands')(
                self.manifest(mode), r'C:\Capture\qc184-01')['start.wds']
            self.assertIn('r @$t19 = 0', startup)
            self.assertIn('.foreach (qc184point { bl }) { r @$t19 = 1; }', startup)
            self.assertNotRegex(startup, r'@\$bp[0-3] == 0(?:[ )])')

    def test_guest_path_cannot_inject_debugger_commands(self):
        render = self.function('render_commands')
        with self.assertRaises(ValueError):
            render(self.manifest(), 'C:\\Capture;g')

    def test_prepare_refuses_a_different_executable_before_creating_output(self):
        prepare = self.function('prepare')
        executable = self.root / 'different.exe'
        executable.write_bytes(b'MZ synthetic')
        output = self.root / 'bundle'
        with self.assertRaises(ValueError):
            prepare(executable, output, r'C:\Capture\qc184-01', 'bonus')
        self.assertFalse(output.exists())

    def test_empty_git_placeholder_is_not_a_checkout_but_real_git_metadata_is(self):
        outside = self.function('_outside_git')
        marker = self.root / '.git'
        marker.mkdir()
        self.assertEqual(outside(self.root / 'capture'), (self.root / 'capture').resolve())
        (marker / 'HEAD').write_text('ref: refs/heads/main\n')
        with self.assertRaises(ValueError):
            outside(self.root / 'capture')

    def test_pe_virtual_address_maps_to_raw_bytes_and_rejects_unbacked_range(self):
        read = self.function('read_va_bytes')
        image = bytearray(0x400)
        image[:2] = b'MZ'
        struct.pack_into('<I', image, 0x3c, 0x80)
        image[0x80:0x84] = b'PE\0\0'
        struct.pack_into('<HH', image, 0x84, 0x14c, 1)
        struct.pack_into('<H', image, 0x94, 0xe0)
        struct.pack_into('<H', image, 0x98, 0x10b)
        struct.pack_into('<I', image, 0xb4, 0x400000)
        struct.pack_into('<IIII', image, 0x180, 0x100, 0x1000, 0x100, 0x200)
        image[0x210:0x214] = b'ABCD'
        self.assertEqual(read(bytes(image), 0x401010, 4), b'ABCD')
        with self.assertRaises(ValueError):
            read(bytes(image), 0x401100, 4)

    def passive(self, bad=0):
        manifest = self.manifest()
        prefix = bytearray(64)
        prefix[0x30] = 0 if bad else 0xfe
        prefix[0x19] = 7
        regions = {'table_before': (0x7758550, b'\x00\x10\x00\x00'),
                   'selector': (0xff0, b'\0' if bad else b'\xfe'), 'wear_base': (0xfd9, b'\x07'),
                   'prefix': (0xfc0, bytes(prefix)),
                   'table_after': (0x7758550, b'\x00\x10\x00\x00'),
                   'sig_item': (0x40dc50, bytes.fromhex('558bec'))}
        lines = ['pid 123']
        for name, (address, data) in regions.items():
            (self.root / f'{name}.bin').write_bytes(data)
            lines.append(f'{name} {address:08x} {len(data):x} paginas=1 '
                         f'ilegibles={bad if name == "selector" else 0} ok')
        lines.append('fin')
        (self.root / 's31mem.txt').write_text('\n'.join(lines) + '\n')
        return manifest

    def test_passive_capture_accepts_real_bytes_and_rejects_zero_filled_failed_reads(self):
        analyze = self.function('analyze_passive')
        manifest = self.passive()
        report = analyze(self.root, manifest)
        self.assertEqual(report['selector'], 0xfe)
        self.assertEqual(report['wear_base'], 7)
        self.assertFalse(report['errors'])
        manifest = self.passive(bad=1)
        self.assertTrue(analyze(self.root, manifest)['errors'])

    def test_passive_pointer_change_and_code_mismatch_are_not_accepted(self):
        analyze = self.function('analyze_passive')
        manifest = self.passive()
        (self.root / 'table_after.bin').write_bytes(b'\x00\x20\x00\x00')
        (self.root / 'sig_item.bin').write_bytes(b'ABC')
        self.assertTrue(analyze(self.root, manifest)['errors'])


if __name__ == '__main__':
    unittest.main()
