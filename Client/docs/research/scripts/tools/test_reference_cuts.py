"""Tests for the cut regex of build_roadmap_matrix.py and build_client_reference.py: a cited line that says the piece is
not ported does not count as ported, in Spanish or in English.

    python3 -I -m unittest discover -s Client/docs/research/scripts/tools -p 'test_reference_cuts.py' -v
"""
import unittest

import build_client_reference
import build_roadmap_matrix


class CutRegexTest(unittest.TestCase):
    def test_both_scripts_use_the_same_regex(self):
        self.assertEqual(build_roadmap_matrix.CUT.pattern, build_client_reference.CUT.pattern)

    def test_a_spanish_cut_is_a_cut(self):
        for line in ('// primer acceso (0x40b440, 0x412810) no se porta: el valor ya está en claro.',
                     '// 0x466560, que MuRet no reproduce'):
            self.assertTrue(build_client_reference.CUT.search(line), line)

    def test_an_english_cut_is_a_cut(self):
        for line in ('// The first access\'s decryption (0x40b440, 0x412810) is not ported: the value is in the clear.',
                     '// MuRet does not reproduce its cached text (0x466000).',
                     '// and 0x60d6dd looks at the next one), whatever the file has. MuRet does not: without a dot',
                     "// MuRet doesn't draw the other rand() calls of 0x637700 here."):
            self.assertTrue(build_client_reference.CUT.search(line), line)

    def test_a_ported_line_in_english_is_not_a_cut(self):
        for line in ('// 0x612c80: a model\'s ten sound WORDs (+0xaa..+0xbc of its BMD in [0x5769ad8]).',
                     '// GL_ADD (the 6ac/2 flame, 0x5489eb): color is texel + color, clamped to [0, 1].',
                     '// The quad of 0x554000: slot 0, in pixels and unscaled, with the 256 x 32 box clipped.'):
            self.assertFalse(build_client_reference.CUT.search(line), line)


if __name__ == '__main__':
    unittest.main()
