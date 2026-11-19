"""Regresiones del índice de saltos del paso de efectos con modelo."""

import os
import struct
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from effect_step_switches import Table, decode_table, reach_sites


class EffectStepSwitchesTest(unittest.TestCase):
    def test_byte_selector_preserves_shared_targets_and_bypassed_keys(self):
        table = Table(0x40, 'tipo', 9, 4, 0x100, 3, 0x10c, (10,))
        image = struct.pack('<III4B', 0x48, 0x60, 0x70, 2, 0, 2, 1)
        read = lambda va, size: image[va - 0x100:va - 0x100 + size]
        entries = decode_table(read, table)
        self.assertEqual(entries, {9: 0x70, 11: 0x70, 12: 0x60})

    def test_rejects_a_selector_past_the_pointer_table(self):
        table = Table(0x40, 'tipo', 9, 1, 0x100, 1, 0x104)
        image = struct.pack('<IB', 0x48, 2)
        read = lambda va, size: image[va - 0x100:va - 0x100 + size]
        with self.assertRaisesRegex(ValueError, 'selector'):
            decode_table(read, table)

    def test_walk_follows_mapped_targets_and_keeps_common_tail_separate(self):
        instructions = {0x40: 'cmp eax,0x1', 0x43: 'je 0x48',
                        0x45: 'jmp DWORD PTR [eax*4+0x200]',
                        0x48: 'call 0x518cf0', 0x4d: 'jmp 0x65',
                        0x55: 'call 0x530860', 0x5a: 'jmp 0x65',
                        0x65: 'call 0x53fe70', 0x6a: 'ret'}
        jumps = {0x45: {0x48, 0x55}}
        self.assertEqual(reach_sites(instructions, jumps, 0x40),
                         {(0x48, 0x518cf0), (0x55, 0x530860), (0x65, 0x53fe70)})
        self.assertEqual(reach_sites(instructions, jumps, 0x40, stop=0x65),
                         {(0x48, 0x518cf0), (0x55, 0x530860)})

    def test_unknown_indirect_jump_is_an_error(self):
        instructions = {0x40: 'jmp DWORD PTR [eax*4+0x200]'}
        with self.assertRaisesRegex(ValueError, '0x40'):
            reach_sites(instructions, {}, 0x40)


if __name__ == '__main__':
    unittest.main()
