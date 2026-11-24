"""Tests for check_comments_only.py on a throwaway git repository. The addresses are made up and outside main.exe's
range, so the client reference does not count them as citations.

    python3 -I -m unittest discover -s Client/tools/architecture -p 'test_check_comments_only.py' -v
"""
import unittest

import check_comments_only
from scratch_repo import ScratchRepo, run_main

SOURCE = 'Client/src/Items/SetBonus.cpp'
SET_BONUS = ('// The set bonus adds the options of every piece of the set\n'
             '// once the whole set is equipped.\n'
             'int Bonus(int x)\n{\n'
             '    int y = f(x); // 0x1d3200: adds the set bonus\n'
             '    return y + 1;\n}\n'
             'const char* kUrl = "http://host";\n'
             "const char kSlash = '/';\n")
TRIMMED = SET_BONUS.replace('// The set bonus adds the options of every piece of the set\n'
                            '// once the whole set is equipped.\n', '').replace(
    '// 0x1d3200: adds the set bonus', '// 0x1d3200')


class CheckCommentsOnlyTest(unittest.TestCase):
    def setUp(self):
        self.repo = ScratchRepo(self)
        self.repo.write(SOURCE, SET_BONUS)
        self.base = self.repo.commit('base')

    def check(self, text):
        self.repo.write(SOURCE, text)
        self.repo.commit('docs(AR3.1): trim and translate comments')
        return run_main(check_comments_only.main, '--repo', str(self.repo.path), '--base', self.base,
                        '--head', 'HEAD')

    def test_comment_trim_passes(self):
        code, output = self.check(TRIMMED)
        self.assertEqual(code, 0, output)

    def test_code_token_change_fails(self):
        code, output = self.check(TRIMMED.replace('return y + 1;', 'return y - 1;'))
        self.assertEqual(code, 1)
        self.assertIn('return y - 1;', output)

    def test_lost_citation_fails(self):
        code, output = self.check(TRIMMED.replace(' // 0x1d3200', ''))
        self.assertEqual(code, 1)
        self.assertIn('0x1d3200', output)

    def test_comment_marker_inside_string_is_code(self):
        code, output = self.check(SET_BONUS.replace('"http://host"', '"http://other"'))
        self.assertEqual(code, 1)
        self.assertIn('http://other', output)

    def test_research_markdown_is_allowed_and_other_files_are_not(self):
        self.repo.write('Client/docs/research/items/set-bonus.md', '# Set bonus\n\nThe long comment, moved here.\n')
        code, output = self.check(TRIMMED)
        self.assertEqual(code, 0, output)
        self.repo.write('Client/CMakeLists.txt', 'add_library(Items)\n')
        code, output = self.check(TRIMMED)
        self.assertEqual(code, 1)
        self.assertIn('Client/CMakeLists.txt', output)


if __name__ == '__main__':
    unittest.main()
